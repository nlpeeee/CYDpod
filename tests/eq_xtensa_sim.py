"""EQ-5BAND: interpret the assembled ESP32 stage loop and compare it with the C++ reference.

Reads the objdump disassembly of .iram1.cydeq_stage_xtensa (what the firmware links)
and the vectors written by tests/eq_vectors.cpp, executes the machine code for every
case with a small model of the instructions it uses, and requires every output word
and the stored history to match cydeq::stageRef exactly.

It also counts executed instructions and applies a simple LX6 stall model (one
bubble when an instruction consumes the result of the instruction just before it
and that producer is a load or a 32-bit multiply). Those cycle numbers are a model,
not a board measurement; the firmware's eq self-test reports measured cycles.
"""
import re
import sys

M32 = 0xFFFFFFFF


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def parse(path):
    prog = {}
    order = []
    line_re = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]+)\s+(\S+)\s*(.*)$")
    for line in open(path, encoding="ascii"):
        m = line_re.match(line)
        if not m:
            continue
        addr, raw, mnem, ops = int(m.group(1), 16), m.group(2), m.group(3), m.group(4)
        ops = re.sub(r"<[^>]*>", "", ops).strip()
        args = [a.strip() for a in ops.split(",")] if ops else []
        prog[addr] = (mnem, args, len(raw) // 2)
        order.append(addr)
    if not order:
        raise SystemExit("FAIL: no instructions in disassembly")
    return prog, order


def reg(name):
    assert name.startswith("a"), name
    return int(name[1:])


def imm(text):
    return int(text, 0)


class Cpu:
    def __init__(self, prog, mem):
        self.prog, self.mem = prog, mem
        self.a = [0] * 16
        self.executed = 0
        self.cycles = 0
        self.counts = {}

    def load(self, addr):
        assert addr % 4 == 0, hex(addr)
        return self.mem[addr]

    def store(self, addr, value):
        assert addr % 4 == 0 and addr in self.mem, hex(addr)
        self.mem[addr] = s32(value)

    def run(self, pc, in_loop_counter=None):
        a = self.a
        lbeg = lend = None
        lcount = 0
        last_dst = None
        last_slow = False
        while True:
            mnem, args, size = self.prog[pc]
            base = mnem.replace(".n", "")
            nxt = pc + size
            self.executed += 1
            self.counts[base] = self.counts.get(base, 0) + 1
            srcs = [reg(x) for x in args[1:] if re.fullmatch(r"a\d+", x)]
            if base in ("s32i",):
                srcs = [reg(args[0]), reg(args[1])]
            if base in ("beqz", "loop"):
                srcs = [reg(args[0])]
            self.cycles += 1 + (1 if last_slow and last_dst in srcs else 0)
            dst, slow = None, False
            if base == "entry":
                a[1] = a[1] - imm(args[1])
            elif base == "retw":
                return
            elif base == "beqz":
                if a[reg(args[0])] == 0:
                    nxt = int(args[1], 16)
            elif base == "loop":
                lcount = a[reg(args[0])] & M32
                assert lcount > 0, "LOOP with zero count runs 2^32 times"
                lbeg, lend = nxt, int(args[1], 16)
            elif base == "l32i":
                dst, slow = reg(args[0]), True
                a[dst] = self.load(a[reg(args[1])] + imm(args[2]))
            elif base == "s32i":
                self.store(a[reg(args[1])] + imm(args[2]), a[reg(args[0])])
            elif base == "movi":
                dst = reg(args[0]); a[dst] = s32(imm(args[1]))
            elif base == "mov":
                dst = reg(args[0]); a[dst] = a[reg(args[1])]
            elif base == "add":
                dst = reg(args[0]); a[dst] = s32(a[reg(args[1])] + a[reg(args[2])])
            elif base == "addi":
                dst = reg(args[0]); a[dst] = s32(a[reg(args[1])] + imm(args[2]))
            elif base == "neg":
                dst = reg(args[0]); a[dst] = s32(-a[reg(args[1])])
            elif base == "min":
                dst = reg(args[0]); a[dst] = min(a[reg(args[1])], a[reg(args[2])])
            elif base == "max":
                dst = reg(args[0]); a[dst] = max(a[reg(args[1])], a[reg(args[2])])
            elif base == "slli":
                dst = reg(args[0]); a[dst] = s32(a[reg(args[1])] << imm(args[2]))
            elif base == "srli":
                dst = reg(args[0]); a[dst] = s32((a[reg(args[1])] & M32) >> imm(args[2]))
            elif base == "mulsh":
                dst, slow = reg(args[0]), True
                a[dst] = s32((a[reg(args[1])] * a[reg(args[2])]) >> 32)
            else:
                raise SystemExit("FAIL: instruction not modelled: %s %s" % (mnem, args))
            last_dst, last_slow = dst, slow
            if lend is not None and nxt == lend:
                lcount -= 1
                if lcount > 0:
                    nxt = lbeg
                else:
                    lbeg = lend = None
            pc = nxt


def main():
    prog, order = parse(sys.argv[1])
    entry = order[0]
    cases = words = mismatches = 0
    per_sample = []
    loop_body = None
    for line in open(sys.argv[2], encoding="ascii"):
        v = [int(x) for x in line.split()]
        n, ch = v[0], v[1]
        coef, hist = v[2:7], v[7:11]
        inp, exp, exp_hist = v[11:11 + 194], v[205:205 + 194], v[399:403]
        mem = {}
        buf, cbase, hbase, stack = 0x10000, 0x20000, 0x30000, 0x40000
        for i, w in enumerate(inp):
            mem[buf + 4 * i] = w
        for i, w in enumerate(coef):
            mem[cbase + 4 * i] = w
        for i, w in enumerate(hist):
            mem[hbase + 4 * i] = w
        for off in range(-256, 0, 4):
            mem[stack + off] = 0
        cpu = Cpu(prog, mem)
        cpu.a[1] = stack
        cpu.a[2], cpu.a[3], cpu.a[4], cpu.a[5] = buf + 4 * ch, n, cbase, hbase
        cpu.run(entry)
        got = [mem[buf + 4 * i] for i in range(194)]
        got_hist = [mem[hbase + 4 * i] for i in range(4)]
        mismatches += sum(1 for x, y in zip(got, exp) if x != y) + (got_hist != exp_hist)
        words += 194
        cases += 1
        if n:
            per_sample.append((n, cpu.executed, cpu.cycles))
    # Marginal cost per sample from two lengths: removes the fixed call overhead.
    by_n = {}
    for n, ins, cyc in per_sample:
        by_n.setdefault(n, (ins, cyc))
    lo, hi = min(by_n), max(by_n)
    ins_per = (by_n[hi][0] - by_n[lo][0]) / (hi - lo)
    cyc_per = (by_n[hi][1] - by_n[lo][1]) / (hi - lo)
    fixed_ins = by_n[lo][0] - ins_per * lo
    fixed_cyc = by_n[lo][1] - cyc_per * lo
    print("eq xtensa sim: cases %d words %d mismatches %d" % (cases, words, mismatches))
    print("eq xtensa sim: per sample %.1f instructions, %.1f modelled cycles; per call %.0f instructions, %.0f modelled cycles"
          % (ins_per, cyc_per, fixed_ins, fixed_cyc))
    if mismatches:
        print("FAIL: eq xtensa loop differs from stageRef")
        return 1
    print("PASS: eq xtensa loop bit-exact with stageRef (assembled ESP32 code, instruction model)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

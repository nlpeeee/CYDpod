// Reuse the caps suite's builders, fork isolation and allocation wrappers.
#define main caps_suite_main
#include "m4a_caps_test.cpp"
#undef main

static Bytes largeMdat(uint64_t size) {
  Bytes b; put32(b, 1); b.insert(b.end(), {'m', 'd', 'a', 't'});
  put32(b, size >> 32); put32(b, size); return b;
}

struct ReviewResult { Result base; bool lcStereo; };
static void review(const char* name, const Bytes& input, const char* want, size_t count) {
  for (size_t slice : {size_t(7), size_t(1024), input.size()}) {
    int fd[2]; if (pipe(fd)) std::abort();
    pid_t pid = fork();
    if (pid == 0) {
      alarm(3);  // Malformed callback payloads must not hang the regression suite.
      g_live = g_peak = g_maxReq = 0;
      size_t frames = 0; bool lcStereo = true; M4aStatus status;
      {
        auto* r = new M4aGuardedReader;
        r->begin([&](const uint8_t* p, size_t n) {
          ++frames;
          lcStereo &= n >= 7 && ((p[2] >> 6) + 1) == 2 &&
                      ((p[2] >> 2) & 15) == 4 && (((p[2] & 1) << 2) | (p[3] >> 6)) == 2;
        });
        status = M4aStatus::ok;
        for (size_t i = 0; i < input.size() && status == M4aStatus::ok; i += slice)
          status = r->write(input.data() + i, std::min(slice, input.size() - i));
        if (status == M4aStatus::ok) status = r->finish();
        delete r;
      }
      ReviewResult result{{status, frames, g_peak, g_maxReq}, lcStereo};
      (void)!write(fd[1], &result, sizeof result); _exit(0);
    }
    close(fd[1]);
    ReviewResult result{{M4aStatus::ok, 0, SIZE_MAX, SIZE_MAX}, false};
    ssize_t got = read(fd[0], &result, sizeof result); int status; waitpid(pid, &status, 0);
    close(fd[0]);
    auto& r = result.base;
    bool ok = got == sizeof result && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
              !std::strcmp(m4aStatusName(r.st), want) && r.frames == count && result.lcStereo &&
              r.peak <= kM4aHeapBudgetBytes && r.maxReq <= kM4aTableBytes;
    std::fprintf(stderr, "%s %s slice=%zu status=%s frames=%zu LC/44.1/stereo=%d peak=%zu maxReq=%zu\n",
                 ok ? "PASS" : "FAIL", name, slice, m4aStatusName(r.st), r.frames, result.lcStereo, r.peak, r.maxReq);
    failures += !ok;
  }
}

int main() {
  const Bytes tables = cat({ftyp(), moov(stsd(), stsz(0, 4, {100,100,100,100}))});
  review("R1 nested ALAC allocation", cat({ftyp(), box("alac", box("alac", Bytes(16, 0), 65536))}), "no_sample_table", 0);
  review("R1 late ESDS override", cat({tables, box("esds", esdsPayload(5,3,1)), box("mdat", frames(4,100))}), "ok", 4);
  for (const char* type : {"alac", "mp4a", "esds", "ac-3"}) {
    review(type, cat({ftyp(), box("moov", box(type, Bytes(32,0))),
                      moov(stsd(), stsz(0,4,{100,100,100,100})), box(type, Bytes(32,0)),
                      box("mdat", frames(4,100))}), "ok", 4);
  }
  // Explicit target-width demonstration even when multilib is unavailable.
  static_assert(uint32_t(0x100000000ull) == 0, "ESP32 payload truncates to zero");
  review("R5 payload 4 GiB", cat({tables, largeMdat(0x100000010ull), frames(1,100)}), "box_too_large", 0);
  review("R5 size UINT32_MAX+1", cat({tables, largeMdat(0x100000000ull)}), "box_too_large", 0);
  review("R5 end overflow", cat({tables, largeMdat(UINT64_MAX)}), "offset_overflow", 0);
  review("R5 largest target size", cat({tables, largeMdat(UINT32_MAX), frames(1,100)}), "ok", 1);
  review("positive", valid(), "ok", 4);
  std::fprintf(stderr, "%s R1/R5: %d failures\n", failures ? "FAIL" : "PASS", failures);
  return failures != 0;
}

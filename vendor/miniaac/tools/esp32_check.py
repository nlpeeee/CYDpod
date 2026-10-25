"""Cross-compile the component without requiring a board or ESP-IDF install.
Usage: python tools/esp32_check.py --toolchain /path/to/xtensa/bin --work build/esp32
Reports target object sizes and per-function STATIC stack estimates, not a
measurement of real-time performance or worst-case dynamic call-chain stack.
"""
import argparse
import json
import subprocess
from pathlib import Path

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--toolchain',type=Path,required=True)
    ap.add_argument('--work',type=Path,required=True)
    args=ap.parse_args(); root=Path(__file__).resolve().parents[1]
    work=args.work.resolve(); work.mkdir(parents=True,exist_ok=True)
    suffix='.exe' if (args.toolchain/'xtensa-esp32-elf-gcc.exe').exists() else ''
    def tool(name): return str(args.toolchain/('xtensa-esp32-elf-'+name+suffix))
    def run(cmd): return subprocess.run(cmd,check=True,capture_output=True,text=True).stdout
    objects=[]
    for src in [root/'src/miniaac.c',*sorted((root/'src/helix').glob('*.c'))]:
        obj=work/(src.stem+'.o'); objects.append(obj)
        run([tool('gcc'),'-std=c99','-DESP32','-Os','-fwrapv','-fno-strict-aliasing',
             '-ffunction-sections','-fdata-sections','-fstack-usage',
             '-I'+str(root/'src'),'-c',str(src),'-o',str(obj)])
    probe=work/'measure.c'
    probe.write_text('#include "miniaac.c"\n'
                     'char miniaac_workspace_measure[sizeof(struct miniaac)];\n'
                     'char miniaac_codec_measure[sizeof(((struct miniaac *)0)->arena)];\n')
    run([tool('gcc'),'-DESP32','-Os','-I'+str(root/'src'),'-c',str(probe),'-o',str(work/'measure.o')])
    nm=run([tool('nm'),'-S','--size-sort',str(work/'measure.o')]); sizes={}
    for line in nm.splitlines():
        cols=line.split()
        if len(cols)==4 and cols[-1].endswith('_measure'):
            sizes[cols[-1]]=int(cols[1],16)
    sections={'text_and_literals':0,'rodata':0,'data':0,'bss':0}
    for obj in objects:
        for line in run([tool('size'),'-A',str(obj)]).splitlines():
            cols=line.split()
            if len(cols)<2 or not cols[1].isdigit(): continue
            name,value=cols[0],int(cols[1])
            if name.startswith(('.text','.literal')): sections['text_and_literals']+=value
            elif name.startswith('.rodata'): sections['rodata']+=value
            elif name.startswith('.data'): sections['data']+=value
            elif name.startswith('.bss'): sections['bss']+=value
    stacks=[]
    for su in work.glob('*.su'):
        for line in su.read_text().splitlines():
            fields=line.split('\t')
            if len(fields)>=3:
                stacks.append({'function':fields[0].rsplit(':',1)[-1],
                               'bytes':int(fields[1]),'kind':fields[2]})
    undefined=run([tool('nm'),'-u',*[str(p) for p in objects]])
    assert not any(line.split()[-1] in {'malloc','calloc','realloc','free','helix_malloc','helix_free'}
                   for line in undefined.splitlines() if line.split())
    assert sizes['miniaac_workspace_measure']+4096<=48*1024
    report={'target':'classic ESP32 / Xtensa','compiler':run([tool('gcc'),'--version']).splitlines()[0],
            'workspace_bytes':sizes['miniaac_workspace_measure'],'codec_arena_bytes':sizes['miniaac_codec_measure'],
            'pcm_bytes':4096,'total_component_ram_bytes':sizes['miniaac_workspace_measure']+4096,
            'object_sections_bytes_before_linker_gc':sections,
            'largest_static_stack_frames':sorted(stacks,key=lambda x:x['bytes'],reverse=True)[:20],
            'heap_allocator_references':False,'hardware_tested':False}
    (work/'esp32-report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))

if __name__=='__main__': main()

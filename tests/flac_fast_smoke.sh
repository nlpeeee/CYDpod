#!/bin/sh
set -eu
OUT=tests/out/flac-fast
mkdir -p "$OUT"
SAMPLES=${CYDPOD_TESTSAMPLES:-/mnt/e/dev/CYDpod/testsamples}
python3 -I tools/stage_flac.py "$OUT/staged" --legacy
gcc -DCYDPOD_FLAC_LEGACY -O2 -fwrapv -fno-strict-aliasing -I"$OUT/staged" -c firmware/cydpod_player/app/flac_core.c -o "$OUT/legacy.o"
g++ -DCYDPOD_FLAC_LEGACY -O2 -std=c++17 -Ivendor/miniflac -Itests/audio_shim tests/flac_bench.cpp "$OUT/legacy.o" -o "$OUT/legacy"
cat > "$OUT/pool.cpp" <<'CPP'
#include "codec_track.h"
#include <cstdio>
int main(int argc,char** argv) {
  if(argc!=3)return 2;
  File file(argv[1],"r"); CodecTrack track;
  if(track.begin(file)!=MINIAAC_OK){printf("REJECT %s\n",track.name());return 3;}
  FILE* out=fopen(argv[2],"wb"); if(!out)return 4;
  for(;;) {
    auto r=track.next();
    if(r==CodecTrack::Pull::eof)break;
    if(r==CodecTrack::Pull::error){fprintf(stderr,"%s\n",track.name());return 5;}
    while(track.pending()){fwrite(track.pcm(),sizeof(M4aStereo),1,out);track.consume(1);}
  }
  fclose(out); track.end(); return 0;
}
CPP
MP=.deps/sketchbook/libraries/arduino-libhelix/src
g++ -Wl,--wrap=xmp3_UnpackFrameHeader -O2 -std=c++17 -Ifirmware/cydpod_player/app -Itests/audio_shim -Itests/catalog_shim -I.tools/staging/miniaac-patched/src -I"$MP" "$OUT/pool.cpp" tests/helix_host_alloc.cpp firmware/cydpod_player/app/codec_track.cpp firmware/cydpod_player/app/resampler.cpp tests/out/miniaac_seek/obj/*.o tests/out/miniaac_seek/mp3/*.o tests/out/miniaac_seek/flac_core.o -o "$OUT/pool"
for name in 'Arctic Monkeys - Suck It and See - 08 - Reckless Serenade.flac' '04 - CAKE - Short Skirt - Long Jacket.flac'; do
 "$OUT/legacy" "$SAMPLES/$name" "$OUT/legacy.pcm"
 "$OUT/pool" "$SAMPLES/$name" "$OUT/compact.pcm"
 cmp "$OUT/legacy.pcm" "$OUT/compact.pcm"
 echo "PASS owner FLAC PCM byte-exact against legacy: $name"
 md5sum "$OUT/compact.pcm"
done
"$OUT/legacy" "$SAMPLES/Sample_BeeMoved_96kHz24bit.flac" /dev/null

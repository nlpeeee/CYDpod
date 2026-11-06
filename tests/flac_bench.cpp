#include "../firmware/cydpod_player/app/flac_pool.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
static FlacState state;
static int32_t right[FlacState::maxBlock + FlacState::slack];
static uint8_t input[2048];
int main(int argc,char** argv) {
  if(argc<3) return 2;
  File f(argv[1],"r"); if(!f) return 3;
  state.right=right; const char* err=state.open(f,input);
  if(err) {printf("REJECT %s\n",err);return 0;}
  FILE* out=fopen(argv[2],"wb"); if(!out)return 4;
  unsigned frames=0; uint64_t samples=0; double us=0;
  for(;;) {
    auto start=std::chrono::steady_clock::now();
    int r=state.frame(f,input,err);
    us+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
    if(r<=0) {if(r<0){fprintf(stderr,"%s\n",err);return 5;}break;}
    ++frames;samples+=state.blockSize;
    for(unsigned i=0;i<state.blockSize;++i){int16_t v[2]={state.clip(state.block[i]),state.clip(right[i])};fwrite(v,2,state.channels,out);}
  }
  fclose(out); printf("PASS decode frames=%u samples=%llu us/frame=%.2f reads=%llu pool-state=%zu\n",frames,(unsigned long long)samples,us/frames,(unsigned long long)hostReadCalls,sizeof(state));
}

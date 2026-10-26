#include "miniaac.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
struct Source { FILE* f; size_t reads=0, bytes=0, largest=0; bool fail=false; };
static int readAt(void* u,uint64_t at,void* dst,size_t n) {
  auto& s=*static_cast<Source*>(u); ++s.reads;s.bytes+=n;s.largest=std::max(s.largest,n);
  return s.fail || fseeko(s.f,at,SEEK_SET) || fread(dst,1,n,s.f)!=n;
}
alignas(MINIAAC_ALIGNMENT) static unsigned char workspace[MINIAAC_WORKSPACE_BYTES];
static int16_t pcm[MINIAAC_PCM_SAMPLES];
static int hook(void* u) { return --*static_cast<int*>(u)==0; }
int main(int argc,char** argv) {
  assert(argc>1);
  for(int a=1;a<argc;++a) {
    Source s{fopen(argv[a],"rb")}; assert(s.f);fseeko(s.f,0,SEEK_END);auto bytes=ftello(s.f);
    miniaac_source source{readAt,&s,uint64_t(bytes)};miniaac* d=nullptr;
    const auto opened=miniaac_open(workspace,sizeof(workspace),&source,&d);
    if(std::strstr(argv[a],".bad")) {
      if(opened==MINIAAC_OK){size_t n=0;assert(miniaac_seek(d,10000,pcm,MINIAAC_PCM_SAMPLES,&n,nullptr,nullptr)==MINIAAC_FORMAT);miniaac_close(d);}
      else assert(opened==MINIAAC_FORMAT);
      fclose(s.f);puts("PASS malformed seek table rejection");continue;
    }
    assert(opened==MINIAAC_OK);
    miniaac_info info{};assert(miniaac_get_info(d,&info)==MINIAAC_OK);
    size_t count=0;
    if(!miniaac_seekable(d)) {
      assert(miniaac_seek(d,0,pcm,MINIAAC_PCM_SAMPLES,&count,nullptr,nullptr)==MINIAAC_UNSUPPORTED);
      miniaac_close(d);fclose(s.f);puts("PASS ADTS seek rejection");continue;
    }
    // Host-only full reference: production never keeps a PCM/index copy.
    std::vector<int16_t> ref;
    miniaac_status rc;
    while((rc=miniaac_decode_next(d,pcm,MINIAAC_PCM_SAMPLES,&count))==MINIAAC_OK)
      ref.insert(ref.end(),pcm,pcm+count);
    assert(rc==MINIAAC_EOF && ref.size()==info.total_frames*info.channels);
    double worst=300;size_t maxReads=0,maxBytes=0;
    uint64_t targets[]={0,1,1023,1024,info.total_frames/2,info.total_frames*9/10,info.total_frames-1,73,info.total_frames/3};
    for(auto target:targets) {
      auto reads=s.reads, transferred=s.bytes;
      assert(miniaac_seek(d,target,pcm,MINIAAC_PCM_SAMPLES,&count,nullptr,nullptr)==MINIAAC_OK);
      assert(count && target*info.channels+count<=ref.size());
      double energy=0,error=0;
      for(size_t i=0;i<count;++i) { double y=ref[target*info.channels+i],e=double(pcm[i])-y;energy+=y*y;error+=e*e; }
      double snr=10*log10((energy+1)/(error+1));if(count>=64 && error) worst=std::min(worst,snr);
      // AAC PNS is reseeded by flush; allow low-level noise variation, require useful agreement.
      if(count>=64 && error && snr<45) {fprintf(stderr,"FAIL seek quality %s frame=%llu snr=%.2f\n",argv[a],(unsigned long long)target,snr);return 1;}
      maxReads=std::max(maxReads,s.reads-reads);maxBytes=std::max(maxBytes,s.bytes-transferred);
      // Continue across subsequent packets/chunks after random access.
      uint64_t cursor=target+count/info.channels;
      for(int following=0;following<3&&cursor<info.total_frames;++following) {
        assert(miniaac_decode_next(d,pcm,MINIAAC_PCM_SAMPLES,&count)==MINIAAC_OK);
        assert(count&&cursor*info.channels+count<=ref.size());
        double signal=0,noise=0;
        for(size_t i=0;i<count;++i){double y=ref[cursor*info.channels+i],e=double(pcm[i])-y;signal+=y*y;noise+=e*e;}
        if(count>=64 && noise) assert(10*log10((signal+1)/(noise+1))>45);
        cursor+=count/info.channels;
      }
    }
    for(int cancel=1;cancel<=2;++cancel) {int budget=cancel;assert(miniaac_seek(d,info.total_frames/2,pcm,MINIAAC_PCM_SAMPLES,&count,hook,&budget)==MINIAAC_CANCELLED);assert(!count);}
    // Cancel on the final hook, after decoder flush/prime, then recover via a later seek.
    int budget=10000;
    assert(miniaac_seek(d,info.total_frames/2,pcm,MINIAAC_PCM_SAMPLES,&count,hook,&budget)==MINIAAC_OK);
    budget=10000-budget;
    assert(miniaac_seek(d,info.total_frames/2,pcm,MINIAAC_PCM_SAMPLES,&count,hook,&budget)==MINIAAC_CANCELLED && !count);
    s.fail=true;rc=miniaac_seek(d,info.total_frames*9/10,pcm,MINIAAC_PCM_SAMPLES,&count,nullptr,nullptr);assert(rc==MINIAAC_IO);s.fail=false;
    assert(miniaac_seek(d,0,pcm,MINIAAC_PCM_SAMPLES,&count,nullptr,nullptr)==MINIAAC_OK);
    assert(miniaac_seek(d,UINT64_MAX,pcm,MINIAAC_PCM_SAMPLES,&count,nullptr,nullptr)==MINIAAC_OK && count==info.channels);
    assert(s.largest<=MINIAAC_INPUT_BYTES);
    printf("PASS seek %s frames=%llu worst_snr=%.2f max_seek_reads=%zu bytes=%zu workspace=%zu\n",argv[a],(unsigned long long)info.total_frames,worst,maxReads,maxBytes,sizeof(workspace));
    miniaac_close(d);fclose(s.f);
  }
}

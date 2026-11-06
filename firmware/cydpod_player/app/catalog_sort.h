#pragma once
// CY-115: bounded-memory sort of track ids for the catalogue orders. Pure C++ (no Arduino).
// Runs of N entries (id + 36 B key = 40 B) are sorted in one arena, spilled to a temp file, then
// merged k ways (k = 16) through 8-entry buffers; more than 16 runs merge in several passes.
// RAM is the arena you pass in and nothing else, whatever the track count (2..65,535).
// The caller's less() must be a total order (ties broken by id) so the result is unique.
#include "library_core.h"
#include <algorithm>
#ifndef CYDSORT_ALLOC
#define CYDSORT_ALLOC(n) malloc(n)
#define CYDSORT_FREE(p) free(p)
#endif

namespace cydlib {
// CT-03 sort key, 36 B per track (CY-115: first eight bytes instead of four, which cuts the unsettled
// comparisons that need two record reads by 2-6x). The prefixes are folded like compareText (ASCII
// case, zero padded), big endian, so the integer order equals the string order whenever they differ.
struct SortKey{uint32_t title,title2,artist,artist2,album,album2,artistCrc,albumCrc;uint16_t disc,track;};
struct SortEntry{SortKey key;uint16_t id,pad;};  // raw 40 B in the temp files
static_assert(sizeof(SortEntry)==40,"sort entry layout");
using CrcFn=uint32_t(*)(uint32_t,const void*,size_t);
inline void foldPrefix(const char* s,uint32_t& first,uint32_t& second){
  uint32_t v[2]={0,0};bool end=false;
  for(int i=0;i<8;++i){unsigned c=0;if(!end){c=(uint8_t)s[i];if(!c)end=true;else if(c<128)c=unsigned(tolower(int(c)));}v[i>>2]=(v[i>>2]<<8)|c;}
  first=v[0];second=v[1];
}
inline uint32_t foldCrc(CrcFn crc,uint32_t value,const char* s){
  for(;;++s){unsigned c=(uint8_t)*s;if(c<128)c=unsigned(tolower(int(c)));const uint8_t b=uint8_t(c);value=crc(value,&b,1);if(!*s)return value;}
}
// `r` must have terminated strings. One key serves all three orders.
inline void makeSortKey(SortKey& k,const Record& r,CrcFn crc){
  char folder[256];albumFolder(r,folder);
  foldPrefix(r.title,k.title,k.title2);foldPrefix(r.artist,k.artist,k.artist2);foldPrefix(r.album,k.album,k.album2);
  k.artistCrc=foldCrc(crc,0,r.artist);
  k.albumCrc=foldCrc(crc,foldCrc(crc,foldCrc(crc,0,r.album),albumOwner(r)),folder);
  k.disc=r.disc;k.track=r.track;
}
// -1/+1 when the keys settle the order exactly as compare() would, 0 when only the records can.
inline int keyCompare(const SortKey& a,const SortKey& b,Order order){
  if(order==Order::Artist){
    if(a.artist!=b.artist)return a.artist<b.artist?-1:1;
    if(a.artist2!=b.artist2)return a.artist2<b.artist2?-1:1;
    if(a.artistCrc!=b.artistCrc)return 0;
  }
  if(order!=Order::Title){
    if(a.albumCrc!=b.albumCrc)return a.album!=b.album?(a.album<b.album?-1:1):a.album2!=b.album2?(a.album2<b.album2?-1:1):0;
    if(a.disc!=b.disc)return a.disc<b.disc?-1:1;
    if(a.track!=b.track)return a.track<b.track?-1:1;
  }
  return a.title!=b.title?(a.title<b.title?-1:1):a.title2!=b.title2?(a.title2<b.title2?-1:1):0;
}

// Everything the sort needs from its environment. Temp files 0 and 1 are random access; the
// sort opens each only while needed and closes both on every exit path (the caller deletes them).
struct SortHost{
  virtual bool key(uint16_t id,SortKey& k)=0;                         // read the record, build its key
  virtual bool less(const SortEntry& a,const SortEntry& b,bool& result)=0;  // false: I/O error
  virtual bool open(int file,bool truncate)=0;
  virtual void close(int file)=0;
  virtual bool read(int file,uint32_t at,void* p,size_t n)=0;
  virtual bool write(int file,uint32_t at,const void* p,size_t n)=0;
  virtual bool put(const uint16_t* ids,size_t n)=0;                   // final order, appended in sequence
  virtual void progress(uint32_t done,uint32_t total)=0;              // one call per entry handled
};
struct SortStats{uint32_t runs=0,passes=0,runEntries=0,comparisons=0,keyCalls=0,storeReads=0,storeWrites=0,
  storeReadBytes=0,storeWriteBytes=0,maxOpen=0;size_t arenaBytes=0;};

namespace sortdetail {
constexpr unsigned Fan=16,Buf=8;
constexpr size_t MergeBytes=Fan*Buf*sizeof(SortEntry)+512;  // cursor buffers + output buffer
template<class Less> bool heapSort(SortEntry* a,int n,Less less){
  auto sift=[&](int root,int end)->bool{
    while(root*2+1<end){int child=root*2+1;bool r=false;
      if(child+1<end){if(!less(a[child],a[child+1],r))return false;if(r)++child;}
      if(!less(a[root],a[child],r))return false;if(!r)break;
      SortEntry t=a[root];a[root]=a[child];a[child]=t;root=child;}
    return true;};
  for(int root=n/2;root>0;)if(!sift(--root,n))return false;
  for(int end=n-1;end>0;--end){SortEntry t=a[0];a[0]=a[end];a[end]=t;if(!sift(0,end))return false;}
  return true;
}
}  // namespace sortdetail

// Sorts ids 0..n-1 and streams the order to host.put(). arenaBudget bounds the only allocation.
inline bool externalSort(SortHost& host,uint32_t n,size_t arenaBudget,SortStats& st){
  using namespace sortdetail;
  st=SortStats{};
  if(n>65535)return false;
  if(!n)return true;
  size_t cap=arenaBudget/sizeof(SortEntry);if(cap>n)cap=n;
  const uint32_t run=uint32_t(cap);
  if(run<64&&run<n)return false;  // also rejects a zero budget
  const uint32_t runs=(n+run-1)/run;
  if(runs>1&&run*sizeof(SortEntry)<MergeBytes)return false;
  uint32_t passes=0;for(uint32_t r=runs;r>1;r=(r+Fan-1)/Fan)++passes;
  st.runs=runs;st.passes=passes;st.runEntries=run;st.arenaBytes=run*sizeof(SortEntry);
  struct Arena{SortEntry* p;~Arena(){CYDSORT_FREE(p);}} arena{(SortEntry*)CYDSORT_ALLOC(st.arenaBytes)};
  if(!arena.p)return false;
  bool isOpen[2]={false,false};unsigned open=0;
  struct Files{SortHost& h;bool* o;~Files(){for(int i=0;i<2;++i)if(o[i])h.close(i);}} files{host,isOpen};
  auto openFile=[&](int f)->bool{if(!host.open(f,true))return false;isOpen[f]=true;if(++open>st.maxOpen)st.maxOpen=open;return true;};
  auto closeFile=[&](int f){if(isOpen[f]){host.close(f);isOpen[f]=false;--open;}};
  auto cmp=[&](const SortEntry& a,const SortEntry& b,bool& r){++st.comparisons;return host.less(a,b,r);};
  auto store=[&](int f,uint32_t at,const void* p,size_t bytes)->bool{++st.storeWrites;st.storeWriteBytes+=uint32_t(bytes);return host.write(f,at,p,bytes);};
  const uint32_t total=n*(1+passes);uint32_t done=0;
  if(runs>1&&!openFile(0))return false;
  for(uint32_t r=0;r<runs;++r){
    const uint32_t first=r*run,count=std::min<uint32_t>(run,n-first);
    for(uint32_t i=0;i<count;++i){
      SortEntry& e=arena.p[i];memset(&e,0,sizeof(e));e.id=uint16_t(first+i);++st.keyCalls;
      if(!host.key(e.id,e.key))return false;
      host.progress(++done,total);
    }
    if(!heapSort(arena.p,int(count),cmp))return false;
    if(runs==1){  // the whole library fits one run: no temp file
      uint16_t ids[128];
      for(uint32_t i=0;i<count;i+=128){const uint32_t m=std::min<uint32_t>(128,count-i);
        for(uint32_t j=0;j<m;++j)ids[j]=arena.p[i+j].id;if(!host.put(ids,m))return false;}
    }else if(!store(0,first*uint32_t(sizeof(SortEntry)),arena.p,count*sizeof(SortEntry)))return false;
  }
  // Merge passes. Pass p reads file `in` (runs of `len` entries) and writes file `in^1`,
  // or the sink on the last pass. Merged runs land where their inputs started, so a run's
  // file position is always start*40.
  int in=0;uint32_t len=run,nRuns=runs;
  uint8_t* outBuf=(uint8_t*)(arena.p+Fan*Buf);
  while(nRuns>1){
    const bool last=nRuns<=Fan;const int out=in^1;
    if(!last&&!openFile(out))return false;
    uint32_t outAt=0,outN=0;  // entries (or ids when last) waiting in outBuf
    const uint32_t outCap=last?256:uint32_t(512/sizeof(SortEntry));
    auto flush=[&]()->bool{
      if(!outN)return true;
      const bool ok=last?host.put((const uint16_t*)outBuf,outN):store(out,outAt*uint32_t(sizeof(SortEntry)),outBuf,outN*sizeof(SortEntry));
      outAt+=outN;outN=0;return ok;};
    for(uint32_t g=0;g<nRuns;g+=Fan){
      const unsigned k=std::min<uint32_t>(Fan,nRuns-g);
      uint32_t pos[Fan],end[Fan];unsigned have[Fan],at[Fan];uint8_t heap[Fan];unsigned size=0;
      auto refill=[&](unsigned c)->bool{
        const unsigned cnt=std::min<uint32_t>(Buf,end[c]-pos[c]);
        ++st.storeReads;st.storeReadBytes+=cnt*uint32_t(sizeof(SortEntry));
        if(!host.read(in,pos[c]*uint32_t(sizeof(SortEntry)),arena.p+c*Buf,cnt*sizeof(SortEntry)))return false;
        pos[c]+=cnt;have[c]=cnt;at[c]=0;return true;};
      auto head=[&](unsigned c)->const SortEntry&{return arena.p[c*Buf+at[c]];};
      auto sift=[&](unsigned i)->bool{
        for(;;){unsigned m=2*i+1;if(m>=size)return true;bool r=false;
          if(m+1<size){if(!cmp(head(heap[m+1]),head(heap[m]),r))return false;if(r)++m;}
          if(!cmp(head(heap[m]),head(heap[i]),r))return false;if(!r)return true;
          const uint8_t t=heap[i];heap[i]=heap[m];heap[m]=t;i=m;}};
      outAt=g*len;
      for(unsigned c=0;c<k;++c){pos[c]=(g+c)*len;end[c]=std::min<uint32_t>(n,pos[c]+len);if(!refill(c))return false;heap[size++]=uint8_t(c);}
      for(int i=int(size)/2-1;i>=0;--i)if(!sift(unsigned(i)))return false;
      while(size){
        const unsigned c=heap[0];const SortEntry& e=head(c);
        if(last)((uint16_t*)outBuf)[outN++]=e.id;else{memcpy(outBuf+outN*sizeof(SortEntry),&e,sizeof(e));++outN;}
        host.progress(++done,total);
        if(outN==outCap&&!flush())return false;
        if(++at[c]==have[c]){
          if(pos[c]<end[c]){if(!refill(c))return false;}
          else heap[0]=heap[--size];
        }
        if(size&&!sift(0))return false;
      }
      if(!flush())return false;
    }
    if(!last){closeFile(in);in=out;}
    len*=Fan;nRuns=(nRuns+Fan-1)/Fan;
  }
  return true;
}
}  // namespace cydlib

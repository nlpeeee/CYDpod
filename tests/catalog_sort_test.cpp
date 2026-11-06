// CY-115 host test for catalog_sort.h: parity with the old full sort, 10,000-track sort, memory bound,
// file-slot bound, failure cleanup, and the SD time model. No Arduino, no SD card.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
// Count every allocation the sorter makes (fixtures use the normal heap and are not counted).
static size_t liveBytes=0,peakBytes=0,allocCalls=0;
struct AllocHdr{size_t n,pad;};
static void* countedAlloc(size_t n){AllocHdr* h=(AllocHdr*)malloc(sizeof(AllocHdr)+n);h->n=n;liveBytes+=n;peakBytes=std::max(peakBytes,liveBytes);++allocCalls;return h+1;}
static void countedFree(void* p){if(!p)return;AllocHdr* h=(AllocHdr*)p-1;liveBytes-=h->n;free(h);}
#define CYDSORT_ALLOC(n) countedAlloc(n)
#define CYDSORT_FREE(p) countedFree(p)
#include "catalog_sort.h"
using namespace cydlib;
static int failures=0;
static void check(bool ok,const char* what){if(!ok){++failures;printf("FAIL %s\n",what);}}
static uint32_t crc32z(uint32_t v,const void* p,size_t n){  // same value as esp_rom_crc32_le
  uint32_t c=~v;for(size_t i=0;i<n;++i){c^=((const uint8_t*)p)[i];for(int k=0;k<8;++k)c=(c>>1)^(0xEDB88320u&(0u-(c&1)));}return ~c;}

// ---- synthetic library ----
static uint32_t seed=1;
static uint32_t rnd(){seed=seed*1664525u+1013904223u;return seed>>8;}
static const char* W[]={"Love","Night","River","Gold","Dream","Fire","Blue","Paper","Silver","Home","Run","Light","Shadow","Summer","Winter",
  "City","Ocean","Heart","Moon","Star","Road","Glass","Echo","Stone","Wild","Slow","Dance","Rain","Tiger","Velvet","Cold","Burn","Wave","Sky"};
static const char* CommonTitles[]={"Intro","Outro","Home","Love Song","The Night","The Light","Untitled","Interlude","Dont Stop","Don't Stop"};
static const char* CommonAlbums[]={"Greatest Hits","Live","Demos","Singles","Unplugged"};
static std::string words(int lo,int hi){int k=lo+int(rnd()%(hi-lo+1));std::string s;if(rnd()%100<15)s="The ";
  for(int i=0;i<k;++i){if(i)s+=' ';s+=W[rnd()%34];}return s;}
static std::vector<Record> makeLibrary(unsigned n,uint32_t seedValue){
  seed=seedValue;std::vector<std::string> artists;
  const unsigned A=n/10+3;
  for(unsigned i=0;i<A;++i){std::string a=words(1,2);
    if(rnd()%100<5)a+=" Bj\xc3\xb6rk";else if(rnd()%100<4)a="\xc3\x85sa "+a;
    artists.push_back(a);
    if(rnd()%12==0){std::string up=a;for(char& c:up)c=char(toupper((unsigned char)c));artists.push_back(up);}}  // case-twin: ties under compareText
  std::vector<Record> lib;lib.reserve(n);unsigned counter=0;
  while(lib.size()<n){
    const std::string& artist=artists[rnd()%artists.size()];
    const std::string album=rnd()%100<25?CommonAlbums[rnd()%5]:words(1,3);
    const bool comp=rnd()%100<5;const unsigned discs=rnd()%100<20?2:1,tracks=6+rnd()%9;
    for(unsigned d=1;d<=discs&&lib.size()<n;++d)for(unsigned t=1;t<=tracks&&lib.size()<n;++t){
      Record r{};const std::string title=rnd()%100<20?CommonTitles[rnd()%10]:words(1,3);
      const std::string who=comp?artists[rnd()%artists.size()]:artist;
      char path[256];snprintf(path,sizeof path,"/music/%s/%s%s/%02u %s %u.m4a",who.c_str(),album.c_str(),discs>1?(d==1?"/Disc 1":"/CD 2"):"",t,title.c_str(),counter++);
      copyText(r.path,path);copyText(r.title,title.c_str());copyText(r.artist,who.c_str());copyText(r.album,album.c_str());
      r.disc=rnd()%50==0?0:uint16_t(d);r.track=rnd()%40==0?0:uint16_t(t);r.compilation=comp;
      if(rnd()%100<3)copyText(r.albumArtist,artist.c_str());
      lib.push_back(r);}
  }
  for(size_t i=lib.size();i>1;--i)std::swap(lib[i-1],lib[rnd()%i]);  // scan order is not sorted order
  return lib;
}

// ---- host: records in RAM, temp files as vectors, every cost counted ----
struct TestHost:SortHost{
  const std::vector<Record>& lib;Order mode;
  std::vector<uint8_t> file[2];bool opened[2]={false,false};int openNow=0,maxOpen=0;
  std::vector<uint16_t> out;
  uint64_t keyReads=0,tieReads=0,ties=0,puts=0,lastDone=0,lastTotal=0;bool progressOk=true;
  int failWriteAt=-1,failReadAt=-1,failLessAt=-1,failKeyAt=-1,failPutAt=-1;int writes=0,reads=0,lessCalls=0,keyCalls=0;
  TestHost(const std::vector<Record>& l,Order o):lib(l),mode(o){}
  bool key(uint16_t id,SortKey& k) override{
    if(++keyCalls==failKeyAt)return false;++keyReads;makeSortKey(k,lib[id],crc32z);return true;}
  bool less(const SortEntry& a,const SortEntry& b,bool& result) override{
    if(++lessCalls==failLessAt)return false;
    int c=keyCompare(a.key,b.key,mode);
    if(!c){++ties;tieReads+=2;c=compare(lib[a.id],lib[b.id],mode);}
    result=c?c<0:a.id<b.id;return true;}
  bool open(int i,bool) override{if(opened[i])return false;opened[i]=true;file[i].clear();if(++openNow>maxOpen)maxOpen=openNow;return true;}
  void close(int i) override{if(opened[i]){opened[i]=false;--openNow;}}
  bool read(int i,uint32_t at,void* p,size_t n) override{
    if(++reads==failReadAt||!opened[i]||at+n>file[i].size())return false;memcpy(p,file[i].data()+at,n);return true;}
  bool write(int i,uint32_t at,const void* p,size_t n) override{
    if(++writes==failWriteAt||!opened[i])return false;if(file[i].size()<at+n)file[i].resize(at+n);memcpy(file[i].data()+at,p,n);return true;}
  bool put(const uint16_t* ids,size_t n) override{if(++puts==uint64_t(failPutAt))return false;out.insert(out.end(),ids,ids+n);return true;}
  void progress(uint32_t done,uint32_t total) override{if(done<=lastDone||done>total)progressOk=false;lastDone=done;lastTotal=total;}
};
static std::vector<uint16_t> referenceSort(const std::vector<Record>& lib,Order mode,uint64_t* comparisons=nullptr){
  std::vector<uint16_t> ids(lib.size());for(size_t i=0;i<ids.size();++i)ids[i]=uint16_t(i);uint64_t cmps=0;
  sortIds(ids.data(),int(ids.size()),[&](uint16_t x,uint16_t y,bool& less){++cmps;int c=compare(lib[x],lib[y],mode);less=c?c<0:x<y;return true;});
  if(comparisons)*comparisons=cmps;return ids;}
static bool sortedAndStable(const std::vector<Record>& lib,const std::vector<uint16_t>& ids,Order mode){
  if(ids.size()!=lib.size())return false;std::vector<char> seen(lib.size(),0);
  for(uint16_t id:ids){if(id>=lib.size()||seen[id])return false;seen[id]=1;}
  for(size_t i=1;i<ids.size();++i){const int c=compare(lib[ids[i-1]],lib[ids[i]],mode);if(c>0||(c==0&&ids[i-1]>ids[i]))return false;}
  return true;}
static const Order Orders[3]={Order::Title,Order::Artist,Order::Album};
static const char* OrderName[3]={"title","artist","album"};
static const size_t Arena=36*1024;

int main(){
  // 1. Parity with the old full sort (heapsort, two records per comparison) at boundary sizes and 2,000 tracks.
  {bool all=true;const unsigned sizes[]={1,2,63,64,999,1000,1285,1286,1500,2000};
   for(unsigned n:sizes){const auto lib=makeLibrary(n,100+n);
     for(int o=0;o<3;++o){TestHost h(lib,Orders[o]);SortStats st;
       const bool ok=externalSort(h,n,Arena,st)&&h.out==referenceSort(lib,Orders[o])&&h.progressOk&&h.lastDone==h.lastTotal;
       if(!ok){all=false;printf("  parity mismatch n=%u order=%s\n",n,OrderName[o]);}}}
   // The same 2,000 tracks through a 8 KB arena (more runs, two merge passes) must agree too.
   const auto lib=makeLibrary(2000,2000);
   for(int o=0;o<3;++o){TestHost h(lib,Orders[o]);SortStats st;
     const bool ok=externalSort(h,2000,8192,st)&&h.out==referenceSort(lib,Orders[o])&&st.passes==1;
     if(!ok){all=false;printf("  small-arena parity mismatch order=%s runs=%u passes=%u\n",OrderName[o],st.runs,st.passes);}}
   check(all,"parity");if(all)printf("PASS parity: external sort order == old full-sort order (n=1..2000, 3 orders, 36 KB and 8 KB arenas)\n");}

  // 2. 10,000 tracks: sorted, stable, equal to the old sort; also through a small arena (two merge passes).
  {const auto lib=makeLibrary(10000,10000);bool all=true;
   for(int o=0;o<3;++o)for(size_t arena:{Arena,size_t(8192)}){
     TestHost h(lib,Orders[o]);SortStats st;
     const bool ok=externalSort(h,10000,arena,st)&&sortedAndStable(lib,h.out,Orders[o])&&h.out==referenceSort(lib,Orders[o]);
     printf("  n=10000 %-6s arena=%5zu runs=%2u (%u entries) passes=%u keyReads=%llu tieCompares=%llu storeReads=%u storeWrites=%u comparisons=%u maxOpen=%d\n",
       OrderName[o],arena,st.runs,st.runEntries,st.passes,(unsigned long long)h.keyReads,(unsigned long long)h.ties,st.storeReads,st.storeWrites,st.comparisons,h.maxOpen);
     all=all&&ok;}
   check(all,"10000");if(all)printf("PASS 10000: sorted, stable (ties by id), permutation, equal to the old full sort, all 3 orders\n");}

  // 3. Memory bound: the sorter's own allocations, whatever the track count.
  {size_t first=0;bool ok=true;const unsigned sizes[]={2000,10000,40000,65535};
   for(unsigned n:sizes){const auto lib=makeLibrary(n,n);
     liveBytes=peakBytes=allocCalls=0;TestHost h(lib,Order::Title);SortStats st;
     const bool good=externalSort(h,n,Arena,st)&&sortedAndStable(lib,h.out,Order::Title);
     printf("  n=%5u peak=%zu B allocations=%zu live-after=%zu runs=%u passes=%u\n",n,peakBytes,allocCalls,liveBytes,st.runs,st.passes);
     ok=ok&&good&&peakBytes<=40960&&allocCalls==1&&liveBytes==0&&peakBytes==st.arenaBytes;
     if(n>=10000){if(!first)first=peakBytes;ok=ok&&peakBytes==first;}}
   {liveBytes=peakBytes=0;const auto lib=makeLibrary(70000/10,1);TestHost h(lib,Order::Title);SortStats st;
    ok=ok&&!externalSort(h,65536,Arena,st)&&liveBytes==0;}  // ids are 16 bit: 65,536 is refused
   check(ok,"memory");if(ok)printf("PASS memory: one arena <= 40960 B (%zu B at n>=10,000), identical for 10,000/40,000/65,535 tracks, freed on exit\n",first);}

  // 4. File slots: the catalogue handle plus the temp files that are open at once.
  {const auto lib=makeLibrary(10000,7);int worst=0;bool ok=true;
   for(size_t arena:{Arena,size_t(8192),size_t(6144)}){TestHost h(lib,Order::Album);SortStats st;
     const bool good=externalSort(h,10000,arena,st);worst=std::max(worst,h.maxOpen);
     ok=ok&&good&&int(st.maxOpen)==h.maxOpen&&h.openNow==0&&h.maxOpen+1<=4;
     printf("  arena=%zu: %u merge pass(es), %d temp file(s) open at once, +1 catalogue = %d of 4\n",arena,st.passes,h.maxOpen,h.maxOpen+1);}
   check(ok,"file slots");if(ok)printf("PASS file slots: at most %d regular files open during a sort (catalogue + %d temp), limit 4\n",worst+1,worst);}

  // 5. Failures stop the sort cleanly: every temp file closed, arena freed.
  {const auto lib=makeLibrary(3000,3);bool ok=true;
   for(int kind=0;kind<5;++kind)for(int at:{1,2,5,40}){
     liveBytes=0;TestHost h(lib,Order::Album);SortStats st;
     if(kind==0)h.failWriteAt=std::min(at,10);else if(kind==1)h.failReadAt=at;else if(kind==2)h.failLessAt=at*500;else if(kind==3)h.failKeyAt=at*70;else h.failPutAt=std::min(at,10);
     const bool done=externalSort(h,3000,8192,st);
     if(done||h.openNow||liveBytes){ok=false;printf("  failure case kind=%d at=%d done=%d open=%d live=%zu\n",kind,at,done,h.openNow,liveBytes);}}
   {TestHost h(lib,Order::Album);SortStats st;h.failPutAt=1;liveBytes=0;ok=ok&&!externalSort(h,3000,Arena,st)&&h.openNow==0&&liveBytes==0;}
   check(ok,"failure cleanup");if(ok)printf("PASS failure cleanup: I/O errors in key/compare/read/write/put abort the sort, files closed, arena freed\n");}

  // 6. SD time model. Random 712 B record read: 2/3.5/5 ms. Run-file refill/spill and sink writes: 1 ms each
  // (a 320-480 B transfer inside one or two sectors). Old fallback = no key table: every comparison reads two records.
  {const double lo=2,mid=3.5,hi=5,io=1.0;
   printf("\n| tracks | order | old (no keys) | key table in RAM (n <= 2,500 only) | new, 36 KB arena | new, passes | tie rate |\n|---|---|---|---|---|---|---|\n");
   for(unsigned n:{2000u,5000u,10000u})for(int o=0;o<3;++o){
     const auto lib=makeLibrary(n,n*3+o);uint64_t oldCmps=0;referenceSort(lib,Orders[o],&oldCmps);
     TestHost h(lib,Orders[o]);SortStats st;check(externalSort(h,n,Arena,st),"time model sort");
     const double oldS[3]={oldCmps*2*lo/1000,oldCmps*2*mid/1000,oldCmps*2*hi/1000};
     auto newS=[&](double t){return (double(h.keyReads)+double(h.tieReads))*t/1000+(double(st.storeReads)+st.storeWrites+h.puts)*io/1000;};
     // Old with a key table (fits only up to roughly 2,400 tracks): n key reads, then two record reads per unsettled comparison.
     char withKeys[48]="does not fit (240 KB at 10,000)";
     if(n<=2500){std::vector<SortKey> keys(n);for(unsigned i=0;i<n;++i)makeSortKey(keys[i],lib[i],crc32z);uint64_t ties=0;
       std::vector<uint16_t> ids(n);for(unsigned i=0;i<n;++i)ids[i]=uint16_t(i);
       sortIds(ids.data(),int(n),[&](uint16_t x,uint16_t y,bool& less){int c=keyCompare(keys[x],keys[y],Orders[o]);if(!c){++ties;c=compare(lib[x],lib[y],Orders[o]);}less=c?c<0:x<y;return true;});
       snprintf(withKeys,sizeof withKeys,"%.0f s (%.0f-%.0f)",(n+ties*2)*mid/1000,(n+ties*2)*lo/1000,(n+ties*2)*hi/1000);}
     printf("| %u | %s | %.0f s (%.0f-%.0f) | %s | %.0f s (%.0f-%.0f) | %u runs, %u merge | %.1f%% of %u compares |\n",n,OrderName[o],oldS[1],oldS[0],oldS[2],withKeys,
       newS(mid),newS(lo),newS(hi),st.runs,st.passes,100.0*h.ties/std::max<uint32_t>(1,st.comparisons),st.comparisons);}
  }
  printf(failures?"\nFAILED (%d)\n":"\nALL PASS\n",failures);
  return failures?1:0;
}

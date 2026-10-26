#include "catalog.h"
#include "storage.h"
#include "playlist.h"
#include "settings.h"
#include "classic_navigation.h"
#include <vector>
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <iostream>
using Bytes=std::vector<uint8_t>;
Settings g_settings;
static const uint16_t* liveQueue=nullptr;
static int liveLength=0,currentTrack=0,audioCalls=0;
void settings_save(){}
bool audio_ready(){return true;}
void audio_setQueue(const uint16_t* tracks,int n,int,bool){liveQueue=tracks;liveLength=n;++audioCalls;}
void audio_playTrack(int i){currentTrack=i;++audioCalls;}
int audio_currentTrack(){return currentTrack;}
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static Bytes atom(uint32_t type,const Bytes& payload){uint32_t size=uint32_t(payload.size()+8);Bytes b;for(int i=3;i>=0;--i)b.push_back(uint8_t(size>>(i*8)));for(int i=3;i>=0;--i)b.push_back(uint8_t(type>>(i*8)));b.insert(b.end(),payload.begin(),payload.end());return b;}
static void append(Bytes& a,const Bytes& b){a.insert(a.end(),b.begin(),b.end());}
static Bytes field(uint32_t key,const std::string& text,unsigned encoding=1){Bytes p={0,0,0,uint8_t(encoding),0,0,0,0};p.insert(p.end(),text.begin(),text.end());return atom(key,atom(cydlib::fcc('d','a','t','a'),p));}
static Bytes number(uint32_t key,int n){Bytes p={0,0,0,0,0,0,0,0,0,0,uint8_t(n>>8),uint8_t(n),0,0,0,0};return atom(key,atom(cydlib::fcc('d','a','t','a'),p));}
static Bytes tagged(const std::string& title,const std::string& artist,const std::string& album,int track,int disc=1,const std::string& owner=""){
  using cydlib::fcc;Bytes fields;
  append(fields,field(fcc(0xa9,'n','a','m'),title));append(fields,field(fcc(0xa9,'A','R','T'),artist));append(fields,field(fcc(0xa9,'a','l','b'),album));
  if(!owner.empty())append(fields,field(fcc('a','A','R','T'),owner));
  append(fields,number(fcc('t','r','k','n'),track));append(fields,number(fcc('d','i','s','k'),disc));
  Bytes meta={0,0,0,0};append(meta,atom(fcc('i','l','s','t'),fields));Bytes top=atom(fcc('m','d','a','t'),Bytes(4096,7));
  append(top,atom(fcc('m','o','o','v'),atom(fcc('u','d','t','a'),atom(fcc('m','e','t','a'),meta))));return top;
}
static void put(const char* relative,const Bytes& bytes){auto p=SD.root/relative;std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write((const char*)bytes.data(),bytes.size());check(bool(f),"write fixture");}
static bool readVector(void* p,uint64_t at,void* dst,size_t n){auto& b=*(Bytes*)p;if(at>b.size()||n>b.size()-at)return false;memcpy(dst,b.data()+at,n);return true;}
static std::vector<std::string> phasesSeen;static int lastPercent=-1;
static void progressSeen(const char* phase,int done,int total){if(phasesSeen.empty()||phasesSeen.back()!=phase)phasesSeen.push_back(phase);lastPercent=total<0?done:-1;}
static std::vector<int> libraryOrder(){
  std::vector<int> order;uint16_t ids[2000];CatalogEntry e;
  for(int i=0;i<storage_browseCount({CatalogKind::Songs,0});++i)if(storage_browseEntry({CatalogKind::Songs,0},i,e))order.push_back(e.track);
  for(int g=0;g<storage_browseCount({CatalogKind::Artists,0});++g){int n=storage_browseTracks({CatalogKind::ArtistSongs,uint16_t(g)},ids,2000);order.push_back(-1);order.insert(order.end(),ids,ids+n);}
  for(int g=0;g<storage_browseCount({CatalogKind::Albums,0});++g){int n=storage_browseTracks({CatalogKind::AlbumSongs,uint16_t(g)},ids,2000);order.push_back(-2);order.insert(order.end(),ids,ids+n);}
  return order;
}
static int find(CatalogView view,const char* label){CatalogEntry e;for(int i=0;i<storage_browseCount(view);++i)if(storage_browseEntry(view,i,e)&&e.label==label)return i;throw std::runtime_error(std::string("missing ")+label);}
static CatalogView child(CatalogView v,const char* label){CatalogEntry e;check(storage_browseEntry(v,find(v,label),e)&&e.navigates,"group child");return e.child;}
int main(){try{
  using namespace cydlib;
  cydclassic::Gesture gesture;gesture.down(20,60,0,0);check(gesture.activate(0),"row tap");
  gesture.move(20,10,-1,true);check(!gesture.activate(0)&&gesture.claimed,"drag suppresses tap");
  gesture.move(20,60,0,true);check(!gesture.activate(0),"return after drag still cancelled");
  gesture.down(20,300,0,-11);check(gesture.activate(-11),"footer control tap");
  gesture.move(68,300,-12,false);check(!gesture.activate(-11),"button exit cancels");
  check(cydclassic::clampTop(100,12,6)==6&&cydclassic::clampTop(-1,12,6)==0,"scroll clamp");
  struct TestPage{int top=0;};cydclassic::Navigation<TestPage> nav;nav.current().top=17;
  for(int i=1;i<8;++i)check(nav.push(TestPage{}),"push navigation");check(!nav.push(TestPage{}),"bounded navigation");
  while(nav.back()){}check(nav.current().top==17,"back restores page state");
  uint16_t order[]={3,1,4,0,2};check(sortIds(order,5,[](uint16_t a,uint16_t b,bool& result){result=a<b;return true;}),"heap sort");
  for(int i=0;i<5;++i)check(order[i]==i,"sorted IDs");
  check(!sortIds(order,5,[](uint16_t,uint16_t,bool&){return false;}),"I/O error stops sort");
  std::ifstream real("metadata-real.m4a",std::ios::binary);Bytes realBytes((std::istreambuf_iterator<char>(real)),{});
  check(!realBytes.empty(),"generated real M4A");Record realTags{};Tags realParser(readVector,&realBytes,realBytes.size());
  check(realParser.parse(realTags)&&!strcmp(realTags.title,"Actual tagged fixture")&&!strcmp(realTags.artist,"Fixture Artist")&&realTags.track==2,"real FFmpeg M4A tags");
  Bytes utf=tagged("alpha","Artist A","Greatest Hits",2);Record parsed{};Tags parser(readVector,&utf,utf.size());check(parser.parse(parsed),"moov-last metadata");check(!strcmp(parsed.title,"alpha")&&parsed.track==2,"read tags");
  Bytes unicode=tagged("  \xe4\xbd\xa0\xe5\xa5\xbd  ","Artist","Album",1);Record unicodeTags{};Tags unicodeParser(readVector,&unicode,unicode.size());
  check(unicodeParser.parse(unicodeTags)&&!strcmp(unicodeTags.title,"\xe4\xbd\xa0\xe5\xa5\xbd"),"UTF-8 and whitespace trim");
  Bytes invalidUtf=tagged(std::string("x\xffy",3),"Artist","Album",1);Record normalized{};Tags invalidParser(readVector,&invalidUtf,invalidUtf.size());
  check(invalidParser.parse(normalized)&&!strcmp(normalized.title,"x\xef\xbf\xbdy"),"malformed UTF-8 replaced");
  Bytes utf16fields=field(fcc(0xa9,'n','a','m'),std::string("\x4f\x60\x59\x7d",4),2),meta={0,0,0,0};
  append(meta,atom(fcc('i','l','s','t'),utf16fields));Bytes utf16=atom(fcc('m','o','o','v'),atom(fcc('m','e','t','a'),meta));Record utf16Tags{};Tags utf16Parser(readVector,&utf16,utf16.size());
  check(utf16Parser.parse(utf16Tags)&&!strcmp(utf16Tags.title,"\xe4\xbd\xa0\xe5\xa5\xbd"),"UTF-16BE tags");
  for(size_t n=1;n<16;++n){Bytes truncated(utf.begin(),utf.begin()+n);Tags p(readVector,&truncated,truncated.size());Record r{};check(!p.parse(r),"reject truncated headers");}
  Bytes bad={0,0,0,4,'m','o','o','v'};Record r{};Tags malformed(readVector,&bad,bad.size());check(!malformed.parse(r),"reject undersized atom");
  SD.root=std::filesystem::current_path()/("catalog-card-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  put("music/A/Disc 1/z.m4a",tagged("Zebra","Artist A","Greatest Hits",1,1));
  put("music/A/Disc 2/a.m4a",tagged("alpha","Artist A","Greatest Hits",1,2));
  put("music/B/a.m4a",tagged("alpha","Artist B","Greatest Hits",2));
  put("music/B/b.m4a",tagged("Beta","Artist B","Greatest Hits",1));
  put("music/untagged.m4a",atom(fcc('m','d','a','t'),Bytes(64,0)));
  put("music/Mix/one.m4a",tagged("Mix A","Artist A","Summer",1,1,"Various Artists"));
  put("music/Mix/two.m4a",tagged("Mix B","Artist B","Summer",2,1,"Various Artists"));
  check(storage_begin(nullptr)&&storage_catalogReady()&&storage_catalogRebuilt(),"initial catalogue build");
  check(storage_trackCount()==7,"track count");
  CatalogView songs{CatalogKind::Songs,0},artists{CatalogKind::Artists,0},albums{CatalogKind::Albums,0};
  check(storage_browseCount(artists)==3&&storage_browseCount(albums)==4,"artist/album groups and same-title separation");
  CatalogEntry e;check(storage_browseEntry(songs,0,e)&&e.label=="alpha","title sort");
  auto a=child(artists,"Artist A");check(storage_browseCount(a)==3,"artist albums plus all songs");
  auto album=child(a,"Greatest Hits");uint16_t ids[20];int n=storage_browseTracks(album,ids,20);check(n==2,"multi-disc album membership");
  check(storage_trackInfo(ids[0],r)&&r.disc==1,"disc order first");check(storage_trackInfo(ids[1],r)&&r.disc==2,"disc order second");
  auto mix=child(albums,"Summer");check(storage_browseTracks(mix,ids,20)==2,"compilation shares album");
  check(storage_begin(nullptr)&&!storage_catalogRebuilt(),"warm cache reuse");
  check(storage_refreshNextBoot(),"refresh marker");hostWriteBudget=0;check(storage_begin(nullptr)&&storage_catalogStale()&&storage_trackCount()==7,"card-full retains previous generation");hostWriteBudget=-1;
  check(!std::filesystem::exists(SD.root/".sys/library.refresh"),"refresh marker removed before the failing build (CT-04)");
  check(storage_begin(nullptr)&&!storage_catalogRebuilt()&&!storage_catalogStale()&&storage_trackCount()==7,"failed build is not repeated on the next boot");
  check(storage_refreshNextBoot()&&storage_begin(nullptr)&&storage_catalogRebuilt(),"explicit refresh rebuilds");
  // Damage newest B header; A remains readable and can be used without rewriting music.
  put(".sys/library-b.cat",Bytes(12,0));check(storage_begin(nullptr)&&storage_catalogReady()&&storage_trackCount()==7,"damaged newest falls back");
  put("music/B/new.m4a",tagged("A new song","Artist B","Greatest Hits",3));check(storage_begin(progressSeen)&&storage_catalogRebuilt()&&storage_trackCount()==8,"new file triggers rebuild");
  {const std::vector<std::string> want={"Reading song tags","Preparing to sort","Sorting songs","Sorting artists","Sorting albums","Grouping artists","Grouping albums","Finishing","Checking library"};
   check(phasesSeen==want,"progress names every rebuild phase (CT-03)");}
  // CT-07: a card that mounts but cannot create a folder is used read-only.
  std::filesystem::remove_all(SD.root/"bg");put("music/B/readonly.m4a",tagged("Added while read-only","Artist B","Greatest Hits",4));
  hostReadOnly=true;check(storage_begin(nullptr)&&storage_readOnly()&&storage_catalogReady()&&storage_trackCount()==8&&!storage_catalogRebuilt()&&storage_catalogStale(),"read-only card keeps the existing catalogue (CT-07)");
  check(!storage_refreshNextBoot(),"no refresh marker on a read-only card");hostReadOnly=false;
  std::filesystem::remove(SD.root/"music/B/readonly.m4a");check(storage_begin(nullptr)&&!storage_readOnly()&&!storage_catalogRebuilt()&&!storage_catalogStale(),"writable again after the card recovers");
  check(!storage_browseEntry(songs,-1,e)&&!storage_browseEntry(songs,2000,e),"range checks");
  check(storage_browseTracks(songs,ids,1)==0,"queue capacity guarded");
  playlist_begin();check(playlist_playView(songs,0),"play catalogue selection");
  std::vector<uint16_t> original(liveQueue,liveQueue+liveLength);int before=audioCalls;
  hostFreeHeap=12000;check(!playlist_playView(songs,0)&&playlist_memoryRefused(),"heap floor refuses staging, reported as memory (CT-08)");hostFreeHeap=64000;
  check(catalog_canAllocate(16)&&!catalog_canAllocate(60000)&&(hostFreeHeap=13000,catalog_canAllocate(16))&&(hostFreeHeap=12300,!catalog_canAllocate(16)),"floor is 12 KiB plus the staged bytes (CT-08)");hostFreeHeap=64000;
  hostLargestBlock=4000;check(!playlist_playView(songs,0)&&playlist_memoryRefused(),"contiguous margin kept");hostLargestBlock=64000;
  check(audioCalls==before&&std::equal(original.begin(),original.end(),liveQueue),"low-memory selection preserves playback");
  hostReadBudget=0;check(!playlist_playView(songs,0),"failed queue read rejected");hostReadBudget=-1;
  check(audioCalls==before&&std::equal(original.begin(),original.end(),liveQueue),"failed queue read preserves playback");
  uint16_t staged[20];check(playlist_preview(2,staged,20)>0,"preview folder");
  check(audioCalls==before&&std::equal(original.begin(),original.end(),liveQueue),"preview leaves live queue untouched");
  check(!playlist_playView(songs,2000)&&audioCalls==before,"invalid selection preserves playback");
  check(storage_browseEntry(songs,0,e)&&currentTrack==e.track,"selected song starts title queue");
  SD.root=std::filesystem::current_path()/("catalog-large-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  for(int i=0;i<1000;++i){char path[100],name[64],artist[32],albumName[32];
    snprintf(path,sizeof(path),"music/Group%d/Album%d/%04d.m4a",(i/50)%10,i/50,i);
    snprintf(name,sizeof(name),"Song %04d",999-i);snprintf(artist,sizeof(artist),"Artist %02d",(i/50)%10);snprintf(albumName,sizeof(albumName),"Album %02d",i/50);
    put(path,tagged(name,artist,albumName,i%50+1));
  }
  check(storage_begin(nullptr)&&storage_trackCount()==1000&&storage_browseCount(artists)==10&&storage_browseCount(albums)==20,"1000-track catalogue");
  check(storage_browseEntry(songs,0,e)&&e.label=="Song 0000","large title index");
  check(storage_begin(nullptr)&&!storage_catalogRebuilt(),"large warm boot");
  {const std::vector<int> withKeys=libraryOrder();
   hostLargestBlock=1000;check(storage_refreshNextBoot()&&storage_begin(nullptr)&&storage_catalogRebuilt(),"rebuild without sort keys");hostLargestBlock=64000;
   check(libraryOrder()==withKeys&&withKeys.size()>1000,"key sort and record sort give the same order (CT-03)");}
  // CT-03: varied titles, artists, albums (short, mixed case, shared prefixes): keys and records must order identically.
  SD.root=std::filesystem::current_path()/("catalog-mixed-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  {unsigned seed=12345;auto next=[&](unsigned n){seed=seed*1103515245u+12345u;return (seed>>16)%n;};
   auto word=[&](unsigned max){std::string w;unsigned len=1+next(max);for(unsigned i=0;i<len;++i){char c=char('a'+next(4));if(next(3)==0)c=char(c-32);w.push_back(c);}return w;};
   std::vector<std::string> artistNames,albumNames;for(int i=0;i<12;++i)artistNames.push_back(word(7));for(int i=0;i<20;++i)albumNames.push_back(word(9));
   for(int i=0;i<400;++i){char path[64];snprintf(path,sizeof(path),"music/F%d/%03d.m4a",int(next(6)),i);
     put(path,tagged(word(10),artistNames[next(12)],albumNames[next(20)],int(1+next(6)),int(1+next(2)),next(5)==0?artistNames[next(12)]:""));}
   check(storage_begin(nullptr)&&storage_trackCount()==400,"mixed catalogue");
   const std::vector<int> withKeys=libraryOrder();
   hostLargestBlock=1000;check(storage_refreshNextBoot()&&storage_begin(nullptr)&&storage_catalogRebuilt(),"mixed rebuild without keys");hostLargestBlock=64000;
   check(libraryOrder()==withKeys,"mixed data: key sort equals record sort (CT-03)");}
  // CT-02: 2,002 files: the extra two are skipped and counted, the library still builds. (A 256+ character path cannot be created by the MSVC host shim, so that branch is covered by reading only.)
  SD.root=std::filesystem::current_path()/("catalog-limit-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  for(int i=0;i<2002;++i){char path[64],name[32];snprintf(path,sizeof(path),"music/G%d/%04d.m4a",i/200,i);snprintf(name,sizeof(name),"L%04d",i);put(path,tagged(name,"Limit Artist","Limit Album",i%99+1));}
  check(storage_begin(nullptr)&&storage_catalogReady()&&storage_trackCount()==2000&&storage_skippedCount()==2,"files beyond 2000 skipped and counted, library still built (CT-02)");
  std::cout<<"PASS catalogue: real/synthetic M4A tags, artists/albums/titles, multidisc, compilations, 1000 tracks, warm cache, full-card recovery, corrupt generation, refresh, queue isolation, gestures and bounded navigation\n";
  return 0;
}catch(const std::exception& e){std::cerr<<"FAIL catalogue: "<<e.what()<<"\n";return 1;}}

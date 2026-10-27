#include "command_queue.h"
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
  gesture.move(50,300,-11,false);check(gesture.activate(-11),"sliding inside a button is not a cancelled drag");
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
  { // F-08: MP3 (ID3v2.2/2.3/2.4, ID3v1), FLAC (Vorbis comments) and WAV (LIST/INFO) tags.
    auto s=[](const char* t){return Bytes(t,t+strlen(t));};
    auto be=[](uint32_t n){return Bytes{uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n)};};
    auto ss=[](uint32_t n){return Bytes{uint8_t((n>>21)&127),uint8_t((n>>14)&127),uint8_t((n>>7)&127),uint8_t(n&127)};};
    auto le=[](uint32_t n){return Bytes{uint8_t(n),uint8_t(n>>8),uint8_t(n>>16),uint8_t(n>>24)};};
    auto cat=[](std::initializer_list<Bytes> parts){Bytes out;for(const Bytes& p:parts)out.insert(out.end(),p.begin(),p.end());return out;};
    auto frame3=[&](const char* id,const Bytes& body,bool v4){return cat({s(id),v4?ss(uint32_t(body.size())):be(uint32_t(body.size())),Bytes{0,0},body});};
    auto id3=[&](unsigned version,const Bytes& frames){Bytes b=cat({s("ID3"),Bytes{uint8_t(version),0,0},ss(uint32_t(frames.size())),frames});append(b,Bytes(2048,0));return b;};
    auto mp3Body=Bytes{0xff,0xfb,0x90,0x00,1,2,3,4};
    uint8_t scratch[712];
    auto parseOf=[&](const Bytes& file,Record& r){Tags p(readVector,(void*)&file,file.size(),scratch);r={};return p.parse(r);};
    {Bytes f=cat({frame3("TIT2",cat({Bytes{0},s("Caf\xe9 Title")}),false),
        frame3("TPE1",cat({Bytes{1,0xff,0xfe},Bytes{'Z',0,0xeb,0}}),false),            // UTF-16LE with BOM: "Z\xc3\xab"
        frame3("TALB",cat({Bytes{3},s("Alb\xe4\xbd\xa0")}),false),
        frame3("APIC",Bytes(1500,0x42),false),                                         // picture: skipped by offset
        frame3("TRCK",cat({Bytes{0},s("5/12")}),false),frame3("TPOS",cat({Bytes{0},s("2/3")}),false),frame3("TCMP",cat({Bytes{0},s("1")}),false)});
     Bytes file=id3(3,f);append(file,mp3Body);Record r;
     check(parseOf(file,r)&&!strcmp(r.title,"Caf\xc3\xa9 Title")&&!strcmp(r.artist,"Z\xc3\xab")&&!strcmp(r.album,"Alb\xe4\xbd\xa0")&&r.track==5&&r.disc==2&&r.compilation,"ID3v2.3 Latin-1, UTF-16 BOM, UTF-8, picture skipped");
     for(size_t n=0;n<=file.size();n+=7){Bytes cut(file.begin(),file.begin()+n);Record t;parseOf(cut,t);} // truncations never crash or overread
    }
    {Bytes f=cat({frame3("TIT2",cat({Bytes{3},s("Four\xe4\xbd\xa0")}),true),frame3("TPE2",cat({Bytes{2},Bytes{0x00,'B',0x00,'B'}}),true),frame3("TPE1",cat({Bytes{3},Bytes{'A','r','t',0,'S','e','c','o','n','d'}}),true)});
     Bytes file=id3(4,f);append(file,mp3Body);Record r;
     check(parseOf(file,r)&&!strcmp(r.title,"Four\xe4\xbd\xa0")&&!strcmp(r.albumArtist,"BB")&&!strcmp(r.artist,"Art"),"ID3v2.4 syncsafe sizes, UTF-16BE, first of multiple values");}
    {Bytes f=cat({s("TT2"),Bytes{0,0,6},Bytes{0},s("Old v"),s("TP1"),Bytes{0,0,6},Bytes{0},s("Old A"),s("TRK"),Bytes{0,0,2},Bytes{0},s("9")});
     Bytes file=cat({s("ID3"),Bytes{2,0,0},ss(uint32_t(f.size())),f,mp3Body});Record r;
     check(parseOf(file,r)&&!strcmp(r.title,"Old v")&&!strcmp(r.artist,"Old A")&&r.track==9,"ID3v2.2 three-letter frames");}
    {Bytes file=mp3Body;file.resize(600,0);Bytes v1(128,0);memcpy(v1.data(),"TAG",3);memcpy(v1.data()+3,"Tagged One",10);memcpy(v1.data()+33,"Some Band",9);memcpy(v1.data()+63,"The \xe9lbum",10);v1[126]=4;append(file,v1);Record r;
     check(parseOf(file,r)&&!strcmp(r.title,"Tagged One")&&!strcmp(r.artist,"Some Band")&&!strcmp(r.album,"The \xc3\xa9lbum")&&r.track==4,"ID3v1 only (headerless MP3)");}
    {Bytes f=cat({frame3("TIT2",cat({Bytes{0},s("v2 title")}),false)});Bytes file=id3(3,f);append(file,mp3Body);file.resize(file.size()+200,0);Bytes v1(128,0);memcpy(v1.data(),"TAG",3);memcpy(v1.data()+3,"v1 title",8);memcpy(v1.data()+33,"v1 artist",9);append(file,v1);Record r;
     check(parseOf(file,r)&&!strcmp(r.title,"v2 title")&&!strcmp(r.artist,"v1 artist"),"ID3v1 only fills what ID3v2 left empty");}
    {Bytes si(34,0);
     Bytes comments=cat({le(1),s("x"),le(7)}); // vendor "x", seven comments
     auto kv=[&](const char* t){return cat({le(uint32_t(strlen(t))),s(t)});};
     Bytes body=cat({comments,kv("TITLE=Flac \xe4\xbd\xa0"),kv("artist=The Band"),kv("ALBUM=Disc1"),kv("ALBUMARTIST=Owner!"),kv("TRACKNUMBER=7"),kv("DISCNUMBER=1/2"),kv("COMPILATION=1")});
     auto block=[&](uint8_t type,bool last,const Bytes& b){Bytes h{uint8_t(type|(last?128:0)),uint8_t(b.size()>>16),uint8_t(b.size()>>8),uint8_t(b.size())};return cat({h,b});};
     Bytes file=cat({s("fLaC"),block(0,false,si),block(6,false,Bytes(5000,9)),block(4,true,body),Bytes(64,0xff)});Record r;
     check(parseOf(file,r)&&!strcmp(r.title,"Flac \xe4\xbd\xa0")&&!strcmp(r.artist,"The Band")&&!strcmp(r.album,"Disc1")&&!strcmp(r.albumArtist,"Owner!")&&r.track==7&&r.disc==1&&r.compilation,"FLAC Vorbis comments, PICTURE skipped, case-insensitive keys");
     for(size_t n=0;n<=file.size();n+=11){Bytes cut(file.begin(),file.begin()+n);Record t;parseOf(cut,t);}
     Bytes hostile=file;hostile[hostile.size()-70]=0xff; for(size_t i=0;i<60;++i)hostile[40+i]=uint8_t(i*37);Record h;parseOf(hostile,h);}
    {auto chunk=[&](const char* id,const Bytes& b){Bytes c=cat({s(id),le(uint32_t(b.size())),b});if(b.size()&1)c.push_back(0);return c;};
     auto sub=[&](const char* id,const char* t){return chunk(id,cat({s(t),Bytes{0}}));};
     Bytes fmt(16,0);fmt[0]=1;fmt[2]=2;Bytes info=cat({s("INFO"),sub("INAM","Wave Title"),sub("IART","Wave Artist"),sub("IPRD","Wave Album"),sub("ITRK","3")});
     Bytes riff=cat({s("WAVE"),chunk("fmt ",fmt),chunk("LIST",info),chunk("data",Bytes(5000,1))});Bytes file=cat({s("RIFF"),le(uint32_t(riff.size())),riff});Record r;
     check(parseOf(file,r)&&!strcmp(r.title,"Wave Title")&&!strcmp(r.artist,"Wave Artist")&&!strcmp(r.album,"Wave Album")&&r.track==3,"WAV LIST/INFO");
     for(size_t n=0;n<=file.size();n+=13){Bytes cut(file.begin(),file.begin()+n);Record t;parseOf(cut,t);}
     Bytes streamed=cat({s("RIFF"),le(0xffffffffu),s("WAVE"),chunk("fmt ",fmt),chunk("LIST",info),s("data"),le(0xffffffffu),Bytes(100,1)});
     check(parseOf(streamed,r)&&!strcmp(r.title,"Wave Title"),"WAV with unsized data chunk still reads tags before it");}
  }
  {cyd::CommandQueue<3> q;uint8_t key=0;
   check(q.push(0x44)&&q.push(0x4b)&&q.push(0x4c)&&!q.push(0x46),"AVRCP full queue refuses without overwrite");
   check(q.pop(key)&&key==0x44&&q.push(0x46)&&q.pop(key)&&key==0x4b&&q.pop(key)&&key==0x4c&&q.pop(key)&&key==0x46&&!q.pop(key),"AVRCP FIFO wrap and empty");
   check(!cyd::currentCompletion(2,3)&&cyd::currentCompletion(3,3),"stale decoder completion cannot replace explicit command");}
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
  auto finishSearch=[](){for(int i=0;i<2000&&storage_searchBusy();++i)storage_searchTick();check(!storage_searchBusy()&&!storage_searchFailed(),"bounded search completes");};
  storage_searchStart("ALPHA artist b");check(storage_searchBuilding(),"first search builds compact index");
  storage_searchTick();storage_searchStart("zEB artist a");finishSearch();check(storage_searchCount()==1,"new key cancels old scan during build; case-insensitive words");
  check(storage_trackLabel(storage_searchTracks()[0])=="Zebra","title and artist matched");
  storage_searchStart("ArTiSt");check(!storage_searchBuilding(),"warm search reuses index");finishSearch();check(storage_searchCount()==7,"artist substring across catalogue (including Unknown Artist)");
  storage_searchStart("nothing matches");finishSearch();check(storage_searchCount()==0,"no matches");
  storage_searchStart("alpha");storage_searchTick();storage_searchStart("beta");finishSearch();check(storage_searchCount()==1&&storage_trackLabel(storage_searchTracks()[0])=="Beta","latest query wins after partial scan");
  // Interrupted cache and card-full build are expendable; retry rebuilds.
  put(".sys/search.idx",Bytes(20,0));hostWriteBudget=0;storage_searchStart("alpha");check(storage_searchFailed(),"index write failure reported");hostWriteBudget=-1;
  storage_searchStart("ALPHA");finishSearch();check(storage_searchCount()==2,"failed build recovers without changing catalogue");
  // F-11: leaving Search mid-build keeps the progress; the next Search resumes instead of restarting at entry 0.
  put(".sys/search.idx",Bytes(20,0));storage_searchStart("alpha");check(storage_searchBuilding(),"cold index builds");
  storage_searchTick();const int kept=storage_searchProgress();check(kept>0&&kept<100,"partial build progress");
  storage_searchCancel();check(!storage_searchBusy(),"cancel stops the scan");
  storage_searchStart("alpha");check(storage_searchBuilding()&&storage_searchProgress()==kept,"restart resumes the build where it stopped");
  finishSearch();check(storage_searchCount()==2,"resumed index is complete and correct");
  storage_searchStart("alpha");check(!storage_searchBuilding(),"resumed index validated and reused");finishSearch();check(storage_searchCount()==2,"resumed index matches");

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
  check(!storage_refreshNextBoot(),"no refresh marker on a read-only card");
  storage_searchStart("new song");finishSearch();check(storage_searchCount()==1,"read-only search streams catalogue without writing");hostReadOnly=false;
  std::filesystem::remove(SD.root/"music/B/readonly.m4a");check(storage_begin(nullptr)&&!storage_readOnly()&&!storage_catalogRebuilt()&&!storage_catalogStale(),"writable again after the card recovers");
  check(!storage_browseEntry(songs,-1,e)&&!storage_browseEntry(songs,2000,e),"range checks");
  check(storage_browseTracks(songs,ids,1)==0,"queue capacity guarded");
  playlist_begin();check(playlist_playView(songs,0),"play catalogue selection");
  {File audioSlot=SD.open(storage_trackPath(currentTrack));check(bool(audioSlot),"simulate concurrent audio file");
   hostWriteBudget=0;check(!playlist_toggleFavourite(currentTrack)&&playlist_favouriteFailed()&&!playlist_isFavourite(currentTrack),"failed favourite add leaves RAM state unchanged");hostWriteBudget=-1;
   check(playlist_toggleFavourite(currentTrack)&&!playlist_favouriteFailed()&&playlist_isFavourite(currentTrack),"favourite add committed");
   {File newline=SD.open("/.sys/newline-test",FILE_WRITE);check(newline.println(String("x"))==3,"shim matches Arduino println CRLF byte count");}
   const int other=(currentTrack+1)%storage_trackCount();
   check(playlist_toggleFavourite(other)&&!playlist_favouriteFailed(),"second favourite copies existing line");
   {std::ifstream saved(SD.root/".sys/fav.txt",std::ios::binary);std::string text((std::istreambuf_iterator<char>(saved)),{});
    check(text==std::string(storage_trackPath(currentTrack).c_str())+"\n"+storage_trackPath(other).c_str()+"\n","favourites persist exact LF bytes");}
   hostWriteBudget=storage_trackPath(currentTrack).length();
   check(playlist_toggleFavourite(other)&&playlist_favouriteFailed()&&playlist_isFavourite(other),"short newline write preserves favourites");hostWriteBudget=-1;
   check(!playlist_toggleFavourite(other)&&!playlist_favouriteFailed(),"remove one favourite while copying another");
   hostReadOnly=true;check(playlist_toggleFavourite(currentTrack)&&playlist_favouriteFailed()&&playlist_isFavourite(currentTrack),"failed favourite removal preserves RAM and old file");hostReadOnly=false;
   check(!playlist_toggleFavourite(currentTrack)&&!playlist_favouriteFailed()&&!playlist_isFavourite(currentTrack),"favourite removal committed");
  }
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
  storage_searchStart("LIMIT");finishSearch();check(storage_searchCount()==50,"search bounded to 50 results in 2000 tracks");
  check(std::filesystem::file_size(SD.root/".sys/search.idx")==20+2000*64,"search index is 64 bytes per track plus header");
  storage_searchStart("limit");hostReadBudget=0;storage_searchTick();check(storage_searchFailed(),"index read failure clears partial results");hostReadBudget=-1;
  SD.root=std::filesystem::current_path()/("catalog-codecs-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  put("music/a.MP3",Bytes(12,0));put("music/b.WAV",Bytes(12,0));put("music/c.m4a",tagged("AAC","Artist","Album",1));put("music/d.FLAC",Bytes(12,0));
  put("music/._ignored.mp3",Bytes(12,0));put("music/._ignored.flac",Bytes(12,0));put("music/no.txt",Bytes(12,0));
  check(storage_begin(nullptr)&&storage_trackCount()==4,"MP3/WAV/M4A/FLAC case-insensitive file filter with fallback titles");
  std::cout<<"PASS catalogue codec filter: MP3/WAV/M4A/FLAC listed, dotfiles ignored, filename fallback titles\n";
  std::cout<<"PASS search: streamed 64-byte keys, word/substrings, case folding, query cancellation, warm reuse, 2000 tracks/50 results, failed writes and read-only fallback\n";
  std::cout<<"PASS catalogue: real/synthetic M4A tags, artists/albums/titles, multidisc, compilations, 1000 tracks, warm cache, full-card recovery, corrupt generation, refresh, queue isolation, gestures and bounded navigation\n";
  return 0;
}catch(const std::exception& e){std::cerr<<"FAIL catalogue: "<<e.what()<<"\n";return 1;}}

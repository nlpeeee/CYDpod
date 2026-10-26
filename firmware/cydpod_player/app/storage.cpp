#include "storage.h"
#include "catalog.h"
#ifndef CYDPOD_CATALOG_HOST
#include "board.h"
#include <SPI.h>
#endif
#include <algorithm>
#include <esp_rom_crc.h>

using namespace cydlib;
static const char* CATALOGS[2]={"/.sys/library-a.cat","/.sys/library-b.cat"};
static const char* REFRESH="/.sys/library.refresh";
static Header header{};
static int active=-1;
static bool stale=false,rebuilt=false;
// Every access to the shared catalogue handle `cat` and the record `cache` takes
// `mutex` (UI task and decode task both call in). validate() and the boot-time
// build use their own handles, and work[] is touched at boot only, never while
// `cat` is in use.
static SemaphoreHandle_t mutex=nullptr;
static uint32_t scanCrc=0;
static int scanned=0;
static bool scanOk=true;
static int plannedTracks=0;  // tag-scan total, known from the cheap first pass
static StorageProgressFn reportFn=nullptr;  // set only while a rebuild runs
static const char* reportPhase="";
// Progress and a 1 ms yield (idle task and watchdog feed) for the long loops. total<0: percent.
static void report(int done,int total){if(reportFn)reportFn(reportPhase,done,total);}
static void phase(const char* name){reportPhase=name;report(0,-1);}
static void breathe(){delay(1);}
static bool readOnly=false;  // card mounted but a folder could not be created: never write (CT-07)
static int skipped=0;  // files left out of the catalogue: path too long or over MaxTracks
// Shared boot scratch, not a whole-library cache.
static Record work[2];
// One open handle on the active catalogue plus the last record read through it
// (CT-06): lookups no longer open, seek and close a 1.4 MB file every time, and
// no 712 B Record has to live on the 5 KB loop-task stack (CT-01).
static File cat;
static Record cache;
static int cacheIndex=-1;

static void closeCat(){if(cat)cat.close();cat=File();cacheIndex=-1;}
static void openCat(){closeCat();if(active>=0)cat=SD.open(CATALOGS[active]);}

static uint32_t crc(uint32_t value,const void* p,size_t n){return esp_rom_crc32_le(value,(const uint8_t*)p,n);}
static bool readAt(File& f,uint32_t at,void* p,size_t n){return (f.position()==at||f.seek(at))&&f.read((uint8_t*)p,n)==n;}
static bool fileRecord(File& f,int i,Record& r){return readAt(f,sizeof(Header)+uint32_t(i)*sizeof(Record),&r,sizeof(r));}
static bool validRecord(const Record& r){
  return memchr(r.path,0,sizeof(r.path))&&memchr(r.title,0,sizeof(r.title))&&memchr(r.artist,0,sizeof(r.artist))&&
    memchr(r.album,0,sizeof(r.album))&&memchr(r.albumArtist,0,sizeof(r.albumArtist))&&
    !strncmp(r.path,"/music/",7)&&!strstr(r.path,"/../");
}
static bool groupAt(File& f,uint32_t at,int index,Group& g){return readAt(f,at+index*sizeof(Group),&g,sizeof(g));}
static bool idAt(File& f,uint32_t at,int index,uint16_t& id){return readAt(f,at+index*2,&id,2)&&id<header.tracks;}
static bool validate(const char* path,Header& h){
  File f=SD.open(path);if(!f)return false;
  bool ok=readAt(f,0,&h,sizeof(h));
  const uint32_t sum=h.headerChecksum;h.headerChecksum=0;
  ok=ok&&!memcmp(h.magic,"CYDCAT1",8)&&sum==crc(0,&h,sizeof(h));h.headerChecksum=sum;
  uint32_t at=sizeof(Header)+uint32_t(h.tracks)*sizeof(Record);
  ok=ok&&h.tracks<=MaxTracks&&h.artists<=h.tracks&&h.albums<=h.tracks&&h.artistAlbums<=h.tracks;
  ok=ok&&h.songsAt==at;at+=uint32_t(h.tracks)*2;
  ok=ok&&h.artistsAt==at;at+=uint32_t(h.tracks)*2;
  ok=ok&&h.albumsAt==at;at+=uint32_t(h.tracks)*2;
  ok=ok&&h.artistGroupsAt==at;at+=uint32_t(h.artists)*sizeof(Group);
  ok=ok&&h.albumGroupsAt==at;at+=uint32_t(h.albums)*sizeof(Group);
  ok=ok&&h.artistAlbumGroupsAt==at;at+=uint32_t(h.artistAlbums)*sizeof(Group);
  ok=ok&&h.bytes==at&&f.size()==at;
  if(!ok){f.close();return false;}
  uint8_t* b=(uint8_t*)&work[1];uint32_t value=0,remaining=h.bytes-sizeof(Header);f.seek(sizeof(Header));  // boot scratch, 712 B >= 512
  const uint32_t body=remaining;uint32_t chunks=0;
  while(remaining){size_t n=std::min<size_t>(remaining,512);if(f.read(b,n)!=n){ok=false;break;}value=crc(value,b,n);remaining-=n;
    if(!(++chunks&63)){breathe();report(int(uint64_t(body-remaining)*50/body),-1);}}
  ok=ok&&value==h.checksum;
  for(uint32_t p=h.songsAt;ok&&p<h.artistGroupsAt;p+=2){uint16_t id;ok=readAt(f,p,&id,2)&&id<h.tracks;}
  for(unsigned kind=0;ok&&kind<3;++kind){uint32_t base=kind==0?h.artistGroupsAt:kind==1?h.albumGroupsAt:h.artistAlbumGroupsAt;
    int count=kind==0?h.artists:kind==1?h.albums:h.artistAlbums;
    for(int i=0;ok&&i<count;++i){Group g{};ok=groupAt(f,base,i,g)&&g.representative<h.tracks&&g.count&&uint32_t(g.first)+g.count<=h.tracks;
      if(kind==0)ok=ok&&g.subCount&&uint32_t(g.subFirst)+g.subCount<=h.artistAlbums;}
  }
  Record& r=work[0];for(int i=0;ok&&i<h.tracks;++i){ok=fileRecord(f,i,r)&&validRecord(r);
    if(!(i&63)){breathe();report(50+i*50/h.tracks,-1);}}
  f.close();return ok;
}

// Two passes: cheap change detection, then tag extraction only on rebuild.
static bool tagRead(void* p,uint64_t at,void* dst,size_t bytes){
  File& f=*(File*)p;return at<=UINT32_MAX&&f.seek(uint32_t(at))&&f.read((uint8_t*)dst,bytes)==bytes;
}
// Leaf of the scan, kept out of scanDir so the (up to three) recursive frames stay small (CT-01).
#ifdef _MSC_VER
#define CYD_NOINLINE
#else
#define CYD_NOINLINE __attribute__((noinline))
#endif
static CYD_NOINLINE void addTrack(const String& p,File& f,File* output,StorageProgressFn progress){
  if(scanned>=MaxTracks||p.length()>=256){++skipped;return;}  // CT-02: skip the file, keep building
  const uint32_t size=f.size(),modified=uint32_t(f.getLastWrite());
  scanCrc=crc(scanCrc,p.c_str(),p.length()+1);scanCrc=crc(scanCrc,&size,4);scanCrc=crc(scanCrc,&modified,4);
  if(output){Record& r=work[0];memset(&r,0,sizeof(r));copyText(r.path,p.c_str());Tags tags(tagRead,&f,size,(uint8_t*)&work[1]);
    if(!tags.parse(r)){memset(r.title,0,sizeof(r)-sizeof(r.path));Serial.printf("catalog: tags unavailable %s\n",p.c_str());}
    fallback(r);if(output->write((const uint8_t*)&r,sizeof(r))!=sizeof(r))scanOk=false;
  }
  ++scanned;if(progress&&output)progress("Reading song tags",scanned,plannedTracks);
}
static void scanDir(const String& path,int depth,File* output,StorageProgressFn progress){
  File dir=SD.open(path);if(!dir||!dir.isDirectory()){scanOk=false;return;}
  for(File f=dir.openNextFile();f;f=dir.openNextFile()){
    const char* name=f.name();
    if(name[0]!='.'){
      String p=path+"/"+name;
      if(f.isDirectory()){
        f.close();if(depth<2)scanDir(p,depth+1,output,progress);
      }else{
        String low=p;low.toLowerCase();
        if(low.endsWith(".m4a"))addTrack(p,f,output,progress);
      }
    }
    f.close();if(!scanOk)break;
  }
  dir.close();
}
static bool writeAll(File& f,const void* p,size_t n){return f.write((const uint8_t*)p,n)==n;}
// CT-03: sort on a 24 B in-RAM key per track instead of reading two 712 B records for
// every comparison (about 130 k comparisons for 2,000 tracks). The records are read only
// when the keys cannot settle an order. Without enough heap the keys are skipped and the
// old record-by-record comparison is used, slower but identical.
struct SortKey{uint32_t title,artist,album,artistCrc,albumCrc;uint16_t disc,track;};
// First four bytes folded like compareText (ASCII case, zero padded), big endian: the
// integer order equals the string order whenever the prefixes differ.
static uint32_t foldPrefix(const char* s){
  uint32_t v=0;bool end=false;
  for(int i=0;i<4;++i){unsigned c=0;if(!end){c=(uint8_t)s[i];if(!c)end=true;else if(c<128)c=unsigned(tolower(int(c)));}v=(v<<8)|c;}
  return v;
}
static uint32_t foldCrc(uint32_t value,const char* s){
  for(;;++s){unsigned c=(uint8_t)*s;if(c<128)c=unsigned(tolower(int(c)));const uint8_t b=uint8_t(c);value=crc(value,&b,1);if(!*s)return value;}
}
// -1/+1 when the keys settle the order exactly as compare() would, 0 when only the records can.
static int keyCompare(const SortKey& a,const SortKey& b,Order order){
  if(order==Order::Artist){
    if(a.artist!=b.artist)return a.artist<b.artist?-1:1;
    if(a.artistCrc!=b.artistCrc)return 0;
  }
  if(order!=Order::Title){
    if(a.albumCrc!=b.albumCrc)return a.album!=b.album?(a.album<b.album?-1:1):0;
    if(a.disc!=b.disc)return a.disc<b.disc?-1:1;
    if(a.track!=b.track)return a.track<b.track?-1:1;
  }
  return a.title!=b.title?(a.title<b.title?-1:1):0;
}
static SortKey* buildKeys(File& f,int n){
  const size_t bytes=size_t(std::max(1,n))*sizeof(SortKey);
  if(ESP.getMaxAllocHeap()<bytes+24u*1024u)return nullptr;
  SortKey* keys=(SortKey*)malloc(bytes);if(!keys)return nullptr;
  phase("Preparing to sort");
  Record& r=work[0];
  for(int i=0;i<n;++i){
    if(!fileRecord(f,i,r)){free(keys);return nullptr;}
    r.path[sizeof(r.path)-1]=r.title[sizeof(r.title)-1]=r.artist[sizeof(r.artist)-1]=r.album[sizeof(r.album)-1]=r.albumArtist[sizeof(r.albumArtist)-1]=0;
    albumFolder(r,work[1].path);
    SortKey& k=keys[i];k.title=foldPrefix(r.title);k.artist=foldPrefix(r.artist);k.album=foldPrefix(r.album);
    k.artistCrc=foldCrc(0,r.artist);
    k.albumCrc=foldCrc(foldCrc(foldCrc(0,r.album),albumOwner(r)),work[1].path);
    k.disc=r.disc;k.track=r.track;
    if(!(i&63)){breathe();report(i*100/n,-1);}
  }
  return keys;
}
static bool appendOrder(File& f,Header& h,Order mode,uint16_t* ids,uint32_t& offset,const SortKey* keys,const char* name){
  for(int i=0;i<h.tracks;++i)ids[i]=i;
  phase(name);
  int bits=0;for(int v=h.tracks;v;v>>=1)++bits;
  const uint64_t estimate=std::max<uint64_t>(1,uint64_t(h.tracks)*bits*2);  // heapsort: about 2 n log2 n comparisons
  uint32_t compares=0;
  // At most 4 KB of IDs plus the key table. IDs and keys are released before BT.
  const bool ok=sortIds(ids,h.tracks,[&](uint16_t x,uint16_t y,bool& less){
    if(!(++compares&255)){breathe();report(int(std::min<uint64_t>(99,compares*100/estimate)),-1);}
    int c=keys?keyCompare(keys[x],keys[y],mode):0;
    if(!c){
      if(!fileRecord(f,x,work[0])||!fileRecord(f,y,work[1]))return false;
      c=compare(work[0],work[1],mode);
    }
    less=c?c<0:x<y;return true;
  });
  if(!ok)return false;
  const uint32_t end=offset;offset+=h.tracks*2;
  return f.seek(end)&&writeAll(f,ids,h.tracks*2);
}
static bool buildGroups(File& f,const Header& h,uint32_t orderAt,bool artist,bool subgroups,
                        File& groups,int& count,File* sub,int& subCount){
  Record& previous=work[0];Record& record=work[1];
  Group group{},album{};count=0;subCount=0;bool have=false;
  for(int pos=0;pos<=h.tracks;++pos){uint16_t id=0;bool changed=pos==h.tracks,albumChanged=changed;
    if(!(pos&63)){breathe();report(pos*100/std::max<int>(1,h.tracks),-1);}
    if(!changed){if(!readAt(f,orderAt+pos*2,&id,2)||!fileRecord(f,id,record))return false;
      changed=!have||(artist?compareText(previous.artist,record.artist)!=0:compareAlbum(previous,record)!=0);
      albumChanged=!have||changed||compareAlbum(previous,record)!=0;
    }
    if(have&&subgroups&&albumChanged){album.count=pos-album.first;if(!writeAll(*sub,&album,sizeof(album)))return false;++subCount;}
    if(have&&changed){group.count=pos-group.first;group.subCount=subCount-group.subFirst;
      if(!writeAll(groups,&group,sizeof(group)))return false;++count;}
    if(pos==h.tracks)break;
    if(changed)group={id,uint16_t(pos),0,uint16_t(subCount),0};
    if(albumChanged)album={id,uint16_t(pos),0,0,0};
    previous=record;have=true;
  }return true;
}
static bool appendFile(File& dst,const char* path,uint32_t& offset){
  File src=SD.open(path);if(!src)return false;uint8_t* b=(uint8_t*)&work[1];bool ok=dst.seek(offset);
  while(ok&&src.available()){size_t n=src.read(b,512);if(!n||!writeAll(dst,b,n)){ok=false;break;}offset+=n;}
  src.close();return ok;
}
static bool buildCatalog(int slot,uint32_t signature,uint32_t generation,StorageProgressFn progress,Header& checked){
  reportFn=progress;reportPhase="Reading song tags";
  struct Done{~Done(){reportFn=nullptr;}} done;  // progress only while a rebuild runs
  File f=SD.open(CATALOGS[slot],"w+");if(!f)return false;
  Header h{};memcpy(h.magic,"CYDCAT1",8);h.signature=signature;h.generation=generation;
  bool ok=writeAll(f,&h,sizeof(h));scanned=0;scanCrc=0;scanOk=true;skipped=0;
  if(ok)scanDir("/music",0,&f,progress);
  h.tracks=scanned;ok=ok&&scanOk&&scanCrc==signature;
  uint16_t* ids=(uint16_t*)malloc(std::max<int>(1,h.tracks)*2);
  if(!ids)ok=false;
  uint32_t at=sizeof(Header)+h.tracks*sizeof(Record);
  SortKey* keys=ok?buildKeys(f,h.tracks):nullptr;
  if(ok){h.songsAt=at;ok=appendOrder(f,h,Order::Title,ids,at,keys,"Sorting songs");}
  if(ok){h.artistsAt=at;ok=appendOrder(f,h,Order::Artist,ids,at,keys,"Sorting artists");}
  if(ok){h.albumsAt=at;ok=appendOrder(f,h,Order::Album,ids,at,keys,"Sorting albums");}
  free(keys);free(ids);
  const char* ag="/.sys/groups.tmp",*al="/.sys/albums.tmp",*aa="/.sys/artistalbums.tmp";
  SD.remove(ag);SD.remove(al);SD.remove(aa);
  if(ok)phase("Grouping artists");
  if(ok){File artists=SD.open(ag,FILE_WRITE),sub=SD.open(aa,FILE_WRITE);int n=0,s=0;
    ok=artists&&sub&&buildGroups(f,h,h.artistsAt,true,true,artists,n,&sub,s);artists.close();sub.close();h.artists=n;h.artistAlbums=s;}
  if(ok)phase("Grouping albums");
  if(ok){File albums=SD.open(al,FILE_WRITE);int n=0,s=0;
    ok=albums&&buildGroups(f,h,h.albumsAt,false,false,albums,n,nullptr,s);albums.close();h.albums=n;}
  if(ok)phase("Finishing");
  if(ok){h.artistGroupsAt=at;ok=appendFile(f,ag,at);}
  if(ok){h.albumGroupsAt=at;ok=appendFile(f,al,at);}
  if(ok){h.artistAlbumGroupsAt=at;ok=appendFile(f,aa,at);}
  SD.remove(ag);SD.remove(al);SD.remove(aa);
  h.bytes=at;
  if(ok){uint8_t* b=(uint8_t*)&work[1];uint32_t remain=at-sizeof(Header);ok=f.seek(sizeof(Header));
    while(ok&&remain){size_t n=std::min<size_t>(remain,512);ok=f.read(b,n)==n;if(ok){h.checksum=crc(h.checksum,b,n);remain-=n;}}
  }
  h.headerChecksum=crc(0,&h,sizeof(h));
  if(ok)ok=f.seek(0)&&writeAll(f,&h,sizeof(h));f.flush();f.close();
  if(ok)phase("Checking library");
  return ok&&validate(CATALOGS[slot],checked);
}

bool storage_mount(){
  if(!mutex)mutex=xSemaphoreCreateMutex();if(!mutex)return false;
#ifndef CYDPOD_CATALOG_HOST
  static SPIClass sdSPI(VSPI);sdSPI.begin(CYD_SD_SCK_PIN,CYD_SD_MISO_PIN,CYD_SD_MOSI_PIN,CYD_SD_CS_PIN);
  bool ok=SD.begin(CYD_SD_CS_PIN,sdSPI,20000000,"/sd",8);
  if(!ok){SD.end();ok=SD.begin(CYD_SD_CS_PIN,sdSPI,4000000,"/sd",8);}if(!ok)return false;
#endif
  readOnly=false;
  for(const char* d:{"/music","/bg","/playlists","/.sys"})if(!SD.exists(d)&&!SD.mkdir(d))readOnly=true;
  return true;
}
bool storage_begin(StorageProgressFn progress){
  if(!storage_mount())return false;
  xSemaphoreTake(mutex,portMAX_DELAY);closeCat();xSemaphoreGive(mutex);
  struct Reopen{~Reopen(){xSemaphoreTake(mutex,portMAX_DELAY);openCat();xSemaphoreGive(mutex);}} reopen;  // every return path
  active=-1;header={};stale=rebuilt=false;
  Header a{},b{};const bool va=validate(CATALOGS[0],a),vb=validate(CATALOGS[1],b);
  if(va||vb){active=vb&&(!va||int32_t(b.generation-a.generation)>0)?1:0;header=active?b:a;}
  scanned=0;scanCrc=0;scanOk=true;skipped=0;scanDir("/music",0,nullptr,nullptr);
  // CT-04: the marker has done its job once this boot has read it. Removing it before
  // the build means a build that keeps failing cannot repeat on every boot.
  const bool force=SD.exists(REFRESH);if(force&&!readOnly)SD.remove(REFRESH);
  if(!scanOk){stale=true;Serial.println("catalog: Library stale: cannot read /music, previous catalogue kept");return true;}
  if(skipped)Serial.printf("catalog: skipped %d\n",skipped);
  plannedTracks=scanned;
  if(active>=0&&!force&&scanCrc==header.signature&&scanned==header.tracks)return true;
  if(readOnly){  // CT-07: nothing may be written, so keep whatever catalogue there is
    stale=true;Serial.println("catalog: SD card is read-only, no rebuild; Library stale");return true;}
  const int target=active==0?1:0;const uint32_t generation=header.generation+1;
  Header candidate{};
  if(buildCatalog(target,scanCrc,generation,progress,candidate)){active=target;header=candidate;rebuilt=true;}
  else{stale=true;Serial.println("catalog: Library stale: rebuild failed, previous catalogue kept");}
  Serial.printf("catalog: %u tracks %u artists %u albums, generation %lu%s\n",header.tracks,header.artists,header.albums,
    (unsigned long)header.generation,stale?" (stale)":"");
  return true;
}
int storage_trackCount(){return active>=0?header.tracks:0;}
uint32_t storage_libraryCrc(){return header.checksum;}
bool storage_catalogReady(){return active>=0;}
bool storage_catalogStale(){return stale;}
bool storage_catalogRebuilt(){return rebuilt;}
int storage_skippedCount(){return skipped;}
bool storage_readOnly(){return readOnly;}
bool storage_refreshNextBoot(){if(readOnly)return false;File f=SD.open(REFRESH,FILE_WRITE);if(!f)return false;bool ok=f.print("refresh\n")>0;f.close();return ok;}

// The *Locked helpers need `mutex`; their results are valid until it is released.
static uint32_t recordAt(int i){return sizeof(Header)+uint32_t(i)*sizeof(Record);}
static const Record* recordLocked(int i){
  if(!cat||i<0||i>=header.tracks)return nullptr;
  if(i==cacheIndex)return &cache;
  cacheIndex=-1;
  if(!readAt(cat,recordAt(i),&cache,sizeof(cache))){openCat();  // one retry on a fresh handle
    if(!cat||!readAt(cat,recordAt(i),&cache,sizeof(cache)))return nullptr;}
  if(!validRecord(cache))return nullptr;
  cacheIndex=i;return &cache;
}
// Reads only the first `bytes` of a record (path, then title) into the cache
// scratch: enough for path/title walks, half the SD traffic. Invalidates the cache.
static const Record* headLocked(int i,size_t bytes){
  cacheIndex=-1;
  if(!cat||i<0||i>=header.tracks||!readAt(cat,recordAt(i),&cache,bytes))return nullptr;
  if(!memchr(cache.path,0,sizeof(cache.path))||strncmp(cache.path,"/music/",7)||strstr(cache.path,"/../"))return nullptr;
  if(bytes>offsetof(Record,title)&&!memchr(cache.title,0,sizeof(cache.title)))return nullptr;
  return &cache;
}
bool storage_trackInfo(int i,Record& out){
  if(active<0||i<0||i>=header.tracks)return false;
  xSemaphoreTake(mutex,portMAX_DELAY);const Record* r=recordLocked(i);if(r)out=*r;xSemaphoreGive(mutex);return r!=nullptr;
}
bool storage_trackSummary(int i,String& title,String& artist){
  if(active<0)return false;
  xSemaphoreTake(mutex,portMAX_DELAY);const Record* r=recordLocked(i);if(r){title=r->title;artist=r->artist;}xSemaphoreGive(mutex);return r!=nullptr;
}
String storage_trackPath(int i){
  String out;if(active<0)return out;
  xSemaphoreTake(mutex,portMAX_DELAY);const Record* r=headLocked(i,offsetof(Record,title));if(r)out=r->path;xSemaphoreGive(mutex);return out;
}
String storage_trackLabel(int i){
  String out;if(active<0)return out;
  xSemaphoreTake(mutex,portMAX_DELAY);const Record* r=headLocked(i,offsetof(Record,artist));if(r)out=r->title;xSemaphoreGive(mutex);return out;
}
int storage_findTrack(const String& path){
  for(int i=0;i<storage_trackCount();++i){
    xSemaphoreTake(mutex,portMAX_DELAY);const Record* r=headLocked(i,offsetof(Record,title));
    const bool hit=r&&compareText(r->path,path.c_str())==0;xSemaphoreGive(mutex);if(hit)return i;
  }return -1;
}
// The lock is taken per record, not per pass, so the decode task can look up the
// next track between two steps; callbacks run unlocked on copies.
void storage_forEach(StorageEachFn fn){
  for(int i=0;i<storage_trackCount();++i){
    String path,title;xSemaphoreTake(mutex,portMAX_DELAY);const Record* r=headLocked(i,offsetof(Record,artist));
    if(r){path=r->path;title=r->title;}xSemaphoreGive(mutex);if(r)fn(i,path,title);
  }
}
void storage_forEachText(void(*fn)(const char*)){
  for(int i=0;i<storage_trackCount();++i){
    String text[4];xSemaphoreTake(mutex,portMAX_DELAY);const Record* r=recordLocked(i);
    if(r){text[0]=r->title;text[1]=r->artist;text[2]=r->album;text[3]=albumOwner(*r);}xSemaphoreGive(mutex);
    if(r)for(const String& t:text)fn(t.c_str());
  }
}

static bool viewGroup(File& f,CatalogView v,Group& g,uint32_t& orderAt){
  int count=0;uint32_t groupsAt=0;
  switch(v.kind){
    case CatalogKind::ArtistAlbums:case CatalogKind::ArtistSongs:groupsAt=header.artistGroupsAt;count=header.artists;orderAt=header.artistsAt;break;
    case CatalogKind::AlbumSongs:groupsAt=header.albumGroupsAt;count=header.albums;orderAt=header.albumsAt;break;
    case CatalogKind::ArtistAlbumSongs:groupsAt=header.artistAlbumGroupsAt;count=header.artistAlbums;orderAt=header.artistsAt;break;
    default:return false;
  }return v.group<count&&groupAt(f,groupsAt,v.group,g);
}
static int browseCountLocked(CatalogView v){
  if(active<0)return 0;
  if(v.kind==CatalogKind::Artists)return header.artists;
  if(v.kind==CatalogKind::Albums)return header.albums;
  if(v.kind==CatalogKind::Songs)return header.tracks;
  Group g{};uint32_t order=0;
  const bool ok=cat&&viewGroup(cat,v,g,order);return ok?(v.kind==CatalogKind::ArtistAlbums?g.subCount+1:g.count):0;
}
static bool browseEntryLocked(CatalogView v,int row,CatalogEntry& entry){
  const int count=browseCountLocked(v);if(row<0||row>=count||!cat)return false;
  File& f=cat;uint16_t id=0;Group g{};uint32_t order=header.songsAt;bool ok=true;
  if(v.kind==CatalogKind::Artists||v.kind==CatalogKind::Albums){
    const bool artist=v.kind==CatalogKind::Artists;ok=groupAt(f,artist?header.artistGroupsAt:header.albumGroupsAt,row,g);id=g.representative;
    entry.navigates=true;entry.child={artist?CatalogKind::ArtistAlbums:CatalogKind::AlbumSongs,uint16_t(row)};
  }else if(v.kind==CatalogKind::ArtistAlbums){
    ok=viewGroup(f,v,g,order);id=g.representative;entry.navigates=true;
    if(row==0){entry.label="All Songs";entry.child={CatalogKind::ArtistSongs,v.group};}
    else{const int index=g.subFirst+row-1;ok=ok&&groupAt(f,header.artistAlbumGroupsAt,index,g);id=g.representative;entry.child={CatalogKind::ArtistAlbumSongs,uint16_t(index)};}
  }else{
    if(v.kind!=CatalogKind::Songs)ok=viewGroup(f,v,g,order);
    ok=ok&&idAt(f,order,(v.kind==CatalogKind::Songs?0:g.first)+row,id);entry.track=id;
  }
  const Record* r=ok?recordLocked(id):nullptr;if(!r)return false;
  if(entry.label.length()==0)entry.label=entry.navigates?(v.kind==CatalogKind::Artists?String(r->artist):String(r->album)):String(r->title);
  if(v.kind==CatalogKind::Artists)entry.detail=String(g.subCount)+" albums";
  else if(v.kind==CatalogKind::ArtistAlbums&&row==0)entry.detail=String(g.count)+" songs";
  else entry.detail=entry.navigates?String(albumOwner(*r)):String(r->artist);
  return true;
}
int storage_browseCount(CatalogView v){
  if(v.kind==CatalogKind::Artists||v.kind==CatalogKind::Albums||v.kind==CatalogKind::Songs)return browseCountLocked(v);
  xSemaphoreTake(mutex,portMAX_DELAY);const int n=browseCountLocked(v);xSemaphoreGive(mutex);return n;
}
bool storage_browseEntry(CatalogView v,int row,CatalogEntry& entry){
  entry=CatalogEntry{};if(active<0)return false;
  xSemaphoreTake(mutex,portMAX_DELAY);
  const bool ok=browseEntryLocked(v,row,entry);
  xSemaphoreGive(mutex);return ok;
}
int storage_browseTracks(CatalogView v,uint16_t* out,int capacity){
  if(!out||active<0)return 0;
  if(v.kind==CatalogKind::Artists||v.kind==CatalogKind::Albums||v.kind==CatalogKind::ArtistAlbums)return 0;
  xSemaphoreTake(mutex,portMAX_DELAY);
  Group g{0,0,header.tracks,0,0};uint32_t order=header.songsAt;
  bool ok=cat&&(v.kind==CatalogKind::Songs||viewGroup(cat,v,g,order));
  ok=ok&&capacity>=g.count;
  for(int i=0;ok&&i<g.count;++i)ok=idAt(cat,order,g.first+i,out[i]);
  xSemaphoreGive(mutex);return ok?g.count:0;
}

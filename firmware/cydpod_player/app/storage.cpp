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
static SemaphoreHandle_t mutex=nullptr;
static uint32_t scanCrc=0;
static int scanned=0;
static bool scanOk=true;
// Shared boot scratch, not a whole-library cache.
static Record work[2];

static uint32_t crc(uint32_t value,const void* p,size_t n){return esp_rom_crc32_le(value,(const uint8_t*)p,n);}
static bool readAt(File& f,uint32_t at,void* p,size_t n){return f.seek(at)&&f.read((uint8_t*)p,n)==n;}
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
  uint8_t b[512];uint32_t value=0,remaining=h.bytes-sizeof(Header);f.seek(sizeof(Header));
  while(remaining){size_t n=std::min<size_t>(remaining,sizeof(b));if(f.read(b,n)!=n){ok=false;break;}value=crc(value,b,n);remaining-=n;}
  ok=ok&&value==h.checksum;
  for(uint32_t p=h.songsAt;ok&&p<h.artistGroupsAt;p+=2){uint16_t id;ok=readAt(f,p,&id,2)&&id<h.tracks;}
  for(unsigned kind=0;ok&&kind<3;++kind){uint32_t base=kind==0?h.artistGroupsAt:kind==1?h.albumGroupsAt:h.artistAlbumGroupsAt;
    int count=kind==0?h.artists:kind==1?h.albums:h.artistAlbums;
    for(int i=0;ok&&i<count;++i){Group g{};ok=groupAt(f,base,i,g)&&g.representative<h.tracks&&g.count&&uint32_t(g.first)+g.count<=h.tracks;
      if(kind==0)ok=ok&&g.subCount&&uint32_t(g.subFirst)+g.subCount<=h.artistAlbums;}
  }
  Record r;for(int i=0;ok&&i<h.tracks;++i)ok=fileRecord(f,i,r)&&validRecord(r);
  f.close();return ok;
}

// Two passes: cheap change detection, then tag extraction only on rebuild.
static bool tagRead(void* p,uint64_t at,void* dst,size_t bytes){
  File& f=*(File*)p;return at<=UINT32_MAX&&f.seek(uint32_t(at))&&f.read((uint8_t*)dst,bytes)==bytes;
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
        if(low.endsWith(".m4a")){
          if(scanned>=MaxTracks||p.length()>=256){scanOk=false;f.close();break;}
          const uint32_t size=f.size(),modified=uint32_t(f.getLastWrite());
          scanCrc=crc(scanCrc,p.c_str(),p.length()+1);scanCrc=crc(scanCrc,&size,4);scanCrc=crc(scanCrc,&modified,4);
          if(output){Record& r=work[0];memset(&r,0,sizeof(r));copyText(r.path,p.c_str());Tags tags(tagRead,&f,size);
            if(!tags.parse(r)){memset(r.title,0,sizeof(r)-sizeof(r.path));Serial.printf("catalog: tags unavailable %s\n",p.c_str());}
            fallback(r);if(output->write((const uint8_t*)&r,sizeof(r))!=sizeof(r))scanOk=false;
          }
          ++scanned;if(progress&&output)progress(scanned,0);
        }
      }
    }
    f.close();if(!scanOk)break;
  }
  dir.close();
}
static bool writeAll(File& f,const void* p,size_t n){return f.write((const uint8_t*)p,n)==n;}
static bool appendOrder(File& f,Header& h,Order mode,uint16_t* ids,uint32_t& offset){
  for(int i=0;i<h.tracks;++i)ids[i]=i;
  // At most 4 KB of IDs plus two bounded records. IDs are released before BT.
  const bool ok=sortIds(ids,h.tracks,[&](uint16_t x,uint16_t y,bool& less){
    if(!fileRecord(f,x,work[0])||!fileRecord(f,y,work[1]))return false;
    const int c=compare(work[0],work[1],mode);less=c?c<0:x<y;return true;
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
  File src=SD.open(path);if(!src)return false;uint8_t b[512];bool ok=dst.seek(offset);
  while(ok&&src.available()){size_t n=src.read(b,sizeof(b));if(!n||!writeAll(dst,b,n)){ok=false;break;}offset+=n;}
  src.close();return ok;
}
static bool buildCatalog(int slot,uint32_t signature,uint32_t generation,StorageProgressFn progress){
  File f=SD.open(CATALOGS[slot],"w+");if(!f)return false;
  Header h{};memcpy(h.magic,"CYDCAT1",8);h.signature=signature;h.generation=generation;
  bool ok=writeAll(f,&h,sizeof(h));scanned=0;scanCrc=0;scanOk=true;
  if(ok)scanDir("/music",0,&f,progress);
  h.tracks=scanned;ok=ok&&scanOk&&scanCrc==signature;
  uint16_t* ids=(uint16_t*)malloc(std::max<int>(1,h.tracks)*2);
  if(!ids)ok=false;
  uint32_t at=sizeof(Header)+h.tracks*sizeof(Record);
  if(ok){h.songsAt=at;ok=appendOrder(f,h,Order::Title,ids,at);}
  if(ok){h.artistsAt=at;ok=appendOrder(f,h,Order::Artist,ids,at);}
  if(ok){h.albumsAt=at;ok=appendOrder(f,h,Order::Album,ids,at);}
  free(ids);
  const char* ag="/.sys/groups.tmp",*al="/.sys/albums.tmp",*aa="/.sys/artistalbums.tmp";
  SD.remove(ag);SD.remove(al);SD.remove(aa);
  if(ok){File artists=SD.open(ag,FILE_WRITE),sub=SD.open(aa,FILE_WRITE);int n=0,s=0;
    ok=artists&&sub&&buildGroups(f,h,h.artistsAt,true,true,artists,n,&sub,s);artists.close();sub.close();h.artists=n;h.artistAlbums=s;}
  if(ok){File albums=SD.open(al,FILE_WRITE);int n=0,s=0;
    ok=albums&&buildGroups(f,h,h.albumsAt,false,false,albums,n,nullptr,s);albums.close();h.albums=n;}
  if(ok){h.artistGroupsAt=at;ok=appendFile(f,ag,at);}
  if(ok){h.albumGroupsAt=at;ok=appendFile(f,al,at);}
  if(ok){h.artistAlbumGroupsAt=at;ok=appendFile(f,aa,at);}
  SD.remove(ag);SD.remove(al);SD.remove(aa);
  h.bytes=at;
  if(ok){uint8_t b[512];uint32_t remain=at-sizeof(Header);ok=f.seek(sizeof(Header));
    while(ok&&remain){size_t n=std::min<size_t>(remain,sizeof(b));ok=f.read(b,n)==n;if(ok){h.checksum=crc(h.checksum,b,n);remain-=n;}}
  }
  h.headerChecksum=crc(0,&h,sizeof(h));
  if(ok)ok=f.seek(0)&&writeAll(f,&h,sizeof(h));f.flush();f.close();
  Header checked{};return ok&&validate(CATALOGS[slot],checked);
}

bool storage_mount(){
  if(!mutex)mutex=xSemaphoreCreateMutex();if(!mutex)return false;
#ifndef CYDPOD_CATALOG_HOST
  static SPIClass sdSPI(VSPI);sdSPI.begin(CYD_SD_SCK_PIN,CYD_SD_MISO_PIN,CYD_SD_MOSI_PIN,CYD_SD_CS_PIN);
  bool ok=SD.begin(CYD_SD_CS_PIN,sdSPI,20000000,"/sd",8);
  if(!ok){SD.end();ok=SD.begin(CYD_SD_CS_PIN,sdSPI,4000000,"/sd",8);}if(!ok)return false;
#endif
  for(const char* d:{"/music","/bg","/playlists","/.sys"})if(!SD.exists(d)&&!SD.mkdir(d))return false;
  return true;
}
bool storage_begin(StorageProgressFn progress){
  if(!storage_mount())return false;
  active=-1;header={};stale=rebuilt=false;
  Header a{},b{};const bool va=validate(CATALOGS[0],a),vb=validate(CATALOGS[1],b);
  if(va||vb){active=vb&&(!va||int32_t(b.generation-a.generation)>0)?1:0;header=active?b:a;}
  scanned=0;scanCrc=0;scanOk=true;scanDir("/music",0,nullptr,nullptr);
  if(!scanOk){stale=true;Serial.println("catalog: scan failed, over 2000 tracks or path too long; previous catalogue retained");return true;}
  const bool force=SD.exists(REFRESH);
  if(active>=0&&!force&&scanCrc==header.signature&&scanned==header.tracks)return true;
  const int target=active==0?1:0;const uint32_t generation=header.generation+1;
  if(buildCatalog(target,scanCrc,generation,progress)){
    Header candidate{};if(validate(CATALOGS[target],candidate)){active=target;header=candidate;rebuilt=true;SD.remove(REFRESH);}
  }else{stale=true;Serial.println("catalog: rebuild failed; previous catalogue retained");}
  Serial.printf("catalog: %u tracks %u artists %u albums, generation %lu%s\n",header.tracks,header.artists,header.albums,
    (unsigned long)header.generation,stale?" (stale)":"");
  return true;
}
int storage_trackCount(){return active>=0?header.tracks:0;}
uint32_t storage_libraryCrc(){return header.checksum;}
bool storage_catalogReady(){return active>=0;}
bool storage_catalogStale(){return stale;}
bool storage_catalogRebuilt(){return rebuilt;}
bool storage_refreshNextBoot(){File f=SD.open(REFRESH,FILE_WRITE);if(!f)return false;bool ok=f.print("refresh\n")>0;f.close();return ok;}
bool storage_trackInfo(int i,Record& out){
  if(active<0||i<0||i>=header.tracks)return false;
  xSemaphoreTake(mutex,portMAX_DELAY);File f=SD.open(CATALOGS[active]);
  bool ok=f&&fileRecord(f,i,out)&&validRecord(out);f.close();xSemaphoreGive(mutex);return ok;
}
String storage_trackPath(int i){Record r;return storage_trackInfo(i,r)?String(r.path):String();}
String storage_trackLabel(int i){Record r;return storage_trackInfo(i,r)?String(r.title):String();}
int storage_findTrack(const String& path){Record r;for(int i=0;i<storage_trackCount();++i)if(storage_trackInfo(i,r)&&compareText(r.path,path.c_str())==0)return i;return -1;}
void storage_forEach(StorageEachFn fn){Record r;for(int i=0;i<storage_trackCount();++i)if(storage_trackInfo(i,r))fn(i,String(r.path),String(r.title));}
void storage_forEachText(void(*fn)(const char*)){Record r;for(int i=0;i<storage_trackCount();++i)if(storage_trackInfo(i,r)){fn(r.title);fn(r.artist);fn(r.album);fn(albumOwner(r));}}

static bool viewGroup(File& f,CatalogView v,Group& g,uint32_t& orderAt){
  int count=0;uint32_t groupsAt=0;
  switch(v.kind){
    case CatalogKind::ArtistAlbums:case CatalogKind::ArtistSongs:groupsAt=header.artistGroupsAt;count=header.artists;orderAt=header.artistsAt;break;
    case CatalogKind::AlbumSongs:groupsAt=header.albumGroupsAt;count=header.albums;orderAt=header.albumsAt;break;
    case CatalogKind::ArtistAlbumSongs:groupsAt=header.artistAlbumGroupsAt;count=header.artistAlbums;orderAt=header.artistsAt;break;
    default:return false;
  }return v.group<count&&groupAt(f,groupsAt,v.group,g);
}
int storage_browseCount(CatalogView v){
  if(active<0)return 0;
  if(v.kind==CatalogKind::Artists)return header.artists;
  if(v.kind==CatalogKind::Albums)return header.albums;
  if(v.kind==CatalogKind::Songs)return header.tracks;
  File f=SD.open(CATALOGS[active]);Group g{};uint32_t order=0;
  const bool ok=f&&viewGroup(f,v,g,order);f.close();return ok?(v.kind==CatalogKind::ArtistAlbums?g.subCount+1:g.count):0;
}
bool storage_browseEntry(CatalogView v,int row,CatalogEntry& entry){
  const int count=storage_browseCount(v);if(row<0||row>=count)return false;
  entry=CatalogEntry{};File f=SD.open(CATALOGS[active]);if(!f)return false;
  uint16_t id=0;Group g{};uint32_t order=header.songsAt;bool ok=true;
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
  Record r{};ok=ok&&fileRecord(f,id,r)&&validRecord(r);f.close();if(!ok)return false;
  if(entry.label.length()==0)entry.label=entry.navigates?(v.kind==CatalogKind::Artists?String(r.artist):String(r.album)):String(r.title);
  if(v.kind==CatalogKind::Artists)entry.detail=String(g.subCount)+" albums";
  else if(v.kind==CatalogKind::ArtistAlbums&&row==0)entry.detail=String(g.count)+" songs";
  else entry.detail=entry.navigates?String(albumOwner(r)):String(r.artist);
  return true;
}
int storage_browseTracks(CatalogView v,uint16_t* out,int capacity){
  if(!out||active<0)return 0;
  if(v.kind==CatalogKind::Artists||v.kind==CatalogKind::Albums||v.kind==CatalogKind::ArtistAlbums)return 0;
  File f=SD.open(CATALOGS[active]);if(!f)return 0;Group g{0,0,header.tracks,0,0};uint32_t order=header.songsAt;
  bool ok=v.kind==CatalogKind::Songs||viewGroup(f,v,g,order);
  ok=ok&&capacity>=g.count;
  for(int i=0;ok&&i<g.count;++i)ok=idAt(f,order,g.first+i,out[i]);
  f.close();return ok?g.count:0;
}

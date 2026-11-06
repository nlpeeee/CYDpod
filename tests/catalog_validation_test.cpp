// Exercise the production boot validator directly, without a 10,000-file tag scan.
#include "../firmware/cydpod_player/app/storage.cpp"
#include "catalog_legacy_validate.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using Bytes=std::vector<uint8_t>;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void put(const Bytes& bytes){
  std::ofstream f(SD.root/"test.cat",std::ios::binary|std::ios::trunc);
  f.write((const char*)bytes.data(),bytes.size());check(bool(f),"write fixture");
}
static Header fixture(int tracks){
  Header h{};memcpy(h.magic,"CYDCAT1",8);h.tracks=tracks;
  h.artists=h.albums=h.artistAlbums=tracks?1:0;h.signature=1234;
  h.songsAt=sizeof(Header)+tracks*sizeof(Record);h.artistsAt=h.songsAt+tracks*2;
  h.albumsAt=h.artistsAt+tracks*2;h.artistGroupsAt=h.albumsAt+tracks*2;
  h.albumGroupsAt=h.artistGroupsAt+h.artists*sizeof(Group);
  h.artistAlbumGroupsAt=h.albumGroupsAt+h.albums*sizeof(Group);
  h.bytes=h.artistAlbumGroupsAt+h.artistAlbums*sizeof(Group);
  Bytes bytes(h.bytes);Record r{};
  strcpy(r.path,"/music/track.mp3");strcpy(r.title,"A song");
  for(int i=0;i<tracks;++i)memcpy(bytes.data()+sizeof(Header)+i*sizeof(Record),&r,sizeof(r));
  for(uint32_t at=h.songsAt;at<h.artistGroupsAt;at+=2){uint16_t id=((at-h.songsAt)/2)%tracks;memcpy(bytes.data()+at,&id,2);}
  if(tracks){Group g{0,0,uint16_t(tracks),0,1};
    for(uint32_t at=h.artistGroupsAt;at<h.bytes;at+=sizeof(Group))memcpy(bytes.data()+at,&g,sizeof(g));}
  h.checksum=crc(0,bytes.data()+sizeof(Header),h.bytes-sizeof(Header));
  h.headerChecksum=crc(0,&h,sizeof(h));memcpy(bytes.data(),&h,sizeof(h));put(bytes);return h;
}
static Bytes upgrade(Header h){
  File f=SD.open("/test.cat");uint32_t sampled=0;check(sampleCrc(f,h.bytes,sampled),"sample fixture");
  Bytes bytes(h.bytes+4);check(readAt(f,0,bytes.data(),h.bytes),"read fixture");f.close();
  memcpy(bytes.data()+h.bytes,&sampled,4);h.bytes+=4;memcpy(h.magic,"CYDCAT2",8);
  h.checksum=crc(0,bytes.data()+sizeof(Header),h.bytes-sizeof(Header));
  h.headerChecksum=0;h.headerChecksum=crc(0,&h,sizeof(h));memcpy(bytes.data(),&h,sizeof(h));put(bytes);return bytes;
}
int main(){try{
  SD.root=std::filesystem::current_path()/"catalog-validation-card";
  std::filesystem::create_directories(SD.root);Record scratch[2];work=scratch;
  for(int tracks:{0,2000,10000}){
    Header h=fixture(tracks),out{};hostReadBytes=0;check(legacyValidate("/test.cat",out),"legacy valid");
    const uint64_t before=hostReadBytes;
    check(validate("/test.cat",out,true),"legacy format remains readable as fallback");Bytes bytes=upgrade(h);
    hostReadBytes=0;check(validate("/test.cat",out),"sampled valid");const uint64_t after=hostReadBytes;
    check(tracks==0||after*100<before,"at least 100x fewer bytes");
    check(validate("/test.cat",out,true),"full rebuilt check valid");
    if(!tracks)continue;
    std::cout<<"PASS validation bytes tracks="<<tracks<<" before="<<before<<" after="<<after<<"\n";
    auto damage=bytes;damage[offsetof(Header,signature)]^=1;put(damage);check(!validate("/test.cat",out),"header flip rejected");
    damage=bytes;damage.pop_back();put(damage);check(!validate("/test.cat",out),"truncation rejected");
    // An ordinary title byte, still structurally valid: sampled CRC must catch it.
    damage=bytes;damage[sizeof(Header)+offsetof(Record,title)]^=1;put(damage);
    check(!validate("/test.cat",out),"sampled flip rejected");
    damage=bytes;damage[sizeof(Header)+sizeof(Record)+offsetof(Record,title)]^=1;put(damage);
    check(validate("/test.cat",out),"documented unsampled flip accepted");
    check(!validate("/test.cat",out,true),"full checksum rejects unsampled flip");
    put(bytes);Header bad=out;bad.songsAt+=2;bad.headerChecksum=0;bad.headerChecksum=crc(0,&bad,sizeof(bad));
    memcpy(bytes.data(),&bad,sizeof(bad));put(bytes);check(!validate("/test.cat",out),"invalid offset with valid CRC rejected");
  }
  std::cout<<"PASS validation corruption: header, truncation, sampled byte, offsets rejected; unsampled byte deferred to full checksum\n";
  // Exercise the real writer, migration from an obsolete slot, warm reuse and lookup.
  check(storage_mount(),"mount");
  {std::ofstream f(SD.root/"music/test.mp3",std::ios::binary);f<<"no tags";}
  work=nullptr;
  check(storage_begin(nullptr)&&storage_catalogRebuilt()&&storage_trackCount()==1,"writer/migration");
  // Downgrade the real writer's slot to a fully valid legacy catalogue.
  closeCat();Bytes old;
  {std::ifstream f(SD.root/".sys/library-a.cat",std::ios::binary);old.assign(std::istreambuf_iterator<char>(f),{});}
  check(old.size()>sizeof(Header)+4,"real catalogue bytes");old.resize(old.size()-4);
  Header legacy{};memcpy(&legacy,old.data(),sizeof(legacy));memcpy(legacy.magic,"CYDCAT1",8);legacy.bytes=old.size();
  legacy.checksum=crc(0,old.data()+sizeof(Header),old.size()-sizeof(Header));
  legacy.headerChecksum=0;legacy.headerChecksum=crc(0,&legacy,sizeof(legacy));memcpy(old.data(),&legacy,sizeof(legacy));
  {std::ofstream f(SD.root/".sys/library-a.cat",std::ios::binary|std::ios::trunc);f.write((const char*)old.data(),old.size());}
  check(storage_begin(nullptr)&&storage_catalogRebuilt(),"valid legacy migrates to other slot");
  hostReadBytes=0;check(storage_begin(nullptr)&&!storage_catalogRebuilt(),"warm boot reuses new format");
  check(hostReadBytes==60+64+8*512,"legacy fallback only reads header when new slot is valid");
  check(storage_findTrack("/music/test.mp3")==0&&storage_findTrack("/music/missing.mp3")==-1,"findTrack unchanged");
  closeCat();
  {std::ofstream f(SD.root/".sys/library-b.cat",std::ios::binary|std::ios::trunc);f<<"corrupt";}
  hostWriteBudget=0;
  check(storage_begin(nullptr)&&storage_catalogStale()&&storage_trackCount()==1&&!storage_catalogRebuilt(),"valid legacy retained after corrupt new slot and failed rebuild");
  hostWriteBudget=-1;closeCat();std::filesystem::remove_all(SD.root);
  std::cout<<"PASS validation writer: CYDCAT2 rebuild, legacy migration/fallback, mixed-slot warm reads=4220, findTrack\n";
  return 0;
}catch(const std::exception& e){std::cerr<<"FAIL validation: "<<e.what()<<"\n";return 1;}}

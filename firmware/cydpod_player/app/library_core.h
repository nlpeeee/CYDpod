#pragma once
// Catalogue records and M4A tag adapter. No Arduino or decoder dependency.
// Box traversal is adapted from vendor/miniaac/src/miniaac.c (box_next/read_bytes),
// SHA256 317fbdec5edbd641f66ff0779f5a8fe651355cac8fc6de8b5ba4676bcceda77d.
// MIT License, Copyright (c) 2026 MinimalAAC contributors.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

namespace cydlib {
constexpr uint16_t MaxTracks = 10000;  // CY-115: sort RAM no longer depends on it; the format allows 65,535
constexpr uint32_t fcc(unsigned a,unsigned b,unsigned c,unsigned d) {
  return (a<<24)|(b<<16)|(c<<8)|d;
}
inline uint32_t be32(const uint8_t* p) { return fcc(p[0],p[1],p[2],p[3]); }
inline uint16_t be16(const uint8_t* p) { return uint16_t((p[0]<<8)|p[1]); }
// Disk records are fixed size and little endian (ESP32 and the host checks).
#pragma pack(push,1)
struct Record {
  char path[256], title[128], artist[96], album[128], albumArtist[96];
  uint16_t disc, track;
  uint8_t compilation, reserved[3];
};
struct Group { uint16_t representative, first, count, subFirst, subCount; };
struct Header {
  char magic[8];
  uint32_t generation, signature, bytes, checksum;
  uint16_t tracks, artists, albums, artistAlbums;
  uint32_t songsAt, artistsAt, albumsAt, artistGroupsAt, albumGroupsAt, artistAlbumGroupsAt;
  uint32_t headerChecksum;
};
#pragma pack(pop)
static_assert(sizeof(Record)==712,"catalogue record schema");
static_assert(sizeof(Group)==10,"catalogue group schema");
static_assert(sizeof(Header)==60,"catalogue header schema");
inline int compareText(const char* a,const char* b) {
  while (*a && *b) {
    unsigned x=(uint8_t)*a++,y=(uint8_t)*b++;
    if(x<128)x=tolower(x); if(y<128)y=tolower(y);
    if(x!=y)return x<y?-1:1;
  }
  return (*a!=0)-(*b!=0);
}
template<size_t N> inline void copyText(char (&out)[N],const char* text) {
  size_t n=strlen(text); if(n>=N)n=N-1;
  // Do not cut a UTF-8 sequence in the middle.
  if(text[n])while(n && ((uint8_t)text[n]&0xc0)==0x80)--n;
  memcpy(out,text,n);out[n]=0;
  for(size_t i=0;i<n;++i)if((uint8_t)out[i]<32)out[i]=' ';
}
inline void albumFolder(const Record& r,char (&out)[256]) {
  copyText(out,r.path);char* slash=strrchr(out,'/');if(slash)*slash=0;
  // Disc subfolders form one album. Other folders disambiguate same-name releases.
  slash=strrchr(out,'/');
  if(slash){const char* p=slash+1;char low[16]={};size_t i=0;
    for(;p[i]&&i<15;++i)low[i]=char(tolower((uint8_t)p[i]));
    const char* n=nullptr;
    if(!strncmp(low,"disc",4))n=low+4;else if(!strncmp(low,"disk",4))n=low+4;else if(!strncmp(low,"cd",2))n=low+2;
    if(n){while(*n==' '||*n=='_'||*n=='-')++n;bool digits=*n!=0;while(*n){if(!isdigit((uint8_t)*n))digits=false;++n;}if(digits)*slash=0;}
  }
}
inline const char* albumOwner(const Record& r) {
  return r.albumArtist[0]?r.albumArtist:r.compilation?"Various Artists":r.artist;
}
inline int compareAlbum(const Record& a,const Record& b) {
  int c=compareText(a.album,b.album);if(c)return c;
  c=compareText(albumOwner(a),albumOwner(b));if(c)return c;
  char x[256],y[256];albumFolder(a,x);albumFolder(b,y);return compareText(x,y);
}
enum class Order : uint8_t { Title, Artist, Album };
inline int compare(const Record& a,const Record& b,Order order) {
  int c=0;
  if(order==Order::Artist){c=compareText(a.artist,b.artist);if(c)return c;}
  if(order!=Order::Title){
    c=compareAlbum(a,b);if(c)return c;
    if(a.disc!=b.disc)return a.disc<b.disc?-1:1;
    if(a.track!=b.track)return a.track<b.track?-1:1;
  }
  c=compareText(a.title,b.title);if(c)return c;
  c=compareText(a.artist,b.artist);if(c)return c;
  return compareText(a.path,b.path);
}
inline void fallback(Record& r) {
  if(!r.title[0]){const char* p=strrchr(r.path,'/');copyText(r.title,p?p+1:r.path);char* dot=strrchr(r.title,'.');if(dot)*dot=0;}
  if(!r.artist[0])copyText(r.artist,"Unknown Artist");
  if(!r.album[0])copyText(r.album,"Unknown Album");
}
// Iterative heapsort stops cleanly on failed SD reads; no comparator failure
// can violate std::sort's strict-weak-ordering precondition.
template<class Less> bool sortIds(uint16_t* ids,int n,Less less) {
  auto sift=[&](int root,int end)->bool {
    while(root*2+1<end){int child=root*2+1;bool result=false;
      if(child+1<end){if(!less(ids[child],ids[child+1],result))return false;if(result)++child;}
      if(!less(ids[root],ids[child],result))return false;if(!result)break;
      uint16_t tmp=ids[root];ids[root]=ids[child];ids[child]=tmp;root=child;
    }return true;
  };
  for(int root=n/2;root>0;)if(!sift(--root,n))return false;
  for(int end=n-1;end>0;--end){uint16_t tmp=ids[0];ids[0]=ids[end];ids[end]=tmp;if(!sift(0,end))return false;}
  return true;
}
using ReadAt=bool(*)(void*,uint64_t,void*,size_t);
class Tags {
  struct Box { uint64_t payload,end;uint32_t type; };
  ReadAt read_;void* user_;uint64_t size_;unsigned boxes_=0,bytes_=0;
  uint8_t* scratch_;  // 264 B item buffer; callers on a small stack pass boot scratch (CY-39 CT-01)
  bool read(uint64_t at,void* p,size_t n) {
    if(at>size_||n>size_-at||bytes_+n>65536)return false;
    bytes_+=unsigned(n);return read_(user_,at,p,n);
  }
  bool next(uint64_t& at,uint64_t end,Box& b) {
    uint8_t h[16];unsigned header=8;
    if(++boxes_>4096||at>end||end-at<8||!read(at,h,8))return false;
    uint64_t length=be32(h);b.type=be32(h+4);
    if(length==1){if(end-at<16||!read(at+8,h+8,8))return false;length=(uint64_t(be32(h+8))<<32)|be32(h+12);header=16;}
    else if(!length)length=end-at;
    if(length<header||length>end-at)return false;
    b.payload=at+header;b.end=at+length;at=b.end;return true;
  }
  template<size_t N> void text(char (&out)[N],const uint8_t* p,size_t n,unsigned encoding) {
    char buf[N]={};size_t at=0;
    if(encoding==1){
      for(size_t i=0;i<n&&p[i];){
        const uint8_t lead=p[i];unsigned width=lead<128?1:lead>=0xc2&&lead<=0xdf?2:lead>=0xe0&&lead<=0xef?3:lead>=0xf0&&lead<=0xf4?4:0;
        bool valid=width&&width<=n-i;uint32_t cp=width?lead&((1u<<(8-width-(width>1?1:0)))-1):0;
        for(unsigned j=1;valid&&j<width;++j){if((p[i+j]&0xc0)!=0x80)valid=false;else cp=(cp<<6)|(p[i+j]&63);}
        if(width>1&&(cp<(width==2?0x80:width==3?0x800:0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)))valid=false;
        if(valid){if(at+width>=N)break;for(unsigned j=0;j<width;++j)buf[at++]=char(p[i++]);}
        else{if(at+3>=N)break;buf[at++]=char(0xef);buf[at++]=char(0xbf);buf[at++]=char(0xbd);++i;}
      }
      copyText(out,buf);
    }
    else if(encoding==4){ // ISO-8859-1 (ID3v1/v2, WAV INFO)
      for(size_t i=0;i<n&&p[i];++i){const uint8_t c=p[i];
        if(c<128){if(at+1>=N)break;buf[at++]=char(c);}else{if(at+2>=N)break;buf[at++]=char(0xc0|(c>>6));buf[at++]=char(0x80|(c&63));}}
      copyText(out,buf);
    }
    else if(encoding==2||encoding==5){ // 2 = UTF-16BE, 5 = UTF-16LE
      const bool le=encoding==5;
      auto unit=[&](size_t i)->uint32_t{return le?uint32_t(p[i]|(p[i+1]<<8)):be16(p+i);};
      for(size_t i=0;i+1<n;){uint32_t cp=unit(i);i+=2;if(!cp)break;if(cp==0xfeff)continue;
        if(cp>=0xd800&&cp<=0xdbff){if(i+1>=n)break;uint32_t lo=unit(i);i+=2;if(lo<0xdc00||lo>0xdfff)continue;cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);}
        else if(cp>=0xdc00&&cp<=0xdfff)continue;
        uint8_t v[4];unsigned k=0;if(cp<128)v[k++]=uint8_t(cp);else if(cp<0x800){v[k++]=0xc0|(cp>>6);v[k++]=0x80|(cp&63);}else if(cp<0x10000){v[k++]=0xe0|(cp>>12);v[k++]=0x80|((cp>>6)&63);v[k++]=0x80|(cp&63);}else{v[k++]=0xf0|(cp>>18);v[k++]=0x80|((cp>>12)&63);v[k++]=0x80|((cp>>6)&63);v[k++]=0x80|(cp&63);}
        if(at+k>=N)break;for(unsigned j=0;j<k;++j)buf[at++]=char(v[j]);
      }copyText(out,buf);
    }
    size_t length=strlen(out);while(length&&isspace((uint8_t)out[length-1]))out[--length]=0;
    size_t first=0;while(out[first]&&isspace((uint8_t)out[first]))++first;
    if(first)memmove(out,out+first,strlen(out+first)+1);
  }
  bool item(const Box& item,Record& r) {
    uint64_t at=item.payload;Box data;
    while(at<item.end){if(!next(at,item.end,data))return false;if(data.type!=fcc('d','a','t','a'))continue;
      if(data.end-data.payload<8)return false;
      struct Own{uint8_t* p=nullptr;~Own(){free(p);}} own;uint8_t* b=scratch_;
      if(!b){own.p=b=(uint8_t*)malloc(264);if(!b)return false;}
      memset(b,0,264);size_t n=size_t(data.end-data.payload);if(n>264)n=264;
      if(!read(data.payload,b,n))return false;unsigned encoding=be32(b)&0xffffff;
      const uint8_t* p=b+8;size_t count=n-8;
      switch(item.type){
        case fcc(0xa9,'n','a','m'):text(r.title,p,count,encoding);break;
        case fcc(0xa9,'A','R','T'):text(r.artist,p,count,encoding);break;
        case fcc(0xa9,'a','l','b'):text(r.album,p,count,encoding);break;
        case fcc('a','A','R','T'):text(r.albumArtist,p,count,encoding);break;
        case fcc('t','r','k','n'):if(count>=4)r.track=be16(p+2);break;
        case fcc('d','i','s','k'):if(count>=4)r.disc=be16(p+2);break;
        case fcc('c','p','i','l'):if(count)r.compilation=p[0]!=0;break;
      }
    }return true;
  }
  bool walk(uint64_t start,uint64_t end,unsigned depth,bool ilst,Record& r) {
    if(depth>5)return false;Box b;
    while(start<end){if(!next(start,end,b))return false;
      if(ilst){if(!item(b,r))return false;continue;}
      if(b.type==fcc('m','o','o','v')||b.type==fcc('u','d','t','a')||b.type==fcc('m','e','t','a')||b.type==fcc('i','l','s','t')){
        uint64_t skip=b.type==fcc('m','e','t','a')?4:0;
        if(skip>b.end-b.payload||!walk(b.payload+skip,b.end,depth+1,b.type==fcc('i','l','s','t'),r))return false;
      }
    }return true;
  }
  // ---- MP3 (ID3v2.2/2.3/2.4, ID3v1), FLAC (Vorbis comments), WAV (LIST/INFO) ----
  // All reads are bounded (read() budget, frame/block/comment counters, at most 255 payload bytes per field), use only the
  // caller's 264 B scratch and never the heap; embedded pictures and other bodies are skipped by offset.
  static uint32_t le32(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
  static uint32_t syncsafe(const uint8_t* p){return uint32_t(p[0]&127)<<21|uint32_t(p[1]&127)<<14|uint32_t(p[2]&127)<<7|(p[3]&127);}
  static bool keyIs(const uint8_t* p,size_t n,const char* key){
    size_t i=0;for(;key[i];++i){if(i>=n||tolower(p[i])!=key[i])return false;}return i==n;
  }
  uint16_t number(const uint8_t* p,size_t n,unsigned encoding){char tmp[12]={};text(tmp,p,n,encoding);return uint16_t(atoi(tmp));}
  enum Field:uint8_t { None,Title,Artist,Album,AlbumArtist,Track,Disc,Compilation };
  void store(Record& r,Field f,const uint8_t* p,size_t n,unsigned encoding) {
    switch(f){
      case Title:text(r.title,p,n,encoding);break;
      case Artist:text(r.artist,p,n,encoding);break;
      case Album:text(r.album,p,n,encoding);break;
      case AlbumArtist:text(r.albumArtist,p,n,encoding);break;
      case Track:r.track=number(p,n,encoding);break;
      case Disc:r.disc=number(p,n,encoding);break;
      case Compilation:r.compilation=number(p,n,encoding)!=0;break;
      default:break;
    }
  }
  static Field id3Field(const uint8_t* id,unsigned version) {
    static const char* const v22[]={"TT2","TP1","TAL","TP2","TRK","TPA","TCP"};
    static const char* const v23[]={"TIT2","TPE1","TALB","TPE2","TRCK","TPOS","TCMP"};
    const unsigned len=version==2?3:4;
    for(unsigned i=0;i<7;++i)if(!memcmp(id,version==2?v22[i]:v23[i],len))return Field(i+1);
    return None;
  }
  bool id3v1(Record& r) {
    if(size_<128||!read(size_-128,scratch_,128)||memcmp(scratch_,"TAG",3))return true;
    const uint8_t* b=scratch_;
    if(!r.title[0])text(r.title,b+3,30,4);
    if(!r.artist[0])text(r.artist,b+33,30,4);
    if(!r.album[0])text(r.album,b+63,30,4);
    if(!r.track&&!b[125]&&b[126])r.track=b[126];
    return true;
  }
  bool id3v2(Record& r,const uint8_t* h) {
    const unsigned version=h[3];
    if(version<2||version>4||((h[6]|h[7]|h[8]|h[9])&128))return true; // unreadable tag: filename fallback
    uint64_t end=10+uint64_t(syncsafe(h+6)),at=10;if(end>size_)end=size_;
    if(version>=3&&(h[5]&0x40)){uint8_t e[4];if(!read(at,e,4))return false;at+=version==4?syncsafe(e):uint64_t(be32(e))+4;}
    const unsigned header=version==2?6:10;
    for(unsigned frames=0;frames<512&&end>at&&end-at>=header;++frames){
      uint8_t f[10];if(!read(at,f,header))return false;
      if(!f[0])break; // padding
      uint64_t body=at+header;
      uint32_t n=version==2?uint32_t(f[3])<<16|uint32_t(f[4])<<8|f[5]:version==3?be32(f+4):syncsafe(f+4);
      if(n>end-body)break; // damaged size: keep what was read
      at=body+n;
      const Field field=id3Field(f,version);
      if(field==None||n<2)continue;
      if(version==3&&(f[9]&0xc0))continue;                 // compressed / encrypted
      if(version==4){if(f[9]&0x0c)continue;if((f[9]&1)&&n>=4){body+=4;n-=4;}} // data-length indicator
      const uint32_t take=n<255?n:255;
      if(!read(body,scratch_,take))return false;
      unsigned encoding=scratch_[0];const uint8_t* p=scratch_+1;size_t count=take-1;
      if(encoding==0)encoding=4;
      else if(encoding==1){
        if(count>=2&&p[0]==0xff&&p[1]==0xfe){encoding=5;p+=2;count-=2;}
        else if(count>=2&&p[0]==0xfe&&p[1]==0xff){encoding=2;p+=2;count-=2;}
        else encoding=2;
      }
      else if(encoding==2)encoding=2;
      else if(encoding==3)encoding=1;
      else continue;
      store(r,field,p,count,encoding);
    }
    return true;
  }
  bool vorbis(uint64_t at,uint32_t n,Record& r) {
    const uint64_t end=at+n;uint8_t w[4];
    if(n<8||!read(at,w,4))return false;
    const uint32_t vendor=le32(w);if(vendor>n-8)return true;
    at+=4+uint64_t(vendor);if(!read(at,w,4))return false;
    const uint32_t count=le32(w);at+=4;
    for(uint32_t i=0;i<count&&i<256&&end>at&&end-at>=4;++i){
      if(!read(at,w,4))return false;
      const uint32_t length=le32(w);at+=4;if(length>end-at)break;
      const uint32_t take=length<255?length:255;
      if(take&&!read(at,scratch_,take))return false;
      at+=length;
      size_t eq=0;while(eq<take&&scratch_[eq]!='=')++eq;
      if(eq>=take)continue;
      const uint8_t* value=scratch_+eq+1;const size_t count2=take-eq-1;
      Field f=None;
      if(keyIs(scratch_,eq,"title"))f=Title;else if(keyIs(scratch_,eq,"artist"))f=Artist;else if(keyIs(scratch_,eq,"album"))f=Album;
      else if(keyIs(scratch_,eq,"albumartist"))f=AlbumArtist;else if(keyIs(scratch_,eq,"tracknumber"))f=Track;
      else if(keyIs(scratch_,eq,"discnumber"))f=Disc;else if(keyIs(scratch_,eq,"compilation"))f=Compilation;
      store(r,f,value,count2,1);
    }
    return true;
  }
  bool flac(Record& r) {
    uint64_t at=4;
    for(unsigned blocks=0;blocks<64&&size_>at&&size_-at>=4;++blocks){
      uint8_t b[4];if(!read(at,b,4))return false;
      const uint32_t n=uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];const uint64_t body=at+4;
      if(n>size_-body)return true;
      if((b[0]&127)==4&&!vorbis(body,n,r))return false;
      if(b[0]&128)break;
      at=body+n; // PICTURE, padding, seek table: skipped by offset
    }
    return true;
  }
  bool info(uint64_t at,uint64_t end,Record& r) {
    for(unsigned i=0;i<32&&end>at&&end-at>=8;++i){
      uint8_t h[8];if(!read(at,h,8))return false;
      const uint32_t n=le32(h+4);const uint64_t body=at+8;if(n>end-body)break;
      Field f=None;
      if(!memcmp(h,"INAM",4))f=Title;else if(!memcmp(h,"IART",4))f=Artist;else if(!memcmp(h,"IPRD",4))f=Album;else if(!memcmp(h,"ITRK",4))f=Track;
      if(f!=None&&n){const uint32_t take=n<255?n:255;if(!read(body,scratch_,take))return false;store(r,f,scratch_,take,1);}
      at=body+n+(n&1);
    }
    return true;
  }
  bool wav(Record& r) {
    uint64_t at=12;
    for(unsigned chunks=0;chunks<64&&size_>at&&size_-at>=8;++chunks){
      uint8_t h[8];if(!read(at,h,8))return false;
      const uint32_t n=le32(h+4);const uint64_t body=at+8;
      if(n>size_-body)return true; // unsized streaming chunk: nothing follows it
      if(!memcmp(h,"LIST",4)&&n>=4){
        if(!read(body,h,4))return false;
        if(!memcmp(h,"INFO",4)&&!info(body+4,body+n,r))return false;
      }
      at=body+n+(n&1); // the audio data chunk is skipped by offset
    }
    return true;
  }
 public:
  // `scratch` (264 B) is required for MP3/FLAC/WAV tags; M4A falls back to the heap without it.
  Tags(ReadAt reader,void* user,uint64_t size,uint8_t* scratch=nullptr):read_(reader),user_(user),size_(size),scratch_(scratch){}
  bool parse(Record& r){
    uint8_t h[12];
    if(scratch_&&size_>=12&&read(0,h,12)){
      if(!memcmp(h,"ID3",3))return id3v2(r,h)&&id3v1(r); // ID3v1 only fills what the v2 tag left empty
      if(h[0]==0xff&&(h[1]&0xe0)==0xe0)return id3v1(r);
      if(!memcmp(h,"fLaC",4))return flac(r);
      if(!memcmp(h,"RIFF",4)&&!memcmp(h+8,"WAVE",4))return wav(r);
    }
    bytes_=0;
    return walk(0,size_,0,false,r);
  }
};
} // namespace cydlib

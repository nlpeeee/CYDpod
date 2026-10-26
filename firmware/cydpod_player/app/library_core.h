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
#include <ctype.h>

namespace cydlib {
constexpr uint16_t MaxTracks = 2000;
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
    else if(encoding==2){
      for(size_t i=0;i+1<n;){uint32_t cp=be16(p+i);i+=2;if(!cp)break;if(cp==0xfeff)continue;
        if(cp>=0xd800&&cp<=0xdbff){if(i+1>=n)break;uint32_t lo=be16(p+i);i+=2;if(lo<0xdc00||lo>0xdfff)continue;cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);}
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
      uint8_t b[264]={};size_t n=size_t(data.end-data.payload);if(n>sizeof(b))n=sizeof(b);
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
 public:
  Tags(ReadAt reader,void* user,uint64_t size):read_(reader),user_(user),size_(size){}
  bool parse(Record& r){return walk(0,size_,0,false,r);}
};
} // namespace cydlib

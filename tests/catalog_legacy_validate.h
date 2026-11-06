// CY-116C byte-count baseline: validator from 4112e82, unchanged.
static bool legacyValidate(const char* path,Header& h){
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


// CY-85: text must be faithful. Included by classic_ui_test.cpp (one translation unit, after its stubs).
//
// Everything here draws through the PRODUCTION glyph paths into the host RGB565 framebuffer:
// classic_text.cpp for the sized faces and the real Font16 tables for TFT font 2, so the checks and the
// PNG previews see the pixels the panel would receive. The v20 nearest-neighbour sampler survives only as
// a negative control that proves the checks detect the original failure. A smooth host font is never used.
#pragma once
#include "classic_text.h"
void classic_hostTrackInfo(const cydlib::Record*);

namespace textq {
using cydtext::Face;
namespace L=cydclassic::layout;

struct Bits{int w=0,h=0,x0=0,y0=0;std::vector<char> v;bool at(int x,int y)const{return x>=0&&y>=0&&x<w&&y<h&&v[size_t(y)*size_t(w)+size_t(x)];}};
static const GFXfont& fontOf(Face f){return f==Face::Title?FreeSansBold12pt7b:f==Face::TitleCompact?FreeSansBold9pt7b:FreeSans9pt7b;}
// Source bitmap of a GFX glyph exactly as stored in the font.
static Bits fontBits(Face f,char c){
  const GFXfont& font=fontOf(f);const GFXglyph& g=font.glyph[c-font.first];Bits b;b.w=g.width;b.h=g.height;b.x0=g.xOffset;b.y0=g.yOffset;b.v.assign(size_t(b.w)*size_t(b.h),0);
  const uint8_t* bits=font.bitmap+g.bitmapOffset;for(int i=0;i<b.w*b.h;++i)b.v[size_t(i)]=(bits[i>>3]>>(7-(i&7)))&1;
  return b;
}
// The 16 px TFT font 2 glyph: the source the v20 sampler shrank.
static Bits nativeBits(char c){
  Bits b;b.w=widtbl_f16[c-32];b.h=16;const int stride=(b.w+6)/8;b.v.assign(size_t(b.w)*16,0);
  for(int y=0;y<16;++y)for(int x=0;x<b.w;++x)b.v[size_t(y)*size_t(b.w)+size_t(x)]=(chrtbl_f16[c-32][y*stride+x/8]>>(7-x%8))&1;
  return b;
}
// v20 playerText sampler, verbatim arithmetic (classic_screen.cpp at 399dfab).
static Bits v20Sample(char c,int height){
  Bits b;const int sourceW=widtbl_f16[c-32],destW=sourceW*height/16,stride=(sourceW+6)/8;const unsigned char* glyph=chrtbl_f16[c-32];
  b.w=destW;b.h=height;b.v.assign(size_t(b.w)*size_t(height),0);
  for(int dy=0;dy<height;++dy)for(int dx=0;dx<destW;++dx){const int sx=dx*16/height;
    b.v[size_t(dy)*size_t(destW)+size_t(dx)]=sx<std::min(sourceW,stride*8)&&((glyph[(dy*16/height)*stride+sx/8]>>(7-sx%8))&1);}
  return b;
}
// Topology: 8-connected ink components and enclosed counters. Stems lost or counters closed by
// resampling change these numbers; they are what makes a glyph recognisable at any size.
static void flood(const Bits& b,std::vector<char>& seen,int sx,int sy,bool want,bool eight){
  std::vector<std::pair<int,int>> todo{{sx,sy}};
  while(!todo.empty()){const auto p=todo.back();todo.pop_back();const int x=p.first,y=p.second;if(x<-1||y<-1||x>b.w||y>b.h)continue;
    const size_t at=size_t(y+1)*size_t(b.w+2)+size_t(x+1);if(seen[at]||b.at(x,y)!=want)continue;seen[at]=1;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if((dx||dy)&&(eight||!dx||!dy))todo.push_back({x+dx,y+dy});}
}
static std::pair<int,int> topology(const Bits& b){
  int ink=0,holes=0;std::vector<char> seen(size_t(b.w+2)*size_t(b.h+2),0),bg(size_t(b.w+2)*size_t(b.h+2),0);
  for(int y=0;y<b.h;++y)for(int x=0;x<b.w;++x)if(b.at(x,y)&&!seen[size_t(y+1)*size_t(b.w+2)+size_t(x+1)]){++ink;flood(b,seen,x,y,true,true);}
  flood(b,bg,-1,-1,false,false);
  for(int y=0;y<b.h;++y)for(int x=0;x<b.w;++x)if(!b.at(x,y)&&!bg[size_t(y+1)*size_t(b.w+2)+size_t(x+1)]){++holes;flood(b,bg,x,y,false,false);}
  return {ink,holes};
}
struct Box{int x0=9999,y0=9999,x1=-1,y1=-1;bool any()const{return x1>=0;}
  bool inside(int ax,int ay,int bx,int by)const{return x0>=ax&&y0>=ay&&x1<=bx&&y1<=by;}
  bool meets(const Rect& r)const{return x0<r.x+r.w&&x1>r.x&&y0<r.y+r.h&&y1>r.y;}};
static Box scan(int x0,int y0,int x1,int y1,uint16_t paper){
  Box k;for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)if(tft.pixel(x,y)!=paper){k.x0=std::min(k.x0,x);k.y0=std::min(k.y0,y);k.x1=std::max(k.x1,x+1);k.y1=std::max(k.y1,y+1);}
  return k;
}
static Bits cropped(const Bits& in){
  int x0=9999,y0=9999,x1=-1,y1=-1;for(int y=0;y<in.h;++y)for(int x=0;x<in.w;++x)if(in.at(x,y)){x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x+1);y1=std::max(y1,y+1);}
  Bits b;if(x1<0)return b;b.w=x1-x0;b.h=y1-y0;b.x0=in.x0+x0;b.y0=in.y0+y0;b.v.assign(size_t(b.w)*size_t(b.h),0);
  for(int y=0;y<b.h;++y)for(int x=0;x<b.w;++x)b.v[size_t(y)*size_t(b.w)+size_t(x)]=in.at(x0+x,y0+y);
  return b;
}
// A glyph drawn by cydtext::draw alone on a clean panel, cropped to its ink, relative to its baseline origin.
static Bits drawnGlyph(Face f,char c){
  tft.setRotation(1);tft.fillScreen(0xffff);const char s[2]={c,0};cydtext::draw(f,s,100,100,cydtext::HAlignLeft,0x0000);
  const Box k=scan(60,40,160,160,0xffff);Bits b;if(!k.any())return b;
  const int cap=cydtext::capHeight(f),base=100-cap/2+cap;
  b.w=k.x1-k.x0;b.h=k.y1-k.y0;b.x0=k.x0-100;b.y0=k.y0-base;b.v.assign(size_t(b.w)*size_t(b.h),0);
  for(int y=0;y<b.h;++y)for(int x=0;x<b.w;++x)b.v[size_t(y)*size_t(b.w)+size_t(x)]=tft.pixel(k.x0+x,k.y0+y)!=0xffff;
  return b;
}
static bool same(const Bits& a,const Bits& b){return a.w==b.w&&a.h==b.h&&a.v==b.v;}

// 1. Glyph integrity: every printable glyph of both faces reaches the panel pixel for pixel at its designed
//    size. The v20 sampler (negative control) demonstrably changes glyph identity at the sizes it used.
static void glyphIntegrity(){
  int checked=0;
  for(Face f:{Face::Title,Face::TitleCompact,Face::Label})for(char c=33;c<=126;++c){
    const Bits source=cropped(fontBits(f,c)),drawn=drawnGlyph(f,c);
    check(same(source,drawn),(std::string("glyph '")+c+"' differs from its font bitmap").c_str());
    check(drawn.x0==source.x0&&drawn.y0==source.y0,(std::string("glyph '")+c+"' is displaced").c_str());++checked;
  }
  check(cydtext::capHeight(Face::Title)>=16&&cydtext::capHeight(Face::TitleCompact)>=12&&cydtext::capHeight(Face::Label)>=12,"legible cap heights: title >=16 px, labels >=12 px (v20 labels were 5..8 px)");
  int lost[3]={0,0,0};const int heights[3]={14,11,8};
  for(int i=0;i<3;++i)for(char c=33;c<=126;++c)if(topology(cropped(nativeBits(c)))!=topology(cropped(v20Sample(c,heights[i]))))++lost[i];
  check(lost[0]>=3&&lost[1]>=8&&lost[2]>=20,"negative control: the v20 sampler changes glyph topology (components/counters) at 14, 11 and 8 px");
  std::cout<<"CY85_GLYPHS_OK: "<<checked<<" glyphs pixel-exact with their font bitmaps; v20 sampler negative control changes topology of "<<lost[0]<<"/"<<lost[1]<<"/"<<lost[2]<<" of 94 glyphs at 14/11/8 px\n";
}

// 2. Measurement: what fit() reports is what draw() paints; truncation is the longest whole-glyph prefix
//    (an independent brute-force oracle); the ellipsis is whole; font-2 fallback truncation is UTF-8 safe.
static void measurement(){
  const char* corpus[]={"...Baby One More Time","Britney Spears","...Baby One More Time (Digital Deluxe Version)","Headphones","JC2432W328C","AirPods Pro 2",
    "Don't Stop Me Now (feat. Queen & Friends) [Remastered 2011] #1","WWWWWWWWWWWWWWWWWWWW","iiiiiiiiiiiiiiiiiiii","a b c d e f g h i j k l m n o p","  leading and trailing  ","1:02:05","-2:36","124 / 1285","x"};
  int cases=0,truncated=0;
  for(Face f:{Face::Title,Face::TitleCompact,Face::Label})for(const char* s:corpus)for(int maxW:{0,3,9,17,31,48,64,76,96,150,200,292,400}){
    char out[128];const int got=cydtext::fit(f,s,out,sizeof(out),maxW);++cases;
    check(got==cydtext::width(f,out)&&got<=maxW,"fit reports the width of what it returns and never exceeds the box");
    const int dots=3*cydtext::width(f,".");const std::string src=s;
    if(cydtext::width(f,s)<=maxW){check(src==out,"text that fits is returned whole");continue;}
    ++truncated;
    int k=0;for(int n=0;n<=int(src.size());++n)if(cydtext::width(f,src.substr(0,size_t(n)).c_str())+dots<=maxW)k=n;
    std::string want=src.substr(0,size_t(k));while(!want.empty()&&want.back()==' ')want.pop_back();
    want=want.empty()?(dots<=maxW?"...":""):want+"...";
    check(want==out,(std::string("whole-glyph truncation differs from the oracle for '")+s+"' in "+std::to_string(maxW)+" px: got '"+out+"' want '"+want+"'").c_str());
    if(out[0]){tft.setRotation(1);tft.fillScreen(0xffff);cydtext::draw(f,out,40,100,cydtext::HAlignLeft,0x0000);
      const Box ink=scan(0,0,320,240,0xffff);check(ink.any()&&ink.x0>=40-2&&ink.x1<=40+maxW+2,"painted ink stays inside the measured box");}
  }
  for(Face f:{Face::Title,Face::TitleCompact,Face::Label}){const char* s="Britney Spears";const int w=cydtext::width(f,s);
    tft.setRotation(1);tft.fillScreen(0xffff);cydtext::draw(f,s,200,100,cydtext::HAlignRight,0);const Box r=scan(0,0,320,240,0xffff);
    check(r.x1<=200+2&&r.x1>=200-3&&r.x0>=200-w-2,"right alignment lands on the measured box");
    tft.fillScreen(0xffff);cydtext::draw(f,s,160,100,cydtext::HAlignCentre,0);const Box c=scan(0,0,320,240,0xffff);
    check(std::abs((c.x0+c.x1)/2-160)<=3,"centre alignment lands on the measured box");}
  check(cydtext::width(Face::Label,"a\x01" "b")==cydtext::width(Face::Label,"a b")&&!cydtext::ascii("Beyonc\xC3\xA9")&&cydtext::ascii("Beyonce"),"controls measure as space; ascii() routes UTF-8 away from the sized faces");
  const String utf8("Beyonc\xC3\xA9 \xE2\x80\x93 Halo \xE5\x91\xA8\xE6\x9D\xB0\xE4\xBC\xA6");
  for(int maxW=0;maxW<=220;maxW+=3){const String shortened=ui_ellipsize(utf8,2,maxW);bool valid=true;
    for(unsigned i=0;i<shortened.length();++i){const uint8_t b=uint8_t(shortened[i]);if((b&0xC0)==0x80){valid=false;break;}
      const unsigned need=b>=0xF0?3:b>=0xE0?2:b>=0xC0?1:0;for(unsigned k2=1;k2<=need;++k2)if(i+k2>=shortened.length()||(uint8_t(shortened[i+k2])&0xC0)!=0x80)valid=false;i+=need;}
    check(valid&&(shortened.length()==0||tft.textWidth(shortened,2)<=maxW),"ui_ellipsize keeps whole UTF-8 characters and fits");}
  std::cout<<"CY85_MEASURE_OK: "<<cases<<" fit/draw cases ("<<truncated<<" truncated) match the brute-force oracle; paint stays inside the measure; UTF-8 fallback truncation is character-safe\n";
}

// 3. Whole screens through the real classic screens. Each label's ink box is recorded by the host
//    recorder; it must lie in its band, never touch another label or a control, and fit its width.
struct Scene{const char* name;const char* title;const char* artist;const char* album;const char* peer;bool wired;int volume;uint32_t position;bool known;int queue;int index;const char* whole;const char* wholeWide;};
static cydlib::Record record(const Scene& s){cydlib::Record r{};cydlib::copyText(r.title,s.title);cydlib::copyText(r.artist,s.artist);cydlib::copyText(r.album,s.album);return r;}
static std::vector<std::string> previews;
static void preview(const std::string& dir,const std::string& name){
  std::filesystem::create_directories(dir);
  tft.savePng((dir+"/"+name+".png").c_str(),1);tft.savePng((dir+"/"+name+"-x3.png").c_str(),3);previews.push_back(name);
}
static void checkPlayer(const Scene& s,const std::string& tag){
  const int w=tft.width(),h=tft.height(),dy=L::playerTextOffset(h);
  const Rect textBand{L::PlayerTextInset,L::PlayerTextTop,w-2*L::PlayerTextInset,L::PlayerTextH+dy};
  const Rect timeBand{L::ProgressInset,h-L::PlayerTimeClearBottom,w-2*L::ProgressInset,L::PlayerTimeClearH};
  const bool portrait=h>=L::PortraitMinH;const int countX=portrait?L::PlayerPortraitCountX:L::PlayerCountX,countW=portrait?L::PlayerPortraitCountW:L::PlayerCountW;
  const Rect countBand{countX-countW/2,0,countW,L::PlayerHeaderH};
  const Rect track=L::playerVolumeTrack<Rect>(w,h),chip=L::outputFace<Rect>(w,h),bar=L::progress<Rect>(w,h);
  const Rect numberBand{track.x+track.w+L::PlayerNumberGap,track.cy()-L::PlayerNumberClearH/2,L::PlayerNumberW,L::PlayerNumberClearH};
  const Rect chipBand{chip.x+L::PlayerChipTextInset,chip.y,chip.w-L::PlayerChipTextInset-L::PlayerChipTextPad,chip.h};
  // Anything that is not a label must stay clear of text bands.
  const Rect protectedRects[]={{bar.x-L::KnobR,bar.cy()-L::KnobR,bar.w+2*L::KnobR,2*L::KnobR},L::transport<Rect>(w,h,0),L::transport<Rect>(w,h,1),L::transport<Rect>(w,h,2),
    L::playerToggle<Rect>(w,h,false),L::playerToggle<Rect>(w,h,true),{track.x-L::KnobR,track.cy()-L::KnobR,track.w+2*L::KnobR,2*L::KnobR},L::playerBack<Rect>(),L::favourite<Rect>(w),L::options<Rect>(w)};
  const Rect* bands[]={&textBand,&timeBand,&countBand,&numberBand,&chipBand};
  for(const Rect& p:protectedRects)for(const Rect* b:bands){const bool apart=b->x>=p.x+p.w||b->x+b->w<=p.x||b->y>=p.y+p.h||b->y+b->h<=p.y;check(apart,(tag+": a text band overlaps a control").c_str());}
  std::vector<std::pair<std::string,Box>> inks;
  // Sized-face runs are always checked; font-2 fallback cells (non-ASCII, no strip on the host) only inside the text band.
  for(const auto& k:tft.inks)if(k.x1>=0&&(!k.cell||(k.y0>=textBand.y&&k.y1<=textBand.y+textBand.h)))inks.push_back({k.text,Box{k.x0,k.y0,k.x1,k.y1}});
  check(!inks.empty(),(tag+": no sized text was drawn").c_str());
  int inText=0;
  for(const auto& item:inks){const Box& b=item.second;
    check(b.inside(0,0,w,h),(tag+": label ink off the panel: "+item.first).c_str());
    const bool placed=b.inside(textBand.x-1,textBand.y,textBand.x+textBand.w+1,textBand.y+textBand.h)||b.inside(timeBand.x-1,timeBand.y,timeBand.x+timeBand.w+1,timeBand.y+timeBand.h)||
      b.inside(countBand.x,countBand.y,countBand.x+countBand.w,countBand.y+countBand.h)||b.inside(numberBand.x,numberBand.y,numberBand.x+numberBand.w,numberBand.y+numberBand.h)||
      b.inside(chipBand.x,chipBand.y,chipBand.x+chipBand.w,chipBand.y+chipBand.h);
    check(placed,(tag+": label ink outside every text band (clipped or overlapping a control): '"+item.first+"' "+std::to_string(b.x0)+","+std::to_string(b.y0)+"-"+std::to_string(b.x1)+","+std::to_string(b.y1)).c_str());
    if(b.inside(textBand.x-1,textBand.y,textBand.x+textBand.w+1,textBand.y+textBand.h))++inText;
    // The smallest label must still be legible: at least a 9 px tall ink box for anything with a letter or digit.
    if(item.first.find_first_not_of(" .")!=std::string::npos)check(b.y1-b.y0>=9,(tag+": label ink under 9 px tall: "+item.first).c_str());
  }
  // Labels never overlap each other (title/artist/album lines, elapsed/remaining, count, number, chip).
  for(size_t i=0;i<inks.size();++i)for(size_t j=i+1;j<inks.size();++j){
    const Box&a=inks[i].second,&b=inks[j].second;if(inks[i].first==inks[j].first&&a.x0==b.x0&&a.y0==b.y0)continue; // the same label drawn twice (redraw)
    const bool apart=a.x1<=b.x0||b.x1<=a.x0||a.y1<=b.y0||b.y1<=a.y0;
    if(!apart){bool redraw=a.x0==b.x0&&a.y0==b.y0;check(redraw,(tag+": labels overlap: '"+inks[i].first+"' and '"+inks[j].first+"'").c_str());}}
  check(inText>=1,(tag+": title missing").c_str());
}
static void sceneRun(const std::string& dir,const Scene& s,bool pngs){
  static cydlib::Record rec;rec=record(s);classic_hostTrackInfo(&rec);
  g_settings.outRoute=s.wired?OUT_WIRED:OUT_BT;btName=s.peer;volume=s.volume;positionMs=s.position;seekAllowed=s.known;
  for(unsigned rotation:{1u,3u,0u,2u}){
    tft.setRotation(rotation);queueLength=s.queue;queueData=nullptr;current=s.index;playing=true;
    toPlayer();ui_redraw();pump();
    const std::string tag=std::string(s.name)+" r"+std::to_string(rotation);
    check(classic_mode()==5,(tag+": Now Playing did not open").c_str());
    checkPlayer(s,tag);
    // Owner-visible labels must be drawn whole, not shortened: '|' separated, the second list only on the 320 px panels.
    for(const char* list:{s.whole,s.wholeWide}){if(list==s.wholeWide&&tft.width()<L::WideMinW)continue;
      std::string all=list;size_t at=0;while(at<=all.size()&&!all.empty()){const size_t bar=all.find('|',at);const std::string one=all.substr(at,bar==std::string::npos?std::string::npos:bar-at);
        check(label(one.c_str()),(tag+": '"+one+"' was shortened or missing").c_str());if(bar==std::string::npos)break;at=bar+1;}}
    if(pngs)preview(dir,std::string(s.name)+"-r"+std::to_string(rotation));
  }
}
// 4. Zero recurring full-screen repaints: a second ticking over redraws the 18 px time band and the bar only.
static void secondTick(){
  tft.setRotation(1);queueLength=10;current=3;playing=true;seekAllowed=true;positionMs=84000;classic_hostTrackInfo(nullptr);toPlayer();ui_redraw();pump();
  tft.resetDrawCounts();positionMs+=1000;ui_tick(clockNow+=1000);
  check(tft.fillCalls>=1&&tft.fillCalls<=8&&tft.largestFill<=long(tft.width())*L::PlayerTimeClearH,"one second of playback repaints only the time band and the bar, never the screen");
  std::cout<<"CY85_REPAINT_OK: a 1 Hz tick makes "<<tft.fillCalls<<" fills, largest "<<tft.largestFill<<" px (time band "<<tft.width()*L::PlayerTimeClearH<<" px)\n";
}
// 3b. Browse pages: the mini-player's two sized lines stay inside their cell and the 44 px footer,
//     and list captions (font 2) are shortened by whole glyphs. Faithful PNGs of the lists as well.
static void browsePages(const std::string& dir){
  static cydlib::Record rec;rec=record({"","...Baby One More Time (Digital Deluxe Version)","Britney Spears & The Very Long Named Orchestra","","",false,24,84000,true,85,6,"",""});
  classic_hostTrackInfo(&rec);btName="AirPods Pro 2";queueLength=85;queueData=nullptr;current=6;playing=true;seekAllowed=true;positionMs=84000;
  for(unsigned rotation:{1u,3u,0u,2u}){
    tft.setRotation(rotation);home();tapTile(0);ui_redraw();pump();
    const int w=tft.width(),h=tft.height();const Rect cell=L::mini<Rect>(w,h,1);
    const std::string tag="mini r"+std::to_string(rotation);int lines=0;
    for(const auto& k:tft.inks)if(!k.cell&&k.x1>=0){const Box b{k.x0,k.y0,k.x1,k.y1};
      check(b.inside(cell.x+L::MiniTextInset,cell.y+L::MiniProgressH,cell.x+cell.w-L::MiniTextInset+1,cell.y+cell.h),(tag+": mini-player text outside its cell: "+k.text).c_str());++lines;}
    check(lines==2,(tag+": mini-player should draw a title and an artist line").c_str());
    preview(dir,"songs-mini-r"+std::to_string(rotation));
  }
  classic_hostTrackInfo(nullptr);
  for(unsigned rotation:{1u,0u}){tft.setRotation(rotation);home();tapTile(5);tapRow(0);ui_redraw();pump();preview(dir,"audio-output-r"+std::to_string(rotation));}
  std::cout<<"CY85_BROWSE_OK: mini-player lines inside their cell in all rotations; list and settings previews written\n";
}
static void run(){
  const int savedQueue=queueLength,savedCurrent=current,savedVolume=volume;const uint16_t* savedData=queueData;const bool savedPlaying=playing,savedSeek=seekAllowed;
  const uint32_t savedPosition=positionMs;const uint8_t savedRoute=g_settings.outRoute;const String savedPeer=btName;const unsigned savedRotation=tft.getRotation();
  glyphIntegrity();
  measurement();
  const std::string dir="text-preview";
  // Real labels from the owner's photo (CY-85), then stress cases: overflow, punctuation, digits, UTF-8 fallback.
  const Scene scenes[]={
    {"britney-bluetooth","...Baby One More Time","Britney Spears","...Baby One More Time (Digital Deluxe Version)","AirPods Pro 2",false,24,84000,true,85,6,"...Baby One More Time|Britney Spears|1:24|-2:36|24","AirPods Pro 2|7 / 85"},
    {"britney-headphones","...Baby One More Time","Britney Spears","...Baby One More Time (Digital Deluxe Version)","",true,100,3725000,true,1285,123,"...Baby One More Time|Britney Spears|1:02:05|-0:00|100|Headphones",""},
    {"overflow-punctuation","Don't Stop Me Now (feat. Queen & Friends) [Remastered 2011] #1","Elton John & Kiki Dee / The Band / Orchestra 100%","Greatest Hits, Vol. 1: 1970-2020!","JC2432W328C",false,7,0,true,12,0,"0:00|-4:00|7","1 / 12"},
    {"unknown-duration","Short","A","","Speaker",false,0,5000,false,1,0,"Short|A|0:05|--:--|0|Speaker","1 / 1"},
    {"utf8-fallback","Beyonc\xC3\xA9 \xE2\x80\x93 Halo","\xE5\x91\xA8\xE6\x9D\xB0\xE4\xBC\xA6","\xE6\x99\xB4\xE5\xA4\xA9 (Caf\xC3\xA9 Edition)","AirPods Pro 2",false,50,125000,true,20,19,"2:05|-1:55|50","20 / 20"},
  };
  for(const Scene& s:scenes)sceneRun(dir,s,true);
  secondTick();
  browsePages(dir);
  classic_hostTrackInfo(nullptr);
  queueLength=savedQueue;queueData=savedData;current=savedCurrent;playing=savedPlaying;volume=savedVolume;positionMs=savedPosition;seekAllowed=savedSeek;
  g_settings.outRoute=savedRoute;btName=savedPeer;tft.setRotation(savedRotation);
  std::cout<<"CY85_SCREENS_OK: "<<previews.size()<<" faithful PNG previews in "<<dir<<"/ (native and x3): labels stay in their bands, never overlap each other or a control, rotations 0/1/2/3\n";
}
}

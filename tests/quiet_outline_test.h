#pragma once
#include "ui_icons.h"
namespace iconq {
namespace L=cydclassic::layout;
static void png(const std::string& name){
  std::filesystem::create_directories("icon-preview");
  check(tft.savePng(("icon-preview/"+name+".png").c_str()),"icon native PNG");
  check(tft.savePng(("icon-preview/"+name+"-x4.png").c_str(),4),"icon x4 PNG");
}
static unsigned blend(uint16_t fg,uint16_t bg,unsigned a){
  unsigned c=0;for(int shift:{0,5,11}){const unsigned mask=shift==5?63:31;
    c|=((((fg>>shift)&mask)*a+((bg>>shift)&mask)*(15-a)+7)/15)<<shift;}
  return c;
}
static void transaction(unsigned n,unsigned pixels,unsigned bursts,bool strict=true){
  check(tft.windows.size()==n&&(!strict||(tft.streamStarts==n&&tft.streamEnds==n)),("one raw-driver window/start/end per glyph or cell: expected "+std::to_string(n)+", windows/starts/ends "+std::to_string(tft.windows.size())+"/"+std::to_string(tft.streamStarts)+"/"+std::to_string(tft.streamEnds)).c_str());
  unsigned np=0,nb=0;for(const auto& w:tft.windows){check(w.pixels==unsigned(w.w*w.h),"whole clipped window filled");np+=w.pixels;nb+=w.bursts;}
  check(np==pixels&&nb==bursts&&!tft.pixelCalls,"row bursts cover window exactly, no drawPixel calls");
}
static void catalogue(){
  std::ifstream golden("../golden/quiet_outline_masks.txt");check(bool(golden),"independent source-mask fingerprints required");
  unsigned pixels=0,bursts=0,negative=0,partial=0;uint32_t offset=0;
  tft.setRotation(1);tft.fillScreen(0xf81f);tft.resetStreamCounts();
  for(unsigned index=0;index<162;++index){
    const unsigned icon=index/9+1,actual=icon==18?ICON_STAR:icon;const int sizes[]={12,14,16,18,24,25,28,32,34};int size=sizes[index%9];
    const auto* m=cydicons::find(actual,size,icon==18);check(m==&cydicons::masks[index],"enum/size/off-star lookup");
    check(m->offset==offset&&m->stride==(m->w+1)/2&&m->w<=34&&m->h<=34,"tight row-aligned mask metadata");offset+=m->stride*m->h;
    std::string name,expected;int expectedSize;golden>>name>>expectedSize>>expected;check(expectedSize==size,"golden catalogue order");
    uint32_t hash=2166136261u,swapped=2166136261u;unsigned peak=0;
    for(int y=0;y<m->h;++y)for(int x=0;x<m->w;++x){
      unsigned a=cydicons::alpha(*m,x,y);hash=(hash^a)*16777619u;
      const unsigned raw=cydicons::coverage[m->offset+y*m->stride+x/2];unsigned wrong=(x&1)?raw>>4:raw&15;swapped=(swapped^wrong)*16777619u;
      partial+=a>0&&a<15;peak=std::max(peak,a);
    }
    check(hash==uint32_t(std::stoul(expected,nullptr,16)),"decoded coverage equals independent source-mask fingerprint");negative+=hash!=swapped;
    check(peak==((size==14&&(actual==ICON_UP||actual==ICON_DOWN))?14u:15u)&&cydicons::alpha(*m,-1,0)==0&&cydicons::alpha(*m,m->w,m->h)==0,"full coverage and bounds");
    // All 162 masks are drawn by the production stream, including hollow star.
    const uint16_t fg=0x1b59,bg=0xffff;
    if(icon==18){
      // All off-star sizes are decoded above; the actual call site uses 18 px.
      const auto* star=cydicons::find(ICON_STAR,18,true);
      ui_iconToggle({140,90,40,36},ICON_STAR,false,bg);pixels+=40*36;bursts+=36;
      for(int y=0;y<star->h;++y)for(int x=0;x<star->w;++x)check(tft.pixel(160-9+star->ox+x,108-9+star->oy+y)==blend(theme().dim,bg,cydicons::alpha(*star,x,y)),"off star production pixels");
    }else{
      ui_icon(Icon(actual),160,120,size,fg,bg);pixels+=m->w*m->h;bursts+=m->h;
      const int gx=160-size/2+m->ox,gy=120-size/2+m->oy;
      for(int y=0;y<m->h;++y)for(int x=0;x<m->w;++x)check(tft.pixel(gx+x,gy+y)==blend(fg,bg,cydicons::alpha(*m,x,y)),"production RGB565 glyph pixels");
    }
  }
  check(offset==30469&&negative>150&&partial>1000,"packed byte count and swapped-nibble negative control");
  transaction(162,pixels,bursts);
  std::cout<<"CY90_MASKS_OK: 162 source fingerprints; "<<partial<<" fractional samples; "<<negative<<" swapped-nibble negatives; "<<pixels<<" streamed pixels / "<<bursts<<" row bursts\n";
}
static void clipping(){
  tft.setRotation(1);
  for(bool datum:{false,true})for(const Rect vp: {Rect{0,0,320,240},Rect{30,40,60,50},Rect{-5,-7,24,30},Rect{310,230,30,20},Rect{400,300,10,10}}){
    tft.fillScreen(0xf81f);tft.setViewport(vp.x,vp.y,vp.w,vp.h,datum);
    const int ox=datum?vp.x:0,oy=datum?vp.y:0;
    const auto* m=cydicons::find(ICON_PLAY,34);
    const int cx=datum?0:vp.x,cy=datum?0:vp.y;
    const int gx=cx-17+m->ox,gy=cy-17+m->oy;
    tft.resetStreamCounts();ui_icon(ICON_PLAY,cx,cy,34,0,0xffff);
    unsigned visible=0;
    for(int y=0;y<240;++y)for(int x=0;x<320;++x){
      unsigned expect=0xf81f;
      if(x>=std::max(0,vp.x)&&x<std::min(320,vp.x+vp.w)&&y>=std::max(0,vp.y)&&y<std::min(240,vp.y+vp.h)&&x>=gx+ox&&x<gx+ox+m->w&&y>=gy+oy&&y<gy+oy+m->h){
        expect=blend(0,0xffff,cydicons::alpha(*m,x-gx-ox,y-gy-oy));++visible;}
      check(tft.pixel(x,y)==expect,"viewport/screen-edge exact clipping and preserved surrounding pixels");
    }
    unsigned rows=tft.windows.empty()?0:tft.windows[0].h;transaction(visible?1:0,visible,rows);tft.resetViewport();
  }
  // Whole cells clip as a unit, with the glyph staying at its original centre.
  for(const Rect r:{Rect{-40,-25,80,52},Rect{290,220,80,52},Rect{400,300,80,52}}){
    tft.fillScreen(0xf81f);tft.resetStreamCounts();ui_button(r,ICON_PAUSE,true,false,34);
    const unsigned pixels=std::max(0,std::min(320,r.x+r.w)-std::max(0,r.x))*std::max(0,std::min(240,r.y+r.h)-std::max(0,r.y));
    transaction(pixels?1:0,pixels,pixels?std::min(240,r.y+r.h)-std::max(0,r.y):0);
  }
  tft.resetViewport();tft.resetStreamCounts();ui_icon(ICON_NONE,0,0,18,0,0xffff);ui_icon(ICON_PLAY,0,0,0,0,0xffff);transaction(0,0,0);
  std::cout<<"CY90_CLIP_OK: translated/absolute/partly offscreen/empty viewports, glyphs and whole cells; no out-of-bounds raw window\n";
}
static void cells(){
  tft.setRotation(1);
  const Rect r[5]={{0,148,52,48},{56,148,70,48},{130,146,80,52},{214,148,70,48},{284,148,36,48}};
  const Icon icons[5]={ICON_SHUFFLE,ICON_PREV,ICON_PLAY,ICON_NEXT,ICON_REPEAT};
  for(int state=0;state<4;++state){tft.fillScreen(theme().bg);tft.resetStreamCounts();for(int i=0;i<5;++i){const bool active=(state==2&&(i==0||i==2||i==4))||(state==3&&i==4);
    const Icon icon=state==2&&i==2?ICON_PAUSE:state==3&&i==4?ICON_REPEAT_ONE:icons[i];
    ui_button(r[i],icon,active,state==1,std::min(r[i].w+6,r[i].h+6)*3/5);}
    transaction(5,15104,244);png("transport-"+std::to_string(state));}
  for(int size:{12,14,16,18,24,25,28,32,34}){
    tft.fillScreen(theme().bg);
    for(int i=1;i<=17;++i)ui_icon(Icon(i),26+((i-1)%6)*52,28+((i-1)/6)*56,size,theme().text,theme().bg);
    ui_iconToggle({266,122,40,36},ICON_STAR,false,theme().bg);
    png("catalogue-"+std::to_string(size));
  }
  tft.fillScreen(theme().band);for(int state=0;state<4;++state)ui_iconToggle({20+state*70,20,40,36},ICON_STAR,state&1,theme().band,state>=2);png("favourite-states");
  std::cout<<"CY90_CELLS_OK: five transport cells = 5 windows, 244 bursts, 15104 pixels, 30263 modelled command/pixel bytes per row in all four states; max row 80 px\n";
}
static void screens(){
  static cydlib::Record rec=textq::record({"","...Baby One More Time","Britney Spears","...Baby One More Time (Digital Deluxe Version)","",false,24,84000,true,85,6,"",""});
  classic_hostTrackInfo(&rec);g_settings.outRoute=OUT_WIRED;btName="AirPods Pro 2";queueLength=85;queueData=nullptr;current=6;volume=24;positionMs=84000;seekAllowed=true;
  for(unsigned rotation:{0u,1u,3u})for(int state=0;state<4;++state){
    tft.setRotation(rotation);playing=state==2;shuffle=state==2;repeatMode=state==3?REPEAT_ONE:state==2?REPEAT_ALL:REPEAT_OFF;
    toPlayer();ui_redraw();pump();
    if(state==1){const Rect r=L::transport<Rect>(tft.width(),tft.height(),1);SCREEN_CLASSIC.press(PRESS_DOWN,r.cx(),r.cy());}
    png("player-r"+std::to_string(rotation)+"-s"+std::to_string(state));
    if(state==1)SCREEN_CLASSIC.press(PRESS_UP,160,120);
  }
  tft.setRotation(1);playing=false;toPlayer();ui_redraw();pump();
  tft.resetStreamCounts();ui_tick(clockNow+=1);transaction(0,0,0,false);
  const Rect r=L::transport<Rect>(320,240,1);
  tft.resetStreamCounts();SCREEN_CLASSIC.press(PRESS_DOWN,r.cx(),r.cy());transaction(1,80*52,52,false);
  tft.resetStreamCounts();SCREEN_CLASSIC.press(PRESS_UP,r.cx(),r.cy());transaction(1,80*52,52,false);
  tft.resetStreamCounts();positionMs+=1000;ui_tick(clockNow+=1000);
  // The overflowing album now scrolls (Now Playing marquee): its text band may stream; no icon cell may.
  {size_t band=0;for(const auto& v:tft.windows)if(v.x==L::PlayerTextInset&&v.w==320-2*L::PlayerTextInset&&v.y>=L::PlayerTextTop&&v.y+v.h<=L::PlayerTextTop+L::PlayerTextH)++band;
   check(band==tft.windows.size(),("time tick streams no icon cell, only the marquee text band: windows "+std::to_string(tft.windows.size())+", band "+std::to_string(band)).c_str());}
  classic_hostTrackInfo(nullptr);
  std::cout<<"CY90_DIRTY_OK: idle/time ticks stream 0 icon cells; Play touch-down and release stream exactly 1 cell each; production player PNGs rotations 0/1/3\n";
}
static void run(){
  const bool wasPlaying=playing,wasShuffle=shuffle,wasSeek=seekAllowed;const uint8_t wasRepeat=repeatMode,wasRoute=g_settings.outRoute;
  const int wasQueue=queueLength,wasCurrent=current,wasVolume=volume;const uint16_t* wasData=queueData;
  const unsigned wasRotation=tft.getRotation();const uint32_t wasPosition=positionMs;const String wasPeer=btName;
  catalogue();clipping();cells();screens();
  playing=wasPlaying;shuffle=wasShuffle;repeatMode=wasRepeat;seekAllowed=wasSeek;g_settings.outRoute=wasRoute;
  queueLength=wasQueue;current=wasCurrent;volume=wasVolume;queueData=wasData;positionMs=wasPosition;btName=wasPeer;tft.setRotation(wasRotation);
}
}

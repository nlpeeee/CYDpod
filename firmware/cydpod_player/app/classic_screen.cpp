#ifndef CYD_UPLOADER
#include "classic_screen.h"
#include "classic_navigation.h"
#include "classic_layout.h"
#include "ui.h"
#include "display.h"
#include "catalog.h"
#include "storage.h"
#include "playlist.h"
#include "audio.h"
#include "sleep_mode.h"
#include "settings.h"
#include "bt.h"
#include "output_switch.h"
#include "speaker.h"
#include "wired.h"
#include "calibrate.h"
#include "titles.h"
#include "firmware.h"
#include <esp_system.h>
#ifndef CYDPOD_CATALOG_HOST
// Reuse TFT_eSPI font 2 in flash; no font copy, bitmap cache or heap.
extern const unsigned char widtbl_f16[96];
extern const unsigned char* const chrtbl_f16[96];
#endif

namespace {
namespace L=cydclassic::layout;
enum class Mode:uint8_t { Home,Playlists,Catalog,Sources,SourceSongs,Player,Settings,Playback,Display,Output,Volume,Options,About,Refresh,Message,Search,Keyboard,System };
struct Page {
  Mode mode=Mode::Home;CatalogView view{};int16_t source=-1;uint16_t top=0;
  SourceKind sourceKind=SRC_M3U;char title[64]={};
};
// Brightness -/+ and Orientation, then Calibrate touch on the resistive board only.
constexpr int DisplayRows=CYD_TOUCH_CAPACITIVE?3:4;
// Landscape -> landscape flipped -> portrait -> portrait flipped, so every rotation can be reached
// and left again (rotation 1 is the W328C's upright landscape, board.h).
const char* const RotationName[4]={"Portrait","Landscape","Portrait flipped","Landscape flipped"};
uint8_t nextRotation(uint8_t rotation){static const uint8_t next[4]={2,3,1,0};return next[rotation&3];}
void resetPin(){g_settings.webPin="000000";settings_save();}
struct Row {
  char label[128]={},detail[96]={};CatalogView child{};
  int16_t track=-1,source=-1;bool valid=false,navigates=false,disabled=false;
};
cydclassic::Navigation<Page> navigation;
cydclassic::Gesture gesture;
Row cache[6];
cydlib::Record playingInfo{};
uint8_t dirty=0;
int count=0,ready=-1,highlight=-1,dimmed=-1;
uint16_t* preview=nullptr;
int previewCount=0;
uint32_t lastPaint=0,lastPlayer=0,shownSerial=UINT32_MAX,saveAt=0;
uint32_t seenError=0,lastStatus=0;
int shownProgress=-1,shownSecond=-1,shownVolume=-1,shownConnection=-1;
bool shownPlaying=false,savePending=false,shownSeeking=false,shownWaiting=false;
bool previewDirty=false;
bool externalReturn=false;
bool seekPreview=false,seekCancelled=false,volumeChanged=false;
int playerPressed=-1,tilePressed=-1;
#ifdef CYDPOD_CATALOG_HOST
int volumeSaveArms=0;
#endif
uint32_t seekMs=0,seekDuration=0,seekSerial=0;
char notice[128]={};
char query[32]={};bool digitKeys=false,lettersSecond=false;uint32_t screenNow=0;
int searchShownCount=-1,searchShownProgress=-1;
constexpr int KeyBase=1000,KeyBack=-40,KeyDigits=-41,KeySpace=-42,KeyClear=-43,KeyDone=-44,KeyPage=-45;
constexpr int TileBase=-50; // Home tile i is target TileBase-i
constexpr int Back=-10,Home=-11,Up=-12,Down=-13,Now=-14,Prev=-20,Play=-21,Next=-22,Volume=-23,Options=-24,Slider=-25,Minus=-26,Plus=-27,Seek=-28,Favourite=-29,Shuffle=-30,Repeat=-31,Output=-32;
Page miniReturn{};uint8_t miniReturnDepth=0;uint32_t miniPressUntil=0;bool miniQueued=false;
Page& page(){return navigation.current();}
bool browse(){return page().mode!=Mode::Player&&page().mode!=Mode::Keyboard;}
int rows(){return L::rows(tft.height());}
int footerY(){return L::footerY(tft.height());}
Rect rowRect(int i){return L::row<Rect>(tft.width(),i);}
Rect transportRect(int action){return L::transport<Rect>(tft.width(),tft.height(),action==Prev?0:action==Play?1:2);}
Rect sliderRect(){return L::slider<Rect>(tft.width());}
void flushSettings(){if(savePending){g_settings.volume=audio_volume();settings_save();savePending=false;}}
void freePreview(){free(preview);preview=nullptr;previewCount=0;}
void text(const char* s,const Rect& r,uint16_t fg,uint16_t bg,TypeRole role=TYPE_BODY){ui_text(String(s),r.x+L::TextMargin,r.cy(),ML_DATUM,fg,bg,r.w-2*L::TextMargin,role);}
void centred(const String& s,int y,uint16_t fg,uint16_t bg,int width=0){ui_text(s,tft.width()/2,y,MC_DATUM,fg,bg,width?width:tft.width()-2*L::TextMargin);}
const char* pageTitle(const Page& value){
  if(value.title[0])return value.title;
  switch(value.mode){
    case Mode::Home:return "CYDpod";case Mode::Playlists:return "Playlists";case Mode::Player:return "Now Playing";
    case Mode::Search:return "Search Results";case Mode::Keyboard:return "Search Songs";
    case Mode::Settings:return "Settings";case Mode::Playback:return "Playback";case Mode::Display:return "Display";
    case Mode::Output:return "Audio Output";case Mode::Volume:return "Volume";case Mode::Options:return "Options";
    case Mode::About:return "About";case Mode::Refresh:return "Refresh Library";case Mode::Message:return "CYDpod";case Mode::System:return "Library & System";
    default:return "CYDpod";
  }
}
const char* title(){return pageTitle(page());}
// Pixel-height text reuses the pinned TFT font bitmap at 22/14/12 px.
// Non-ASCII retains the existing UTF-8 strip path, with its native strip height.
int playerGlyphWidth(unsigned char c,int height){
#ifdef CYDPOD_CATALOG_HOST
  const char glyph[2]={char(c),0};return tft.textWidth(String(glyph),2)*height/L::FontSourceH;
#else
  return widtbl_f16[c-L::FontFirst]*height/L::FontSourceH;
#endif
}
void playerText(const char* value,int x,int y,int height,int width,uint16_t fg,uint16_t bg,bool centre=true){
  for(const unsigned char* c=(const unsigned char*)value;*c;++c)if(*c>L::FontLast){ui_text(String(value),x,y,centre?MC_DATUM:ML_DATUM,fg,bg,width);return;}
  titles_release(); // Match ui_text: an ASCII draw must not retain a prior UTF-8 strip.
  char label[128];cydlib::copyText(label,value);int used=0,n=0;
  for(;label[n];++n){if((unsigned char)label[n]<L::FontFirst)label[n]=' ';used+=playerGlyphWidth(label[n],height);}
  if(used>width){const int dots=3*playerGlyphWidth('.',height);while(n&&used+dots>width)used-=playerGlyphWidth(label[--n],height);memcpy(label+n,"...",4);used+=dots;}
  int left=centre?x-used/2:x;
#ifdef CYDPOD_CATALOG_HOST
  tft.classicText(label,left,y,height,fg,bg);
#else
  tft.startWrite();
  for(const unsigned char* c=(const unsigned char*)label;*c;++c){
    const int sourceW=widtbl_f16[*c-L::FontFirst],destW=playerGlyphWidth(*c,height),stride=(sourceW+6)/L::FontByteBits;
    const unsigned char* glyph=chrtbl_f16[*c-L::FontFirst];
    for(int dy=0;dy<height;++dy){int run=-1;for(int dx=0;dx<=destW;++dx){const int sx=dx*L::FontSourceH/height;
      const bool on=dx<destW&&sx<min(sourceW,stride*L::FontByteBits)&&((glyph[(dy*L::FontSourceH/height)*stride+sx/L::FontByteBits]>>(7-sx%L::FontByteBits))&1);
      if(on&&run<0)run=dx;else if(!on&&run>=0){tft.drawFastHLine(left+run,y-height/2+dy,dx-run,fg);run=-1;}}
    }left+=destW;
  }tft.endWrite();
#endif
}
void playerHeader(){
  const Palette& p=theme();tft.fillRect(0,0,tft.width(),L::PlayerHeaderH,p.band);tft.drawFastHLine(0,L::PlayerHeaderH,tft.width(),p.outline);
  if(playerPressed==Back)tft.fillRect(0,0,L::PlayerBackW,L::PlayerHeaderH,p.pressed);
  ui_icon(ICON_BACK,L::PlayerBackX,L::PlayerHeaderY,L::PlayerHeaderIcon,p.text,p.band);
  const Page& parent=miniReturnDepth==navigation.depth?miniReturn:navigation.entries[navigation.depth>1?navigation.depth-2:0];
  const char* name=parent.mode==Mode::Home?"Home":pageTitle(parent);
  ui_text(String(name),L::PlayerParentX,L::PlayerHeaderY,ML_DATUM,p.text,p.band,L::PlayerBackW-L::PlayerParentX,TYPE_CAPTION);
  const int pos=audio_queuePos(audio_currentTrack());
  const String position=String(pos<0?0:pos+1)+" / "+String(audio_queueLen());const bool portrait=tft.height()>=L::PortraitMinH;
  playerText(position.c_str(),portrait?L::PlayerPortraitCountX:L::PlayerCountX,L::PlayerHeaderY,portrait?L::PlayerPortraitCountH:L::PlayerCountH,portrait?L::PlayerPortraitCountW:L::PlayerCountW,p.dim,p.band);
  Rect f=L::favourite<Rect>(tft.width());f.h=L::PlayerHeaderH;ui_iconToggle(f,ICON_STAR,storage_trackCount()&&playlist_isFavourite(audio_currentTrack()),p.band,playerPressed==Favourite);
  Rect o=L::options<Rect>(tft.width());if(playerPressed==Options)tft.fillRect(o.x,o.y,o.w,L::PlayerHeaderH,p.pressed);
  for(int i=-1;i<=1;++i)tft.fillCircle(o.cx()+i*L::PlayerDotsGap,L::PlayerHeaderY,L::PlayerDotR,p.text);
}
void header(){
  if(page().mode==Mode::Player){playerHeader();return;}
  const Palette& p=theme();
  tft.fillRect(0,0,tft.width(),L::HeaderH,p.band);
  tft.drawFastHLine(0,L::HeaderH-1,tft.width(),p.outline);
  if(navigation.depth>1){
    const Page& parent=navigation.entries[navigation.depth-2];
    ui_icon(ICON_BACK,L::HeaderBackX,L::HeaderTextY,L::HeaderIconSize,p.text,p.band);
    ui_text(String(parent.mode==Mode::Home?"Home":pageTitle(parent)),L::HeaderParentX,L::HeaderTextY,ML_DATUM,p.text,p.band,L::BackW-L::HeaderParentX,TYPE_CAPTION);
  }
  if(navigation.depth>=4&&browse()){const Rect h=L::deepHome<Rect>();tft.drawLine(h.cx()-L::HomeGlyphHalf,h.cy(),h.cx(),h.cy()-L::HomeRoofH,p.text);tft.drawLine(h.cx(),h.cy()-L::HomeRoofH,h.cx()+L::HomeGlyphHalf,h.cy(),p.text);tft.drawRect(h.cx()-L::HomeGlyphHalf,h.cy(),2*L::HomeGlyphHalf,L::HomeWallH,p.text);}
  const int left=navigation.depth>=4&&browse()?L::BackW+L::DeepHomeW:navigation.depth>1?L::BackW:0,right=tft.width()-L::HeaderTitleRight;
  ui_text(String(title()),(left+right)/2,L::HeaderTextY,MC_DATUM,p.text,p.band,right-left);
  const bool wired=g_settings.outRoute==OUT_WIRED;
  const bool connected=wired?wired_active():bt_connected();
  ui_icon(wired?ICON_HEADPHONES:ICON_BLUETOOTH,tft.width()-L::RouteInset,L::HeaderTextY,L::RouteSize,connected?p.text:p.dim,p.band);
}
void miniProgress(){
  const Rect r=L::miniProgress<Rect>(tft.width(),tft.height());
  const uint32_t duration=audio_durationMs();
  const int px=audio_queueLen()&&duration?min<int>(r.w,uint64_t(audio_positionMs())*r.w/duration):0;
  tft.fillRect(r.x,r.y,r.w,r.h,theme().outline);
  if(px)tft.fillRect(r.x,r.y,px,r.h,theme().accent);
}
void miniPlayer(){
  const Palette& p=theme();const int w=tft.width(),h=tft.height();
  tft.fillRect(0,L::miniY(h),w,L::FooterH,p.band);
  miniQueued=audio_queueLen()>0;shownSerial=audio_trackSerial();shownPlaying=audio_isPlaying();
  if(!miniQueued){text("Shuffle all",{0,L::miniY(h),w,L::FooterH},storage_trackCount()?p.text:p.disabled,p.band);return;}
  for(int cell:{0,2}){const Rect r=L::mini<Rect>(w,h,cell);const uint16_t bg=playerPressed==(cell?Next:Play)?p.pressed:p.band;
    tft.fillRect(r.x,r.y+L::MiniProgressH,r.w,r.h-L::MiniProgressH,bg);
    ui_icon(cell?ICON_NEXT:shownPlaying?ICON_PAUSE:ICON_PLAY,r.cx(),r.cy(),L::MiniIconSize,p.text,bg);}
  const Rect r=L::mini<Rect>(w,h,1);
  // Reuse the existing Player record: one bounded metadata read only on state/track changes.
  if(!storage_trackInfo(audio_currentTrack(),playingInfo)){cydlib::copyText(playingInfo.title,"Track unavailable");playingInfo.artist[0]=0;}
  playerText(playingInfo.title,r.x+L::MiniTextInset,r.y+L::MiniTitleY,L::MiniTitleH,r.w-2*L::MiniTextInset,p.text,p.band,false);
  playerText(playingInfo.artist,r.x+L::MiniTextInset,r.y+L::MiniArtistY,L::MiniArtistH,r.w-2*L::MiniTextInset,p.dim,p.band,false);
  miniProgress();
}
void footer(){
  if(page().mode==Mode::Player)return;
  if(browse()){if(shownSerial!=audio_trackSerial()||shownPlaying!=audio_isPlaying()||miniQueued!=(audio_queueLen()>0))miniPlayer();return;}
  const Palette& p=theme();tft.fillRect(0,footerY(),tft.width(),L::FooterH,p.band);
  tft.drawFastHLine(0,footerY(),tft.width(),p.outline);
  if(page().mode==Mode::Keyboard){
    text("Space",L::action<Rect>(0,tft.height(),0),p.text,p.band);text("Clear",L::action<Rect>(0,tft.height(),1),p.text,p.band);text("Done",L::action<Rect>(0,tft.height(),2,true),p.text,p.band);return;
  }
}

int tileOf(int target){return target<=TileBase&&target>TileBase-L::TileCount?TileBase-target:-1;}
// Artists, Albums and Search have no ui_icon(); draw them from plain primitives like the Home glyph.
void tileGlyph(int i,int cx,int cy,uint16_t fg,uint16_t bg){
  const int s=L::TileIconSize;
  if(i==0)ui_icon(ICON_NOTE,cx,cy,s,fg,bg);
  else if(i==1){tft.fillCircle(cx,cy-s/4,s/5,fg);tft.fillRoundRect(cx-s/3,cy+s/12,2*s/3,s/2-s/12,s/6,fg);}
  else if(i==2){for(int k=0;k<L::TileStroke;++k)tft.drawRect(cx-s/2+k,cy-s/2+k,s-2*k,s-2*k,fg);tft.fillCircle(cx,cy,s/7,fg);}
  else if(i==3)ui_icon(ICON_STAR,cx,cy,s,fg,bg);
  else if(i==4){
    tft.fillCircle(cx-s/8,cy-s/8,s/3,fg);tft.fillCircle(cx-s/8,cy-s/8,s/3-L::TileStroke-1,bg);
    for(int k=0;k<L::TileStroke;++k)tft.drawLine(cx+s/8+k,cy+s/8,cx+s/2-1+k,cy+s/2-1,fg);
  }else ui_icon(ICON_GEAR,cx,cy,s,fg,bg);
}
void paintTile(int i,bool pressed){
  if(i<0||i>=L::TileCount)return;
  const char* names[]={"Songs","Artists","Albums","Playlists","Search","Settings"};
  const Palette& p=theme();const Rect r=L::tile<Rect>(tft.width(),tft.height(),i);const uint16_t face=pressed?p.pressed:p.band;
  tft.fillRoundRect(r.x,r.y,r.w,r.h,L::TileRadius,face);tft.drawRoundRect(r.x,r.y,r.w,r.h,L::TileRadius,p.outline);
  tileGlyph(i,r.cx(),r.y+L::TileIconY,p.text,face);
  ui_text(String(names[i]),r.cx(),r.y+r.h-L::TileLabelBottom,MC_DATUM,p.text,face,r.w-2*L::TileTextInset);
}
void paintTiles(){for(int i=0;i<L::TileCount;++i)paintTile(i,false);}
void paintRow(int i,bool pressed){
  if(i<0||i>=rows())return;Row& r=cache[i];const Palette& p=theme();const Rect box=rowRect(i);
  uint16_t bg=pressed?p.accent:p.bg,fg=pressed?p.onAccent:r.disabled?p.disabled:p.text;
  tft.fillRect(box.x,box.y,box.w,box.h,bg);
  if(!r.valid)return;
  if(page().mode==Mode::Output&&r.disabled){
    // Fixed small font keeps the no-detection hint on one line on the W328C.
    tft.setTextDatum(ML_DATUM);tft.setTextColor(fg,bg);tft.drawString(r.label,L::TextMargin,box.cy(),1);
  }else if(r.detail[0]){
    ui_text(String(r.label),L::TextMargin,box.y+L::RowTitleY,ML_DATUM,fg,bg,box.w-L::RowTextInset);
    ui_text(String(r.detail),L::TextMargin,box.y+L::RowDetailY,ML_DATUM,pressed?p.onAccent:p.dim,bg,box.w-L::RowTextInset,TYPE_CAPTION);
  }else text(r.label,box,fg,bg);
  if(r.navigates)ui_icon(ICON_CHEVRON,box.w-L::RowIconInset,box.cy(),L::RowIconSize,fg,bg);
  else if(r.track==audio_currentTrack()&&audio_isPlaying())ui_icon(ICON_PLAY,box.w-L::RowIconInset,box.cy(),L::RowIconSize,fg,bg);
  tft.drawFastHLine(0,box.y+L::RowH-1,box.w,p.outlineSubtle);
}
void invalidate(){
  page().top=cydclassic::clampTop(page().top,count,rows());dirty=uint8_t((1u<<rows())-1);
  for(int i=0;i<6;++i)cache[i].valid=false;
  const Palette& p=theme();const int x=tft.width()-L::StripW,h=rows()*L::RowH;
  tft.fillRect(x,L::HeaderH,L::StripW,h,p.band);
  if(count>rows())for(bool down:{false,true}){const Rect r=L::pageStrip<Rect>(tft.width(),tft.height(),down);ui_icon(down?ICON_DOWN:ICON_UP,r.cx(),r.cy(),L::FooterIconSize,p.text,p.band);}
  if(count>rows()){const int thumb=max(L::ThumbMin,h*rows()/count);const int y=L::HeaderH+(h-thumb)*page().top/max(1,count-rows());tft.fillRect(x+L::ThumbInset,y,L::ThumbW,thumb,p.dim);}
}
void setRow(Row& r,const char* label,const String& detail=String(),bool navigate=false,bool disabled=false){
  cydlib::copyText(r.label,label);cydlib::copyText(r.detail,detail.c_str());r.valid=true;r.navigates=navigate;r.disabled=disabled;
}
int sourceId(int wanted){
  return playlist_sourceAt(page().sourceKind,wanted);
}
int rowCount(){
  switch(page().mode){
    case Mode::Home:return 0;case Mode::Playlists:return 2+playlist_sourceCount(SRC_M3U);
    case Mode::Search:return 1+max(1,storage_searchCount());
    case Mode::Catalog:return storage_browseCount(page().view);
    case Mode::Sources:return playlist_sourceCount(page().sourceKind);
    case Mode::SourceSongs:return previewCount;
    case Mode::Settings:return 6;case Mode::System:return 4;case Mode::Playback:return 4;case Mode::Display:return DisplayRows;case Mode::Output:return OUT_SELECTABLE_COUNT + (CYD_WIRED_OUTPUT ? 1 : 0);
    case Mode::Options:return 4;case Mode::Refresh:return 2;
    default:return 0;
  }
}
void loadRow(int i){
  Row& r=cache[i];r=Row{};const int at=page().top+i;
  if(at>=count){r.valid=true;paintRow(i,false);return;}
  switch(page().mode){
    case Mode::Playlists:
      if(at<2)setRow(r,at?"Folders":"Favourites",String(),true);
      else{const int source=playlist_sourceAt(SRC_M3U,at-2);setRow(r,playlist_name(source).c_str(),String(),true,source<0);r.source=source;}
      break;
    case Mode::Catalog:{CatalogEntry e;if(storage_browseEntry(page().view,at,e)){setRow(r,e.label.c_str(),e.detail,e.navigates);r.track=e.track;r.child=e.child;}
      else setRow(r,"Unable to read entry","Check SD card",false,true);break;}
    case Mode::Search:
      if(at==0)setRow(r,(String("Edit: ")+query).c_str(),storage_searchFailed()?String("SD error. Edit to retry"):storage_searchBusy()?String(storage_searchBuilding()?"Preparing index ":"Searching ")+String(storage_searchProgress())+"%":String(storage_searchCount())+" results (max 50)",true);
      else if(at-1<storage_searchCount()){
        String song,artist;r.track=storage_searchTracks()[at-1];
        if(storage_trackSummary(r.track,song,artist))setRow(r,song.c_str(),artist);else setRow(r,"Track unavailable",String(),false,true);
      }else setRow(r,storage_searchBusy()?"Searching...":"No matches",String(),false,true);
      break;
    case Mode::Sources:{int source=sourceId(at);setRow(r,playlist_name(source).c_str(),String(),true,source<0);r.source=source;break;}
    case Mode::SourceSongs:{String title,artist;if(preview&&at<previewCount&&storage_trackSummary(preview[at],title,artist)){setRow(r,title.c_str(),artist);r.track=preview[at];}else setRow(r,"Track unavailable",String(),false,true);break;}
    case Mode::Settings:{const char* names[]={"Audio Output","Bluetooth","Playback","Display","Library & System","About"};setRow(r,names[at],String(),true);break;}
    case Mode::System:{const char* names[]={"Refresh Library","Upload music","Web passcode","Language"};
      setRow(r,names[at],at==2?g_settings.webPin:at==3?String(tr(T_LANG_NAME)):String(),at<2);break;}
    case Mode::Playback:
      if(at<2)setRow(r,at==0?"Shuffle":"Repeat",at==0?String(audio_shuffle()?"On":"Off"):String(audio_repeat()==REPEAT_ONE?"One":audio_repeat()==REPEAT_ALL?"All":"Off"));
      else setRow(r,at==2?"Screen timeout":"Sleep timer",at==2?String(sleep_timeoutLabel(g_settings.screenTimeout)):String(sleep_timerLabel(g_settings.sleepTimer)));break;
    case Mode::Display:{const char* names[]={"Brightness -","Brightness +","Orientation","Calibrate touch"};
      setRow(r,names[at],at<2?String(g_settings.brightness)+"%":at==2?String(RotationName[g_settings.rotation&3]):String(),at==3);break;}
    case Mode::Output:{if(at==OUT_SELECTABLE_COUNT){setRow(r,"Switch back to Bluetooth if there is no sound",String(),false,true);break;}const uint8_t route=outRouteSelectable(at);setRow(r,route==OUT_WIRED?"Headphones":"Bluetooth",g_settings.outRoute==route?String("Selected"):String());break;}
    case Mode::Options:{const char* names[]={"Favourite","Shuffle","Repeat","Audio Output"};setRow(r,names[at],at==0?String(playlist_isFavourite(audio_currentTrack())?"Remove":"Add"):String(),at==3);break;}
    case Mode::Refresh:setRow(r,at==0?"Rebuild and restart":"Cancel",at==0?String("Music files are preserved"):String());break;
    default:break;
  }
  paintRow(i,highlight==i);
}
void message(const char* value);
void external(ScreenId id){externalReturn=true;ui_go(id);}
void show(Mode mode,const char* name=""){
  Page next{};next.mode=mode;cydlib::copyText(next.title,name);
  if(!navigation.push(next)){message("Menu depth limit. Use Home.");return;}
  ui_go(SCR_CLASSIC);
}
void catalog(CatalogView view,const char* name){
  Page next{};next.mode=Mode::Catalog;next.view=view;cydlib::copyText(next.title,name);
  if(navigation.push(next))ui_go(SCR_CLASSIC);
}
bool buildPreview(int source){
  const size_t bytes=max(1,storage_trackCount())*sizeof(uint16_t);
  const size_t scratch=playlist_kind(source)==SRC_M3U?min(storage_trackCount(),1000)*sizeof(uint32_t):0;
  catalog_logStage("preview","before",bytes+scratch);
  if(!catalog_canAllocate(bytes+scratch)){catalog_logStage("preview","refused",bytes+scratch);return false;}
  uint16_t* candidate=(uint16_t*)malloc(max(1,storage_trackCount())*sizeof(uint16_t));
  if(!candidate){catalog_logStage("preview","refused",bytes+scratch);return false;}
  int n=playlist_preview(source,candidate,max(1,storage_trackCount()));
  catalog_logStage("preview","after",bytes+scratch);
  if(n<=0){free(candidate);return false;}
  freePreview();preview=candidate;previewCount=n;previewDirty=false;
  return true;
}
void openSource(int source){
  if(!buildPreview(source)){message("Unable to read list or allocate memory. Try again.");return;}
  Page next{};next.mode=Mode::SourceSongs;next.source=source;
  cydlib::copyText(next.title,source==1?"Favourites":playlist_name(source).c_str());
  if(navigation.push(next))ui_go(SCR_CLASSIC);else freePreview();
}
void message(const char* value){
  cydlib::copyText(notice,value);
  if(navigation.depth==8)navigation.back();
  Page next{};next.mode=Mode::Message;navigation.push(next);ui_go(SCR_CLASSIC);
}
void openPlaying(){
  if(page().mode==Mode::Player)return;
  for(unsigned i=0;i<navigation.depth;++i)if(navigation.entries[i].mode==Mode::Player){navigation.depth=i+1;ui_go(SCR_CLASSIC);return;}
  miniReturn=page();miniReturnDepth=navigation.depth;
  page()=Page{};page().mode=Mode::Player;ui_go(SCR_CLASSIC);
}
void back(){
  if(miniReturnDepth==navigation.depth){page()=miniReturn;miniReturnDepth=0;ui_go(SCR_CLASSIC);return;}
  if(page().mode==Mode::SourceSongs)freePreview();
  if(navigation.back())ui_go(SCR_CLASSIC);
}
void armVolumeSave(){if(volumeChanged){
#ifdef CYDPOD_CATALOG_HOST
++volumeSaveArms;
#endif
g_settings.volume=audio_volume();savePending=true;saveAt=millis()+1500;volumeChanged=false;}}
void volumeAt(int x){
  Rect s=page().mode==Mode::Player?L::playerVolumeTrack<Rect>(tft.width(),tft.height()):sliderRect();
  const int value=L::sliderValue(x,s.x,page().mode==Mode::Player?s.w:s.w-1,100);
  if(value!=audio_volume()){audio_setVolume(value);g_settings.volume=audio_volume();volumeChanged=true;}
}
void volumePaint(){
  const Palette& p=theme();centred(String(audio_volume())+"%",90,p.text,p.bg);
  Rect s=sliderRect();tft.fillRect(s.x,s.y,s.w,s.h,p.bg);ui_slider(s,audio_volume(),100,p.bg);
  ui_textButton(L::step<Rect>(0,false),T_COUNT);ui_textButton(L::step<Rect>(tft.width(),true),T_COUNT);
  ui_icon(ICON_VOL_DOWN,L::StepIconX,L::StepIconY,L::StepIconSize,p.text,p.btn);ui_icon(ICON_VOL_UP,tft.width()-L::StepIconX,L::StepIconY,L::StepIconSize,p.text,p.btn);
}
// ui_button normally insets its face. Clip an expanded cell so the painted
// Player face, as well as its hit target, matches the proposal's rectangle.
void playerButton(const Rect& r,Icon icon,bool active,bool pressed){
  const int inset=STYLE_INSET;tft.setViewport(r.x,r.y,r.w,r.h);
  ui_button({-inset,-inset,r.w+2*inset,r.h+2*inset},icon,active,pressed);tft.resetViewport();
}
void playerTransport(){
  playerButton(transportRect(Prev),ICON_PREV,false,playerPressed==Prev);
  playerButton(transportRect(Play),audio_isPlaying()?ICON_PAUSE:ICON_PLAY,audio_isPlaying(),playerPressed==Play);
  playerButton(transportRect(Next),ICON_NEXT,false,playerPressed==Next);
  playerButton(L::playerToggle<Rect>(tft.width(),tft.height(),false),ICON_SHUFFLE,audio_shuffle(),playerPressed==Shuffle);
  playerButton(L::playerToggle<Rect>(tft.width(),tft.height(),true),audio_repeat()==REPEAT_ONE?ICON_REPEAT_ONE:ICON_REPEAT,audio_repeat()!=REPEAT_OFF,playerPressed==Repeat);
  shownPlaying=audio_isPlaying();
}
void playerBar(const Rect& r,int value,int maximum){
  const Palette& p=theme();const int px=int(int64_t(value)*r.w/max(1,maximum));
  tft.fillRect(r.x-L::KnobR,r.cy()-L::KnobR,r.w+2*L::KnobR,2*L::KnobR,p.bg);
  tft.fillRoundRect(r.x,r.y,r.w,r.h,r.h/2,p.barBg);
  if(px)tft.fillRoundRect(r.x,r.y,px,r.h,r.h/2,p.bar);
  tft.fillCircle(r.x+px,r.cy(),L::KnobR,p.bg);tft.fillCircle(r.x+px,r.cy(),L::KnobR-L::KnobBorder,p.bar);
}
void playerVolume(){
  const int level=audio_volume();
  const Rect r=L::playerVolumeTrack<Rect>(tft.width(),tft.height());playerBar(r,level,100);
  tft.fillRect(r.x+r.w+L::PlayerNumberGap,r.cy()-L::PlayerArtistH/2,L::PlayerNumberW,L::PlayerArtistH,theme().bg);
  playerText(String(level).c_str(),r.x+r.w+L::PlayerNumberGap,r.cy(),L::PlayerAlbumH,L::PlayerNumberW,theme().dim,theme().bg,false);
  shownVolume=level;
}
void playerOutput(){
  const Palette& p=theme();const Rect r=L::outputFace<Rect>(tft.width(),tft.height());
  const uint16_t bg=playerPressed==Output?p.pressed:p.band;
  tft.fillRoundRect(r.x,r.y,r.w,r.h,L::PlayerChipRadius,bg);tft.drawRoundRect(r.x,r.y,r.w,r.h,L::PlayerChipRadius,p.outline);
  const bool wired=g_settings.outRoute==OUT_WIRED;
  if(wired){playerText("Headphones",r.cx(),r.cy(),L::PlayerChipTextH,r.w-2*L::PlayerChipInset,p.text,bg);return;}
  ui_icon(ICON_BLUETOOTH,r.x+L::PlayerChipIconInset,r.cy(),L::PlayerHeaderIcon,p.text,bg);
  const String name=wired?String("Headphones"):bt_speakerName();
  playerText(name.isEmpty()?"Bluetooth":name.c_str(),r.x+L::PlayerChipTextInset,r.cy(),L::PlayerChipTextH,r.w-L::PlayerChipTextInset-L::PlayerChipInset,p.text,bg,false);
}
void playerBottom(){
  const Palette& p=theme();const Rect r=L::playerVolume<Rect>(tft.width(),tft.height());
  tft.drawFastHLine(0,r.y,tft.width(),p.outline);ui_icon(ICON_SPEAKER,L::PlayerSpeakerX,r.cy(),L::PlayerSpeakerSize,p.text,p.bg);playerVolume();playerOutput();
}
void playerTrack(){
  const Palette& p=theme();memset(&playingInfo,0,sizeof(playingInfo));
  if(!storage_trackInfo(audio_currentTrack(),playingInfo)){cydlib::copyText(playingInfo.title,"No songs");cydlib::copyText(playingInfo.artist,"Add M4A files to /music");}
  const int dy=L::playerTextOffset(tft.height()),width=tft.width()-2*L::PlayerTextInset;
  tft.fillRect(0,L::PlayerTextTop,tft.width(),L::PlayerTextH+dy,p.bg);
  playerText(playingInfo.title,tft.width()/2,L::PlayerTitleY+dy,L::PlayerTitleH,width,p.text,p.bg);
  playerText(playingInfo.artist,tft.width()/2,L::PlayerArtistY+dy,L::PlayerArtistH,width,p.dim,p.bg);
  playerText(playingInfo.album,tft.width()/2,L::PlayerAlbumY+dy,L::PlayerAlbumH,width,p.dim,p.bg);
  shownProgress=shownSecond=-1;shownSerial=audio_trackSerial();playerHeader();
}
void playerTick(uint32_t now){
  if(shownVolume!=audio_volume())playerVolume();
  if(lastPlayer&&now-lastPlayer<250&&!seekPreview)return;lastPlayer=now;
  const Palette& p=theme();if(shownSerial!=audio_trackSerial())playerTrack();if(shownPlaying!=audio_isPlaying())playerTransport();
  const uint32_t pos=seekPreview?seekMs:audio_positionMs(),duration=audio_durationMs();
  const Rect bar=L::progress<Rect>(tft.width(),tft.height());
  const int progress=duration?min<int>(bar.w,uint64_t(pos)*bar.w/duration):0;
  if(progress!=shownProgress){playerBar(bar,progress,bar.w);shownProgress=progress;}
  const bool busy=audio_isSeeking(),waiting=audio_isPlaying()&&g_settings.outRoute==OUT_BT&&!bt_connected();
  if(int(pos/1000)!=shownSecond||busy!=shownSeeking||waiting!=shownWaiting){shownSecond=pos/1000;shownSeeking=busy;shownWaiting=waiting;
    tft.fillRect(0,tft.height()-L::PlayerTimeClearBottom,tft.width(),L::PlayerTimeClearH,p.bg);
    const String elapsed=busy?String("Seeking..."):waiting?String("Waiting for BT"):ui_time(pos);
    playerText(elapsed.c_str(),L::ProgressInset,tft.height()-L::PlayerTimeBottom,L::PlayerTimeH,tft.width()/2-L::ProgressInset,p.dim,p.bg,false);
    const String remain=String("-")+ui_time(duration>pos?duration-pos:0,duration>0);
    int remainWidth=0;for(const char* c=remain.c_str();*c;++c)remainWidth+=playerGlyphWidth(*c,L::PlayerTimeH);
    playerText(remain.c_str(),tft.width()-L::ProgressInset-remainWidth,tft.height()-L::PlayerTimeBottom,L::PlayerTimeH,tft.width()/2-L::ProgressInset,p.dim,p.bg,false);
  }
}
// Five rows of 40 px keys, with a 44 px action bar. No title list in RAM.
const char* keyLetters(){return digitKeys?"0123456789":tft.height()<L::PortraitMinH?(lettersSecond?"qrstuvwxyz":"abcdefghijklmnop"):"abcdefghijklmnopqrstuvwxyz";}
Rect keyRect(int i){return L::key<Rect>(tft.width(),tft.height(),i,digitKeys);}
Rect keyBackRect(){return L::keyBack<Rect>(tft.height());}
Rect keyDigitsRect(){return L::keyDigits<Rect>(tft.height());}
void keyFace(const Rect& r,const char* label){
  tft.fillRect(r.x+L::KeyGap/2,r.y+L::KeyGap/2,r.w-L::KeyGap,r.h-L::KeyGap,theme().band);
  ui_text(String(label),r.cx(),r.cy(),MC_DATUM,theme().text,theme().band,r.w-L::KeyTextInset);
}
void queryPaint(){
  tft.fillRect(0,L::HeaderH,tft.width(),L::QueryH,theme().bg);
  ui_text(String(query[0]?query:"Type a title or artist"),L::QueryInset,L::QueryY,ML_DATUM,theme().text,theme().bg,tft.width()-L::QueryTextInset);
  ui_text(storage_searchFailed()?String("SD!"):storage_searchBusy()?String(storage_searchProgress())+"%":String(storage_searchCount()),tft.width()-L::QueryStatusInset,L::QueryY,MR_DATUM,theme().dim,theme().bg,L::QueryStatusW,TYPE_CAPTION);
}
void keyboardPaint(){
  tft.fillRect(0,L::KeyY,tft.width(),footerY()-L::KeyY,theme().bg);
  const char* keys=keyLetters();
  for(int i=0;keys[i];++i){char label[2]={keys[i],0};keyFace(keyRect(i),label);}
  keyFace(keyBackRect(),"Delete");keyFace(keyDigitsRect(),digitKeys?"ABC":"123");
  if(tft.height()<L::PortraitMinH)keyFace(L::keyPage<Rect>(),lettersSecond?"A-P":"Q-Z");
  keyFace(L::action<Rect>(0,tft.height(),0),"Space");keyFace(L::action<Rect>(0,tft.height(),1),"Clear");keyFace(L::action<Rect>(tft.width(),tft.height(),2),"Done");
  queryPaint();
}
void editQuery(int target){
  size_t n=strlen(query);bool changed=true;
  if(target==KeyDigits){digitKeys=!digitKeys;keyboardPaint();return;}
  if(target==KeyPage){digitKeys=false;lettersSecond=!lettersSecond;keyboardPaint();return;}
  if(target==KeyBack){if(n)query[n-1]=0;}
  else if(target==KeyClear)query[0]=0;
  else if(n<sizeof(query)-1){query[n]=target==KeySpace?' ':char(target-KeyBase);query[n+1]=0;}
  else {changed=false;ui_toast("Search text is full (31 characters)");}
  if(changed){storage_searchStart(query);queryPaint();}
}
void enter(){
  seekPreview=false;playerPressed=tilePressed=-1;ready=highlight=dimmed=-1;gesture=cydclassic::Gesture{};
  if(page().mode==Mode::SourceSongs&&(previewDirty||!preview)){
    if(!buildPreview(page().source)){message("Unable to reload list. Check SD card or try again.");return;}
  }
  if(page().mode==Mode::Search)storage_searchStart(query);
  count=rowCount();shownSerial=UINT32_MAX;tft.fillScreen(theme().bg);header();footer();
  shownVolume=shownConnection=-1;
  if(page().mode==Mode::Keyboard){keyboardPaint();searchShownCount=searchShownProgress=-1;}
  else if(page().mode==Mode::Player){playerTrack();playerTransport();playerBottom();lastPlayer=0;playerTick(millis());}
  else if(page().mode==Mode::Home)paintTiles();
  else if(page().mode==Mode::Volume){volumePaint();shownVolume=audio_volume();}
  else if(page().mode==Mode::Message){
    // Bounded, word-wrapped ASCII status text; music labels use cached UTF-8 strips.
    tft.setTextColor(theme().text,theme().bg);tft.setTextFont(2);tft.setTextWrap(true);tft.setCursor(14,80);
    tft.println(notice);tft.setTextWrap(false);
  }else if(page().mode==Mode::About){centred("CYDpod Classic Touch",75,theme().text,theme().bg);centred(String(storage_trackCount())+" songs",110,theme().dim,theme().bg);centred("M4A / AAC-LC 44.1 kHz",140,theme().dim,theme().bg);centred(String(ESP.getFreeHeap()/1024)+" KB free heap",170,theme().dim,theme().bg);
    if(storage_readOnly())centred("SD card is read-only",200,theme().warn,theme().bg);else if(storage_catalogStale())centred("Library stale",200,theme().warn,theme().bg);
    if(storage_skippedCount())centred(String(storage_skippedCount())+" files skipped",224,theme().dim,theme().bg);}
  else {invalidate();if(!count){centred(storage_readOnly()?"SD card is read-only":storage_catalogStale()?"Library unavailable":"No entries",100,theme().dim,theme().bg);}}
}
void leave(){
  seekPreview=false;armVolumeSave();flushSettings();dirty=0;titles_release();
  // ui_go calls leave after a navigation push/pop: current() is the destination.
  // Keep a newly built destination preview, otherwise rebuild lazily on Back.
  if(page().mode!=Mode::SourceSongs||externalReturn)freePreview();
  if(page().mode!=Mode::Search&&page().mode!=Mode::Keyboard)storage_searchCancel();
}
void tick(uint32_t now){
  screenNow=now;
  if(savePending&&!(touch_isDown()&&gesture.target==Slider)&&int32_t(now-saveAt)>=0)flushSettings();
  if(now-lastStatus>=1000){
    lastStatus=now;char error[48];int track=-1;
    uint32_t serial=audio_error(error,sizeof(error),&track);
    if(serial!=seenError){
      seenError=serial;char detail[128];
      if(track>=0)snprintf(detail,sizeof(detail),"Track %d unavailable (%s). Try another song.",track+1,error);
      else snprintf(detail,sizeof(detail),"Audio unavailable (%s). Restart to retry.",error);
      if(page().mode!=Mode::Message)message(detail);else{cydlib::copyText(notice,detail);enter();}
      return;
    }
    uint32_t state=g_settings.outRoute==OUT_BT?(bt_connected()?2:1):0;
    if(page().mode==Mode::Player&&g_settings.outRoute==OUT_BT){const String name=bt_speakerName();for(unsigned i=0;i<name.length();++i)state=state*33+uint8_t(name[i]);}
    if(state!=shownConnection){shownConnection=state;header();footer();if(page().mode==Mode::Player)playerOutput();}
  }
  if(page().mode==Mode::Search||page().mode==Mode::Keyboard){
    storage_searchTick();
    const int n=storage_searchCount(),progress=storage_searchBusy()?storage_searchProgress():101;
    if(n!=searchShownCount||progress!=searchShownProgress){
      searchShownCount=n;searchShownProgress=progress;
      if(page().mode==Mode::Keyboard)queryPaint();
      else {count=rowCount();invalidate();footer();}
    }
    if(page().mode==Mode::Keyboard)return;
  }
  if(page().mode==Mode::Player){playerTick(now);return;}
  if(browse()){
    if(playerPressed!=-1&&!miniPressUntil)miniPressUntil=now+L::MiniPressMs;
    if(playerPressed!=-1&&int32_t(now-miniPressUntil)>=0&&(!touch_isDown()||!L::mini<Rect>(tft.width(),tft.height(),playerPressed==Play?0:2).contains(gesture.x,gesture.y))){playerPressed=-1;miniPlayer();}
    if(shownSerial!=audio_trackSerial()||shownPlaying!=audio_isPlaying()||miniQueued!=(audio_queueLen()>0))miniPlayer();
    if(now-lastPlayer>=1000){lastPlayer=now;if(miniQueued)miniProgress();}
  }
  if(page().mode==Mode::Volume&&shownVolume!=audio_volume()){shownVolume=audio_volume();volumePaint();}
  // A single row per loop bounds SD and drawing work; audio/Bluetooth run between rows.
  if(dirty&&(!audio_bufferLow()||now-lastPaint>=120)){
    for(int i=0;i<rows();++i)if(dirty&(1u<<i)){dirty&=~(1u<<i);loadRow(i);lastPaint=now;break;}
  }
}
void sourceMenu(SourceKind kind,const char* label){show(Mode::Sources,label);page().sourceKind=kind;count=rowCount();invalidate();footer();}
void cycleShuffle(){audio_setShuffle(!audio_shuffle());g_settings.shuffle=audio_shuffle();settings_save();invalidate();}
void cycleRepeat(){audio_setRepeat((audio_repeat()+1)%REPEAT_MODE_COUNT);g_settings.repeat=audio_repeat();settings_save();invalidate();}
void toggleFavourite(){
        if(storage_trackCount()){
          if(playlist_favouritesFull()&&!playlist_isFavourite(audio_currentTrack())){message("Favourites list is full.");return;}
          playlist_toggleFavourite(audio_currentTrack());
          if(playlist_favouriteFailed()){message("Unable to save favourite. Check SD card or memory.");return;}
          previewDirty=true;
        }
  if(page().mode==Mode::Player)playerHeader();else invalidate();
}
// Home tiles go straight to the page: Music is no longer an intermediate list.
void openTile(int i){
  switch(i){
    case 0:catalog({CatalogKind::Songs,0},"Songs");break;
    case 1:catalog({CatalogKind::Artists,0},"Artists");break;
    case 2:catalog({CatalogKind::Albums,0},"Albums");break;
    case 3:show(Mode::Playlists);break;
    case 4:storage_searchStart(query);show(Mode::Search);show(Mode::Keyboard);break;
    default:show(Mode::Settings);break;
  }
}
void activate(int at){
  const int index=at-page().top;if(index<0||index>=rows())return;Row selected=cache[index];
  if(!selected.valid||selected.disabled){Serial.println("cmd: row ignored (unavailable)");ui_toast("Entry unavailable. Try again.");return;}
  Serial.printf("cmd: row %d accepted screen %u\n",at,unsigned(page().mode));
  switch(page().mode){
    case Mode::Playlists:if(at==0)openSource(1);else if(at==1)sourceMenu(SRC_FOLDER,"Folders");else openSource(selected.source);break;
    case Mode::Catalog:
      if(selected.navigates)catalog(selected.child,selected.label);
      else if(playlist_playView(page().view,at))show(Mode::Player);else message(playlist_memoryRefused()?"Not enough memory, try again":"Unable to read songs or allocate queue.");break;
    case Mode::Search:
      if(at==0)show(Mode::Keyboard);
      else if(at-1<storage_searchCount()){
        if(playlist_playTracks(storage_searchTracks(),storage_searchCount(),at-1,-1)){
          // A query is ephemeral: resume the selected song in the full Songs queue.
          g_settings.srcKind=SRC_ALL;settings_save();show(Mode::Player);
          Serial.println("cmd: search result played (queue committed)");
        }else message("Unable to start search result. Try again.");
      }break;
    case Mode::Sources:openSource(selected.source);break;
    case Mode::SourceSongs:
      if(playlist_playTracks(preview,previewCount,at,page().source)){settings_save();show(Mode::Player);}else message("Unable to start this list.");break;
    case Mode::Settings:
      if(at==0)show(Mode::Output);else if(at==1)external(SCR_BLUETOOTH);else if(at==2)show(Mode::Playback);else if(at==3)show(Mode::Display);
      else if(at==4)show(Mode::System);else show(Mode::About);break;
    case Mode::System:
      if(at==0)show(Mode::Refresh);
      else if(at==1){
        if(!firmware_uploaderPresent()){ui_toast(T_UPLOADER_MISSING);break;}
        // Keep the exact spot in the song: upload mode ends in a restart.
        g_settings.lastTrack=audio_currentTrack();g_settings.lastPosMs=audio_positionMs();settings_saveResume();
        firmware_bootUploader(); // restarts
      }else if(at==2)ui_confirm(T_PIN_RESET_Q,T_PIN_RESET_1,T_PIN_RESET_2,T_RESET,resetPin);
      else{g_settings.lang=(g_settings.lang+1)%LANG_COUNT;settings_save();enter();}
      break;
    case Mode::Playback:
      if(at==0)cycleShuffle();else if(at==1)cycleRepeat();
      else if(at==2){g_settings.screenTimeout=(g_settings.screenTimeout+1)%4;settings_save();invalidate();}
      else {sleep_setTimer((g_settings.sleepTimer+1)%5,screenNow);settings_save();invalidate();}break;
    case Mode::Display:
      if(at<2){g_settings.brightness=constrain(int(g_settings.brightness)+(at?10:-10),10,100);display_setBacklight(g_settings.brightness);settings_save();invalidate();}
      else if(at==2){g_settings.rotation=nextRotation(g_settings.rotation);display_setRotation(g_settings.rotation);settings_save();enter();}
      else{calibrate_run();enter();}break;
    case Mode::Output:
      if(at<OUT_SELECTABLE_COUNT){const uint8_t next=outRouteSelectable(at);
        if(next==g_settings.outRoute)break;
        if(!output_switch(next)){message("Unable to change output. Try again.");break;}
        invalidate();header();}break;
    case Mode::Options:
      if(at==0)toggleFavourite();
      else if(at==1)cycleShuffle();else if(at==2)cycleRepeat();else show(Mode::Output);break;
    case Mode::Refresh:
      if(at==1)back();else if(storage_refreshNextBoot()){g_settings.lastTrack=audio_currentTrack();g_settings.lastPath=storage_trackPath(audio_currentTrack());g_settings.lastPosMs=audio_positionMs();settings_save();ESP.restart();}
      else message("Cannot write refresh request. Check SD card.");break;
    default:break;
  }
}
int targetAt(int x,int y){
  if(x<0||y<0||x>=tft.width()||y>=tft.height())return -1;
  if(page().mode==Mode::Player){
    if(L::playerBack<Rect>().contains(x,y))return Back;
    if(L::favourite<Rect>(tft.width()).contains(x,y))return Favourite;
    if(L::options<Rect>(tft.width()).contains(x,y))return Options;
    if(L::seek<Rect>(tft.width(),tft.height()).contains(x,y))return Seek;
    for(int a:{Prev,Play,Next})if(transportRect(a).contains(x,y))return a;
    if(L::playerToggle<Rect>(tft.width(),tft.height(),false).contains(x,y))return Shuffle;
    if(L::playerToggle<Rect>(tft.width(),tft.height(),true).contains(x,y))return Repeat;
    if(L::playerVolume<Rect>(tft.width(),tft.height()).contains(x,y))return Slider;
    if(L::output<Rect>(tft.width(),tft.height()).contains(x,y))return Output;return -1;
  }
  if(y<L::HeaderH){if(browse()&&navigation.depth>=4&&L::deepHome<Rect>().contains(x,y))return Home;return navigation.depth>1&&x<L::BackW?Back:-1;}
  if(page().mode==Mode::Keyboard){
    if(y>=footerY())return x<L::ActionW?KeySpace:x<L::PlayingX?KeyClear:KeyDone;
    if(keyBackRect().contains(x,y))return KeyBack;
    if(keyDigitsRect().contains(x,y))return KeyDigits;
    if(tft.height()<L::PortraitMinH&&L::keyPage<Rect>().contains(x,y))return KeyPage;
    const char* keys=keyLetters();
    for(int i=0;keys[i];++i)if(keyRect(i).contains(x,y))return KeyBase+keys[i];
    return -1;
  }
  if(browse()&&y>=L::miniY(tft.height())&&y<L::miniY(tft.height())+L::FooterH){
    if(!audio_queueLen())return storage_trackCount()?Shuffle:-1;
    return L::mini<Rect>(tft.width(),tft.height(),0).contains(x,y)?Play:L::mini<Rect>(tft.width(),tft.height(),2).contains(x,y)?Next:Now;
  }
  if(page().mode==Mode::Home){for(int i=0;i<L::TileCount;++i)if(L::tile<Rect>(tft.width(),tft.height(),i).contains(x,y))return TileBase-i;return -1;}
  if(browse()&&L::strip<Rect>(tft.width(),tft.height()).contains(x,y))return count>rows()?(L::pageStrip<Rect>(tft.width(),tft.height(),false).contains(x,y)?Up:Down):-1;
  if(page().mode==Mode::Volume){
    if(sliderRect().contains(x,y))return Slider;
    if(L::step<Rect>(tft.width(),false).contains(x,y))return Minus;
    if(L::step<Rect>(tft.width(),true).contains(x,y))return Plus;return -1;
  }
  int row=(y-L::HeaderH)/L::RowH;return row>=0&&row<rows()&&page().top+row<count?page().top+row:-1;
}
// True when (x,y) is within `slop` px of the control the press landed on. Probing the
// hit test keeps this exact for every control shape and can never reach a neighbour:
// only the captured target counts.
bool within(int x,int y,int slop){
  const int step=slop>0?slop:1;
  for(int dy=-slop;dy<=slop;dy+=step)for(int dx=-slop;dx<=slop;dx+=step)if(targetAt(x+dx,y+dy)==gesture.target)return true;
  return false;
}
void seekPreviewAt(int x){
  const int width=tft.width()-2*L::ProgressInset;
  seekMs=seekDuration?uint32_t(uint64_t(L::sliderValue(x,L::ProgressInset,width,width))*(seekDuration-1)/width):0;
  shownProgress=shownSecond=-1;lastPlayer=0;
}
// Play/Pause, Previous and Next act on the admitted touch-down: a touch the UI has
// recognised must take effect at once. The caller then claims the press, so its
// release and long-hold events are swallowed and nothing fires twice.
void transportDown(int target){
  const char* name=target==Prev?"previous":target==Next?"next":"play/pause";
  if(!storage_trackCount()){Serial.printf("cmd: %s ignored on touch-down (no songs)\n",name);return;}
  Serial.printf("cmd: %s on touch-down\n",name);
  if(target==Prev)audio_prev();else if(target==Next)audio_next();else audio_togglePause();
  lastPlayer=0; // repaint the transport and track now, not on the next 250 ms tick
}
// Swipe-back is a shortcut for the Back button on browse pages, started in the body (not header, mini-player or strip).
bool swipeable(){return browse()&&navigation.depth>1&&gesture.target!=Slider&&gesture.y>=L::HeaderH&&gesture.y<L::miniY(tft.height())&&gesture.x<tft.width()-L::StripW;}
bool press(PressPhase phase,int x,int y){
  const int hit=targetAt(x,y);
  if(phase==PRESS_DOWN){
    ready=-1;dimmed=-1;gesture.down(x,y,page().top,hit);
    if(hit==Slider){volumeChanged=false;volumeAt(x);return true;}
    if(hit==Seek){
      seekCancelled=!audio_canSeek();seekPreview=!seekCancelled;
      seekDuration=audio_durationMs();seekSerial=audio_trackSerial();seekPreviewAt(x);return true;
    }
    if(browse()&&(hit==Play||hit==Next)){playerPressed=hit;miniPressUntil=0;transportDown(hit);gesture.claimed=true;miniPlayer();return true;}
    if(page().mode==Mode::Player){playerPressed=hit;if(hit==Prev||hit==Play||hit==Next){transportDown(hit);gesture.claimed=true;}
      if(hit==Prev||hit==Play||hit==Next||hit==Shuffle||hit==Repeat)playerTransport();else if(hit==Output)playerOutput();else if(hit==Favourite||hit==Options||hit==Back)playerHeader();
      if(gesture.claimed)return true;
    }
    if(tileOf(hit)>=0){tilePressed=tileOf(hit);paintTile(tilePressed,true);return false;}
    highlight=hit>=0&&page().mode!=Mode::Keyboard?hit-page().top:-1;if(highlight>=0&&highlight<rows()&&cache[highlight].valid)paintRow(highlight,true);
    return false;
  }
  if(phase==PRESS_MOVE){
    if(gesture.target==Slider){volumeAt(x);return true;}
    if(gesture.target==Seek){
      if(abs(y-gesture.y)>cydclassic::Gesture::SlopFar||seekSerial!=audio_trackSerial())seekCancelled=true;
      if(!seekCancelled)seekPreviewAt(x);return true;
    }
    if(gesture.target==Prev||gesture.target==Play||gesture.target==Next){if(browse()&&!within(x,y,cydclassic::Gesture::SlopNear)&&playerPressed!=-1){playerPressed=-1;miniPlayer();}return true;}
    const bool claimed=gesture.move(x,y,!within(x,y,cydclassic::Gesture::SlopFar),gesture.target>=0&&gesture.target<KeyBase&&count>rows());
    if(gesture.claimed&&gesture.target>=0&&abs(y-gesture.y)>=10){int top=gesture.scrollTop(y,count,rows());
      if(top!=page().top){page().top=top;highlight=-1;invalidate();footer();}}
    // CY-80 swipe-back: travelling right claims the press so the release never taps a row or tile.
    if(swipeable()&&gesture.swipeBack(x,y))gesture.claimed=true;
    // Show what a release would do: lit while within the slop, plain once outside, lit again on return.
    const bool inside=!gesture.claimed&&!gesture.strayed&&within(x,y,cydclassic::Gesture::SlopNear);
    if(tileOf(gesture.target)>=0){const int want=inside?tileOf(gesture.target):-1;if(want!=tilePressed){tilePressed=want;paintTile(tileOf(gesture.target),inside);}return gesture.claimed||gesture.strayed;}
    if(page().mode==Mode::Player){const int pressed=inside?gesture.target:-1;if(pressed!=playerPressed){playerPressed=pressed;playerTransport();playerHeader();playerOutput();}return claimed;}
    if(!inside&&highlight>=0){paintRow(highlight,false);dimmed=highlight;highlight=-1;}
    else if(inside&&highlight<0&&dimmed>=0){highlight=dimmed;dimmed=-1;paintRow(highlight,true);}
    return claimed||gesture.claimed;
  }
  if(phase==PRESS_UP){
    if(gesture.target==Slider){armVolumeSave();gesture.target=-1;return true;}
    if(gesture.target==Seek){
      seekPreview=false;shownProgress=shownSecond=-1;lastPlayer=0;
      if(!seekCancelled&&within(x,y,cydclassic::Gesture::SlopNear)&&seekSerial==audio_trackSerial()){
        if(!audio_seekTo(seekMs))ui_toast("Seeking unavailable for this track",1500);
      }else if(!audio_canSeek())ui_toast("Seeking unavailable for this track",1500);
      return true;
    }
    if(page().mode==Mode::Player){playerPressed=-1;if(gesture.target==Prev||gesture.target==Play||gesture.target==Next||gesture.target==Shuffle||gesture.target==Repeat)playerTransport();else if(gesture.target==Output)playerOutput();else if(gesture.target==Favourite||gesture.target==Options||gesture.target==Back)playerHeader();}
    if(browse()&&(gesture.target==Play||gesture.target==Next)&&int32_t(screenNow-miniPressUntil)>=0){playerPressed=-1;miniPlayer();}
    if(gesture.target==Prev||gesture.target==Play||gesture.target==Next)return true; // already acted on touch-down
    if(tilePressed>=0){const int lit=tilePressed;tilePressed=-1;if(page().mode==Mode::Home)paintTile(lit,false);}
    if(swipeable()&&gesture.swipeBack(x,y)){ready=-1;highlight=dimmed=-1;back();return true;}
    const bool ok=gesture.activate(within(x,y,cydclassic::Gesture::SlopNear));
    ready=ok?gesture.target:-1;
    if(!ok&&gesture.target!=-1){
      if(gesture.claimed)++cydtouch::stats().claim;else ++cydtouch::stats().slop;
    }
    if(highlight>=0){paintRow(highlight,false);highlight=-1;}
    dimmed=-1;
    return gesture.claimed||gesture.strayed;
  }return false;
}
void touch(TouchEvent ev,int,int){
  const int target=ready;ready=-1;
  // CT-05: a firm, slow press (600 ms to 4 s) that was not claimed by a drag or the 4 s calibration hold acts like a tap.
  if(ev!=TOUCH_TAP&&ev!=TOUCH_LONG_PRESS)return;
  if(page().mode==Mode::Keyboard&&target!=Back&&target!=-1){if(target==KeyDone)back();else editQuery(target);return;}
  if(target>=0){activate(target);return;}
  if(tileOf(target)>=0){openTile(tileOf(target));return;}
  switch(target){
    case Back:back();break;
    case Home:classic_home();ui_go(SCR_CLASSIC);break;
    case Up:case Down:page().top=cydclassic::clampTop(page().top+(target==Up?-1:1)*max(1,rows()-1),count,rows());invalidate();footer();break;
    case Now:if(storage_trackCount())openPlaying();break;
    case Volume:show(Mode::Volume);break;
    case Options:show(Mode::Options);break;
    case Favourite:toggleFavourite();break;
    case Shuffle:if(browse()&&!audio_queueLen()){
      const int n=storage_trackCount();if(n){audio_setShuffle(true);g_settings.shuffle=true;
        if(playlist_playView({CatalogKind::Songs,0},esp_random()%n))openPlaying();else message("Unable to start playback.");}
    }else {cycleShuffle();playerTransport();}break;
    case Repeat:cycleRepeat();playerTransport();break;
    case Output:show(Mode::Output);break;
    case Minus:case Plus:audio_setVolume(constrain(int(audio_volume())+(target==Plus?5:-5),0,100));g_settings.volume=audio_volume();savePending=true;saveAt=millis()+1500;break;
    default:break;
  }
}
} // namespace

void classic_home(){miniReturnDepth=0;flushSettings();freePreview();externalReturn=false;navigation.home();}
// Back from a page outside Classic (the Bluetooth page): the page it was opened from when Settings opened it, otherwise Now Playing.
void classic_return(){if(externalReturn)externalReturn=false;else if(page().mode!=Mode::Player){Page p{};p.mode=Mode::Player;if(!navigation.push(p)){navigation.home();navigation.push(p);}}ui_go(SCR_CLASSIC);}
const Screen SCREEN_CLASSIC={enter,leave,tick,touch,press};
#ifdef CYDPOD_CATALOG_HOST
// Exercise the original private geometry/hit test, without adding firmware state.
#include <sstream>
#include <cassert>
int classic_volumeSaveArms(){return volumeSaveArms;}
uint32_t classic_volumeSaveAt(){return saveAt;}
int classic_target(int x,int y){return targetAt(x,y);}
uint32_t classic_seekPreviewMs(){return seekMs;}
unsigned classic_depth(){return navigation.depth;}
unsigned classic_mode(){return unsigned(page().mode);}
const char* classic_modeName(unsigned m){
  static const char* const names[]={"Home","Playlists","Catalog","Sources","SourceSongs","Player","Settings","Playback","Display","Output","Volume","Options","About","Refresh","Message","Search","Keyboard","System"};
  static_assert(sizeof(names)/sizeof(names[0])==unsigned(Mode::System)+1,"mode name table matches Mode");
  return m<sizeof(names)/sizeof(names[0])?names[m]:"?";
}
int classic_top(){return page().top;}
std::string classic_geometry(){
  std::ostringstream out;
  auto rect=[&](Rect r){out<<r.x<<','<<r.y<<','<<r.w<<','<<r.h<<';';};
  for(unsigned rotation:{0u,1u,2u,3u}){
    tft.setRotation(rotation);out<<"rotation="<<rotation<<" size="<<tft.width()<<'x'<<tft.height()<<" rows="<<rows()<<" rects=";
    for(int i=0;i<rows();++i)rect(rowRect(i));
    for(int i=0;i<L::TileCount;++i)rect(L::tile<Rect>(tft.width(),tft.height(),i));
    for(int cell:{0,1,2})rect(L::mini<Rect>(tft.width(),tft.height(),cell));rect(L::miniProgress<Rect>(tft.width(),tft.height()));rect(L::deepHome<Rect>());for(bool down:{false,true})rect(L::pageStrip<Rect>(tft.width(),tft.height(),down));
    for(int a:{Prev,Play,Next})rect(transportRect(a));rect(sliderRect());
    for(Rect r:{L::playerBack<Rect>(),L::favourite<Rect>(tft.width()),L::options<Rect>(tft.width()),L::progress<Rect>(tft.width(),tft.height()),L::playerVolume<Rect>(tft.width(),tft.height()),L::playerVolumeTrack<Rect>(tft.width(),tft.height()),L::output<Rect>(tft.width(),tft.height()),L::outputFace<Rect>(tft.width(),tft.height()),L::playerToggle<Rect>(tft.width(),tft.height(),false),L::playerToggle<Rect>(tft.width(),tft.height(),true)})rect(r);
    rect(L::seek<Rect>(tft.width(),tft.height()));rect(L::strip<Rect>(tft.width(),tft.height()));rect(L::step<Rect>(tft.width(),false));rect(L::step<Rect>(tft.width(),true));rect({0,L::HeaderH,tft.width(),L::QueryH});out<<'\n';
    for(int m=0;m<=int(Mode::Keyboard);++m){
      out<<"mode="<<m;
      for(int variant=0;variant<4;++variant){
        navigation.home();navigation.depth=variant==0?1:variant==3?4:2;page().mode=Mode(m);
        audio_setQueue(nullptr,variant>=2?1:0,-1,false);
        count=variant==0?0:variant==1?1:7;page().top=variant==3?2:0;
        digitKeys=variant==2;lettersSecond=variant==3;
        assert(rows()==(tft.height()==240?4:6));
        const Rect strip=L::strip<Rect>(tft.width(),tft.height());
        assert(strip.y==L::HeaderH&&strip.h==rows()*L::RowH&&strip.y+strip.h==footerY());
        for(int i=0;i<rows();++i){const Rect r=rowRect(i);
          assert(r.x>=0&&r.y==L::HeaderH+i*40&&r.h==40&&r.y+r.h<=footerY());
          assert(r.x+r.w<=strip.x);
          if(i)assert(rowRect(i-1).y+rowRect(i-1).h==r.y);
        }
        int end=0;for(int cell:{0,1,2}){const Rect r=L::mini<Rect>(tft.width(),tft.height(),cell);
          assert(r.x==end&&r.y==footerY()&&r.y+r.h==tft.height());end=r.x+r.w;
        }assert(end==tft.width());
        out<<" v="<<variant;
        if(page().mode==Mode::Keyboard){out<<" keys=";for(int i=0;keyLetters()[i];++i)rect(keyRect(i));rect(keyBackRect());rect(keyDigitsRect());}
        // Lossless run-length table: each y band contains x-end:target runs.
        std::string previous;int start=0;
        for(int y=0;y<=tft.height();++y){
          std::ostringstream line;
          if(y<tft.height()){int target=targetAt(0,y);for(int x=1;x<=tft.width();++x){const int next=x==tft.width()?target-1:targetAt(x,y);if(next!=target){line<<x<<':'<<target<<',';target=next;}}}
          const auto current=line.str();if(y&&current!=previous){out<<" ["<<start<<'-'<<y<<' '<<previous<<']';start=y;}previous=current;
        }
      }out<<'\n';
    }
  }
  navigation.home();audio_setQueue(nullptr,0,-1,false);digitKeys=lettersSecond=false;count=0;tft.setRotation(0);return out.str();
}
#endif

#endif

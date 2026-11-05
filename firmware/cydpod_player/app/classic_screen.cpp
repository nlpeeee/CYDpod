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
#include "classic_text.h"
#include <esp_system.h>

namespace {
namespace L=cydclassic::layout;
enum class Mode:uint8_t { Home,Playlists,Catalog,Sources,SourceSongs,Player,Settings,Playback,Display,Output,Options,About,Refresh,Message,Search,Keyboard,System };
struct Page {
  Mode mode=Mode::Home;CatalogView view{};int16_t source=-1;int32_t offsetPx=0;
  SourceKind sourceKind=SRC_M3U;char title[64]={};
};
// Brightness -/+, Orientation and Theme, then Calibrate touch on the resistive board only.
constexpr int DisplayRows=CYD_TOUCH_CAPACITIVE?4:5;
// Landscape -> landscape flipped -> portrait -> portrait flipped, so every rotation can be reached
// and left again (rotation 1 is the W328C's upright landscape, board.h).
const char* const RotationName[4]={"Portrait","Landscape","Portrait flipped","Landscape flipped"};
uint8_t nextRotation(uint8_t rotation){static const uint8_t next[4]={2,3,1,0};return next[rotation&3];}
void resetPin(){g_settings.webPin="000000";settings_save();}
struct Row {
  char label[128]={},detail[96]={};CatalogView child{};
  int32_t absolute=-1;int16_t track=-1,source=-1;bool valid=false,navigates=false,disabled=false;
};
cydclassic::Navigation<Page> navigation;
cydclassic::Gesture gesture;
constexpr int CacheSlots=L::MaxRows+1;
Row cache[CacheSlots];
String rowUtf8; // Reserved once; the legacy UTF-8 strip loader accepts String.
static_assert(sizeof(cache)<=6*sizeof(Row)+512,"Viewport cache RAM budget");
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
const cydlib::Record* hostTrackInfo=nullptr; // CY-85 previews: exact labels through the real screens
#endif
uint32_t seekMs=0,seekDuration=0,seekSerial=0;
char notice[128]={};
char query[32]={};bool digitKeys=false;uint32_t screenNow=0;
int searchShownCount=-1,searchShownProgress=-1;
enum class SearchState:uint8_t { Empty,Active,Cancelled,Error };
struct SearchSession {
  char applied[32]={};uint32_t due=0;
  SearchState state=SearchState::Empty;bool pending=false;
} searchSession;
static_assert(sizeof(SearchSession)<=48,"Search session RAM budget");
Page& page();
// Backend hits belong only to the applied text. Never expose obsolete hits while editing.
bool searchCurrent(){return !searchSession.pending&&searchSession.state==SearchState::Active&&!strcmp(query,searchSession.applied);}
int searchCount(){return searchCurrent()?storage_searchCount():0;}
bool searchWaiting(){return searchSession.pending||(searchCurrent()&&storage_searchBusy());}
bool searchError(){return !searchSession.pending&&searchSession.state==SearchState::Error;}
// Starts/opens stay in tick; Done makes the next loop apply without waiting for idle.
void scheduleSearch(uint32_t wait){
  // Input precedes tick in loop(). Timestamp edits at input, rather than the previous
  // loop's start (which may be old after SD I/O); the host supplies its fake tick clock.
#ifdef CYDPOD_CATALOG_HOST
  const uint32_t editedAt=screenNow;
#else
  const uint32_t editedAt=millis();
#endif
  searchSession.pending=true;searchSession.due=(wait?editedAt:screenNow)+wait;
}
void applySearch(uint32_t now){
  if(!searchSession.pending||int32_t(now-searchSession.due)<0)return;
  searchSession.pending=false;
  if(strcmp(query,searchSession.applied)||searchSession.state==SearchState::Cancelled||searchSession.state==SearchState::Error){
    cydlib::copyText(searchSession.applied,query);
    if(query[0]){storage_searchStart(query);searchSession.state=SearchState::Active;}
    else {storage_searchCancel();searchSession.state=SearchState::Empty;}
    page().offsetPx=0;
  }
  searchShownCount=searchShownProgress=-1;
}
constexpr int KeyBase=1000,KeyBack=-40,KeyDigits=-41,KeySpace=-42,KeyClear=-43,KeyDone=-44;
constexpr int TileBase=-50; // Home tile i is target TileBase-i
constexpr int Back=-10,Home=-11,Up=-12,Down=-13,Now=-14,Prev=-20,Play=-21,Next=-22,Options=-24,Slider=-25,Seek=-28,Favourite=-29,Shuffle=-30,Repeat=-31,Output=-32;
Page miniReturn{};uint8_t miniReturnDepth=0;uint32_t miniPressUntil=0;bool miniQueued=false;
Page& page(){return navigation.current();}
bool browse(){return page().mode!=Mode::Player&&page().mode!=Mode::Keyboard;}
int rows(){return L::rows(tft.height());}
int footerY(){return L::footerY(tft.height());}
int topRow(){return page().offsetPx/L::RowH;}
int visibleRows(){return rows()+(page().offsetPx%L::RowH?1:0);}
Rect rowRect(int i){Rect r=L::row<Rect>(tft.width(),i);r.y-=page().offsetPx%L::RowH;return r;}
Row& cachedRow(int absolute){return cache[absolute%CacheSlots];}
void setTop(int top){page().offsetPx=cydclassic::clampTop(top,count,rows())*L::RowH;}
Rect transportRect(int action){return L::transport<Rect>(tft.width(),tft.height(),action==Prev?0:action==Play?1:2);}
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
    case Mode::Output:return "Audio Output";case Mode::Options:return "Options";
    case Mode::About:return "About";case Mode::Refresh:return "Refresh Library";case Mode::Message:return "CYDpod";case Mode::System:return "Library & System";
    default:return "CYDpod";
  }
}
const char* title(){return pageTitle(page());}
// Player and mini-player labels: ASCII is drawn 1:1 from the sized faces in classic_text.h
// (whole-glyph "..." when too wide). Non-ASCII keeps the existing UTF-8 strip path, with its
// native strip height. `y` is the vertical centre of capitals for the sized faces.
using cydtext::Face;
void playerText(const char* value,int x,int y,Face face,int width,uint16_t fg,uint16_t bg,int align=cydtext::HAlignCentre){
  if(!cydtext::ascii(value)){ui_text(String(value),x,y,uint8_t(3+align),fg,bg,width);return;}
  titles_release(); // Match ui_text: an ASCII draw must not retain a prior UTF-8 strip.
  char label[128],fitted[128];cydlib::copyText(label,value);
  for(char* c=label;*c;++c)if((unsigned char)*c<32)*c=' ';
  // A title too wide for the large bold face steps down to the compact bold face before any ellipsis.
  if(face==Face::Title&&cydtext::width(face,label)>width)face=Face::TitleCompact;
  cydtext::fit(face,label,fitted,sizeof(fitted),width);
  cydtext::draw(face,fitted,x,y,align,fg);
}
// "24 / 85", degrading to "24/85" and then "24" rather than ever cutting a number in half.
void queuePosition(int pos,int length,int x,int y,int width,uint16_t fg,uint16_t bg){
  char label[24];
  snprintf(label,sizeof(label),"%d / %d",pos,length);
  if(cydtext::width(Face::Label,label)>width)snprintf(label,sizeof(label),"%d/%d",pos,length);
  if(cydtext::width(Face::Label,label)>width)snprintf(label,sizeof(label),"%d",pos);
  playerText(label,x,y,Face::Label,width,fg,bg);
}
void playerHeader(){
  const Palette& p=theme();tft.fillRect(0,0,tft.width(),L::PlayerHeaderH,p.band);tft.drawFastHLine(0,L::PlayerHeaderH,tft.width(),p.outline);
  if(playerPressed==Back)tft.fillRect(0,0,L::PlayerBackW,L::PlayerHeaderH,p.pressed);
  ui_icon(ICON_BACK,L::PlayerBackX,L::PlayerHeaderY,L::PlayerHeaderIcon,p.text,p.band);
  const Page& parent=miniReturnDepth==navigation.depth?miniReturn:navigation.entries[navigation.depth>1?navigation.depth-2:0];
  const char* name=parent.mode==Mode::Home?"Home":pageTitle(parent);
  ui_text(String(name),L::PlayerParentX,L::PlayerHeaderY,ML_DATUM,p.text,p.band,L::PlayerBackW-L::PlayerParentX,TYPE_CAPTION);
  const int pos=audio_queuePos(audio_currentTrack());
  const bool portrait=tft.height()>=L::PortraitMinH;
  queuePosition(pos<0?0:pos+1,audio_queueLen(),portrait?L::PlayerPortraitCountX:L::PlayerCountX,L::PlayerHeaderY,portrait?L::PlayerPortraitCountW:L::PlayerCountW,p.dim,p.band);
  Rect f=L::favourite<Rect>(tft.width());f.h=L::PlayerHeaderH;ui_iconToggle(f,ICON_STAR,storage_trackCount()&&playlist_isFavourite(audio_currentTrack()),p.band,playerPressed==Favourite);
  Rect o=L::options<Rect>(tft.width());if(playerPressed==Options)tft.fillRect(o.x,o.y,o.w,L::PlayerHeaderH,p.pressed);
  for(int i=-1;i<=1;++i)tft.fillCircle(o.cx()+i*L::PlayerDotsGap,L::PlayerHeaderY,L::PlayerDotR,playerPressed==Options?p.onPressed:p.text);
}
void header(){
  if(page().mode==Mode::Player){playerHeader();return;}
  const Palette& p=theme();
  tft.fillRect(0,0,tft.width(),L::HeaderH,p.band);
  tft.drawFastHLine(0,L::HeaderH-1,tft.width(),p.outline);
  int parentEnd=0;
  if(navigation.depth>1){
    const Page& parent=navigation.entries[navigation.depth-2];
    ui_icon(ICON_BACK,L::HeaderBackX,L::HeaderTextY,L::HeaderIconSize,p.text,p.band);
    parentEnd=L::HeaderParentX+ui_text(String(parent.mode==Mode::Home?"Home":pageTitle(parent)),L::HeaderParentX,L::HeaderTextY,ML_DATUM,p.text,p.band,L::BackW-L::HeaderParentX,TYPE_CAPTION);
  }
  if(navigation.depth>=4&&browse()){const Rect h=L::deepHome<Rect>();tft.drawLine(h.cx()-L::HomeGlyphHalf,h.cy(),h.cx(),h.cy()-L::HomeRoofH,p.text);tft.drawLine(h.cx(),h.cy()-L::HomeRoofH,h.cx()+L::HomeGlyphHalf,h.cy(),p.text);tft.drawRect(h.cx()-L::HomeGlyphHalf,h.cy(),2*L::HomeGlyphHalf,L::HomeWallH,p.text);}
  int left=navigation.depth>=4&&browse()?L::BackW+L::DeepHomeW:navigation.depth>1?L::BackW:0;const int right=tft.width()-L::HeaderTitleRight;
  // A short parent label leaves room: let a long ASCII title borrow it rather than lose glyphs (portrait).
  if(navigation.depth>1&&left==L::BackW&&tft.textWidth(String(title()),2)>right-left)left=max(parentEnd+L::HeaderTitleGap,L::HeaderParentX);
  ui_text(String(title()),(left+right)/2,L::HeaderTextY,MC_DATUM,p.text,p.band,right-left);
  const bool wired=g_settings.outRoute==OUT_WIRED;
  const bool connected=wired?wired_active():bt_connected();
  ui_icon(wired?ICON_HEADPHONES:ICON_BLUETOOTH,tft.width()-L::RouteInset,L::HeaderTextY,L::RouteSize,connected?p.text:p.dim,p.band);
}
void miniProgress(){
  const Rect r=L::miniProgress<Rect>(tft.width(),tft.height());
  const uint32_t duration=audio_durationMs();
  const int px=audio_queueLen()&&duration?min<int>(r.w,uint64_t(audio_positionMs())*r.w/duration):0;
  tft.fillRect(r.x,r.y,r.w,r.h,theme().miniTrack);
  if(px)tft.fillRect(r.x,r.y,px,r.h,theme().miniFill);
}
void miniPlayer(){
  const Palette& p=theme();const int w=tft.width(),h=tft.height();
  tft.fillRect(0,L::miniY(h),w,L::FooterH,p.band);
  miniQueued=audio_queueLen()>0;shownSerial=audio_trackSerial();shownPlaying=audio_isPlaying();
  if(!miniQueued){text("Shuffle all",{0,L::miniY(h),w,L::FooterH},storage_trackCount()?p.text:p.disabled,p.band);return;}
  for(int cell:{0,2}){const Rect r=L::mini<Rect>(w,h,cell);const bool lit=playerPressed==(cell?Next:Play);const uint16_t bg=lit?p.pressed:p.band;
    tft.fillRect(r.x,r.y+L::MiniProgressH,r.w,r.h-L::MiniProgressH,bg);
    ui_icon(cell?ICON_NEXT:shownPlaying?ICON_PAUSE:ICON_PLAY,r.cx(),r.cy(),L::MiniIconSize,lit?p.onPressed:p.text,bg);}
  const Rect r=L::mini<Rect>(w,h,1);
  // Reuse the existing Player record: one bounded metadata read only on state/track changes.
  if(!storage_trackInfo(audio_currentTrack(),playingInfo)){cydlib::copyText(playingInfo.title,"Track unavailable");playingInfo.artist[0]=0;}
#ifdef CYDPOD_CATALOG_HOST
  if(hostTrackInfo)playingInfo=*hostTrackInfo;
#endif
  playerText(playingInfo.title,r.x+L::MiniTextInset,r.y+L::MiniTitleY,Face::Label,r.w-2*L::MiniTextInset,p.text,p.band,cydtext::HAlignLeft);
  playerText(playingInfo.artist,r.x+L::MiniTextInset,r.y+L::MiniArtistY,Face::Label,r.w-2*L::MiniTextInset,p.dim,p.band,cydtext::HAlignLeft);
  miniProgress();
}
void keyboardActions();
void footer(){
  if(page().mode==Mode::Player)return;
  if(browse()){if(shownSerial!=audio_trackSerial()||shownPlaying!=audio_isPlaying()||miniQueued!=(audio_queueLen()>0))miniPlayer();return;}
  const Palette& p=theme();tft.fillRect(0,footerY(),tft.width(),L::FooterH,p.band);
  tft.drawFastHLine(0,footerY(),tft.width(),p.outline);
  if(page().mode==Mode::Keyboard){
    keyboardActions();return;
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
  const uint16_t ink=pressed?p.onPressed:p.text;
  tileGlyph(i,r.cx(),r.y+L::TileIconY,ink,face);
  ui_text(String(names[i]),r.cx(),r.y+r.h-L::TileLabelBottom,MC_DATUM,ink,face,r.w-2*L::TileTextInset);
}
void paintTiles(){for(int i=0;i<L::TileCount;++i)paintTile(i,false);}
// Fixed text scratch keeps ASCII painting allocation-free; UTF-8 retains the legacy strip loader.
void rowText(const char* value,int y,int width,uint16_t fg,uint16_t bg){
  if(!cydtext::ascii(value)){
    rowUtf8=value;
    if(titles_load(rowUtf8)){titles_draw({L::TextMargin,y-TITLE_STRIP_H/2,min(width,titles_width()),TITLE_STRIP_H},0,0,fg,bg);return;}
  }
  titles_release();tft.setTextColor(fg,bg);tft.setTextDatum(ML_DATUM);
  char fitted[132];cydlib::copyText(fitted,value);
  if(tft.textWidth(fitted,2)>width){
    size_t n=strlen(fitted);
    do{if(n)--n;while(n&&(uint8_t(fitted[n])&0xC0)==0x80)--n;fitted[n]=0;
      while(n&&fitted[n-1]==' ')fitted[--n]=0;
      memcpy(fitted+n,"...",4);
    }while(n&&tft.textWidth(fitted,2)>width);
    if(tft.textWidth(fitted,2)>width)fitted[0]=0;
  }
  tft.drawString(fitted,L::TextMargin,y,2);
}
void paintRow(int i,bool pressed){
  if(i<0||i>=visibleRows())return;
  const int absolute=topRow()+i;Row& r=cachedRow(absolute);const Palette& p=theme();const Rect box=rowRect(i);
  const bool loaded=r.absolute==absolute&&r.valid;
  const uint16_t bg=pressed?p.accent:p.bg,fg=pressed?p.onAccent:loaded&&r.disabled?p.disabled:p.text;
  // Absolute coordinates preserve row origins while TFT clips both partial edges.
  tft.setViewport(0,L::HeaderH,box.w,rows()*L::RowH,false);
  tft.fillRect(box.x,box.y,box.w,box.h,bg);
  if(!loaded){
    if(absolute<count)tft.fillRoundRect(L::TextMargin,box.y+L::RowTitleY-3,box.w/2,6,2,p.outlineSubtle);
  }else if(page().mode==Mode::Output&&r.disabled){
    tft.setTextDatum(ML_DATUM);tft.setTextColor(fg,bg);tft.drawString(r.label,L::TextMargin,box.cy(),1);
  }else if(r.detail[0]){
    rowText(r.label,box.y+L::RowTitleY,box.w-L::RowTextInset,fg,bg);
    rowText(r.detail,box.y+L::RowDetailY,box.w-L::RowTextInset,pressed?p.onAccent:p.dim,bg);
  }else rowText(r.label,box.cy(),box.w-2*L::TextMargin,fg,bg);
  if(loaded){
    if(r.navigates)ui_icon(ICON_CHEVRON,box.w-L::RowIconInset,box.cy(),L::RowIconSize,fg,bg);
    else if(r.track==audio_currentTrack()&&audio_isPlaying())ui_icon(ICON_PLAY,box.w-L::RowIconInset,box.cy(),L::RowIconSize,fg,bg);
  }
  tft.drawFastHLine(0,box.y+L::RowH-1,box.w,p.outlineSubtle);tft.resetViewport();
}
// Cache-only seam for part 2: no row loading or catalogue reads here.
void paintViewport(int32_t offsetPx){
  page().offsetPx=cydclassic::clampOffset(offsetPx,count,rows());
  dirty=0;
  for(int i=0;i<visibleRows();++i){
    const int absolute=topRow()+i;const Row& r=cachedRow(absolute);
    if(r.absolute!=absolute||!r.valid)dirty|=uint8_t(1u<<i);
    paintRow(i,highlight==i);
  }
}
void invalidate(){
  page().offsetPx=cydclassic::clampOffset(page().offsetPx,count,rows());dirty=uint8_t((1u<<visibleRows())-1);
  for(Row& r:cache){r.valid=false;r.absolute=-1;}
  const Palette& p=theme();const int x=tft.width()-L::StripW,h=rows()*L::RowH;
  tft.fillRect(x,L::HeaderH,L::StripW,h,p.band);
  if(count>rows())for(bool down:{false,true}){const Rect r=L::pageStrip<Rect>(tft.width(),tft.height(),down);ui_icon(down?ICON_DOWN:ICON_UP,r.cx(),r.cy(),L::FooterIconSize,p.text,p.band);}
  if(count>rows()){const int thumb=max(L::ThumbMin,h*rows()/count);const int y=L::HeaderH+int(int64_t(h-thumb)*page().offsetPx/max(1,(count-rows())*L::RowH));tft.fillRect(x+L::ThumbInset,y,L::ThumbW,thumb,p.dim);}
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
    case Mode::Search:return 1+max(1,searchCount());
    case Mode::Catalog:return storage_browseCount(page().view);
    case Mode::Sources:return playlist_sourceCount(page().sourceKind);
    case Mode::SourceSongs:return previewCount;
    case Mode::Settings:return 6;case Mode::System:return 4;case Mode::Playback:return 4;case Mode::Display:return DisplayRows;case Mode::Output:return OUT_SELECTABLE_COUNT + (CYD_WIRED_OUTPUT ? 2 : 0);
    case Mode::Options:return 4;case Mode::Refresh:return 2;
    default:return 0;
  }
}
void loadRow(int i){
  const int at=topRow()+i;Row& r=cachedRow(at);r=Row{};r.absolute=at;
  if(at>=count){r.valid=true;paintRow(i,false);return;}
  switch(page().mode){
    case Mode::Playlists:
      if(at<2)setRow(r,at?"Folders":"Favourites",String(),true);
      else{const int source=playlist_sourceAt(SRC_M3U,at-2);setRow(r,playlist_name(source).c_str(),String(),true,source<0);r.source=source;}
      break;
    case Mode::Catalog:{CatalogEntry e;if(storage_browseEntry(page().view,at,e)){setRow(r,e.label.c_str(),e.detail,e.navigates);r.track=e.track;r.child=e.child;}
      else setRow(r,"Unable to read entry","Check SD card",false,true);break;}
    case Mode::Search:
      if(at==0)setRow(r,(String("Edit: ")+query).c_str(),searchError()?String("SD error. Edit to retry"):searchSession.pending?String("Searching..."):searchWaiting()?String(storage_searchBuilding()?"Preparing index ":"Searching ")+String(storage_searchProgress())+"%":String(searchCount())+" results (max 50)",true);
      else if(at-1<searchCount()){
        String song,artist;r.track=storage_searchTracks()[at-1];
        if(storage_trackSummary(r.track,song,artist))setRow(r,song.c_str(),artist);else setRow(r,"Track unavailable",String(),false,true);
      }else setRow(r,searchWaiting()?"Searching...":"No matches",String(),false,true);
      break;
    case Mode::Sources:{int source=sourceId(at);setRow(r,playlist_name(source).c_str(),String(),true,source<0);r.source=source;break;}
    case Mode::SourceSongs:{String title,artist;if(preview&&at<previewCount&&storage_trackSummary(preview[at],title,artist)){setRow(r,title.c_str(),artist);r.track=preview[at];}else setRow(r,"Track unavailable",String(),false,true);break;}
    case Mode::Settings:{const char* names[]={"Audio Output","Bluetooth","Playback","Display","Library & System","About"};setRow(r,names[at],String(),true);break;}
    case Mode::System:{const char* names[]={"Refresh Library","Upload music","Web passcode","Language"};
      setRow(r,names[at],at==2?g_settings.webPin:at==3?String(tr(T_LANG_NAME)):String(),at<2);break;}
    case Mode::Playback:
      if(at<2)setRow(r,at==0?"Shuffle":"Repeat",at==0?String(audio_shuffle()?"On":"Off"):String(audio_repeat()==REPEAT_ONE?"One":audio_repeat()==REPEAT_ALL?"All":"Off"));
      else setRow(r,at==2?"Screen timeout":"Sleep timer",at==2?String(sleep_timeoutLabel(g_settings.screenTimeout)):String(sleep_timerLabel(g_settings.sleepTimer)));break;
    case Mode::Display:{const char* names[]={"Brightness -","Brightness +","Orientation","Theme","Calibrate touch"};
      setRow(r,names[at],at<2?String(g_settings.brightness)+"%":at==2?String(RotationName[g_settings.rotation&3]):at==3?String(themeName(g_settings.colorTheme)):String(),at==4);break;}
    case Mode::Output:{
#if CYD_WIRED_OUTPUT
      if(at==OUT_SELECTABLE_COUNT){setRow(r,g_settings.headphoneLimit?"Headphone limit: On":"Headphone limit: Off",String("Digital output limit (wired only)"));break;}
#endif
      if(at==OUT_SELECTABLE_COUNT+1){setRow(r,"Switch back to Bluetooth if there is no sound",String(),false,true);break;}const uint8_t route=outRouteSelectable(at);setRow(r,route==OUT_WIRED?"Headphones":"Bluetooth",g_settings.outRoute==route?String("Selected"):String());break;}
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
  const Rect s=L::playerVolumeTrack<Rect>(tft.width(),tft.height());
  const int value=L::sliderValue(x,s.x,s.w,100);
  if(value!=audio_volume()){audio_setVolume(value);g_settings.volume=audio_volume();volumeChanged=true;}
}
// Preserve the old expand-by-3 nominal GLYPH field, while composing the new
// face inside the ORIGINAL cell. Passing an expanded rectangle would resize
// the C pill and clip its 2 px inset; passing no override would shrink icons.
void playerButton(const Rect& r,Icon icon,bool active,bool pressed){
  ui_button(r,icon,active,pressed,min(r.w+2*STYLE_INSET,r.h+2*STYLE_INSET)*3/5);
}
void playerTransport(bool force=false){
  // Only changed cells repaint. Full page entry forces all five, including
  // after rotation/theme/page clears. This is state, not pixel scratch storage.
  static uint8_t shown[5]={255,255,255,255,255};
  auto cell=[&](unsigned i,const Rect& r,Icon icon,bool active,int target){
    const bool pressed=playerPressed==target;
    const uint8_t state=uint8_t(icon)|(active?32:0)|(pressed?64:0);
    if(force||shown[i]!=state){playerButton(r,icon,active,pressed);shown[i]=state;}
  };
  cell(0,transportRect(Prev),ICON_PREV,false,Prev);
  cell(1,transportRect(Play),audio_isPlaying()?ICON_PAUSE:ICON_PLAY,audio_isPlaying(),Play);
  cell(2,transportRect(Next),ICON_NEXT,false,Next);
  cell(3,L::playerToggle<Rect>(tft.width(),tft.height(),false),ICON_SHUFFLE,audio_shuffle(),Shuffle);
  cell(4,L::playerToggle<Rect>(tft.width(),tft.height(),true),audio_repeat()==REPEAT_ONE?ICON_REPEAT_ONE:ICON_REPEAT,audio_repeat()!=REPEAT_OFF,Repeat);
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
  tft.fillRect(r.x+r.w+L::PlayerNumberGap,r.cy()-L::PlayerNumberClearH/2,L::PlayerNumberW,L::PlayerNumberClearH,theme().bg);
  playerText(String(level).c_str(),r.x+r.w+L::PlayerNumberGap,r.cy(),Face::Label,L::PlayerNumberW,theme().dim,theme().bg,cydtext::HAlignLeft);
  shownVolume=level;
}
void playerOutput(){
  const Palette& p=theme();const Rect r=L::outputFace<Rect>(tft.width(),tft.height());
  const bool lit=playerPressed==Output;const uint16_t bg=lit?p.pressed:p.band;
  tft.fillRoundRect(r.x,r.y,r.w,r.h,L::PlayerChipRadius,bg);tft.drawRoundRect(r.x,r.y,r.w,r.h,L::PlayerChipRadius,p.outline);
  const bool wired=g_settings.outRoute==OUT_WIRED;
  // No icon: the chip's width goes to the text, which names the route (peer name or Headphones).
  const String name=wired?String("Headphones"):bt_speakerName();
  playerText(name.isEmpty()?"Bluetooth":name.c_str(),r.x+L::PlayerChipTextInset,r.cy(),Face::Label,r.w-L::PlayerChipTextInset-L::PlayerChipTextPad,lit?p.onPressed:p.text,bg,cydtext::HAlignLeft);
}
void playerBottom(){
  const Palette& p=theme();const Rect r=L::playerVolume<Rect>(tft.width(),tft.height());
  tft.drawFastHLine(0,r.y,tft.width(),p.outline);ui_icon(ICON_SPEAKER,L::playerSpeakerX(tft.width()),r.cy(),L::PlayerSpeakerSize,p.text,p.bg);playerVolume();playerOutput();
}
void playerTrack(){
  const Palette& p=theme();memset(&playingInfo,0,sizeof(playingInfo));
  if(!storage_trackInfo(audio_currentTrack(),playingInfo)){cydlib::copyText(playingInfo.title,"No songs");cydlib::copyText(playingInfo.artist,"Add M4A files to /music");}
#ifdef CYDPOD_CATALOG_HOST
  if(hostTrackInfo)playingInfo=*hostTrackInfo;
#endif
  const int dy=L::playerTextOffset(tft.height()),width=tft.width()-2*L::PlayerTextInset;
  tft.fillRect(0,L::PlayerTextTop,tft.width(),L::PlayerTextH+dy,p.bg);
  playerText(playingInfo.title,tft.width()/2,L::PlayerTitleY+dy,Face::Title,width,p.text,p.bg);
  playerText(playingInfo.artist,tft.width()/2,L::PlayerArtistY+dy,Face::Label,width,p.dim,p.bg);
  playerText(playingInfo.album,tft.width()/2,L::PlayerAlbumY+dy,Face::Label,width,p.dim,p.bg);
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
    playerText(elapsed.c_str(),L::ProgressInset,tft.height()-L::PlayerTimeBottom,Face::Label,tft.width()/2-L::ProgressInset,p.dim,p.bg,cydtext::HAlignLeft);
    const String remain=duration>0?String("-")+ui_time(duration>pos?duration-pos:0):String("--:--"); // unknown length: not "---:--"
    playerText(remain.c_str(),tft.width()-L::ProgressInset,tft.height()-L::PlayerTimeBottom,Face::Label,tft.width()/2-L::ProgressInset,p.dim,p.bg,cydtext::HAlignRight);
  }
}
// Bounded read-only tables; no per-key state or allocation. Same QWERTY in all rotations.
const char* keyLetters(){
  static const char letters[]="qwertyuiopasdfghjklzxcvbnm";
  static const char symbols[]="0123456789-/:;()&@.,?!'\"_+=";
  return digitKeys?symbols:letters;
}
Rect keyRect(int i){return L::key<Rect>(tft.width(),tft.height(),i,digitKeys);}
Rect keyBackRect(){return L::keyBack<Rect>(tft.width(),tft.height());}
Rect keyDigitsRect(){return L::keyDigits<Rect>(tft.width(),tft.height());}
void keyFace(const Rect& r,const char* label){
  tft.fillRect(r.x+L::KeyGap/2,r.y+L::KeyGap/2,r.w-L::KeyGap,r.h-L::KeyGap,theme().band);
  ui_text(String(label),r.cx(),r.cy(),MC_DATUM,theme().text,theme().band,r.w-L::KeyTextInset);
}
void keyboardActions(){
  const char* labels[]={digitKeys?"ABC":"123","Space","Clear","Done"};
  for(int i=0;i<4;++i)keyFace(L::keyAction<Rect>(tft.width(),tft.height(),i),labels[i]);
}
void queryPaint(){
  tft.fillRect(0,L::HeaderH,tft.width(),L::QueryH,theme().bg);
  ui_text(String(query[0]?query:"Type a title or artist"),L::QueryInset,L::QueryY,ML_DATUM,theme().text,theme().bg,tft.width()-L::QueryTextInset);
  ui_text(searchError()?String("SD!"):searchSession.pending?String("..."):searchWaiting()?String(storage_searchProgress())+"%":String(searchCount()),tft.width()-L::QueryStatusInset,L::QueryY,MR_DATUM,theme().dim,theme().bg,L::QueryStatusW,TYPE_CAPTION);
}
void keyboardPaint(){
  tft.fillRect(0,L::KeyY,tft.width(),footerY()-L::KeyY,theme().bg);
  const char* keys=keyLetters();
  for(int i=0;keys[i];++i){char label[2]={keys[i],0};keyFace(keyRect(i),label);}
  keyFace(keyBackRect(),"Delete");keyboardActions();
  queryPaint();
}
void editQuery(int target){
  size_t n=strlen(query);bool changed=true;
  if(target==KeyDigits){digitKeys=!digitKeys;keyboardPaint();return;}
  if(target==KeyBack){if(n)query[n-1]=0;else changed=false;}
  else if(target==KeyClear){if(n)query[0]=0;else changed=false;}
  else if(n<sizeof(query)-1){query[n]=target==KeySpace?' ':char(target-KeyBase);query[n+1]=0;}
  else {changed=false;ui_toast("Search text is full (31 characters)");}
  if(changed){scheduleSearch(150);queryPaint();}
}
void enter(){
#ifndef CYDPOD_CATALOG_HOST
  rowUtf8.reserve(128);
#endif
  seekPreview=false;playerPressed=tilePressed=-1;ready=highlight=dimmed=-1;gesture=cydclassic::Gesture{};
  if(page().mode==Mode::SourceSongs&&(previewDirty||!preview)){
    if(!buildPreview(page().source)){message("Unable to reload list. Check SD card or try again.");return;}
  }
  if((page().mode==Mode::Search||page().mode==Mode::Keyboard)&&searchSession.state==SearchState::Cancelled&&!searchSession.pending)scheduleSearch(0);
  count=rowCount();shownSerial=UINT32_MAX;tft.fillScreen(theme().bg);header();footer();
  shownVolume=shownConnection=-1;
  if(page().mode==Mode::Keyboard){keyboardPaint();searchShownCount=searchShownProgress=-1;}
  else if(page().mode==Mode::Player){playerTrack();playerTransport(true);playerBottom();lastPlayer=0;playerTick(millis());}
  else if(page().mode==Mode::Home)paintTiles();
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
  if(externalReturn||(page().mode!=Mode::Search&&page().mode!=Mode::Keyboard)){
    if(searchSession.state==SearchState::Active){storage_searchCancel();searchSession.state=SearchState::Cancelled;}
  }
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
    applySearch(now);
    if(searchCurrent()){
      storage_searchTick();
      if(storage_searchFailed())searchSession.state=SearchState::Error;
    }
    const int n=searchCount(),progress=searchSession.pending?-2:searchError()?-3:searchWaiting()?storage_searchProgress():101;
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
  // A single row per loop bounds SD and drawing work; audio/Bluetooth run between rows.
  if(dirty&&(!audio_bufferLow()||now-lastPaint>=120)){
    for(int i=0;i<visibleRows();++i)if(dirty&(1u<<i)){dirty&=~(1u<<i);loadRow(i);lastPaint=now;break;}
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
    case 4:show(Mode::Search);show(Mode::Keyboard);break;
    default:show(Mode::Settings);break;
  }
}
void activate(int at){
  const int index=at-topRow();if(index<0||index>=visibleRows())return;Row selected=cachedRow(at);
  if(selected.absolute!=at)return;
  if(!selected.valid||selected.disabled){Serial.println("cmd: row ignored (unavailable)");ui_toast("Entry unavailable. Try again.");return;}
  Serial.printf("cmd: row %d accepted screen %u\n",at,unsigned(page().mode));
  switch(page().mode){
    case Mode::Playlists:if(at==0)openSource(1);else if(at==1)sourceMenu(SRC_FOLDER,"Folders");else openSource(selected.source);break;
    case Mode::Catalog:
      if(selected.navigates)catalog(selected.child,selected.label);
      else if(playlist_playView(page().view,at))show(Mode::Player);else message(playlist_memoryRefused()?"Not enough memory, try again":"Unable to read songs or allocate queue.");break;
    case Mode::Search:
      if(at==0)show(Mode::Keyboard);
      else if(at-1<searchCount()&&selected.track==storage_searchTracks()[at-1]){
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
      // One full repaint on release: enter() clears the screen in the new bg and redraws header, footer/mini-player and every row (cache invalidated).
      else if(at==3){g_settings.colorTheme=themeNext(g_settings.colorTheme);settings_save();enter();}
      else{calibrate_run();enter();}break;
    case Mode::Output:
#if CYD_WIRED_OUTPUT
      if(at==OUT_SELECTABLE_COUNT){
        if(!settings_setHeadphoneLimit(!g_settings.headphoneLimit)){message("Unable to save headphone limit. Try again.");break;}
        wired_setHeadphoneLimit(g_settings.headphoneLimit);invalidate();break;
      }
#endif
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
    for(int i=0;i<4;++i)if(L::keyAction<Rect>(tft.width(),tft.height(),i).contains(x,y))return i==0?KeyDigits:i==1?KeySpace:i==2?KeyClear:KeyDone;
    if(keyBackRect().contains(x,y))return KeyBack;
    if(keyDigitsRect().contains(x,y))return KeyDigits;
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
  const int local=y-L::HeaderH;const int at=(local+page().offsetPx)/L::RowH;
  return local>=0&&local<rows()*L::RowH&&at<count?at:-1;
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
    ready=-1;dimmed=-1;gesture.down(x,y,topRow(),hit);
    if(page().mode==Mode::Keyboard&&hit>=KeyBase){editQuery(hit);gesture.claimed=true;return true;}
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
    highlight=hit>=0&&page().mode!=Mode::Keyboard?hit-topRow():-1;if(highlight>=0&&highlight<rows()&&cachedRow(hit).absolute==hit&&cachedRow(hit).valid)paintRow(highlight,true);
    return false;
  }
  if(page().mode==Mode::Keyboard&&gesture.target>=KeyBase)return true; // captured character: no slide/release/hold repeat
  if(phase==PRESS_MOVE){
    if(gesture.target==Slider){volumeAt(x);return true;}
    if(gesture.target==Seek){
      if(abs(y-gesture.y)>cydclassic::Gesture::SlopFar||seekSerial!=audio_trackSerial())seekCancelled=true;
      if(!seekCancelled)seekPreviewAt(x);return true;
    }
    if(gesture.target==Prev||gesture.target==Play||gesture.target==Next){if(browse()&&!within(x,y,cydclassic::Gesture::SlopNear)&&playerPressed!=-1){playerPressed=-1;miniPlayer();}return true;}
    const bool claimed=gesture.move(x,y,!within(x,y,cydclassic::Gesture::SlopFar),gesture.target>=0&&gesture.target<KeyBase&&count>rows());
    if(gesture.claimed&&gesture.target>=0&&abs(y-gesture.y)>=10){int top=gesture.scrollTop(y,count,rows());
      if(top!=topRow()){setTop(top);highlight=-1;invalidate();footer();}}
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
  if(page().mode==Mode::Keyboard&&target!=Back&&target!=-1){if(target==KeyDone){if(searchSession.pending||searchSession.state==SearchState::Error)scheduleSearch(0);back();}else editQuery(target);return;}
  if(target>=0){activate(target);return;}
  if(tileOf(target)>=0){openTile(tileOf(target));return;}
  switch(target){
    case Back:back();break;
    case Home:classic_home();ui_go(SCR_CLASSIC);break;
    case Up:case Down:setTop(topRow()+(target==Up?-1:1)*max(1,rows()-1));invalidate();footer();break;
    case Now:if(storage_trackCount())openPlaying();break;
    case Options:show(Mode::Options);break;
    case Favourite:toggleFavourite();break;
    case Shuffle:if(browse()&&!audio_queueLen()){
      const int n=storage_trackCount();if(n){audio_setShuffle(true);g_settings.shuffle=true;
        if(playlist_playView({CatalogKind::Songs,0},esp_random()%n))openPlaying();else message("Unable to start playback.");}
    }else {cycleShuffle();playerTransport();}break;
    case Repeat:cycleRepeat();playerTransport();break;
    case Output:show(Mode::Output);break;
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
const char* classic_query(){return query;}
uint32_t classic_seekPreviewMs(){return seekMs;}
unsigned classic_depth(){return navigation.depth;}
unsigned classic_mode(){return unsigned(page().mode);}
const char* classic_modeName(unsigned m){
  static const char* const names[]={"Home","Playlists","Catalog","Sources","SourceSongs","Player","Settings","Playback","Display","Output","Options","About","Refresh","Message","Search","Keyboard","System"};
  static_assert(sizeof(names)/sizeof(names[0])==unsigned(Mode::System)+1,"mode name table matches Mode");
  return m<sizeof(names)/sizeof(names[0])?names[m]:"?";
}
int classic_top(){return topRow();}
int classic_offset(){return page().offsetPx;}
void classic_paintViewport(int offset){paintViewport(offset);}
void classic_invalidateViewport(){invalidate();}
int classic_cacheSlots(){return CacheSlots;}
#ifdef CYDPOD_CATALOG_HOST
void classic_hostTrackInfo(const cydlib::Record* record){hostTrackInfo=record;}
#endif
std::string classic_geometry(){
  std::ostringstream out;
  auto rect=[&](Rect r){out<<r.x<<','<<r.y<<','<<r.w<<','<<r.h<<';';};
  for(unsigned rotation:{0u,1u,2u,3u}){
    tft.setRotation(rotation);out<<"rotation="<<rotation<<" size="<<tft.width()<<'x'<<tft.height()<<" rows="<<rows()<<" rects=";
    for(int i=0;i<rows();++i)rect(rowRect(i));
    for(int i=0;i<L::TileCount;++i)rect(L::tile<Rect>(tft.width(),tft.height(),i));
    for(int cell:{0,1,2})rect(L::mini<Rect>(tft.width(),tft.height(),cell));rect(L::miniProgress<Rect>(tft.width(),tft.height()));rect(L::deepHome<Rect>());for(bool down:{false,true})rect(L::pageStrip<Rect>(tft.width(),tft.height(),down));
    for(int a:{Prev,Play,Next})rect(transportRect(a));
    for(Rect r:{L::playerBack<Rect>(),L::favourite<Rect>(tft.width()),L::options<Rect>(tft.width()),L::progress<Rect>(tft.width(),tft.height()),L::playerVolume<Rect>(tft.width(),tft.height()),L::playerVolumeTrack<Rect>(tft.width(),tft.height()),L::output<Rect>(tft.width(),tft.height()),L::outputFace<Rect>(tft.width(),tft.height()),L::playerToggle<Rect>(tft.width(),tft.height(),false),L::playerToggle<Rect>(tft.width(),tft.height(),true)})rect(r);
    rect(L::seek<Rect>(tft.width(),tft.height()));rect(L::strip<Rect>(tft.width(),tft.height()));rect({0,L::HeaderH,tft.width(),L::QueryH});out<<'\n';
    for(int m=0;m<=int(Mode::Keyboard);++m){
      out<<"mode="<<m;
      for(int variant=0;variant<4;++variant){
        navigation.home();navigation.depth=variant==0?1:variant==3?4:2;page().mode=Mode(m);
        audio_setQueue(nullptr,variant>=2?1:0,-1,false);
        count=variant==0?0:variant==1?1:7;page().offsetPx=(variant==3?2:0)*L::RowH;
        digitKeys=variant==2;
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
        if(page().mode==Mode::Keyboard){out<<" keys=";for(int i=0;keyLetters()[i];++i)rect(keyRect(i));rect(keyBackRect());for(int cell=0;cell<4;++cell)rect(L::keyAction<Rect>(tft.width(),tft.height(),cell));}
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
  navigation.home();audio_setQueue(nullptr,0,-1,false);digitKeys=false;count=0;tft.setRotation(0);return out.str();
}
#endif

#endif

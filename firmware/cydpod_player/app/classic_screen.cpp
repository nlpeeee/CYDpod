#ifndef CYD_UPLOADER
#include "classic_screen.h"
#include "classic_navigation.h"
#include "ui.h"
#include "display.h"
#include "catalog.h"
#include "storage.h"
#include "playlist.h"
#include "audio.h"
#include "sleep_mode.h"
#include "settings.h"
#include "bt.h"
#include "speaker.h"
#include "background.h"
#include "calibrate.h"
#include "titles.h"
#include "firmware.h"
#include <esp_system.h>

namespace {
enum class Mode:uint8_t { Home,Music,Catalog,Sources,SourceSongs,Player,Settings,Playback,Display,Output,Volume,Options,About,Refresh,Message,Search,Keyboard };
struct Page {
  Mode mode=Mode::Home;CatalogView view{};int16_t source=-1;uint16_t top=0;
  SourceKind sourceKind=SRC_M3U;char title[64]={};
};
struct Row {
  char label[128]={},detail[96]={};CatalogView child{};
  int16_t track=-1,source=-1;bool valid=false,navigates=false,disabled=false;
};
cydclassic::Navigation<Page> navigation;
cydclassic::Gesture gesture;
Row cache[6];
cydlib::Record playingInfo{};
uint8_t dirty=0;
int count=0,ready=-1,highlight=-1;
uint16_t* preview=nullptr;
int previewCount=0;
uint32_t lastPaint=0,lastPlayer=0,shownSerial=UINT32_MAX,saveAt=0;
uint32_t seenError=0,lastStatus=0;
int shownProgress=-1,shownSecond=-1,shownVolume=-1,shownConnection=-1;
bool shownPlaying=false,savePending=false,shownSeeking=false;
bool previewDirty=false;
bool externalReturn=false;
bool seekPreview=false,seekCancelled=false;
uint32_t seekMs=0,seekDuration=0,seekSerial=0,pressAt=0;
char notice[128]={};
char query[32]={};bool digitKeys=false,lettersSecond=false;uint32_t screenNow=0;
int searchShownCount=-1,searchShownProgress=-1;
constexpr int KeyBase=1000,KeyBack=-40,KeyDigits=-41,KeySpace=-42,KeyClear=-43,KeyDone=-44,KeyPage=-45;
constexpr int Back=-10,Home=-11,Up=-12,Down=-13,Now=-14,Prev=-20,Play=-21,Next=-22,Volume=-23,Options=-24,Slider=-25,Minus=-26,Plus=-27,Seek=-28;
Page& page(){return navigation.current();}
int rows(){return min(6,(tft.height()-88)/44);}
int footerY(){return tft.height()-44;}
Rect rowRect(int i){return {0,44+i*44,tft.width()-8,44};}
Rect footerRect(int action){
  const int w=tft.width();
  if(action==Home)return {0,footerY(),64,44};
  if(action==Up)return {64,footerY(),48,44};
  if(action==Down)return {112,footerY(),48,44};
  return {160,footerY(),w-160,44};
}
Rect transportRect(int action){const int column=action==Prev?0:action==Play?1:2;return {4+column*(tft.width()/3),tft.height()-100,tft.width()/3-8,52};}
Rect sliderRect(){return {24,126,tft.width()-48,52};}
void flushSettings(){if(savePending){g_settings.volume=audio_volume();settings_save();savePending=false;}}
void freePreview(){free(preview);preview=nullptr;previewCount=0;}
void text(const char* s,const Rect& r,uint16_t fg,uint16_t bg,TypeRole role=TYPE_BODY){ui_text(String(s),r.x+10,r.cy(),ML_DATUM,fg,bg,r.w-20,role);}
void centred(const String& s,int y,uint16_t fg,uint16_t bg,int width=0){ui_text(s,tft.width()/2,y,MC_DATUM,fg,bg,width?width:tft.width()-20);}
const char* title(){
  if(page().title[0])return page().title;
  switch(page().mode){
    case Mode::Home:return "CYDpod";case Mode::Music:return "Music";case Mode::Player:return "Now Playing";
    case Mode::Search:return "Search Results";case Mode::Keyboard:return "Search Songs";
    case Mode::Settings:return "Settings";case Mode::Playback:return "Playback";case Mode::Display:return "Display";
    case Mode::Output:return "Audio Output";case Mode::Volume:return "Volume";case Mode::Options:return "Options";
    case Mode::About:return "About";case Mode::Refresh:return "Refresh Library";case Mode::Message:return "CYDpod";
    default:return "Music";
  }
}
void header(){
  const Palette& p=theme();
  for(int y=0;y<44;++y)tft.drawFastHLine(0,y,tft.width(),tft.alphaBlend(uint8_t(y*180/44),p.band,0xffff));
  tft.drawFastHLine(0,43,tft.width(),p.outline);
  if(navigation.depth>1)ui_text("< Back",6,22,ML_DATUM,p.text,p.band,54);
  ui_text(String(title()),tft.width()/2,22,MC_DATUM,p.text,p.band,tft.width()-116);
  const char* status=g_settings.outRoute==OUT_BT?(bt_connected()?"BT":"BT..."):"SPK";
  ui_text(String(status),tft.width()-5,22,MR_DATUM,p.dim,p.band,36,TYPE_CAPTION);
}
void footer(){
  const Palette& p=theme();tft.fillRect(0,footerY(),tft.width(),44,p.band);
  tft.drawFastHLine(0,footerY(),tft.width(),p.outline);
  if(page().mode==Mode::Keyboard){
    text("Space",{0,footerY(),80,44},p.text,p.band);text("Clear",{80,footerY(),80,44},p.text,p.band);text("Done",{160,footerY(),80,44},p.text,p.band);return;
  }
  if(page().mode==Mode::Player){
    text("Volume",{0,footerY(),80,44},p.text,p.band);
    const uint16_t remaining=sleep_remainingMinutes(screenNow);
    text((remaining?String(remaining)+" min":String(audio_volume())+"%").c_str(),{80,footerY(),80,44},p.dim,p.band,TYPE_CAPTION);
    text("Options",{160,footerY(),tft.width()-160,44},p.text,p.band);
  }else{
    text("Home",footerRect(Home),p.text,p.band);
    if(count>rows()){
      ui_icon(ICON_UP,88,footerY()+22,14,page().top?p.text:p.disabled,p.band);
      ui_icon(ICON_DOWN,136,footerY()+22,14,page().top+rows()<count?p.text:p.disabled,p.band);
    }
    text("Playing",footerRect(Now),storage_trackCount()?p.text:p.disabled,p.band,TYPE_CAPTION);
  }
}
void paintRow(int i,bool pressed){
  if(i<0||i>=rows())return;Row& r=cache[i];const Palette& p=theme();const Rect box=rowRect(i);
  uint16_t bg=pressed?p.accent:p.bg,fg=pressed?p.onAccent:r.disabled?p.disabled:p.text;
  tft.fillRect(box.x,box.y,box.w,box.h,bg);
  if(!r.valid)return;
  if(r.detail[0]){
    ui_text(String(r.label),10,box.y+14,ML_DATUM,fg,bg,box.w-34);
    ui_text(String(r.detail),10,box.y+33,ML_DATUM,pressed?p.onAccent:p.dim,bg,box.w-34,TYPE_CAPTION);
  }else text(r.label,box,fg,bg);
  if(r.navigates)ui_icon(ICON_CHEVRON,box.w-12,box.cy(),12,fg,bg);
  else if(r.track==audio_currentTrack()&&audio_isPlaying())ui_icon(ICON_PLAY,box.w-12,box.cy(),12,fg,bg);
  tft.drawFastHLine(0,box.y+43,box.w,p.outlineSubtle);
}
void invalidate(){
  page().top=cydclassic::clampTop(page().top,count,rows());dirty=uint8_t((1u<<rows())-1);
  for(int i=0;i<6;++i)cache[i].valid=false;
  const Palette& p=theme();const int x=tft.width()-7,h=rows()*44;
  tft.fillRect(x,44,7,h,p.band);
  if(count>rows()){const int thumb=max(12,h*rows()/count);const int y=44+(h-thumb)*page().top/max(1,count-rows());tft.fillRect(x+2,y,3,thumb,p.dim);}
}
void setRow(Row& r,const char* label,const String& detail=String(),bool navigate=false,bool disabled=false){
  cydlib::copyText(r.label,label);cydlib::copyText(r.detail,detail.c_str());r.valid=true;r.navigates=navigate;r.disabled=disabled;
}
int sourceId(int wanted){
  return playlist_sourceAt(page().sourceKind,wanted);
}
int rowCount(){
  switch(page().mode){
    case Mode::Home:return 5;case Mode::Music:return 7;
    case Mode::Search:return 1+max(1,storage_searchCount());
    case Mode::Catalog:return storage_browseCount(page().view);
    case Mode::Sources:return playlist_sourceCount(page().sourceKind);
    case Mode::SourceSongs:return previewCount;
    case Mode::Settings:return 7;case Mode::Playback:return 4;case Mode::Display:return 5;case Mode::Output:return 4;
    case Mode::Options:return 4;case Mode::Refresh:return 2;
    default:return 0;
  }
}
void loadRow(int i){
  Row& r=cache[i];r=Row{};const int at=page().top+i;
  if(at>=count){r.valid=true;paintRow(i,false);return;}
  switch(page().mode){
    case Mode::Home:{const char* names[]={"Music","Now Playing","Shuffle Songs","Settings","Search Songs"};setRow(r,names[at],String(),true,at==1&&!storage_trackCount());break;}
    case Mode::Music:{const char* names[]={"Artists","Albums","Songs","Playlists","Folders","Favourites","Search Songs"};setRow(r,names[at],String(),true);break;}
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
    case Mode::Settings:{const char* names[]={"Audio Output","Bluetooth","Playback","Display","Refresh Library","Upload / Advanced","About"};setRow(r,names[at],String(),true);break;}
    case Mode::Playback:
      if(at<2)setRow(r,at==0?"Shuffle":"Repeat",at==0?String(audio_shuffle()?"On":"Off"):String(audio_repeat()==REPEAT_ONE?"One":audio_repeat()==REPEAT_ALL?"All":"Off"));
      else setRow(r,at==2?"Screen timeout":"Sleep timer",at==2?String(sleep_timeoutLabel(g_settings.screenTimeout)):String(sleep_timerLabel(g_settings.sleepTimer)));break;
    case Mode::Display:{const char* names[]={"Brightness -","Brightness +","Flip portrait","Calibrate touch","Panel / Advanced"};setRow(r,names[at],at<2?String(g_settings.brightness)+"%":String(),at>=3);break;}
    case Mode::Output:{const char* names[]={"Bluetooth","On-board speaker","Stereo line out","Headphones"};setRow(r,names[at],at<2?String((at==0)==(g_settings.outRoute==OUT_BT)?"Selected":""):String("Hardware path pending"),false,at>=2);break;}
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
void back(){
  if(page().mode==Mode::SourceSongs)freePreview();
  if(navigation.back())ui_go(SCR_CLASSIC);
}
void volumeAt(int x){
  Rect s=sliderRect();const int value=constrain((x-s.x)*100/max(1,s.w-1),0,100);
  if(value!=audio_volume()){audio_setVolume(value);g_settings.volume=value;savePending=true;saveAt=millis()+1500;}
}
void volumePaint(){
  const Palette& p=theme();centred(String(audio_volume())+"%",90,p.text,p.bg);
  Rect s=sliderRect();tft.fillRect(s.x,s.y,s.w,s.h,p.bg);ui_slider(s,audio_volume(),100,p.bg);
  ui_textButton({8,190,96,52},T_COUNT);ui_textButton({tft.width()-104,190,96,52},T_COUNT);
  ui_icon(ICON_VOL_DOWN,56,216,26,p.text,p.btn);ui_icon(ICON_VOL_UP,tft.width()-56,216,26,p.text,p.btn);
}
void playerTransport(){
  ui_button(transportRect(Prev),ICON_PREV);
  ui_button(transportRect(Play),audio_isPlaying()?ICON_PAUSE:ICON_PLAY,audio_isPlaying());
  ui_button(transportRect(Next),ICON_NEXT);
  shownPlaying=audio_isPlaying();
}
void playerTrack(){
  const Palette& p=theme();memset(&playingInfo,0,sizeof(playingInfo));
  if(!storage_trackInfo(audio_currentTrack(),playingInfo)){
    cydlib::copyText(playingInfo.title,"No songs");cydlib::copyText(playingInfo.artist,"Add M4A files to /music");
  }
  const bool portrait=tft.height()>=300;
  if(portrait){Rect art{tft.width()/2-28,48,56,56};tft.fillRect(art.x,art.y,art.w,art.h,p.band);ui_icon(ICON_NOTE,art.cx(),art.cy(),34,p.dim,p.band);}
  const int y=portrait?120:54;
  tft.fillRect(0,y-12,tft.width(),58,p.bg);
  centred(String(playingInfo.title),y,p.text,p.bg);
  centred(String(playingInfo.artist),y+20,p.dim,p.bg);
  centred(String(playingInfo.album),y+36,p.dim,p.bg);
  shownProgress=shownSecond=-1;shownSerial=audio_trackSerial();
}
void playerTick(uint32_t now){
  if(now-lastPlayer<250)return;lastPlayer=now;
  const Palette& p=theme();
  if(shownSerial!=audio_trackSerial())playerTrack();
  if(shownPlaying!=audio_isPlaying())playerTransport();
  const uint32_t pos=seekPreview?seekMs:audio_positionMs(),duration=audio_durationMs();
  const int barY=tft.height()-136,barWidth=tft.width()-24;
  int progress=duration?min<int>(barWidth,uint64_t(pos)*barWidth/duration):0;
  if(progress!=shownProgress){tft.fillRect(12,barY,progress,5,p.bar);tft.fillRect(12+progress,barY,barWidth-progress,5,p.barBg);shownProgress=progress;}
  const bool busy=audio_isSeeking();
  if(int(pos/1000)!=shownSecond||busy!=shownSeeking){shownSecond=pos/1000;shownSeeking=busy;tft.fillRect(0,barY+7,tft.width(),18,p.bg);
    ui_text(busy?String("Seeking..."):ui_time(pos),12,barY+16,ML_DATUM,p.dim,p.bg,tft.width()/2-16,TYPE_CAPTION);
    ui_text(String("-")+ui_time(duration>pos?duration-pos:0,duration>0),tft.width()-12,barY+16,MR_DATUM,p.dim,p.bg,tft.width()/2-16,TYPE_CAPTION);}
  int connection=g_settings.outRoute==OUT_BT?(bt_connected()?2:1):0;
  static uint16_t shownSleep=UINT16_MAX;
  const uint16_t remaining=sleep_remainingMinutes(now);
  if(connection!=shownConnection||remaining!=shownSleep){shownSleep=remaining;shownConnection=connection;header();footer();}
}
// Five rows of 40 px keys, with a 44 px action bar. No title list in RAM.
const char* keyLetters(){return digitKeys?"0123456789":tft.height()<300?(lettersSecond?"qrstuvwxyz":"abcdefghijklmnop"):"abcdefghijklmnopqrstuvwxyz";}
Rect keyRect(int i){const int cols=tft.height()<300?8:digitKeys?5:6;return {(i%cols)*(tft.width()/cols),76+(i/cols)*40,tft.width()/cols,40};}
Rect keyBackRect(){return tft.height()<300?Rect{0,156,106,40}:Rect{80,236,80,40};}
Rect keyDigitsRect(){return tft.height()<300?Rect{212,156,108,40}:Rect{160,236,80,40};}
void keyFace(const Rect& r,const char* label){
  tft.fillRect(r.x+1,r.y+1,r.w-2,r.h-2,theme().band);
  ui_text(String(label),r.cx(),r.cy(),MC_DATUM,theme().text,theme().band,r.w-4);
}
void queryPaint(){
  tft.fillRect(0,44,tft.width(),32,theme().bg);
  ui_text(String(query[0]?query:"Type a title or artist"),8,60,ML_DATUM,theme().text,theme().bg,tft.width()-54);
  ui_text(storage_searchFailed()?String("SD!"):storage_searchBusy()?String(storage_searchProgress())+"%":String(storage_searchCount()),tft.width()-6,60,MR_DATUM,theme().dim,theme().bg,42,TYPE_CAPTION);
}
void keyboardPaint(){
  tft.fillRect(0,76,tft.width(),footerY()-76,theme().bg);
  const char* keys=keyLetters();
  for(int i=0;keys[i];++i){char label[2]={keys[i],0};keyFace(keyRect(i),label);}
  keyFace(keyBackRect(),"Delete");keyFace(keyDigitsRect(),digitKeys?"ABC":"123");
  if(tft.height()<300)keyFace({106,156,106,40},lettersSecond?"A-P":"Q-Z");
  keyFace({0,footerY(),80,44},"Space");keyFace({80,footerY(),80,44},"Clear");keyFace({160,footerY(),tft.width()-160,44},"Done");
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
  bg_stop();seekPreview=false;ready=highlight=-1;gesture=cydclassic::Gesture{};
  if(page().mode==Mode::SourceSongs&&(previewDirty||!preview)){
    if(!buildPreview(page().source)){message("Unable to reload list. Check SD card or try again.");return;}
  }
  if(page().mode==Mode::Search)storage_searchStart(query);
  count=rowCount();tft.fillScreen(theme().bg);header();footer();
  shownVolume=shownConnection=-1;
  if(page().mode==Mode::Keyboard){keyboardPaint();searchShownCount=searchShownProgress=-1;}
  else if(page().mode==Mode::Player){playerTrack();playerTransport();lastPlayer=0;playerTick(millis());}
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
  seekPreview=false;flushSettings();dirty=0;titles_release();
  // ui_go calls leave after a navigation push/pop: current() is the destination.
  // Keep a newly built destination preview, otherwise rebuild lazily on Back.
  if(page().mode!=Mode::SourceSongs||externalReturn)freePreview();
  if(page().mode!=Mode::Search&&page().mode!=Mode::Keyboard)storage_searchCancel();
}
void tick(uint32_t now){
  screenNow=now;
  if(savePending&&int32_t(now-saveAt)>=0)flushSettings();
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
    int state=g_settings.outRoute==OUT_BT?(bt_connected()?2:1):0;
    if(state!=shownConnection){shownConnection=state;header();footer();}
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
  if(page().mode==Mode::Volume&&shownVolume!=audio_volume()){shownVolume=audio_volume();volumePaint();}
  // A single row per loop bounds SD and drawing work; audio/Bluetooth run between rows.
  if(dirty&&(!audio_bufferLow()||now-lastPaint>=120)){
    for(int i=0;i<rows();++i)if(dirty&(1u<<i)){dirty&=~(1u<<i);loadRow(i);lastPaint=now;break;}
  }
}
void sourceMenu(SourceKind kind,const char* label){show(Mode::Sources,label);page().sourceKind=kind;count=rowCount();invalidate();footer();}
void cycleShuffle(){audio_setShuffle(!audio_shuffle());g_settings.shuffle=audio_shuffle();settings_save();invalidate();}
void cycleRepeat(){audio_setRepeat((audio_repeat()+1)%REPEAT_MODE_COUNT);g_settings.repeat=audio_repeat();settings_save();invalidate();}
void activate(int at){
  const int index=at-page().top;if(index<0||index>=rows())return;Row selected=cache[index];
  if(!selected.valid||selected.disabled){Serial.println("cmd: row ignored (unavailable)");ui_toast("Entry unavailable. Try again.");return;}
  Serial.printf("cmd: row %d accepted screen %u\n",at,unsigned(page().mode));
  switch(page().mode){
    case Mode::Home:
      if(at==0)show(Mode::Music);else if(at==1)show(Mode::Player);else if(at==2){
        const int n=storage_trackCount();if(!n){message("No songs on the SD card.");break;}
        audio_setShuffle(true);g_settings.shuffle=true;
        if(playlist_playView({CatalogKind::Songs,0},esp_random()%n))show(Mode::Player);else message(playlist_memoryRefused()?"Not enough memory, try again":"Unable to start playback.");
      }else if(at==3)show(Mode::Settings);else {storage_searchStart(query);show(Mode::Search);show(Mode::Keyboard);}break;
    case Mode::Music:
      if(at<3){const CatalogKind kinds[]={CatalogKind::Artists,CatalogKind::Albums,CatalogKind::Songs};catalog({kinds[at],0},selected.label);}
      else if(at==3)sourceMenu(SRC_M3U,"Playlists");else if(at==4)sourceMenu(SRC_FOLDER,"Folders");else if(at==5)openSource(1);else {storage_searchStart(query);show(Mode::Search);show(Mode::Keyboard);}break;
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
      else if(at==4)show(Mode::Refresh);else if(at==5)external(SCR_SETUP);else show(Mode::About);break;
    case Mode::Playback:
      if(at==0)cycleShuffle();else if(at==1)cycleRepeat();
      else if(at==2){g_settings.screenTimeout=(g_settings.screenTimeout+1)%4;settings_save();invalidate();}
      else {sleep_setTimer((g_settings.sleepTimer+1)%5,screenNow);settings_save();invalidate();}break;
    case Mode::Display:
      if(at<2){g_settings.brightness=constrain(int(g_settings.brightness)+(at?10:-10),10,100);display_setBacklight(g_settings.brightness);settings_save();invalidate();}
      else if(at==2){g_settings.rotation=(g_settings.rotation&2)?0:2;display_setRotation(g_settings.rotation);settings_save();enter();}
      else if(at==3){calibrate_run();enter();}else external(SCR_SETUP);break;
    case Mode::Output:
      if(at<2){const uint8_t next=at==0?OUT_BT:OUT_SPEAKER;
        if(!speaker_setRoute(next)){message("Unable to change output. Try again.");break;}
        g_settings.outRoute=next;settings_save();invalidate();header();}break;
    case Mode::Options:
      if(at==0){
        if(storage_trackCount()){
          if(playlist_favouritesFull()&&!playlist_isFavourite(audio_currentTrack())){message("Favourites list is full.");break;}
          playlist_toggleFavourite(audio_currentTrack());
          if(playlist_favouriteFailed()){message("Unable to save favourite. Check SD card or memory.");break;}
          previewDirty=true;
        }invalidate();
      }
      else if(at==1)cycleShuffle();else if(at==2)cycleRepeat();else show(Mode::Output);break;
    case Mode::Refresh:
      if(at==1)back();else if(storage_refreshNextBoot()){g_settings.lastTrack=audio_currentTrack();g_settings.lastPath=storage_trackPath(audio_currentTrack());g_settings.lastPosMs=audio_positionMs();settings_save();ESP.restart();}
      else message("Cannot write refresh request. Check SD card.");break;
    default:break;
  }
}
int targetAt(int x,int y){
  if(y<44)return navigation.depth>1&&x<64?Back:-1;
  if(page().mode==Mode::Keyboard){
    if(y>=footerY())return x<80?KeySpace:x<160?KeyClear:KeyDone;
    if(keyBackRect().contains(x,y))return KeyBack;
    if(keyDigitsRect().contains(x,y))return KeyDigits;
    if(tft.height()<300&&Rect{106,156,106,40}.contains(x,y))return KeyPage;
    const char* keys=keyLetters();
    for(int i=0;keys[i];++i)if(keyRect(i).contains(x,y))return KeyBase+keys[i];
    return -1;
  }
  if(y>=footerY()){
    if(page().mode==Mode::Player)return x<80?Volume:x>=160?Options:-1;
    if(footerRect(Home).contains(x,y))return Home;
    if(footerRect(Up).contains(x,y))return count>rows()?Up:-1;
    if(footerRect(Down).contains(x,y))return count>rows()?Down:-1;
    return Now;
  }
  if(page().mode==Mode::Player){
    if(Rect{8,tft.height()-148,tft.width()-16,44}.contains(x,y))return Seek;
    for(int a:{Prev,Play,Next})if(transportRect(a).contains(x,y))return a;return -1;
  }
  if(page().mode==Mode::Volume){
    if(sliderRect().contains(x,y))return Slider;
    if(Rect{8,190,96,52}.contains(x,y))return Minus;
    if(Rect{tft.width()-104,190,96,52}.contains(x,y))return Plus;return -1;
  }
  int row=(y-44)/44;return row>=0&&row<rows()&&page().top+row<count?page().top+row:-1;
}
void seekPreviewAt(int x){
  const int width=tft.width()-24;
  seekMs=seekDuration?uint32_t(uint64_t(constrain(x-12,0,width))*(seekDuration-1)/width):0;
  shownProgress=shownSecond=-1;lastPlayer=0;
}
void seekTransport(int target,bool held){
  if(!storage_trackCount())return;
  if(held){if(audio_canSeek())audio_seekRelative(target==Prev?-10:10);else ui_toast("Seeking unavailable for this track",1500);}
  else if(target==Prev)audio_prev();else audio_next();
}
bool press(PressPhase phase,int x,int y){
  const int hit=targetAt(x,y);
  if(phase==PRESS_DOWN){
    ready=-1;gesture.down(x,y,page().top,hit);
    if(hit==Slider){volumeAt(x);return true;}
    if(hit==Seek){
      seekCancelled=!audio_canSeek();seekPreview=!seekCancelled;
      seekDuration=audio_durationMs();seekSerial=audio_trackSerial();seekPreviewAt(x);return true;
    }
    if(hit==Prev||hit==Next){pressAt=millis();return true;}
    highlight=hit>=0&&page().mode!=Mode::Keyboard?hit-page().top:-1;if(highlight>=0&&highlight<rows()&&cache[highlight].valid)paintRow(highlight,true);
    return false;
  }
  if(phase==PRESS_MOVE){
    if(gesture.target==Slider){volumeAt(x);return true;}
    if(gesture.target==Seek){
      if(abs(y-gesture.y)>24||seekSerial!=audio_trackSerial())seekCancelled=true;
      if(!seekCancelled)seekPreviewAt(x);return true;
    }
    if(gesture.target==Prev||gesture.target==Next){if(hit!=gesture.target)gesture.cancelled=true;return true;}
    bool claimed=gesture.move(x,y,hit,count>rows());
    if(claimed&&gesture.target>=0&&abs(y-gesture.y)>=10){int top=gesture.scrollTop(y,count,rows());
      if(top!=page().top){page().top=top;highlight=-1;invalidate();footer();}}
    if(claimed&&highlight>=0){paintRow(highlight,false);highlight=-1;}
    return claimed;
  }
  if(phase==PRESS_UP){
    if(gesture.target==Slider)return true;
    if(gesture.target==Seek){
      seekPreview=false;shownProgress=shownSecond=-1;lastPlayer=0;
      if(!seekCancelled&&seekSerial==audio_trackSerial()){
        if(!audio_seekTo(seekMs))ui_toast("Seeking unavailable for this track",1500);
      }else if(!audio_canSeek())ui_toast("Seeking unavailable for this track",1500);
      return true;
    }
    if(gesture.target==Prev||gesture.target==Next){
      if(!gesture.cancelled&&hit==gesture.target)seekTransport(gesture.target,millis()-pressAt>=600);
      return true;
    }
    ready=gesture.activate(hit)?hit:-1;
    if(highlight>=0){paintRow(highlight,false);highlight=-1;}
    return gesture.claimed||gesture.cancelled;
  }return false;
}
void touch(TouchEvent ev,int,int){
  const int target=ready;ready=-1;
  // CT-05: a firm, slow press (600 ms to 4 s) that was not claimed by a drag or the 4 s calibration hold acts like a tap.
  if(ev!=TOUCH_TAP&&ev!=TOUCH_LONG_PRESS)return;
  if(page().mode==Mode::Keyboard&&target!=Back&&target!=-1){if(target==KeyDone)back();else editQuery(target);return;}
  if(target>=0){activate(target);return;}
  switch(target){
    case Back:back();break;
    case Home:classic_home();ui_go(SCR_CLASSIC);break;
    case Up:case Down:page().top=cydclassic::clampTop(page().top+(target==Up?-1:1)*max(1,rows()-1),count,rows());invalidate();footer();break;
    case Now:if(storage_trackCount())show(Mode::Player);break;
    case Prev:if(storage_trackCount())audio_prev();break;
    case Play:if(storage_trackCount())audio_togglePause();break;
    case Next:if(storage_trackCount())audio_next();break;
    case Volume:show(Mode::Volume);break;
    case Options:show(Mode::Options);break;
    case Minus:case Plus:audio_setVolume(constrain(int(audio_volume())+(target==Plus?5:-5),0,100));g_settings.volume=audio_volume();savePending=true;saveAt=millis()+1500;break;
    default:break;
  }
}
} // namespace

void classic_home(){flushSettings();freePreview();externalReturn=false;navigation.home();}
void classic_preparePlayer(){if(externalReturn){externalReturn=false;return;}if(page().mode!=Mode::Player){Page p{};p.mode=Mode::Player;if(!navigation.push(p)){navigation.home();navigation.push(p);}}}
void classic_prepareLibrary(){classic_home();Page p{};p.mode=Mode::Music;navigation.push(p);}
const Screen SCREEN_CLASSIC={enter,leave,tick,touch,press};
#endif

#include "ui.h"
#include "sleep_mode.h"
#include "display.h"
#include "classic_screen.h"
#include "settings.h"
#include "catalog.h"
#include "playlist.h"
#include "storage.h"
#include "titles.h"
#include "bt.h"
#include "speaker.h"
#include "output_switch.h"
#include "audio.h"
#include "calibrate.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <functional>
#include <cmath>
#include <map>
#include <set>
#include "touch_gesture.h"
#include "classic_layout.h"
TFT_eSPI tft;
Settings g_settings;
static int current=0,calls=0,volume=32,queueLength=0;
static uint32_t serial=0,clockNow=0,lastSeek=0,positionMs=84000,underruns=0;
uint32_t audio_underruns(){return underruns;}
static int seekCalls=0,relativeSeek=0,backlight=100,fadeGain=100;
void audio_setSleepGain(uint8_t p){fadeGain=p;}
static bool seekAllowed=true;
static bool playing=false,shuffle=false,down=false,routeAllowed=true;
static uint8_t repeatMode=0;
static const uint16_t* queueData=nullptr;
static int tx=0,ty=0;
static int nextCalls=0,prevCalls=0,toggleCalls=0,shuffleCalls=0,recalCount=0;
static cydtouch::PressTracker tracker;static bool useTracker=false;
static int settingsSaves=0,volumeCalls=0;
bool settings_save(){++settingsSaves;return true;}
bool audio_ready(){return true;}
uint32_t audio_error(char*,size_t,int*){return 0;}
void audio_setQueue(const uint16_t* p,int n,int,bool){queueData=p;queueLength=n;++calls;}
void audio_playTrack(int n){current=n;playing=true;++serial;++calls;}
int audio_currentTrack(){return current;}
bool audio_isPlaying(){return playing;}
void audio_next(){++nextCalls;int pos=0;for(int i=0;i<queueLength;++i)if(queueData[i]==current)pos=i;audio_playTrack(queueData[(pos+1)%queueLength]);}
void audio_prev(){++prevCalls;audio_playTrack(current);}
void audio_togglePause(){++toggleCalls;playing=!playing;}
void audio_setVolume(uint8_t n){volume=n;++volumeCalls;}
uint8_t audio_volume(){return uint8_t(volume);}
void audio_setShuffle(bool on){shuffle=on;++shuffleCalls;}
bool audio_shuffle(){return shuffle;}
void audio_setRepeat(uint8_t mode){repeatMode=mode;}
uint8_t audio_repeat(){return repeatMode;}
bool audio_bufferLow(){return false;}
bool audio_canSeek(){return seekAllowed;}
bool audio_isSeeking(){return false;}
bool audio_seekTo(uint32_t ms){lastSeek=ms;++seekCalls;return seekAllowed;}
void audio_seekRelative(int s){relativeSeek=s;++seekCalls;}
uint32_t audio_durationMs(){return seekAllowed?240000:0;}
uint32_t audio_positionMs(){return positionMs;}
uint32_t audio_trackSerial(){return serial;}
static int btCalls=0;static bool btConnected=true;
bool bt_connected(){++btCalls;return btConnected;}
bool bt_connecting(){++btCalls;return false;}
static bool btPairingNow=false,btCancelOk=true;
bool bt_pairing(){++btCalls;return btPairingNow;}
bool bt_choosing(){++btCalls;return false;}
static String btName="Speaker";
String bt_speakerName(){++btCalls;return btName;}
cyd::PairingSession::Phase bt_pairingPhase(){++btCalls;return cyd::PairingSession::Normal;}
BtFailure bt_failure(){++btCalls;return BT_FAIL_NONE;}
uint32_t bt_passkey(){++btCalls;return 0;}
uint32_t bt_listSerial(){++btCalls;return 0;}
bool bt_requestPairing(){++btCalls;return true;}
bool bt_cancelPairing(){++btCalls;if(btCancelOk)btPairingNow=false;return btCancelOk;}
int bt_deviceCount(){++btCalls;return 0;}
bool bt_device(int,BtDevice&){++btCalls;return false;}
bool bt_isConnectedTo(const uint8_t*){++btCalls;return false;}
int bt_knownCount(){++btCalls;return 0;}
bool bt_known(int,BtDevice&){++btCalls;return false;}
void bt_connectKnown(int){++btCalls;}
void bt_forget(int){++btCalls;}
void bt_choose(int){++btCalls;}
void audio_setPlaying(bool on){playing=on;}
bool audio_startupComplete(){return true;}
AudioCommands audio_commands(){return {-1,-1,0,false,false,false};}
static bool saveAllowed=true;
static int restartSaves=0,limitSaves=0,limitRequests=0;
static bool limitSaveAllowed=true,appliedLimit=true;
bool settings_setHeadphoneLimit(bool on){++limitSaves;if(!limitSaveAllowed)return false;g_settings.headphoneLimit=on;return true;}
void wired_setHeadphoneLimit(bool on){++limitRequests;appliedLimit=on;}
static uint8_t liveRoute=OUT_BT;
static bool savedParked=false;
bool settings_saveOutputResume(uint8_t){++restartSaves;savedParked=liveRoute==OUT_NONE&&!playing;return saveAllowed;}
bool speaker_setRoute(uint8_t r){if(!routeAllowed)return false;liveRoute=r;return true;}
uint8_t speaker_route(){return liveRoute;}
bool speaker_parkForRestart(){if(!routeAllowed)return false;liveRoute=OUT_NONE;return true;}
void speaker_restoreAfterRestartPark(uint8_t r){liveRoute=r;}
#if CYD_WIRED_OUTPUT
bool wired_active(){return true;}
#endif
bool calibrate_run(){return true;}
void display_setRotation(uint8_t r){tft.setRotation(r);}
void display_setBacklight(uint8_t p){backlight=p;}
int audio_queueLen(){return queueLength;}
int audio_queueTrack(int n){return n>=0&&n<queueLength?(queueData?queueData[n]:n):-1;}
int audio_queuePos(int track){for(int i=0;i<queueLength;++i)if(audio_queueTrack(i)==track)return i;return -1;}
void settings_saveResume(){}
const char* display_panelName(uint16_t){return "ST7789";}
bool firmware_uploaderPresent(){return false;}
bool firmware_bootUploader(){return false;}
bool touch_isDown(){return useTracker?tracker.down():down;}
void touch_position(int& x,int& y){if(useTracker)tracker.landing(x,y);else{x=tx;y=ty;}}
void touch_livePosition(int& x,int& y){if(useTracker)tracker.live(x,y);else{x=tx;y=ty;}}
bool titles_load(const String&){return false;}
void titles_release(){}
int titles_width(){return 0;}
void titles_draw(const Rect&,int,int,uint16_t,uint16_t){}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void pump(){for(int i=0;i<10;++i)ui_tick(clockNow+=130);}
static void tap(int x,int y){tx=x;ty=y;down=true;ui_tick(clockNow+=130);down=false;ui_touch(TOUCH_TAP,x,y);pump();}
static bool label(const char* s){return std::find(tft.labels.begin(),tft.labels.end(),s)!=tft.labels.end();}
// A drawn title is either whole or whole glyphs plus "..." (never a half-clipped glyph): CY-85.
static bool shown(const char* full){
  if(label(full))return true;const std::string want=full;
  for(const auto& l:tft.labels)if(l.size()>3&&l.compare(l.size()-3,3,"...")==0&&want.compare(0,l.size()-3,l,0,l.size()-3)==0)return true;
  return false;
}
static int countLabel(const char* s){return int(std::count(tft.labels.begin(),tft.labels.end(),s));}
static void snapshot(const char* path){if(std::getenv("CYDPOD_VIEWPORT_ONLY")&&std::string(path).find("cy98b-")!=0)return;tft.save(path);std::string png=path;png.replace(png.size()-4,4,".png");tft.savePng(png.c_str(),1);}

// ---------------------------------------------------------------------------
// CY-62: the real press classifier (touch_gesture.h) -> UI hook -> command path.
// loopStep() is the body of loop() in app.ino: poll, route the event, tick the UI.
// ---------------------------------------------------------------------------
using Path=std::function<void(uint32_t,int&,int&)>;
static void loopStep(uint32_t now,bool valid,int x,int y){
  int ex=0,ey=0;
  const TouchEvent ev=tracker.update(now,valid,x,y,&ex,&ey);
  if(ev==TOUCH_RECALIBRATE&&!ui_pressClaimed()&&!sleep_touchBlocked())++recalCount;
  else if(ev!=TOUCH_NONE)ui_touch(ev,ex,ey);
  ui_tick(now);
}
// A finger on the glass for `dur` ms, polled every `period` ms from `phase` ms after it lands.
static void pressRun(uint32_t dur,uint32_t period,uint32_t phase,const Path& path){
  tracker=cydtouch::PressTracker{};useTracker=true;
  const uint32_t start=clockNow+300,end=start+dur+600;
  for(uint32_t now=start+phase;now<=end;now+=period){
    const bool valid=now>=start&&now<start+dur;int x=0,y=0;
    if(valid)path(now-start,x,y);
    loopStep(now,valid,x,y);
  }
  clockNow=end;useTracker=false;pump();
}
static Path at(int x,int y){return [=](uint32_t,int& px,int& py){px=x;py=y;};}
static void home(){classic_home();ui_go(SCR_CLASSIC);pump();}
// CY-80: Home is six tiles and the mini-player; tests reach pages through the real hit targets.
static void tapTile(int i){const Rect r=cydclassic::layout::tile<Rect>(tft.width(),tft.height(),i);tap(r.cx(),r.cy());}
static void tapMini(){const Rect r=cydclassic::layout::mini<Rect>(tft.width(),tft.height(),1);tap(r.cx(),r.cy());}
static void toPlayer(){home();tapMini();}
static void toPlayback(){home();tapTile(5);tap(80,154);}
static void toSongs(){home();tapTile(0);}
unsigned classic_depth();int classic_top();
int classic_offset();bool classic_thumbVisible();
static void dragListRows(int amount){
  const int y=cydclassic::layout::HeaderH+60,end=y-amount*cydclassic::layout::RowH-(amount>0?7:-7);
  SCREEN_CLASSIC.press(PRESS_DOWN,60,y);SCREEN_CLASSIC.press(PRESS_MOVE,60,end);
  SCREEN_CLASSIC.tick(clockNow+=41);SCREEN_CLASSIC.press(PRESS_UP,60,end);SCREEN_CLASSIC.touch(TOUCH_TAP,60,end);pump();
}

static void touchRegression(){
  const uint32_t durations[]={1,20,50,100,300,650,4000,4500},periods[]={10,80,150};
  int cases=0;
  // Every recognised press takes effect exactly once, whatever its length or the poll rate.
  struct Button{const char* name;int x,y;int* counter;};
  const Button buttons[]={{"Play",120,246,&toggleCalls},{"Next",182,246,&nextCalls},{"Previous",40,246,&prevCalls}};
  for(const Button& b:buttons)for(uint32_t d:durations)for(uint32_t p:periods)for(uint32_t phase:{0u,7u}){
    if(phase>=d)continue;
    toPlayer();const int before=*b.counter,recals=recalCount;
    pressRun(d,p,phase,at(b.x,b.y));
    const std::string tag=std::string(b.name)+" "+std::to_string(d)+" ms poll "+std::to_string(p);
    check(*b.counter==before+1,("exactly one transport command: "+tag).c_str());
    check(recalCount==recals,("transport hold never starts calibration: "+tag).c_str());
    check(relativeSeek==0,("no hold jog: "+tag).c_str());
    ++cases;
  }
  for(uint32_t d:durations)for(uint32_t p:periods)for(uint32_t phase:{0u,7u}){
    if(phase>=d)continue;
    toPlayback();const int before=shuffleCalls,recals=recalCount;
    pressRun(d,p,phase,at(80,66));
    const std::string tag="row "+std::to_string(d)+" ms poll "+std::to_string(p);
    if(d>=4500){check(shuffleCalls==before&&recalCount==recals+1,("recalibrate hold is not a tap: "+tag).c_str());}
    else check(shuffleCalls==before+1&&recalCount==recals,("exactly one row activation: "+tag).c_str());
    ++cases;
  }
  std::cout<<"PASS CY-62 classifier->UI->command: "<<cases<<" cases of Play/Next/Previous (on down) and a row (on release), 1 to 4500 ms at 10/80/150 ms polling, one command each, no jog, no recalibration from transport\n";

  // Jitter inside the target, a long hold with a split-second dropout, and a quick second touch.
  toPlayback();int before=shuffleCalls;
  pressRun(300,10,0,[](uint32_t t,int& x,int& y){x=80+((t/10)%2?-10:10);y=66+int((t/10)%3)*4-4;});
  check(shuffleCalls==before+1,"jitter inside the row still activates it once");
  toPlayer();before=toggleCalls;tracker=cydtouch::PressTracker{};useTracker=true;
  {uint32_t t=clockNow+300;for(int i=0;i<40;++i,t+=10)loopStep(t,i<15||i>=20,120,246);   // 50 ms dropout inside a long hold
   for(int i=0;i<20;++i,t+=10)loopStep(t,false,0,0);clockNow=t;}
  useTracker=false;pump();check(toggleCalls==before+1,"a dropout inside the grace does not toggle twice");
  toPlayer();before=toggleCalls;tracker=cydtouch::PressTracker{};useTracker=true;
  {uint32_t t=clockNow+300;const uint32_t rearm=cydtouch::stats().rearm;
   for(int i=0;i<10;++i,t+=10)loopStep(t,true,120,246);                           // 100 ms press
   for(int i=0;i<10;++i,t+=10)loopStep(t,i>=7,120,246);                           // lifts, lands again 70 ms later
   for(int i=0;i<20;++i,t+=10)loopStep(t,false,0,0);clockNow=t;
   check(cydtouch::stats().rearm>rearm,"re-touch inside the 120 ms re-arm window is counted");}
  useTracker=false;pump();check(toggleCalls==before+1,"a second touch inside the re-arm window does not fire again");

  // Edge excursions: 1 to 8 px past the row edge and back, or ending there, still activate.
  for(int e=1;e<=8;++e){
    toPlayback();before=shuffleCalls;
    pressRun(300,10,0,[e](uint32_t t,int& x,int& y){x=80;y=(t>=100&&t<200)?75+e:72;});
    check(shuffleCalls==before+1,"excursion that returns activates");
    toPlayback();before=shuffleCalls;
    pressRun(300,10,0,[e](uint32_t t,int& x,int& y){x=80;y=t>=100?75+e:72;});
    check(shuffleCalls==before+1,"release within the slop of the captured row activates");
  }
  // 14 px out: not within the slop, so releasing there does nothing; coming back re-arms; 40 px is a deliberate exit.
  const uint32_t slopBefore=cydtouch::stats().slop;
  toPlayback();before=shuffleCalls;
  pressRun(300,10,0,[](uint32_t t,int& x,int& y){x=80;y=t>=100?89:72;});
  check(shuffleCalls==before&&cydtouch::stats().slop==slopBefore+1,"release 14 px outside the row cancels and is counted");
  toPlayback();before=shuffleCalls;
  pressRun(400,10,0,[](uint32_t t,int& x,int& y){x=80;y=(t>=100&&t<250)?89:72;});
  check(shuffleCalls==before+1,"returning inside the slop re-arms the press");
  toPlayback();before=shuffleCalls;
  pressRun(400,10,0,[](uint32_t t,int& x,int& y){x=80;y=(t>=100&&t<250)?115:72;});
  check(shuffleCalls==before,"a deliberate far exit cancels for good, even after returning");
  // Header Back retains its captured target when released just outside its right edge.
  home();tapTile(5);   // Settings Back returns Home on release.
  pressRun(200,10,0,[](uint32_t t,int& x,int& y){x=t<60?99:106;y=18;});
  check(classic_depth()==1&&label("Playlists")&&label("Search"),"Back released 7 px beyond its edge still goes Home");
  std::cout<<"PASS CY-62 tolerant release: 1-8 px excursions return or end near, 14 px outside cancels and re-arms, 40 px exits for good, neighbour not retargeted\n";

  // Lists: row tap with horizontal jitter, sideways and vertical claims, true scrolling.
  toSongs();int songCalls=calls;pressRun(150,10,0,[](uint32_t t,int& x,int& y){x=80+((t/10)%2?-10:10);y=66;});
  check(classic_depth()==3&&calls>songCalls,"horizontal jitter in a scrolling list does not suppress the row tap");
  const uint32_t claimBefore=cydtouch::stats().claim;
  toSongs();songCalls=calls;pressRun(150,10,0,[](uint32_t t,int& x,int& y){x=80+((t/10)%2?-24:24);y=66;});
  check(classic_depth()==2&&calls==songCalls&&cydtouch::stats().claim==claimBefore+1,"a large sideways swing claims the press and is counted");
  toSongs();songCalls=calls;pressRun(150,10,0,[](uint32_t t,int& x,int& y){x=80+(t>=50?3:0);y=t>=50?52:66;});
  check(classic_depth()==2&&calls==songCalls,"a mostly vertical 14 px move claims the press");
  toSongs();check(classic_top()==0,"Songs page starts at the top");
  pressRun(300,10,0,[](uint32_t t,int& x,int& y){x=80;y=int(150-std::min<uint32_t>(t,200)*120/200);});
  check(classic_top()==2&&classic_depth()==2,"a true vertical drag scrolls the list instead of activating a row");
  std::cout<<"PASS CY-62 lists: jitter under 16 px keeps the row tap, sideways/vertical drags claim, vertical drag scrolls\n";

  // Wake: the whole touch is swallowed, the next one works.
  toPlayer();g_settings.screenTimeout=2;sleep_begin(clockNow);ui_tick(clockNow+=61000);check(backlight==0,"screen off before the wake touch");
  const uint32_t wakeBefore=cydtouch::stats().wake;before=toggleCalls;
  pressRun(100,10,0,at(120,246));
  check(toggleCalls==before&&backlight==100&&cydtouch::stats().wake==wakeBefore+1,"wake touch is swallowed whole and counted");
  pressRun(100,10,0,at(120,246));check(toggleCalls==before+1,"the next touch operates the transport");
  g_settings.screenTimeout=0;ui_tick(clockNow+=1000);
  std::cout<<"PASS CY-62 wake swallow unchanged under the real classifier\n";
}
unsigned classic_depth();int classic_top();


int classic_volumeSaveArms();uint32_t classic_volumeSaveAt();int classic_target(int,int);uint32_t classic_seekPreviewMs();
static void nowPlayingRegression(){
  namespace L=cydclassic::layout;
  for(unsigned rotation:{0u,1u,3u}){
    tft.setRotation(rotation);toPlayer();
    const int w=tft.width(),h=tft.height();
    const Rect track=L::playerVolumeTrack<Rect>(w,h),hit=L::playerVolume<Rect>(w,h),seek=L::seek<Rect>(w,h),bar=L::progress<Rect>(w,h);
    check(hit.h>=40&&seek.h==40&&bar.h==8,"40 px seek and volume targets, 8 px track");
    check(L::favourite<Rect>(w).w>=40&&L::favourite<Rect>(w).h>=40,"favourite target at least 40 px");
    for(Rect r:{hit,seek,bar,L::output<Rect>(w,h),L::transport<Rect>(w,h,0),L::transport<Rect>(w,h,1),L::transport<Rect>(w,h,2)})check(r.x>=0&&r.y>=0&&r.x+r.w<=w&&r.y+r.h<=h,"Player rectangles fit each orientation");
    if(w==320){const Rect prev=L::transport<Rect>(w,h,0),play=L::transport<Rect>(w,h,1),next=L::transport<Rect>(w,h,2),chip=L::outputFace<Rect>(w,h);
      check(prev.x==56&&prev.y==148&&prev.w==70&&prev.h==48&&play.x==130&&play.y==146&&play.w==80&&play.h==52&&next.x==214,"transport matches proposal A SVG");
      check(track.x==44&&track.y==216&&track.w==102&&chip.x==190&&chip.y==205&&chip.w==124&&chip.h==30,"bottom row: CY-85 widened the output chip to 124 px (was 76) for 9 pt route names; the volume track is 102 px (was 150)");
      check(bar.x==14&&bar.y==116&&bar.w==292,"seek track matches proposal A SVG (CY-85 moved it up 2 px so the 9 pt time labels clear the knob)");
    }
    check(classic_target(hit.x,hit.y)==-25&&classic_target(hit.x+hit.w-1,hit.y+hit.h-1)==-25,"volume hit target includes outer edges");
    check(classic_target(-1,hit.cy())==-1&&classic_target(w,hit.cy())==-1&&classic_target(track.x,h)==-1,"outside screen never hits volume");
    check(classic_target(seek.x,seek.y)==-28&&classic_target(seek.x+seek.w-1,seek.y+seek.h-1)==-28,"seek hit zone includes outer edges");
    check(classic_target(seek.x-1,seek.cy())==-1&&classic_target(seek.x+seek.w,seek.cy())==-1,"outside seek hit zone stays inactive");
    check(L::sliderValue(track.x-1,track.x,track.w,100)==0&&L::sliderValue(track.x+track.w+1,track.x,track.w,100)==100,"volume values clamp outside the visible track");
    check(L::sliderValue(bar.x-1,bar.x,bar.w,bar.w)==0&&L::sliderValue(bar.x+bar.w+1,bar.x,bar.w,bar.w)==bar.w,"seek values clamp outside the visible track");
    const int arms=classic_volumeSaveArms(),saves=settingsSaves,callsBefore=volumeCalls;
    tx=track.x;ty=track.cy();down=true;ui_tick(clockNow+=130);
    check(volume==0&&volumeCalls==callsBefore+1,"volume sets minimum in one touch-down event");
    int last=volume;for(int x=track.x+1;x<=track.x+track.w;++x){tx=x;ui_tick(clockNow+=10);check(volume>=last,"volume drag monotonic");last=volume;}
    check(volume==100&&classic_volumeSaveArms()==arms&&settingsSaves==saves,"live drag reaches max with no save arm or write per step");
    down=false;ui_touch(TOUCH_TAP,tx,ty);check(classic_volumeSaveArms()==arms+1,"volume save arms once at drag end");SCREEN_CLASSIC.tick(classic_volumeSaveAt()-1);check(settingsSaves==saves,"save remains deferred before deadline");SCREEN_CLASSIC.tick(classic_volumeSaveAt());check(settingsSaves==saves+1,"one deferred settings save after release");pump();
    audio_setVolume(65);tft.labels.clear();ui_tick(clockNow+=260);check(label("65"),"AVRCP shared audio volume repaints slider and numeric value");
    tap(track.x+track.w,track.cy());check(volume==100,"slider can raise shared AVRCP volume");
    int seeks=seekCalls;tx=bar.x;ty=bar.cy();down=true;ui_tick(clockNow+=130);check(classic_seekPreviewMs()==0&&seekCalls==seeks,"seek down previews zero without committing");
    uint32_t lastTime=0;for(int x=bar.x+1;x<=bar.x+bar.w;++x){tx=x;ui_tick(clockNow+=10);check(classic_seekPreviewMs()>=lastTime,"seek drag monotonic");lastTime=classic_seekPreviewMs();}
    check(lastTime==239999&&label("3:59"),"seek end clamp and preview time update while dragging");
    down=false;ui_touch(TOUCH_TAP,tx,ty);pump();check(seekCalls==seeks+1&&lastSeek==239999,"seek commits once through existing seek path");
    // The final up may arrive without a move; near slop accepts 7 px, 9 px rejects.
    for(int slop:{7,9}){seeks=seekCalls;SCREEN_CLASSIC.press(PRESS_DOWN,bar.x,bar.cy());SCREEN_CLASSIC.press(PRESS_UP,bar.x,seek.y+seek.h-1+slop);check(seekCalls==seeks+(slop==7),"seek tolerant release boundary");}
    const Rect f=L::favourite<Rect>(w);bool favourite=playlist_isFavourite(current);pressRun(200,10,0,[=](uint32_t t,int& x,int& y){x=t<60?f.x:f.x-7;y=18;});check(playlist_isFavourite(current)!=favourite,"favourite star toggles");tap(f.cx(),18);
    const Rect sh=L::playerToggle<Rect>(w,h,false),rep=L::playerToggle<Rect>(w,h,true);bool wasShuffle=shuffle;tap(sh.cx(),sh.cy());check(shuffle!=wasShuffle,"shuffle toggle");const int repeat=repeatMode;tap(rep.cx(),rep.cy());check(repeatMode==(repeat+1)%REPEAT_MODE_COUNT,"repeat toggle");
    const Rect play=L::transport<Rect>(w,h,1);const int toggles=toggleCalls,pressedFaces=tft.colorCount(theme().pressed);tx=play.cx();ty=play.cy();down=true;ui_tick(clockNow+=130);check(toggleCalls==toggles+1,"transport fires once on down");check(tft.colorCount(theme().pressed)>pressedFaces,"transport visibly pressed on touch-down");down=false;ui_touch(TOUCH_LONG_PRESS,tx,ty);pump();check(toggleCalls==toggles+1,"transport release does nothing");
    const Rect output=L::output<Rect>(w,h);pressRun(200,10,0,[=](uint32_t t,int& x,int& y){x=t<60?output.x:output.x-7;y=output.cy();});check(label("Audio Output"),"output chip opens Output page");
    tap(20,20);const Rect options=L::options<Rect>(w);tap(options.cx(),18);check(label("Options")&&label("Favourite"),"Options remains reachable from top bar");tap(20,20);
    ui_redraw();pump();snapshot(rotation==0?"cy61-player-portrait.svg":rotation==1?"cy61-player-landscape.svg":"cy61-player-landscape3.svg");
  }
  tft.setRotation(0);home();tapTile(5);tap(120,300); // Settings -> mini-player text
  ui_redraw();pump();check(label("Settings"),"Player Back label names a Settings parent, not Home");
  tap(20,18);check(label("Audio Output"),"Back returns to the named Settings parent");home();
  std::cout<<"CY61_NOWPLAYING_OK: rotations 0/1/3, SVG geometry, slider/seek edges and monotonic drag, live shared volume, one deferred save, down-only transport, favourite, shuffle, repeat, Output and Options\n";
}
unsigned classic_depth();int classic_top();


static void miniPlayerRegression(){
  namespace L=cydclassic::layout;
  for(unsigned rotation:{0u,1u,2u,3u}){
    tft.setRotation(rotation);toSongs();
    const int w=tft.width(),h=tft.height(),capacity=L::rows(h);
    const Rect play=L::mini<Rect>(w,h,0),next=L::mini<Rect>(w,h,2),title=L::mini<Rect>(w,h,1);
    check(play.w==52&&play.h==44&&next.x==w-52&&title.w==w-104,"mini-player rectangles match proposal D");
    check(classic_target(play.x,play.y)==-21&&classic_target(next.x+next.w-1,next.y+next.h-1)==-22,"mini transport outer edges");
    check(classic_target(title.x,title.y)==-14&&classic_target(title.x+title.w-1,title.y+title.h-1)==-14,"mini title outer edges");
    for(auto button:{std::make_pair(play,&toggleCalls),std::make_pair(next,&nextCalls)}){
      const int before=*button.second,faces=tft.colorCount(theme().pressed);
      tx=button.first.cx();ty=button.first.cy();down=true;ui_tick(clockNow+=130);
      check(*button.second==before+1&&tft.colorCount(theme().pressed)>faces,"mini transport acts and paints pressed face on down");
      down=false;ui_touch(TOUCH_LONG_PRESS,tx,ty);pump();check(*button.second==before+1,"mini release and hold never repeat");
    }
    for(int expected:{std::min(10-capacity,capacity-1),std::min(10-capacity,2*(capacity-1)),10-capacity,10-capacity}){dragListRows(capacity-1);check(classic_top()==expected,"drag advances pixels and clamps at end");}
    const int oldTop=classic_top();const unsigned depth=classic_depth();
    for(int i=0;i<10;++i){tap(title.cx(),title.cy());check(classic_depth()==depth,"mini opens Playing without stack growth");tap(20,18);check(classic_depth()==depth&&classic_top()==oldTop,"Back restores exact list position and depth");}
    // Return from Player Options to an existing Playing page instead of nesting it.
    tap(title.cx(),title.cy());tap(L::options<Rect>(w).cx(),18);tap(title.cx(),title.cy());check(classic_depth()==depth,"mini returns to existing Playing through Options");tap(20,18);
    for(int expected:{std::max(0,oldTop-capacity+1),0,0}){dragListRows(-(capacity-1));check(classic_top()==expected,"drag moves back and clamps at start");}
    ui_redraw();pump();snapshot(rotation==0?"cy79-mini-portrait.svg":rotation==1?"cy79-mini-landscape.svg":rotation==2?"cy79-mini-portrait2.svg":"cy79-mini-landscape3.svg");
    // Count actual recorder calls; a settled 1 Hz tick can only fill the two progress rectangles.
    SCREEN_CLASSIC.tick(clockNow+=1100);tft.resetDrawCounts();positionMs+=1000;SCREEN_CLASSIC.tick(clockNow+=1000);
    check(tft.fillCalls==2&&tft.rowFillCalls==0&&tft.labels.size()==tft.labelsAtReset,"progress tick redraws only the 2 px line, no rows or text");
    tft.resetDrawCounts();SCREEN_CLASSIC.tick(clockNow+=999);check(tft.fillCalls==0,"progress never draws faster than 1 Hz");
    SCREEN_CLASSIC.tick(++clockNow);check(tft.fillCalls==2,"next progress line draws at one second");positionMs=84000;
    // Home remains available beside Back on deep catalogue pages; release slop cannot retarget Back.
    home();tapTile(1);tap(80,56);tap(80,56);check(classic_depth()==4,"artist songs reaches depth four");
    const Rect homeHit=L::deepHome<Rect>();check(classic_target(homeHit.x,homeHit.y)==-11&&classic_target(homeHit.x+homeHit.w-1,homeHit.h-1)==-11,"deep Home outer edges");
    SCREEN_CLASSIC.press(PRESS_DOWN,homeHit.x,homeHit.cy());check(classic_depth()==4,"Home waits for release");
    SCREEN_CLASSIC.press(PRESS_UP,homeHit.x-7,homeHit.cy());SCREEN_CLASSIC.touch(TOUCH_TAP,homeHit.x-7,homeHit.cy());pump();check(classic_depth()==1,"deep Home uses captured-target tolerant release");
    const int savedLength=queueLength;queueLength=0;ui_redraw();pump();check(label("Shuffle all")&&classic_target(play.cx(),play.cy())==-30,"empty queue shows Shuffle all across mini strip");
    snapshot(rotation==1?"cy79-mini-empty.svg":"cy79-mini-home.svg");const int commands=calls;
    tx=title.cx();ty=title.cy();down=true;ui_tick(clockNow+=130);check(calls==commands,"Shuffle all waits for release");down=false;ui_touch(TOUCH_TAP,tx,ty);pump();
    check(queueLength>0&&playing&&shuffle&&calls>commands,"empty-queue action shuffles full library through existing queue helper");
    if(!queueLength)queueLength=savedLength;
  }
  tft.setRotation(0);home();std::cout<<"CY79_MINI_PLAYER_OK: down-only transport, pressed feedback, stack-free Playing return, drag clamps, empty queue, line-only 1 Hz progress, all rotations\n";
}
// CY-80: Home tiles, flatter navigation and swipe-back, driven through the real press hooks.
static bool dragRun(int x0,int y0,std::initializer_list<std::pair<int,int>> moves){
  bool claimed=SCREEN_CLASSIC.press(PRESS_DOWN,x0,y0);int ex=x0,ey=y0;
  for(const auto& m:moves){claimed|=SCREEN_CLASSIC.press(PRESS_MOVE,m.first,m.second);ex=m.first;ey=m.second;}
  SCREEN_CLASSIC.press(PRESS_UP,ex,ey);
  if(!claimed)SCREEN_CLASSIC.touch(TOUCH_TAP,x0,y0); // the UI drops the classifier event of a claimed press
  pump();return claimed;
}
static void homeTilesRegression(){
  namespace L=cydclassic::layout;
  const char* names[]={"Songs","Artists","Albums","Playlists","Search","Settings"};
  const int depthAfter[]={2,2,2,2,3,2};
  for(unsigned rotation:{0u,1u,2u,3u}){
    tft.setRotation(rotation);home();
    const int w=tft.width(),h=tft.height(),cols=L::tileCols(h);
    check((w==320)==(cols==3)&&L::TileCount==6,"3 x 2 tiles on landscape, 2 x 3 on portrait");
    Rect t[6];for(int i=0;i<6;++i)t[i]=L::tile<Rect>(w,h,i);
    if(w==320){const int ex[6][4]={{8,44,100,72},{112,44,100,72},{216,44,100,72},{8,120,100,72},{112,120,100,72},{216,120,100,72}};
      for(int i=0;i<6;++i)check(t[i].x==ex[i][0]&&t[i].y==ex[i][1]&&t[i].w==ex[i][2]&&t[i].h==ex[i][3],"landscape tiles match proposal C");}
    else{const int ex[6][4]={{8,44,112,73},{124,44,112,73},{8,121,112,73},{124,121,112,73},{8,198,112,73},{124,198,112,73}};
      for(int i=0;i<6;++i)check(t[i].x==ex[i][0]&&t[i].y==ex[i][1]&&t[i].w==ex[i][2]&&t[i].h==ex[i][3],"portrait tiles are 2 x 3");}
    const int miniTop=L::miniY(h);
    for(int i=0;i<6;++i){
      check(t[i].w>=80&&t[i].h>=64,"each tile is at least 80 x 64");
      check(t[i].x>=0&&t[i].x+t[i].w<=w&&t[i].y>=L::HeaderH&&t[i].y+t[i].h<=miniTop,"tile lies between header and mini-player");
      for(int j=i+1;j<6;++j)check(t[i].x+t[i].w+L::TileGap<=t[j].x||t[j].x+t[j].w+L::TileGap<=t[i].x||t[i].y+t[i].h+L::TileGap<=t[j].y||t[j].y+t[j].h+L::TileGap<=t[i].y,"tiles keep their gap");
      const int id=-50-i;
      check(classic_target(t[i].cx(),t[i].cy())==id,"tile centre");
      check(classic_target(t[i].x,t[i].y)==id&&classic_target(t[i].x+t[i].w-1,t[i].y)==id&&classic_target(t[i].x,t[i].y+t[i].h-1)==id&&classic_target(t[i].x+t[i].w-1,t[i].y+t[i].h-1)==id,"tile outer corners");
      check(classic_target(t[i].x-1,t[i].cy())!=id&&classic_target(t[i].x+t[i].w,t[i].cy())!=id&&classic_target(t[i].cx(),t[i].y-1)!=id&&classic_target(t[i].cx(),t[i].y+t[i].h)!=id,"one pixel outside a tile is not that tile");
    }
    for(int i=0;i<6;++i)for(int j=0;j<6;++j)if(i!=j){
      const bool across=t[i].y==t[j].y&&t[j].x==t[i].x+t[i].w+L::TileGap,below=t[i].x==t[j].x&&t[j].y==t[i].y+t[i].h+L::TileGap;
      if(across)for(int g=0;g<L::TileGap;++g)check(classic_target(t[i].x+t[i].w+g,t[i].cy())==-1,"gap between columns is ignored");
      if(below)for(int g=0;g<L::TileGap;++g)check(classic_target(t[i].cx(),t[i].y+t[i].h+g)==-1,"gap between rows is ignored");
    }
    check(classic_target(w-1,t[cols-1].cy())==-1||classic_target(w-1,t[cols-1].cy())==-50-(cols-1),"Home has no paging strip over its right tiles");
    check(classic_target(t[cols-1].cx(),t[cols-1].cy())==-50-(cols-1),"the right tile column is not swallowed by the list strip");
    const Rect play=L::mini<Rect>(w,h,0);check(classic_target(play.cx(),play.cy())==-21||classic_target(play.cx(),play.cy())==-30,"mini-player stays below the tiles");

    // Navigation depth after each tile and after Back; Music is gone.
    for(int i=0;i<6;++i){
      home();check(classic_depth()==1,"Home is the root");tapTile(i);
      check(classic_depth()==depthAfter[i],"tile goes straight to its page");
      if(i!=4)check(label(names[i]),"destination header names the tile");
      check(!label("Music"),"the intermediate Music page is not shown");
      for(int back=0;classic_depth()>1&&back<4;++back)tap(20,20);
      check(classic_depth()==1&&label("Playlists")&&label("Settings"),"Back from each tile reaches Home");
    }
    home();tapTile(3);check(label("Favourites")&&label("Folders"),"Playlists lists Favourites and Folders");
    const Rect row1=L::row<Rect>(w,1),row0=L::row<Rect>(w,0);
    tap(row1.cx(),row1.cy());check(classic_depth()==3&&label("Folders"),"Playlists > Folders");tap(20,20);check(classic_depth()==2,"Back to Playlists");
    if(!playlist_isFavourite(0))playlist_toggleFavourite(0);
    tap(row0.cx(),row0.cy());check(classic_depth()==3&&label("Favourites"),"Playlists > Favourites");tap(20,20);tap(20,20);check(classic_depth()==1,"Back to Home");
    // The old Library/Sources entry points land on Home instead of a Music list.

    // Pressed feedback on touch-down; the action waits for a tolerant release.
    home();const Rect a=t[0];const int paints=tft.colorCount(theme().pressed);
    SCREEN_CLASSIC.press(PRESS_DOWN,a.cx(),a.cy());
    check(tft.colorCount(theme().pressed)>paints&&classic_depth()==1,"tile shows pressed ink on touch-down and does not act yet");
    SCREEN_CLASSIC.press(PRESS_UP,a.x+a.w-1+7,a.cy());SCREEN_CLASSIC.touch(TOUCH_TAP,a.cx(),a.cy());pump();
    check(classic_depth()==2,"tile acts on release 7 px beyond its edge");
    home();SCREEN_CLASSIC.press(PRESS_DOWN,a.cx(),a.cy());SCREEN_CLASSIC.press(PRESS_UP,a.cx(),a.y+a.h-1+14);SCREEN_CLASSIC.touch(TOUCH_TAP,a.cx(),a.cy());pump();
    check(classic_depth()==1,"release 14 px outside cancels the tile");
    home();SCREEN_CLASSIC.press(PRESS_DOWN,a.cx(),a.cy());SCREEN_CLASSIC.press(PRESS_MOVE,a.x+a.w+30,a.cy());SCREEN_CLASSIC.press(PRESS_MOVE,a.cx(),a.cy());SCREEN_CLASSIC.press(PRESS_UP,a.cx(),a.cy());SCREEN_CLASSIC.touch(TOUCH_TAP,a.cx(),a.cy());pump();
    check(classic_depth()==1,"a far excursion cancels the tile for good");
    home();const Rect b=t[1];SCREEN_CLASSIC.press(PRESS_DOWN,a.x+a.w+1,a.cy());SCREEN_CLASSIC.press(PRESS_UP,b.cx(),b.cy());SCREEN_CLASSIC.touch(TOUCH_TAP,a.x+a.w+1,a.cy());pump();
    check(classic_depth()==1,"a press that lands in a gap never reaches a tile");
    home();SCREEN_CLASSIC.press(PRESS_DOWN,a.x+a.w-1,a.cy());SCREEN_CLASSIC.press(PRESS_UP,b.x,a.cy());SCREEN_CLASSIC.touch(TOUCH_TAP,a.x+a.w-1,a.cy());pump();
    check(classic_depth()==2&&label("Songs"),"slop never retargets the neighbouring tile");
    // Jittered finger inside the tile still activates exactly once (CY-62 harness).
    home();int before=calls;
    pressRun(200,10,0,[=](uint32_t tm,int& x,int& y){x=t[2].cx()+((tm/10)%2?-6:6);y=t[2].cy()+int((tm/10)%3)*3-3;});
    check(classic_depth()==2&&label("Albums"),"jittered tile tap opens Albums once");

    // Swipe-back: at least 40 px right, horizontal lead, from a list body. Exact boundary through the press hook.
    toSongs();const Rect r0=L::row<Rect>(w,0);const int sy=r0.cy(),sx=60;before=calls;
    dragRun(sx,sy,{{sx+20,sy},{sx+39,sy}});
    check(classic_depth()==2&&calls==before,"39 px drag neither goes Back nor plays the row");
    check(dragRun(sx,sy,{{sx+20,sy},{sx+40,sy}})&&classic_depth()==1&&calls==before,"40 px swipe right is Back and swallows the row tap");
    toSongs();check(dragRun(sx,sy,{{sx+100,sy+30}})&&classic_depth()==1&&calls==before,"horizontal-dominant diagonal 100 x 30 is Back");
    toSongs();dragRun(sx,sy,{{sx+60,sy+60}});check(classic_depth()==2&&calls==before,"45 degree drag is not a swipe");
    toSongs();dragRun(sx,sy,{{sx-60,sy}});check(classic_depth()==2&&calls==before,"leftward swipe is not Back");
    toSongs();dragRun(sx,sy,{{sx+8,sy-12},{sx+60,sy-12}});check(classic_depth()==2&&classic_top()==0,"a vertical lead latches list ownership: no swipe afterwards");
    toSongs();dragRun(sx,sy+30,{{sx+10,sy-30}});check(classic_depth()==2&&classic_top()==1,"vertical drag scrolls the list");
    toSongs();dragRun(sx,sy,{{sx+30,sy+9},{sx+70,sy-9}});check(classic_depth()==1,"9 px of vertical jitter does not block a swipe");
    // Jittered harness inputs through the classifier: Back, scroll and tap each win in their own case.
    for(uint32_t period:{10u,80u}){
      toSongs();before=calls;
      pressRun(300,period,0,[=](uint32_t tm,int& x,int& y){x=sx+int(std::min<uint32_t>(tm,200)*90/200);y=sy+((tm/10)%2?5:-5);});
      check(classic_depth()==1&&calls==before,"swipe with 90 px travel and 5 px jitter goes Back once without playing");
      toSongs();before=calls;
      pressRun(300,period,0,[=](uint32_t tm,int& x,int& y){x=sx+((tm/10)%2?6:-6);y=int(sy+60-std::min<uint32_t>(tm,200)*100/200);});
      check(classic_depth()==2&&classic_top()>0&&calls==before,"vertical scroll with sideways jitter stays on the list");
      toSongs();before=calls;
      pressRun(150,period,0,[=](uint32_t tm,int& x,int& y){x=sx+((tm/10)%2?-6:6);y=sy+((tm/10)%3?2:-2);});
      check(classic_depth()==3&&calls>before,"small jitter is still a row tap");
    }
    // Pages that do not scroll still take the swipe, and the row under the finger does not fire.
    toPlayback();const int shuffles=shuffleCalls;dragRun(sx,sy,{{sx+30,sy},{sx+80,sy}});
    check(classic_depth()==2&&shuffleCalls==shuffles,"Playback swipes back to Settings without cycling Shuffle");
    // Not started in a list body: header, mini-player and the paging strip are exempt; the Back button remains.
    toSongs();dragRun(sx,L::HeaderH-10,{{sx+80,L::HeaderH-10}});check(classic_depth()==2,"a header drag is not a swipe");
    const Rect mt=L::mini<Rect>(w,h,1);dragRun(mt.x+10,mt.cy(),{{mt.x+80,mt.cy()}});check(classic_depth()==2&&label("Songs"),"a mini-player drag is neither a swipe nor Now Playing");
    toSongs();tap(20,20);check(classic_depth()==1,"the Back button still works");
    // Seek bar: dragging across it seeks and never goes Back.
    toPlayer();const int depth=classic_depth();const Rect bar=L::seek<Rect>(w,h);int seeks=seekCalls;
    dragRun(bar.x+10,bar.cy(),{{bar.x+60,bar.cy()},{bar.x+160,bar.cy()}});
    check(classic_depth()==depth&&seekCalls==seeks+1,"a horizontal drag on the seek bar seeks and is not Back");
    // A tile swipe has no parent to go to and must not open anything.
    home();dragRun(a.cx(),a.cy(),{{a.cx()+40,a.cy()},{a.cx()+90,a.cy()}});check(classic_depth()==1,"swiping a tile on Home opens nothing");
    ui_redraw();pump();home();
    snapshot(rotation==0?"cy80-home-portrait.svg":rotation==1?"cy80-home-landscape.svg":rotation==2?"cy80-home-portrait2.svg":"cy80-home-landscape3.svg");
  }
  tft.setRotation(0);home();
  std::cout<<"CY80_HOME_TILES_OK: six tiles 3x2 / 2x3 with hit-tests (centre, corners, edges, gaps), depth after each tile and Back, Music removed, Playlists holds Favourites/Folders, pressed ink on down + tolerant release, swipe-back vs list scroll vs tile tap vs seek, jittered harness inputs, all rotations\n";
}
// Linear Rec.709 luminance, normalized to 0..1 after RGB565 expansion.
static double luminance(uint16_t c){
  auto linear=[](double v){return v<=0.04045?v/12.92:std::pow((v+0.055)/1.055,2.4);};
  return 0.2126*linear(((c>>11)&31)/31.0)+0.7152*linear(((c>>5)&63)/63.0)+0.0722*linear((c&31)/31.0);
}
static std::string routeDrawing(Icon icon,uint16_t ink){
  namespace L=cydclassic::layout;
  tft.fillScreen(theme().bg);const auto start=tft.drawing().size();
  ui_icon(icon,tft.width()-L::RouteInset,L::HeaderTextY,L::RouteSize,ink,theme().band);
  return tft.drawing().substr(start);
}
static void compactChromeRegression(){
  namespace L=cydclassic::layout;
  for(unsigned rotation:{0u,1u,2u,3u}){
    tft.setRotation(rotation);const int w=tft.width(),h=tft.height();
    check(L::RowTitleY-TITLE_STRIP_H/2>=0&&L::RowTitleY+TITLE_STRIP_H/2<=L::RowDetailY-TITLE_STRIP_H/2&&L::RowDetailY+TITLE_STRIP_H/2<L::RowH,"native UTF-8 title/detail strips fit without overlap or touching the row rule");
    check(L::HeaderH==36&&L::RowH==40&&L::rows(h)==(w==320?4:6),"compact header, four landscape / six portrait rows");
    const int right=w-L::HeaderTitleRight;
    check(L::HeaderParentX+(L::BackW-L::HeaderParentX)<=L::BackW&&right<=w-L::RouteInset-L::RouteSize/2,"parent, title and route have disjoint bounds");
    home();tapTile(0);
    check(label("Home")&&!label("< Back")&&classic_depth()==2,"Songs header Back names Home");
    for(int row=0;row<L::rows(h);++row){const Rect r=L::row<Rect>(w,row);
      check(classic_target(r.x,r.y)==row&&classic_target(r.x+r.w-1,r.y+r.h-1)==row,"row outer edges follow the 40 px hit grid");
    }
    check(classic_target(L::BackW-1,L::HeaderH-1)==-10&&classic_target(L::BackW,L::HeaderH-1)==-1,"Back hit ends at the parent-label boundary");
    snapshot(rotation==0?"cy78-songs-portrait.svg":rotation==1?"cy78-songs-landscape.svg":rotation==2?"cy78-songs-portrait2.svg":"cy78-songs-landscape3.svg");
    // Parent labels are release actions with existing CY-62 slop, including the enlarged right edge.
    const int commands=calls;
    SCREEN_CLASSIC.press(PRESS_DOWN,L::BackW-1,L::HeaderTextY);
    check(label("Home")&&classic_depth()==2&&calls==commands,"Back does not navigate or play on down");
    SCREEN_CLASSIC.press(PRESS_UP,L::BackW+6,L::HeaderTextY);SCREEN_CLASSIC.touch(TOUCH_TAP,L::BackW+6,L::HeaderTextY);pump();
    check(classic_depth()==1&&label("Artists")&&label("Albums"),"Back releases within 7 px slop to the named Home parent");
    home();tapTile(5);tap(80,L::row<Rect>(w,2).cy());
    check(label("Settings")&&label("Shuffle"),"Playback header names its Settings parent");
    tap(L::HeaderBackX,L::HeaderTextY);check(label("Audio Output"),"Back returns to Settings");
  }
  tft.setRotation(1);g_settings.outRoute=OUT_BT;
  double minimum=1;
  {
    const Palette& p=theme();const double difference=std::abs(luminance(p.text)-luminance(p.band));minimum=std::min(minimum,difference);
    check(difference>0.25,"every theme header text has linear luminance difference > 0.25 from band");
    check(std::abs(luminance(p.dim)-luminance(p.band))>0.08,"every theme dimmed route has luminance difference > 0.08 from band");
    btConnected=true;const auto connected=routeDrawing(ICON_BLUETOOTH,p.text);home();
    check(tft.drawing().find(connected)!=std::string::npos&&!label("BT")&&!label("BT..."),"connected route uses the Bluetooth icon in header text ink");
    check(tft.drawing().find("x='0' y='0' width='320' height='36'")!=std::string::npos,"header paints one flat 36 px band");
    btConnected=false;const auto reconnecting=routeDrawing(ICON_BLUETOOTH,p.dim);home();
    check(tft.drawing().find(reconnecting)!=std::string::npos,"reconnecting Bluetooth icon is dimmed");
    btConnected=true;ui_tick(clockNow+=1100);
    check(tft.drawing().find(connected)!=std::string::npos,"Bluetooth reconnection refreshes the icon without page navigation");
#if CYD_WIRED_OUTPUT
    g_settings.outRoute=OUT_WIRED;const auto headphones=routeDrawing(ICON_HEADPHONES,p.text);const int radio=btCalls;home();
    check(tft.drawing().find(headphones)!=std::string::npos&&btCalls==radio,"Headphones icon swaps without touching Bluetooth");
    snapshot("cy78-headphones-home.svg");g_settings.outRoute=OUT_BT;
#endif
    std::cout<<"CY78_HEADER_CONTRAST Classic Touch luminance_difference="<<difference<<"\n";
  }
  std::cout<<"CY78_COMPACT_CHROME_OK: all rotations, 4/6 rows, parent Back on release, both route icons, reconnect dimming, flat band; the one theme, text luminance difference="<<minimum<<" (>0.25)\n";
  tft.setRotation(0);home();
}
std::string classic_geometry();std::string classic_dialog_geometry();
static void geometryGolden(){
  const auto dialog=classic_dialog_geometry();if(std::getenv("CYDPOD_REGENERATE_LAYOUT"))std::ofstream("../golden/classic_dialog.txt")<<dialog;
  std::ifstream d("../golden/classic_dialog.txt");check(dialog==std::string((std::istreambuf_iterator<char>(d)),{}),"Confirmation dialog geometry differs");
  const auto actual=classic_geometry();std::ofstream("classic_layout.actual.txt")<<actual;
  if(std::getenv("CYDPOD_REGENERATE_LAYOUT")){std::ofstream("../golden/classic_layout.txt")<<actual;}
  std::ifstream f("../golden/classic_layout.txt");std::string expected((std::istreambuf_iterator<char>(f)),{});
  check(actual==expected,"Classic geometry differs from approved golden table");
  std::cout<<"CLASSIC_GEOMETRY_GOLDEN: 17 modes x 4 variants x rotations 0/1/2/3, 240x320 and 320x240\n";
}

// ---------------------------------------------------------------------------
// INT-1: labelled render inventory. Every reachable Classic page, in all four rotations,
// recorded through the same host drawing recorder; layout evidence only (host Arial, not the
// device bitmap font). Files land in render/ next to the test binary with an inventory.tsv.
// ---------------------------------------------------------------------------
struct RenderEntry{std::string file,page,state,labels;unsigned rotation;int w,h;};
static std::vector<RenderEntry> renderLog;
static void shot(const char* page,const char* state,const char* expect,bool fresh=true){
  if(fresh){ui_redraw();pump();}  // a full repaint, so the SVG and the label list describe exactly this page
  const std::string tag=std::string(page)+"/"+state+" r"+std::to_string(tft.getRotation());
  if(expect&&!shown(expect)){std::string seen;for(const auto& l:tft.labels)seen+="["+l+"]";check(false,("render inventory marker '"+std::string(expect)+"' missing in "+tag+"; labels: "+seen).c_str());}
  const std::string file=std::string("render/")+page+"-"+state+"-r"+std::to_string(tft.getRotation())+".svg";
  tft.save(file.c_str());
  std::string labels;int n=0;for(const auto& l:tft.labels){if(n++)labels+="|";labels+=l;if(n>=16)break;}
  renderLog.push_back({file,page,state,labels,tft.getRotation(),tft.width(),tft.height()});
}
static void tapRow(int i){const Rect r=cydclassic::layout::row<Rect>(tft.width(),i);tap(r.cx(),r.cy());}
static void tapRect(const Rect& r){tap(r.cx(),r.cy());}
// Settings has 7 rows: past the visible ones, page down first and use the real scroll position.
static void tapSettingsRow(int idx){
  namespace L=cydclassic::layout;
  if(idx>=classic_top()+L::rows(tft.height()))dragListRows(idx-classic_top());
  tapRow(idx-classic_top());
}
static void tapBackButton(){tap(cydclassic::layout::HeaderBackX,cydclassic::layout::HeaderTextY);}
// Keys are found through the real hit map; 1000 is the key target base in classic_screen.cpp.
static bool findKey(char c,int& x,int& y){
  for(int yy=cydclassic::layout::KeyY;yy<tft.height();yy+=4)for(int xx=0;xx<tft.width();xx+=4)if(classic_target(xx,yy)==1000+c){x=xx;y=yy;return true;}
  return false;
}
static void typeQuery(const char* s){
  namespace L=cydclassic::layout;
  for(;*s;++s){int x=0,y=0;
    if(!findKey(*s,x,y))tapRect(L::keyDigits<Rect>(tft.width(),tft.height()));
    check(findKey(*s,x,y),(std::string("search key reachable through the hit map: ")+*s+" r"+std::to_string(tft.getRotation())).c_str());tap(x,y);
  }
  pump();
}
// CY-63: drive the real screen press/touch hooks without pump() hiding the debounce boundary.
static void searchTap(const Rect& r){
  SCREEN_CLASSIC.press(PRESS_DOWN,r.cx(),r.cy());SCREEN_CLASSIC.press(PRESS_UP,r.cx(),r.cy());
  SCREEN_CLASSIC.touch(TOUCH_TAP,r.cx(),r.cy());
}
static void searchKey(char c){int x=0,y=0;check(findKey(c,x,y),"session test key in real hit map");searchTap({x,y,1,1});}
static uint64_t searchIO(){return hostOpenCalls+hostReadCalls+hostWriteCalls+hostCloseCalls;}
static void searchStep(uint32_t elapsed=1){SCREEN_CLASSIC.tick(clockNow+=elapsed);}
static void searchSettle(){for(int i=0;i<100&&storage_searchBusy();++i)searchStep();check(!storage_searchBusy()&&!storage_searchFailed(),"session scan completes");}
static std::vector<uint16_t> activeQueue(){std::vector<uint16_t> q;for(int i=0;i<audio_queueLen();++i)q.push_back(uint16_t(audio_queueTrack(i)));return q;}
const char* classic_query();unsigned classic_mode();
static void qwertyRegression(){
  namespace L=cydclassic::layout;
  const std::string letters="qwertyuiopasdfghjklzxcvbnm",symbols="0123456789-/:;()&@.,?!'\"_+=";
  unsigned cases=0;
  for(unsigned rotation:{0u,1u,2u,3u}){
    tft.setRotation(rotation);home();tapTile(4);
    const int w=tft.width(),h=tft.height();
    const Rect page=L::keyDigits<Rect>(w,h),clear=L::keyAction<Rect>(w,h,2),space=L::keyAction<Rect>(w,h,1),done=L::keyAction<Rect>(w,h,3),del=L::keyBack<Rect>(w,h);
    if(classic_target(L::key<Rect>(w,h,0,false).cx(),L::KeyY)!=1000+'q')searchTap(page);
    searchTap(clear);searchStep(150);
    for(int cell=0;cell<4;++cell){const Rect r=L::keyAction<Rect>(w,h,cell);
      for(int x:{r.x,r.cx(),r.x+r.w-1})for(int y:{r.y,r.cy(),r.y+r.h-1})
        check(classic_target(x,y)==-41-cell,"control centres/edges/gaps have exact owners");
    }
    for(int x:{del.x,del.cx(),del.x+del.w-1})for(int y:{del.y,del.cy(),del.y+del.h-1})check(classic_target(x,y)==-40,"Delete edges own their gap");
    check(classic_target(-1,L::KeyY)==-1&&classic_target(w,L::KeyY)==-1&&classic_target(0,L::KeyY-1)==-1,"outside and query strip never type");
    // Tolerant control release stays with the captured page key across the Space boundary.
    SCREEN_CLASSIC.press(PRESS_DOWN,page.x+page.w-1,page.cy());check(label("123"),"page waits for release");
    SCREEN_CLASSIC.press(PRESS_UP,page.x+page.w+6,page.cy());SCREEN_CLASSIC.touch(TOUCH_TAP,page.x+page.w+6,page.cy());
    check(classic_target(0,L::KeyY)==1000+'0'&&!classic_query()[0],"page release slop toggles once without neighbouring Space");searchTap(page);
    for(bool digits:{false,true}){
      if(digits)searchTap(page);
      const auto& chars=digits?symbols:letters;
      check(chars.size()==(digits?27u:26u),"bounded keyboard table lengths");
      for(size_t i=0;i<chars.size();++i){
        const Rect r=L::key<Rect>(w,h,int(i),digits);
        check(r.w>=w/10&&r.h>0&&r.y>=L::KeyY&&r.y+r.h<=L::footerY(h),"maximized key geometry inside keyboard");
        // Every face corner and its inset gap belongs to exactly this key, including boundaries.
        for(int x:{r.x,r.cx(),r.x+r.w-1})for(int y:{r.y,r.cy(),r.y+r.h-1}){
          check(classic_target(x,y)==1000+chars[i],"key centres/edges/gaps have one exact owner");
          searchTap(clear);const uint64_t io=searchIO();
          SCREEN_CLASSIC.press(PRESS_DOWN,x,y);
          check(std::string(classic_query())==std::string(1,chars[i]),"character echoes once on down");
          // Cross into a neighbour, hold, return and release: capture cannot retarget or repeat.
          for(int n=0;n<4;++n)SCREEN_CLASSIC.press(PRESS_MOVE,(x+w/10)%w,y);
          SCREEN_CLASSIC.press(PRESS_UP,x,y);SCREEN_CLASSIC.touch(TOUCH_LONG_PRESS,x,y);
          check(std::string(classic_query())==std::string(1,chars[i])&&searchIO()==io,"adjacent jitter/hold/release never doubles or opens storage");++cases;
        }
      }
      searchTap(clear);
      const uint64_t io=searchIO();
      for(char c:chars)searchKey(c);
      check(std::string(classic_query())==chars&&searchIO()==io,"actual target burst types exact table without storage work");
      searchTap(page);check(std::string(classic_query())==chars,"page switching preserves query");searchTap(page);
      // Control keys stay release actions with captured-target slop.
      SCREEN_CLASSIC.press(PRESS_DOWN,del.cx(),del.cy());check(std::string(classic_query())==chars,"Delete waits for release");
      SCREEN_CLASSIC.press(PRESS_UP,del.cx(),del.cy());SCREEN_CLASSIC.touch(TOUCH_TAP,del.cx(),del.cy());
      check(std::string(classic_query())==chars.substr(0,chars.size()-1),"Delete removes one byte");
      searchTap(space);check(std::string(classic_query())==chars.substr(0,chars.size()-1)+" ","Space inserts once");
      searchTap(clear);check(!classic_query()[0],"Clear resets text");
    }
    searchTap(page); // return from symbols to QWERTY
    searchTap(clear);searchStep(150);const uint64_t io=searchIO();
    for(int i=0;i<35;++i)searchKey('q');
    check(std::string(classic_query())==std::string(31,'q')&&searchIO()==io,"31-byte query cap, no per-key storage");
    searchTap(clear);ui_go(SCR_CLASSIC); // dismiss the expected full-query toast before classifier input
    const int recals=recalCount;
    const Rect q=L::key<Rect>(w,h,0,false);
    for(uint32_t duration:{1u,650u,4500u}){
      searchTap(clear);pressRun(duration,10,0,at(q.cx(),q.cy()));
      check(std::string(classic_query())=="q"&&recalCount==recals,"real classifier short/long/recal hold emits exactly one character");
    }
    searchTap(clear);searchStep(150);const uint64_t burstIO=searchIO(),burstOpens=hostOpenCalls;
    for(char c:std::string("song")){searchKey(c);searchStep(20);}searchStep(129);
    check(searchIO()==burstIO,"QWERTY burst waits for final 150 ms deadline in each rotation");searchStep();
    check(hostOpenCalls==burstOpens+1&&storage_searchCount()==8,"one warm applied query per QWERTY burst in each rotation");
    const int progress=storage_searchProgress();const uint64_t retainedIO=searchIO();
    searchTap(done);ui_redraw();
    check(storage_searchProgress()==progress&&searchIO()==retainedIO,"Done/redraw retain in-flight progress without opens in each rotation");
    searchStep(); // paint the Edit row through the real one-dirty-row scheduler before tapping it
    const uint64_t editIO=searchIO();const int editProgress=storage_searchProgress();searchTap(L::row<Rect>(w,0));
    check(classic_mode()==15&&std::string(classic_query())=="song"&&storage_searchProgress()==editProgress&&searchIO()==editIO,"Edit retains same-query progress without opens in each rotation");
    searchSettle();searchTap(clear);searchStep(150);
    std::cout<<"CY66_GEOMETRY rotation="<<rotation<<" key="<<w/10<<"x"<<q.h<<" rows="<<L::KeyY<<".."<<L::footerY(h)<<" Delete="<<del.w<<"x"<<del.h<<" controls="<<page.w<<"/"<<space.w<<"/"<<clear.w<<"/"<<done.w<<"\n";
  }
  tft.setRotation(0);home();
  std::cout<<"PASS CY-66 QWERTY: "<<cases<<" centres/edges/gaps; all letters/digits/punctuation, captured adjacent jitter, held/released keys, classifier 1/650/4500 ms, controls/slop, page retention, 31-byte bound, one query per burst, retained progress and no per-key storage; all rotations\n";
}
static void searchSessionRegression(){
  namespace L=cydclassic::layout;
  tft.setRotation(0);g_settings.screenTimeout=0;home();tapTile(4);
  const Rect clear=L::keyAction<Rect>(tft.width(),tft.height(),2),done=L::keyAction<Rect>(tft.width(),tft.height(),3);
  const Rect back{L::HeaderBackX,L::HeaderTextY,1,1},edit=L::row<Rect>(tft.width(),0),hit=L::row<Rect>(tft.width(),1);
  searchTap(clear);searchStep(150);
  const auto queue=activeQueue();const int commands=calls;const uint64_t emptyIO=searchIO();
  for(int i=0;i<3;++i){searchTap(clear);searchTap(L::keyBack<Rect>(tft.width(),tft.height()));searchStep(200);}
  searchTap(done);ui_redraw();searchStep();searchTap(edit);searchStep();
  check(searchIO()==emptyIO,"empty Clear/Delete/Done/navigation performs zero storage work");
  // Cold build: edits echo immediately, nothing opens until the final 150 ms deadline.
  SD.remove("/.sys/search.idx");const uint64_t coldIO=searchIO();
  for(char c:std::string("song")){tft.labels.clear();searchKey(c);check(label(c=='s'?"s":c=='o'?"so":c=='n'?"son":"song"),"edit echoes before tick or storage");searchStep(40);}
  searchStep(109);check(searchIO()==coldIO&&!storage_searchBusy(),"burst waits through 149 ms after newest edit");
  searchStep();check(storage_searchBuilding()&&storage_searchProgress()==40,"only final burst query starts once at 150 ms");
  const int built=storage_searchProgress();const uint64_t busyIO=searchIO();
  searchTap(back);ui_redraw();
  check(searchIO()==busyIO&&storage_searchProgress()==built&&storage_searchBuilding(),"unchanged Back/redraw retain busy cursor with no reopen");
  // Leaving both pages cancels the query, but reentry resumes the backend's partial build.
  searchTap(back);check(!storage_searchBusy(),"leaving Search cancels scan");
  const uint64_t cancelledIO=searchIO();for(int i=0;i<3;++i)searchStep();check(searchIO()==cancelledIO&&!storage_searchBusy(),"off-page tick never advances cancelled build");
  const uint64_t reopen=hostOpenCalls;searchTap(L::tile<Rect>(tft.width(),tft.height(),4));
  check(hostOpenCalls==reopen,"Home/reentry queues work without opening files in touch");
  searchStep();check(storage_searchBuilding()&&storage_searchProgress()==80,"reentry resumes partial index instead of rebuilding first records");
  searchSettle();check(storage_searchCount()==10,"settled final burst is song, not an intermediate edit");
  const uint64_t finishedIO=searchIO();const int finishedProgress=storage_searchProgress();
  for(int i=0;i<3;++i){searchTap(done);ui_redraw();searchStep();searchTap(edit);searchTap(back);searchStep();searchTap(edit);searchStep();}
  check(searchIO()==finishedIO&&storage_searchProgress()==finishedProgress&&storage_searchCount()==10,"unchanged completed navigation retains count/cursor with no storage work");
  // Returning to applied text before idle also retains the session, even across Done.
  searchKey('x');searchTap(L::keyBack<Rect>(tft.width(),tft.height()));searchTap(done);searchStep();searchTap(edit);
  check(searchIO()==finishedIO&&storage_searchCount()==10,"reverted edit keeps completed query without redundant start");
  // Warm scan at rollover: one open, 8/10 records per step, no start on intermediate text.
  searchTap(clear);searchStep(150);searchStep();clockNow=UINT32_MAX-90;searchStep(0);
  const uint64_t warmOpen=hostOpenCalls,warmIO=searchIO();
  searchKey('s');searchStep(50);searchKey('o');searchStep(149);
  check(searchIO()==warmIO,"deadline survives rollover and still waits 149 ms");searchStep();
  check(hostOpenCalls==warmOpen+1&&storage_searchProgress()==80&&storage_searchCount()==8,"one warm open applies latest text at wrapped deadline");
  const uint64_t scanOpen=hostOpenCalls;const int scanCount=storage_searchCount(),scanProgress=storage_searchProgress();
  searchTap(done);ui_redraw();
  check(hostOpenCalls==scanOpen&&storage_searchProgress()==scanProgress&&storage_searchCount()==scanCount,"unchanged warm scan navigation retains cursor and hits");
  searchStep();searchSettle();searchTap(edit);
  // Back while a new edit is pending must not expose old query hits or permit a queue commit.
  searchTap(clear);searchKey('z');searchTap(back);check(storage_searchCount()==10,"old backend hits retained privately until apply");
  searchTap(hit);check(calls==commands&&activeQueue()==queue,"pending old-query row cannot commit playing queue");
  searchStep(149);check(storage_searchCount()==10,"pending query does not advance obsolete backend scan");
  searchStep();searchSettle();check(storage_searchCount()==0,"newest miss replaces old hits");
  for(int i=0;i<6;++i)searchStep();check(label("No matches"),"obsolete hits do not paint as new-query results");
  searchTap(hit);check(calls==commands&&activeQueue()==queue,"no-match row cannot commit old results");
  // Done bypasses idle and applies in the next tick; starts never run in the handler.
  searchTap(edit);searchTap(clear);searchKey('s');const uint64_t immediateOpen=hostOpenCalls;
  searchTap(done);check(hostOpenCalls==immediateOpen,"Done itself performs no storage opens");searchStep();
  check(hostOpenCalls==immediateOpen+1&&storage_searchCount()==8,"Done immediately applies pending text on next tick before 150 ms");searchSettle();
  // I/O failure retains an explicit error; redraw does not retry, unchanged Done does.
  searchTap(edit);searchKey('o');hostReadBudget=0;searchTap(done);searchStep();hostReadBudget=-1;
  check(storage_searchFailed()&&!storage_searchBusy(),"failed scan enters error state");
  const uint64_t errorIO=searchIO();ui_redraw();searchStep();check(searchIO()==errorIO,"error redraw never starts an implicit retry");
  for(int i=0;i<6;++i)searchStep();searchTap(edit);searchTap(done);searchStep();searchSettle();
  check(!storage_searchFailed()&&storage_searchCount()==10&&hostOpenCalls>immediateOpen+1,"unchanged Done explicitly retries failed query");
  // Queue identity and all entries stay intact until an explicit valid row selection.
  check(calls==commands&&activeQueue()==queue,"all session edits/cancellation/error retries preserve playing queue");
  const std::vector<uint16_t> expected(storage_searchTracks(),storage_searchTracks()+storage_searchCount());
  for(int i=0;i<8;++i)searchStep();searchTap(hit);
  check(calls>commands&&activeQueue()==expected&&current==expected[0],"valid selection commits entire intended results queue and selected track");
  // Leaving Keyboard directly through Home cancels too; restored completed text restarts safely.
  home();tapTile(4);check(storage_searchBusy()||storage_searchCount()==10,"cancelled completed query can be reentered");
  classic_home();ui_go(SCR_CLASSIC);check(!storage_searchBusy(),"leaving Keyboard cancels query");
  tapTile(4);searchKey('z');searchTap(back);searchTap(back);searchTap(L::tile<Rect>(tft.width(),tft.height(),4));
  const uint64_t pendingReentry=hostOpenCalls;searchStep(149);
  check(hostOpenCalls==pendingReentry,"leaving/reentering retains newest edit deadline rather than bypassing idle");
  searchStep();searchSettle();check(storage_searchCount()==0,"pending reentry applies latest text after its deadline");
  classic_home();ui_go(SCR_CLASSIC);
  std::cout<<"PASS CY-63 Search session: immediate echo, 150 ms burst/rollover, tick-only Done, retained busy/completed cursor/count/no opens, empty no-I/O, cancel/resume, error retry, obsolete-hit and full queue isolation\n";
}
static void tapDialogCancel(){
  const int W=tft.width(),H=tft.height(),w=std::min(W-SP_L*2,296),h=150,rx=(W-w)/2,ry=(H-h)/2,bh=44,by=ry+h-SP_M-bh,bw=(w-SP_M*3)/2;
  tap(rx+SP_M+bw/2,by+bh/2);
}
static void tapDialogOk(){
  const int W=tft.width(),H=tft.height(),w=std::min(W-SP_L*2,296),h=150,rx=(W-w)/2,ry=(H-h)/2,bh=44,by=ry+h-SP_M-bh,bw=(w-SP_M*3)/2;
  tap(rx+SP_M*2+bw+bw/2,by+bh/2);
}
// CY-81: the Settings tree after the legacy Setup screen was retired. Every row that used to be
// reachable only through Setup and is still wanted lives here; the retired rows are gone.
static void settingsRegression(){
  namespace L=cydclassic::layout;
  const unsigned rotations[]={0,1,2,3};
  for(unsigned rotation:rotations){
    tft.setRotation(rotation);g_settings.rotation=uint8_t(rotation);g_settings.outRoute=OUT_BT;
    home();tapTile(5);
    check(label("Audio Output")&&label("Bluetooth")&&label("Playback")&&label("Display"),"Settings shows Output, Bluetooth, Playback and Display");
    tapSettingsRow(4);check(shown("Library & System"),"Library & System page title");
    for(const char* kept:{"Refresh Library","Upload music","Web passcode","Language"})check(label(kept),(std::string("System row kept: ")+kept).c_str());
    for(const char* retired:{"Style","Layout","Background","Invert colours","Panel type","Panel / Advanced","Upload / Advanced","Theme","Flip portrait"})
      check(!label(retired),(std::string("retired row absent: ")+retired).c_str());
  }
  // Orientation visits all four rotations and returns to where it started (the old Flip portrait
  // could not leave portrait once on the landscape W328C).
  tft.setRotation(1);g_settings.rotation=1;home();tapTile(5);tapRow(3);
  check(label("Brightness -")&&label("Brightness +")&&label("Orientation")&&label("Landscape"),"Display shows brightness and the orientation name");
#if CYD_TOUCH_CAPACITIVE
  check(!label("Calibrate touch"),"no touch calibration row on the capacitive board");
#else
  dragListRows(1);  // CY-91: Theme took the fourth landscape row; Calibrate is on page 2
  check(label("Calibrate touch"),"touch calibration row on the resistive board");
  dragListRows(-1);
#endif
  const uint8_t expectedOrder[]={3,0,2,1};std::vector<uint8_t> seen;
  for(uint8_t want:expectedOrder){
    const int saves=settingsSaves;tapRow(2);
    check(g_settings.rotation==want&&tft.getRotation()==want&&settingsSaves==saves+1,"Orientation steps to the next rotation and saves once");seen.push_back(g_settings.rotation);
    check(label("Orientation"),"still on Display after rotating");
  }
  check(seen.size()==4&&g_settings.rotation==1,"four Orientation taps visit every rotation and come back to landscape");
  const int level=g_settings.brightness;tapRow(0);check(g_settings.brightness==std::max(10,level-10)&&backlight==g_settings.brightness,"Brightness - lowers and applies the backlight");
  tapRow(1);tapRow(1);check(g_settings.brightness==std::min(100,std::max(10,level-10)+20),"Brightness + raises it");
  // Library & System.
  tft.setRotation(1);g_settings.rotation=1;g_settings.lang=LANG_EN;g_settings.webPin="123456";
  home();tapTile(5);tapSettingsRow(4);check(label("123456"),"the web passcode is shown in full");
  int saves=settingsSaves;tapRow(3);check(g_settings.lang==LANG_ZH&&settingsSaves==saves+1,"Language row cycles to Chinese and saves");
  tapRow(3);check(g_settings.lang==LANG_EN&&label("English"),"Language row cycles back");
  tapRow(2);tapDialogCancel();check(g_settings.webPin=="123456","cancelling the passcode reset keeps the passcode");
  tapRow(2);tapDialogOk();check(g_settings.webPin=="000000","confirming resets the web passcode");
  tapRow(1);check(label("Uploader not installed")&&classic_depth()==3,"Upload music without the uploader says so and stays put");
  tap(20,20);
  tapRow(0);check(label("Rebuild and restart"),"Refresh Library opens its confirmation from Library & System");
  tft.setRotation(0);g_settings.rotation=0;g_settings.lang=LANG_EN;
  std::cout<<"PASS CY-81 Settings: live rows kept, retired rows absent in all rotations, Orientation visits all four rotations, brightness, Library & System rows (refresh, upload, passcode reset, language)\n";
}
// CY-91: the Theme row and the colour palettes, focused. Light stays v24 byte for byte; Black and High
// Contrast keep the production role pairs legible; one release cycles the theme once, a cancelled press
// does nothing, and the change repaints the whole page with none of the old palette left behind.
static double themeContrast(uint16_t a,uint16_t b){
  auto lin=[](int v){const double c=v/255.0;return c<=0.03928?c/12.92:std::pow((c+0.055)/1.055,2.4);};
  auto lum=[&](uint16_t c){int r=(c>>11)&31,g=(c>>5)&63,bl=c&31;r=r<<3|r>>2;g=g<<2|g>>4;bl=bl<<3|bl>>2;return .2126*lin(r)+.7152*lin(g)+.0722*lin(bl);};
  const double x=lum(a),y=lum(b);return (std::max(x,y)+.05)/(std::min(x,y)+.05);
}
static int themePixels(int x0,int y0,int w,int h,uint16_t c){int n=0;for(int y=y0;y<y0+h;++y)for(int x=x0;x<x0+w;++x)n+=tft.pixel(x,y)==c;return n;}
static Palette themeOf(uint8_t id){const uint8_t was=g_settings.colorTheme;g_settings.colorTheme=id;const Palette p=theme();g_settings.colorTheme=was;return p;}
static void themeRegression(){
  namespace L=cydclassic::layout;
  static_assert(sizeof(Palette)%sizeof(uint16_t)==0,"Palette is uint16_t fields only");
  const unsigned fields=sizeof(Palette)/sizeof(uint16_t);
  // Light is the v24 palette exactly (the three CY-91 roles repeat what those screens used before).
  const uint16_t v24[]={0xFFFF,0xD6BA,0xF79E,0x1082,0x5ACB,0x9CD3,0x8C71,0xE71C,0xE71C,0x1082,0x1B59,0xFFFF,0x3C5C,0x1B59,0xFFFF,0xD6DB,0x3C5B,0x0480,0xBA80,0xC000,0x1082,0x8C71,0x1B59};
  static_assert(sizeof(v24)==sizeof(Palette),"v24 table covers every role");
  const Palette light=themeOf(THEME_LIGHT);
  check(!memcmp(&light,v24,sizeof(v24)),"Light palette is v24 byte for byte");
  g_settings.colorTheme=200;{const Palette invalid=theme();check(!memcmp(&invalid,v24,sizeof(v24)),"an invalid theme id draws Light");}
  check(themeSanitize(3)==THEME_LIGHT&&themeSanitize(255)==THEME_LIGHT&&themeNext(2)==THEME_LIGHT&&themeNext(0)==THEME_BLACK,"theme id is bounded to three values");
  g_settings.colorTheme=THEME_LIGHT;
  // Production role pairs for the two new palettes (Light is the approved legacy table).
  struct Pair{const char* name;uint16_t Palette::*a;uint16_t Palette::*b;double floor;};
  const Pair pairs[]={
    {"text/bg",&Palette::text,&Palette::bg,4.5},{"dim/bg",&Palette::dim,&Palette::bg,4.5},{"disabled/bg",&Palette::disabled,&Palette::bg,3},
    {"text/band",&Palette::text,&Palette::band,4.5},{"dim/band",&Palette::dim,&Palette::band,4.5},{"disabled/band",&Palette::disabled,&Palette::band,3},
    {"text/raised",&Palette::text,&Palette::surfaceRaised,4.5},{"dim/raised",&Palette::dim,&Palette::surfaceRaised,4.5},
    {"btnText/raised",&Palette::btnText,&Palette::surfaceRaised,4.5},{"btnText/btn",&Palette::btnText,&Palette::btn,4.5},{"dim/btn",&Palette::dim,&Palette::btn,4.5},
    {"onControlActive/btnActive",&Palette::onControlActive,&Palette::btnActive,4.5},{"onControlActive/pressed",&Palette::onControlActive,&Palette::pressed,4.5},
    {"onPressed/pressed",&Palette::onPressed,&Palette::pressed,4.5},{"onAccent/accent",&Palette::onAccent,&Palette::accent,4.5},
    {"accent/bg",&Palette::accent,&Palette::bg,4.5},{"bar/barBg",&Palette::bar,&Palette::barBg,3},{"bar/bg",&Palette::bar,&Palette::bg,3},
    {"miniFill/miniTrack",&Palette::miniFill,&Palette::miniTrack,3},{"miniTrack/band",&Palette::miniTrack,&Palette::band,2},
    {"outline/bg",&Palette::outline,&Palette::bg,3},{"outline/band",&Palette::outline,&Palette::band,3},
    {"good/bg",&Palette::good,&Palette::bg,4.5},{"warn/bg",&Palette::warn,&Palette::bg,4.5},{"bad/bg",&Palette::bad,&Palette::bg,4.5},
    {"pressed/band",&Palette::pressed,&Palette::band,1.3},{"pressed/bg",&Palette::pressed,&Palette::bg,1.3}};
  double worst[THEME_COUNT]={0,99,99};
  for(uint8_t id:{THEME_BLACK,THEME_HIGH_CONTRAST}){
    const Palette p=themeOf(id);
    for(const Pair& pair:pairs){const double c=themeContrast(p.*pair.a,p.*pair.b);worst[id]=std::min(worst[id],c/pair.floor);
      check(c>=pair.floor,(std::string(themeName(id))+" "+pair.name+" contrast "+std::to_string(c)).c_str());}
    check(p.bg==0,"Black and High Contrast backgrounds are pure black");
  }
  // The row, in every rotation: label, cycle once on release, nothing on cancel, full repaint.
  for(unsigned rotation:{0u,1u,2u,3u}){
    tft.setRotation(rotation);g_settings.rotation=uint8_t(rotation);g_settings.outRoute=OUT_BT;g_settings.colorTheme=THEME_LIGHT;
    home();tapTile(5);tapSettingsRow(3);
    check(label("Brightness -")&&label("Orientation")&&label("Theme")&&label("Light"),"Display shows the Theme row and its value");
    const int w=tft.width(),h=tft.height();const Rect row=L::row<Rect>(w,3);
    for(int step=0;step<4;++step){
      const uint8_t was=g_settings.colorTheme,want=themeNext(was);const Palette old=themeOf(was),next=themeOf(want);
      int saves=settingsSaves;
      SCREEN_CLASSIC.press(PRESS_DOWN,row.cx(),row.cy());SCREEN_CLASSIC.press(PRESS_UP,row.cx(),row.cy()+60);pump();
      check(g_settings.colorTheme==was&&settingsSaves==saves&&label(themeName(was)),"a press that leaves the row does not change the theme");
      saves=settingsSaves;SCREEN_CLASSIC.press(PRESS_DOWN,row.cx(),row.cy());check(g_settings.colorTheme==was,"Theme waits for release");
      SCREEN_CLASSIC.press(PRESS_UP,row.cx(),row.cy());SCREEN_CLASSIC.touch(TOUCH_TAP,row.cx(),row.cy());pump();
      check(g_settings.colorTheme==want&&settingsSaves==saves+1,"one release cycles the theme once and saves once");
      check(label("Theme")&&label(themeName(want))&&!label(themeName(was))&&label("Orientation"),"the row cache is rebuilt with the new value");
      {const uint16_t got[4]={tft.pixel(row.x+row.w-3,row.cy()),tft.pixel(w-3,2),tft.pixel(w-L::StripW+2,L::HeaderH+1),tft.pixel(2,h-3)};
       const uint16_t exp[4]={next.bg,next.band,next.bg,next.band};
       for(int k=0;k<4;++k)check(got[k]==exp[k],("page/header/strip/footer sample "+std::to_string(k)+" took the new palette: r"+std::to_string(rotation)+" "+std::to_string(got[k])+" vs "+std::to_string(exp[k])).c_str());}
      check(!queueLength||tft.pixel(w/2,L::footerY(h))==next.miniTrack||tft.pixel(w/2,L::footerY(h))==next.miniFill,"mini-player progress line takes the new palette");
      const uint16_t* a=reinterpret_cast<const uint16_t*>(&old);const uint16_t* b=reinterpret_cast<const uint16_t*>(&next);
      for(unsigned f=0;f<fields;++f){
        bool kept=false;for(unsigned g=0;g<fields;++g)kept=kept||a[f]==b[g];
        if(!kept){const int left=themePixels(0,0,w,h,a[f]);check(left<100,("no old-palette rectangle or strip survives the repaint (a few anti-aliasing blends may coincide): role "+std::to_string(f)+" "+std::to_string(left)+" px, theme "+themeName(was)+" -> "+themeName(want)+" r"+std::to_string(rotation)).c_str());}
      }
    }
    check(g_settings.colorTheme==THEME_BLACK,"four releases step Light, Black, High Contrast, Light, Black");
    g_settings.colorTheme=THEME_LIGHT;
  }
  // Selected/pressed spot check: a pressed Home tile shows its ink on the pressed face in every theme.
  for(uint8_t id:{THEME_LIGHT,THEME_BLACK,THEME_HIGH_CONTRAST}){
    tft.setRotation(1);g_settings.rotation=1;g_settings.colorTheme=id;const Palette p=theme();
    home();const Rect t=L::tile<Rect>(tft.width(),tft.height(),0);
    SCREEN_CLASSIC.press(PRESS_DOWN,t.cx(),t.cy());
    check(themePixels(t.x+2,t.y+2,t.w-4,t.h-4,p.pressed)>t.w*t.h/3&&themePixels(t.x,t.y,t.w,t.h,p.onPressed)>30,"pressed tile shows its ink on the pressed face");
    SCREEN_CLASSIC.press(PRESS_UP,t.cx(),t.cy()+80);pump();
  }
  g_settings.colorTheme=THEME_LIGHT;tft.setRotation(1);g_settings.rotation=1;
  std::cout<<"PASS CY-91 themes: Light is v24, Black/HC role contrast (smallest margin Black "<<worst[THEME_BLACK]<<"x, HC "<<worst[THEME_HIGH_CONTRAST]<<"x of floor), Theme row cycles once on release in all rotations, cancel does nothing, full repaint leaves no old-palette strip, pressed ink\n";
}
// CY-91 previews: production renders of the real screens, written only when CYDPOD_THEME_PREVIEW names a directory.
static void themePreviews(){
  const char* dir=std::getenv("CYDPOD_THEME_PREVIEW");if(!dir)return;
  namespace L=cydclassic::layout;
  std::filesystem::create_directories(dir);
  const char* names[]={"light","black","hc"};
  auto save=[&](const std::string& name){check(tft.savePng((std::string(dir)+"/"+name+".png").c_str(),1),"theme preview PNG");};
  for(uint8_t id:{THEME_LIGHT,THEME_BLACK,THEME_HIGH_CONTRAST}){
    g_settings.colorTheme=id;tft.setRotation(1);g_settings.rotation=1;
    toPlayer();ui_redraw();pump();save(std::string("player-")+names[id]+"-r1");
    home();tapTile(5);tapSettingsRow(3);save(std::string("display-")+names[id]+"-r1");
    home();const Rect t=L::tile<Rect>(tft.width(),tft.height(),0);SCREEN_CLASSIC.press(PRESS_DOWN,t.cx(),t.cy());save(std::string("pressed-tile-")+names[id]+"-r1");
    SCREEN_CLASSIC.press(PRESS_UP,t.cx(),t.cy()+80);pump();
    toSongs();{const Rect r=L::row<Rect>(tft.width(),1);SCREEN_CLASSIC.press(PRESS_DOWN,r.cx(),r.cy());save(std::string("pressed-row-")+names[id]+"-r1");SCREEN_CLASSIC.press(PRESS_UP,r.cx(),r.cy()+80);pump();}
    tft.setRotation(0);g_settings.rotation=0;
    toPlayer();ui_redraw();pump();save(std::string("player-")+names[id]+"-r0");
    home();tapTile(5);tapSettingsRow(3);save(std::string("display-")+names[id]+"-r0");
  }
  g_settings.colorTheme=THEME_LIGHT;tft.setRotation(1);g_settings.rotation=1;
}
// CY-81: the Bluetooth page used to return with ui_go(SCR_PLAYER), which redirected into Classic.
// It now calls classic_return(); every way out must land where it did before: Settings when
// Settings opened the page, Now Playing when the page was shown at boot.
unsigned classic_mode();const char* classic_modeName(unsigned);
static void bluetoothReturns(){
  const auto mode=[]{return std::string(classic_modeName(classic_mode()));};
  for(unsigned rotation:{0u,1u,2u,3u}){
    tft.setRotation(rotation);g_settings.rotation=uint8_t(rotation);g_settings.outRoute=OUT_BT;btConnected=true;btPairingNow=false;btCancelOk=true;
    // Opened from Settings: Back returns to Settings.
    home();tapTile(5);tapRow(1);check(ui_current()==SCR_BLUETOOTH,"Settings opens the Bluetooth page");
    tap(10,10);check(ui_current()==SCR_CLASSIC&&mode()=="Settings"&&classic_depth()==2,"Back from Bluetooth returns to Settings");
    // Opened from Settings while pairing: success leaves the page by itself, to Settings.
    btConnected=false;btPairingNow=true;
    tapRow(1);check(ui_current()==SCR_BLUETOOTH,"Settings opens the Bluetooth page while pairing");
    btConnected=true;btPairingNow=false;ui_tick(clockNow+=1000);pump();
    check(ui_current()==SCR_CLASSIC&&mode()=="Settings"&&classic_depth()==2,"successful pairing returns to Settings");
    // Back while pairing cancels the session and returns; a refused cancel keeps the page.
    btConnected=false;btPairingNow=true;btCancelOk=false;
    tapRow(1);tap(10,10);check(ui_current()==SCR_BLUETOOTH&&label("Unable to cancel pairing"),"a refused cancel stays on the Bluetooth page");
    tap(10,10);btCancelOk=true;tap(10,10);   // the first tap only dismisses the toast
    check(ui_current()==SCR_CLASSIC&&mode()=="Settings"&&classic_depth()==2,"Back cancels pairing and returns to Settings");
    btConnected=true;btPairingNow=false;
    // Shown at boot (no speaker known), with Home as the only Classic page: Back opens Now Playing.
    classic_home();ui_go(SCR_BLUETOOTH);pump();check(ui_current()==SCR_BLUETOOTH,"boot shows the Bluetooth page");
    tap(10,10);check(ui_current()==SCR_CLASSIC&&mode()=="Player"&&classic_depth()==2,"Back from the boot Bluetooth page opens Now Playing");
    // Shown at boot, pairing succeeds: Now Playing as well.
    btConnected=false;btPairingNow=true;classic_home();ui_go(SCR_BLUETOOTH);pump();
    btConnected=true;btPairingNow=false;ui_tick(clockNow+=1000);pump();
    check(ui_current()==SCR_CLASSIC&&mode()=="Player"&&classic_depth()==2,"successful pairing at boot opens Now Playing");
#if CYD_WIRED_OUTPUT
    // Bluetooth off (Headphones): Back returns the same way.
    g_settings.outRoute=OUT_WIRED;home();tapTile(5);tapRow(1);check(ui_current()==SCR_BLUETOOTH,"Settings opens the Bluetooth-off page");
    tap(10,10);check(ui_current()==SCR_CLASSIC&&mode()=="Settings","Back from the Bluetooth-off page returns to Settings");
    g_settings.outRoute=OUT_BT;
#endif
  }
  tft.setRotation(0);g_settings.rotation=0;home();
  std::cout<<"PASS CY-81 Bluetooth returns: Back, successful pairing, refused and accepted cancel, boot page and Bluetooth-off page, all rotations\n";
}
static void renderInventory(){
  namespace L=cydclassic::layout;
  std::filesystem::create_directories("render");
  // A second card with an accented title so Search can show the CY-64 folding end to end.
  std::ifstream input("metadata-real.m4a",std::ios::binary);std::vector<char> original((std::istreambuf_iterator<char>(input)),{});
  const std::string tagText="Actual tagged fixture";auto found=std::search(original.begin(),original.end(),tagText.begin(),tagText.end());
  check(found!=original.end(),"locate fixture title");const size_t pos=size_t(found-original.begin());
  SD.root=std::filesystem::current_path()/("render-card-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(SD.root/"music/Example");
  const char* titles[]={"Beyonc\xC3\xA9","Song 00","Song 01","Song 02","Song 03"};
  for(int i=0;i<5;++i){auto bytes=original;memset(bytes.data()+pos,0,tagText.size());memcpy(bytes.data()+pos,titles[i],strlen(titles[i]));
    char name[32];snprintf(name,sizeof(name),"Track %02d.m4a",i);std::ofstream out(SD.root/"music/Example"/name,std::ios::binary);out.write(bytes.data(),bytes.size());}
  check(storage_begin(nullptr)&&storage_trackCount()==5,"render card catalogue");playlist_begin();
  g_settings.screenTimeout=0;g_settings.outRoute=OUT_BT;btConnected=true;routeAllowed=true;
  for(unsigned rotation:{0u,1u,2u,3u}){
    tft.setRotation(rotation);queueLength=0;queueData=nullptr;playing=false;
    home();shot("home","idle-no-queue","Songs");
    tapTile(0);shot("songs","populated","Song 00");
    home();tapTile(1);shot("artists","populated","Fixture Artist");
    tapRow(0);shot("artists-child","albums-of-artist","Fixture Album");
    tapRow(1);shot("artist-album-songs","tracks-of-album","Song 00");
    home();tapTile(2);shot("albums","populated","Fixture Album");
    tapRow(0);shot("albums-child","tracks-of-album","Song 00");
    home();tapTile(3);shot("playlists","menu","Favourites");
    tapRow(0);shot("favourites","read-or-allocation-error-notice","Unable to read list or allocate memory. Try again.");  // existing message for an empty source, not an empty-browse state
    tapBackButton();tapRow(1);shot("folders","populated","Example");
    tapRow(0);shot("folder-songs","populated","Song 00");
    // Search: empty keyboard, a typed query, an accented match, a miss, and the digit page.
    home();tapTile(4);tapRect(L::keyAction<Rect>(tft.width(),tft.height(),2));
    {int x=0,y=0;if(!findKey('a',x,y))tapRect(L::keyDigits<Rect>(tft.width(),tft.height()));}
    shot("search-keyboard","letters-empty","Search Songs");
    typeQuery("beyonce");shot("search-keyboard","typed-beyonce","Search Songs");
    shot("search-keyboard","qwerty-all-rows","z");
    tapRect(L::keyAction<Rect>(tft.width(),tft.height(),3));pump();
    shot("search-results","beyonce-accented-match",nullptr);
    check(storage_searchCount()==1,"beyonce finds the accented title");
    tapRow(0);tapRect(L::keyAction<Rect>(tft.width(),tft.height(),2));typeQuery("zzz");tapRect(L::keyAction<Rect>(tft.width(),tft.height(),3));pump();
    shot("search-results","no-matches","No matches");
    tapRow(0);tapRect(L::keyDigits<Rect>(tft.width(),tft.height()));shot("search-keyboard","digits","9");tapRect(L::keyDigits<Rect>(tft.width(),tft.height()));
    // Settings tree.
    home();tapTile(5);shot("settings","menu","Audio Output");
    ui_confirm(T_FORGET_Q,T_FORGET_HINT,T_COUNT,T_FORGET,nullptr);shot("dialog","confirm-forget-device",nullptr,false);tapDialogCancel();
    ui_confirm(T_PIN_RESET_Q,T_PIN_RESET_1,T_PIN_RESET_2,T_RESET,nullptr);shot("dialog","confirm-reset-pin",nullptr,false);tapDialogCancel();
    home();tapTile(5);tapRow(0);shot("output","bluetooth-selected","Audio Output");
#if CYD_WIRED_OUTPUT
    g_settings.outRoute=OUT_WIRED;ui_redraw();pump();shot("output","headphones-selected","Headphones");
    ui_go(SCR_BLUETOOTH);pump();shot("bluetooth","off-headphones-route","Use Bluetooth");g_settings.outRoute=OUT_BT;
    home();tapTile(5);tapRow(0);routeAllowed=false;tapRow(1);shot("message","output-change-failed","Unable to change output. Try again.");routeAllowed=true;
#endif
    home();tapTile(5);tapRow(1);shot("bluetooth","bluetooth-route","Pair new");
    home();tapTile(5);tapRow(2);shot("playback","menu","Screen timeout");
    home();tapTile(5);tapRow(3);shot("display","menu","Brightness -");
    home();tapTile(5);tapSettingsRow(4);shot("system","menu","Web passcode");
    tapRow(0);shot("refresh-library","confirm","Rebuild and restart");
    home();tapTile(5);tapSettingsRow(5);shot("about","menu",nullptr);
    // Playing: Now Playing (volume slider and output chip), Options, mini-player on Home and in lists.
    home();tapTile(0);tapRow(1);check(playing&&ui_current()==SCR_CLASSIC,"a song row starts playback");
    shot("nowplaying","playing","Fixture Artist");
    tapRect(L::options<Rect>(tft.width()));shot("options","menu","Favourite");
    tapRow(3);shot("output","from-options-bluetooth-selected","Audio Output");
    home();shot("home","playing-mini-player","Songs");
    tapTile(0);shot("songs","with-mini-player","Song 00");
  }
  // A card with no music: the normal empty browse states (distinct from the Favourites error notice above).
  SD.root=std::filesystem::current_path()/("render-empty-card-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(SD.root/"music");
  check(storage_begin(nullptr)&&storage_trackCount()==0,"empty card catalogue");playlist_begin();
  for(unsigned rotation:{0u,1u,2u,3u}){
    tft.setRotation(rotation);queueLength=0;queueData=nullptr;playing=false;
    home();shot("home","empty-catalogue","Songs");
    tapTile(0);shot("songs","empty-catalogue",nullptr);
    home();tapTile(1);shot("artists","empty-catalogue",nullptr);
    home();tapTile(2);shot("albums","empty-catalogue",nullptr);
    home();tapTile(3);tapRow(1);shot("folders","empty-catalogue",nullptr);
  }
  tft.setRotation(0);home();
  std::ofstream tsv("render/inventory.tsv");tsv<<"file\trotation\tsize\tpage\tstate\tfirst_labels\n";
  for(const auto& e:renderLog)tsv<<e.file<<'\t'<<e.rotation<<'\t'<<e.w<<'x'<<e.h<<'\t'<<e.page<<'\t'<<e.state<<'\t'<<e.labels<<'\n';
  std::cout<<"RENDER_INVENTORY_OK: "<<renderLog.size()<<" SVG renders, rotations 0/1/2/3, see render/inventory.tsv\n";
}
// ---------------------------------------------------------------------------
// CY-81: navigation graph. Breadth-first search from Home over every hit target the real hit map
// exposes (classic_target scanned on a 4 px grid, including each scroll position of a long list;
// the Bluetooth page, which has its own touch handler, on a 16 px grid). A page class is the
// screen plus the Classic mode. Every probe replays its tap path from Home, so none sees leftover
// state. Fails if any page outside the expected set is reachable (Classic.Volume, the Setup screen
// and the old Player/Library/Sources screens are not in it), or any expected page is not,
// or a screen id outside Classic and Bluetooth ever becomes current.
// ---------------------------------------------------------------------------
unsigned classic_mode();const char* classic_modeName(unsigned);
struct NavPoint{int x,y;};
static std::string navPage(){
  switch(ui_current()){
    case SCR_CLASSIC:return std::string("Classic.")+classic_modeName(classic_mode());
    case SCR_BLUETOOTH:return "Bluetooth";
    default:return "UNKNOWN";
  }
}
static std::string navNode(){return navPage()+(ui_current()==SCR_CLASSIC?"@"+std::to_string(classic_top()):std::string());}
static unsigned navRotation=0;
static void navReset(){
  g_settings.rotation=uint8_t(navRotation);tft.setRotation(navRotation);
  queueLength=0;queueData=nullptr;playing=false;current=0;g_settings.outRoute=OUT_BT;routeAllowed=true;saveAllowed=true;btConnected=true;
  g_settings.screenTimeout=0;sleep_begin(clockNow);   // no dim or swallowed wake touch left over from a previous probe
}
static void navReplay(const std::vector<NavPoint>& path){navReset();home();for(const NavPoint& p:path){if(p.x==-1)dragListRows(p.y);else tap(p.x,p.y);}}
static std::vector<NavPoint> navTargets(){
  std::vector<NavPoint> out;std::vector<int> seen;
  if(ui_current()==SCR_BLUETOOTH){for(int y=0;y<tft.height();y+=16)for(int x=0;x<tft.width();x+=16)out.push_back({x+4,y+4});return out;}
  for(int y=0;y<tft.height();y+=4)for(int x=0;x<tft.width();x+=4){
    const int t=classic_target(x,y);if(t==-1||std::find(seen.begin(),seen.end(),t)!=seen.end())continue;
    seen.push_back(t);if(t==-12){out.push_back({-1,1});out.push_back({-1,-1});}else out.push_back({x,y});
  }
  return out;
}
static void navigationGraph(){
  std::set<std::string> nodes,classes;std::map<std::string,std::set<std::string>> edges;std::set<std::string> unknown;
  std::vector<std::vector<NavPoint>> queue{{}};
  navReplay({});nodes.insert(navNode());classes.insert(navPage());
  for(size_t head=0;head<queue.size();++head){
    const std::vector<NavPoint> path=queue[head];navReplay(path);const std::string fromNode=navNode(),from=navPage();
    for(const NavPoint& t:navTargets()){
      navReplay(path);check(navNode()==fromNode,("replay of a tap path reaches a different page: "+fromNode+" vs "+navNode()).c_str());
      if(t.x==-1)dragListRows(t.y);else tap(t.x,t.y);const std::string to=navPage(),toNode=navNode();
      if(to.rfind("Legacy.",0)==0||to=="UNKNOWN")unknown.insert(to);
      if(to!=from)edges[from].insert(to);
      classes.insert(to);
      if(toNode!=fromNode&&nodes.insert(toNode).second){auto next=path;next.push_back(t);queue.push_back(next);}
    }
  }
  std::cout<<"NAV_GRAPH rotation "<<navRotation<<": "<<classes.size()<<" pages, "<<nodes.size()<<" page/scroll states\n";
  for(const auto& e:edges){std::cout<<"  "<<e.first<<" ->";for(const auto& to:e.second)std::cout<<' '<<to;std::cout<<'\n';}
  check(unknown.empty(),"a screen outside Classic and Bluetooth became current");
  const std::set<std::string> expected{"Bluetooth","Classic.About","Classic.Catalog","Classic.Display","Classic.Home","Classic.Keyboard","Classic.Options","Classic.Output","Classic.Playback","Classic.Player","Classic.Playlists","Classic.Refresh","Classic.Search","Classic.Settings","Classic.SourceSongs","Classic.Sources","Classic.System"};
  for(const auto& want:expected)check(classes.count(want)>0,("page not reachable from Home: "+want).c_str());
  for(const auto& got:classes)check(expected.count(got)>0,("unexpected page reachable from Home: "+got).c_str());
}
unsigned classic_mode();
#include "text_quality_test.h"
#include "quiet_outline_test.h"
// Focused entry in the existing UI target: no media/pixel regression matrix.
static void headphoneLimitRegression(const char* preview){
  namespace L=cydclassic::layout;
  tft.setRotation(1);g_settings.rotation=1;g_settings.screenTimeout=0;
  g_settings.outRoute=OUT_BT;liveRoute=OUT_BT;g_settings.headphoneLimit=true;
  home();tapTile(5);tapRow(0);
#if CYD_WIRED_OUTPUT
  check(label("Headphone limit: On")&&label("Bluetooth")&&label("Headphones"),"limit row distinct from route choices");
  const int saves=limitSaves,requests=limitRequests,restarts=hostRestarts,routes=restartSaves;
  const Rect row=L::row<Rect>(tft.width(),OUT_SELECTABLE_COUNT);
  SCREEN_CLASSIC.press(PRESS_DOWN,row.cx(),row.cy());
  SCREEN_CLASSIC.press(PRESS_UP,row.cx(),row.cy()+60);SCREEN_CLASSIC.touch(TOUCH_TAP,row.cx(),row.cy()+60);pump();
  check(limitSaves==saves&&g_settings.headphoneLimit,"cancelled release does nothing");
  SCREEN_CLASSIC.press(PRESS_DOWN,row.x,row.cy());check(g_settings.headphoneLimit,"limit waits for release");
  SCREEN_CLASSIC.press(PRESS_UP,row.x-6,row.cy());SCREEN_CLASSIC.touch(TOUCH_TAP,row.x-6,row.cy());pump();
  check(!g_settings.headphoneLimit&&!appliedLimit&&limitSaves==saves+1&&limitRequests==requests+1&&label("Headphone limit: Off"),"tolerant release toggles once and publishes saved Off");
  check(g_settings.outRoute==OUT_BT&&liveRoute==OUT_BT&&hostRestarts==restarts&&restartSaves==routes,"limit under Bluetooth never switches route or restarts");
  limitSaveAllowed=false;tapRow(OUT_SELECTABLE_COUNT);
  check(!g_settings.headphoneLimit&&!appliedLimit&&limitRequests==requests+1&&label("Unable to save headphone limit. Try again."),"save failure reports and retains applied/displayed/saved choice");
  tapBackButton();limitSaveAllowed=true;tapRow(OUT_SELECTABLE_COUNT);
  check(g_settings.headphoneLimit&&appliedLimit&&label("Headphone limit: On"),"successful enable shows On");
  if(preview&&*preview)check(tft.savePng(preview,1),"production Audio Output preview");
#else
  check(!label("Headphone limit: On")&&!label("Headphone limit: Off"),"no wired limit row without wired module");
  (void)preview;
#endif
  home();tapTile(5);tapSettingsRow(3);tapRow(3);
  check(g_settings.colorTheme==THEME_BLACK&&label("Black"),"CY-91 Theme selection spot check");
  std::cout<<"PASS CY-92 UI: distinct row, tolerant release once, cancel, BT route unchanged, failed save coherent, production preview; CY-91 Theme spot check\n";
}
// CY-98a exercises the cache-only renderer independently of future pixel input.
int classic_offset();void classic_paintViewport(int);void classic_invalidateViewport();int classic_cacheSlots();
static void viewportRegression(){
  namespace L=cydclassic::layout;
  check(classic_cacheSlots()==L::MaxRows+1,"one extra partial-row cache slot");
  for(unsigned rotation:{0u,1u}){
    g_settings.rotation=rotation;tft.setRotation(rotation);toSongs();tap(80,56);tap(20,20);
    const int w=tft.width(),h=tft.height(),capacity=L::rows(h);
    for(int offset:{1,39,40,81}){
      std::vector<uint16_t> chrome;
      for(int y=0;y<h;++y)if(y<L::HeaderH||y>=L::footerY(h))for(int x=0;x<w;++x)chrome.push_back(tft.pixel(x,y));
      const auto io=searchIO();tft.labels.clear();classic_paintViewport(offset);
      check(classic_offset()==offset&&searchIO()==io,"pixel paint keeps offset and performs no catalogue IO");
      size_t at=0;for(int y=0;y<h;++y)if(y<L::HeaderH||y>=L::footerY(h))for(int x=0;x<w;++x)check(tft.pixel(x,y)==chrome[at++],"partial rows clip before header/footer");
      check(classic_target(20,L::HeaderH)==offset/L::RowH&&classic_target(20,L::HeaderH+L::RowH-offset%L::RowH)==offset/L::RowH+1,"partial row hit boundaries agree with pixels");
      classic_invalidateViewport();check(classic_offset()==offset,"invalidation preserves fractional offset");pump();
    }
    classic_paintViewport(1);tapMini();tap(20,20);check(classic_offset()==1,"mini-player return retains pixel offset");
    tap(20,20);tapTile(0);classic_paintViewport(1);ui_redraw();pump();check(classic_offset()==1,"re-enter retains pixel offset");
    classic_invalidateViewport();tft.labels.clear();const auto io=searchIO();classic_paintViewport(1);
    check(!label("Song 00")&&searchIO()==io,"missing rows paint title-less placeholders without reads");pump();
    tft.labels.clear();classic_paintViewport(1);check(label("Song 00")&&label(capacity==6?"Song 06":"Song 04"),"loader recovers both partial edges");
    classic_paintViewport(100000);check(classic_offset()==(10-capacity)*L::RowH,"offset clamps to last full viewport");
    classic_paintViewport(-10);check(classic_offset()==0,"offset clamps at zero");
    dragListRows(capacity-1);
    check(classic_offset()==std::min(10-capacity,capacity-1)*L::RowH,"drag helper maps to pixel multiple");
  }
  home();tapTile(4);typeQuery("song");tapRect(L::keyAction<Rect>(tft.width(),tft.height(),3));searchSettle();pump();
  classic_paintViewport(41);tapMini();tap(20,20);searchSettle();pump();
  check(classic_offset()==41,"same-query reload after mini-player return restores pixel bookmark");
  ui_redraw();pump();check(classic_offset()==41,"search invalidation retains pixel bookmark");
  std::cout<<"PASS CY-98a viewport: offsets/clamp, partial-edge clipping, cache-only placeholders/recovery, mini return, invalidation, row steps; 4/6 rows\n";
}
static void dragRegression(){
  namespace L=cydclassic::layout;
  for(unsigned rotation:{0u,1u}){
    g_settings.rotation=rotation;tft.setRotation(rotation);toSongs();
    const int h=tft.height(),w=tft.width(),y=L::HeaderH+100,maxOffset=(10-L::rows(h))*L::RowH;
    const int before=calls;const auto io=searchIO();
    SCREEN_CLASSIC.press(PRESS_DOWN,60,y);SCREEN_CLASSIC.press(PRESS_MOVE,60,y-7);
    check(classic_offset()==0,"slop does not move the list");
    SCREEN_CLASSIC.press(PRESS_MOVE,60,y-8);check(classic_offset()==1,"slop is subtracted");
    SCREEN_CLASSIC.tick(++clockNow);tft.resetDrawCounts();
    SCREEN_CLASSIC.press(PRESS_MOVE,60,y-19);SCREEN_CLASSIC.press(PRESS_MOVE,60,y-32);
    check(classic_offset()==25&&tft.fillCalls==0,"moves coalesce without painting");
    SCREEN_CLASSIC.tick(++clockNow);check(classic_thumbVisible()&&searchIO()==io,"drag frame shows thumb without catalogue IO");
    snapshot(rotation==0?"cy98b-drag-portrait.svg":"cy98b-drag-landscape.svg");
    SCREEN_CLASSIC.press(PRESS_MOVE,60,y-10000);SCREEN_CLASSIC.tick(++clockNow);
    check(classic_offset()==maxOffset&&searchIO()==io,"drag hard clamps without reads");
    SCREEN_CLASSIC.press(PRESS_MOVE,60,y);SCREEN_CLASSIC.tick(++clockNow);
    SCREEN_CLASSIC.tick(clockNow+=41);SCREEN_CLASSIC.press(PRESS_UP,60,y);SCREEN_CLASSIC.touch(TOUCH_TAP,60,y);
    check(classic_depth()==2&&calls==before,"claimed drag returning to start never selects");
    SCREEN_CLASSIC.tick(clockNow+=L::ThumbHoldMs-1);check(classic_thumbVisible(),"thumb holds briefly after release");
    SCREEN_CLASSIC.tick(++clockNow);check(!classic_thumbVisible(),"thumb hides after delay");pump();
    const Rect r=L::thumb<Rect>(w,h,10,classic_offset());const auto thumbIO=searchIO();
    SCREEN_CLASSIC.press(PRESS_DOWN,w-5,r.cy());SCREEN_CLASSIC.press(PRESS_MOVE,w-5,10000);SCREEN_CLASSIC.tick(++clockNow);
    check(classic_offset()==maxOffset&&searchIO()==thumbIO,"thumb drag maps to bottom without IO");
    SCREEN_CLASSIC.press(PRESS_MOVE,w-5,-10000);SCREEN_CLASSIC.tick(++clockNow);
    check(classic_offset()==0,"thumb drag maps to top");
    SCREEN_CLASSIC.press(PRESS_UP,w-5,-10000);SCREEN_CLASSIC.touch(TOUCH_TAP,w-5,-10000);pump();
    for(int offset:{0,25,maxOffset}){const Rect thumb=L::thumb<Rect>(w,h,10,offset);
      check(abs(L::thumbOffset(thumb.cy(),h,10,thumb.h/2)-offset)<=2,"thumb mapping round trip");}
    toSongs();tap(60,L::HeaderH+20);check(classic_depth()==3&&calls>before,"a tap still selects");
    home();tapTile(5);tapSettingsRow(5);check(std::string(classic_modeName(classic_mode()))=="About","Settings bottom action is reachable by drag");
  }
  std::cout<<"PASS CY-98b drag: slop/1:1, coalesced frames, clamp, tap versus claimed drag, no drag IO, thumb mapping/timeout, Settings; 4/6 rows\n";
}
extern bool classic_flingActive();
extern float classic_flingVelocity();
static void flingRegression(){
  namespace L=cydclassic::layout;
  g_settings.rotation=0;tft.setRotation(0);
  auto flick=[](){
    toSongs();
    SCREEN_CLASSIC.press(PRESS_DOWN,60,160);
    SCREEN_CLASSIC.tick(clockNow+=20);SCREEN_CLASSIC.press(PRESS_MOVE,60,150);
    SCREEN_CLASSIC.tick(clockNow+=20);SCREEN_CLASSIC.press(PRESS_MOVE,60,140);
    SCREEN_CLASSIC.press(PRESS_UP,60,140);SCREEN_CLASSIC.touch(TOUCH_TAP,60,140);
    check(classic_flingActive(),"release starts fling");return clockNow;
  };
  const auto initial=classic_scrollStats();const int before=calls;
  uint32_t start=flick();const auto io=searchIO();int last=classic_offset();
  for(int dt:{20,40,60,80,100,120}){
    const auto frames=classic_scrollStats().frames;SCREEN_CLASSIC.tick(clockNow=start+dt);
    check(classic_offset()>=last&&classic_scrollStats().frames<=frames+1,"one monotonic viewport frame per pass");last=classic_offset();
  }
  check(searchIO()==io&&classic_thumbVisible(),"fast fling has no SD reads and keeps thumb");
  const int dense=classic_offset();SCREEN_CLASSIC.tick(clockNow=start+1500);const int final=classic_offset();
  check(!classic_flingActive()&&calls==before,"fling completes without selection");
  start=flick();SCREEN_CLASSIC.tick(clockNow=start+120);
  check(classic_offset()==dense,"fake clock gives identical position with skipped frames");
  SCREEN_CLASSIC.tick(clockNow=start+1500);check(classic_offset()==final,"fling distance independent of frame rate");
  SCREEN_CLASSIC.tick(clockNow+=L::ThumbHoldMs);check(!classic_thumbVisible(),"thumb expires after fling");
  start=flick();SCREEN_CLASSIC.tick(clockNow=start+40);const int stopped=classic_offset();
  underruns+=3;
  check(SCREEN_CLASSIC.press(PRESS_DOWN,60,140)&&!classic_flingActive(),"touch-down stops fling and claims drag");
  SCREEN_CLASSIC.press(PRESS_UP,60,140);SCREEN_CLASSIC.touch(TOUCH_TAP,60,140);
  SCREEN_CLASSIC.tick(clockNow+=200);
  check(classic_offset()==stopped&&calls==before&&classic_depth()==2,"tap-to-stop never selects or resumes");
  check(classic_scrollStats().underruns==3,"last fling underrun delta");
  // Stopping touch immediately becomes a 1:1 drag with no extra slop.
  start=flick();SCREEN_CLASSIC.tick(clockNow=start+40);
  const int grab=classic_offset();SCREEN_CLASSIC.press(PRESS_DOWN,60,140);
  SCREEN_CLASSIC.tick(clockNow+=20);SCREEN_CLASSIC.press(PRESS_MOVE,60,135);
  check(classic_offset()==grab+5,"tap-to-stop continues as drag without slop");
  SCREEN_CLASSIC.tick(clockNow+=41);SCREEN_CLASSIC.press(PRESS_UP,60,135);
  toSongs();classic_paintViewport((10-L::rows(tft.height()))*L::RowH-10);
  SCREEN_CLASSIC.press(PRESS_DOWN,60,160);
  SCREEN_CLASSIC.tick(clockNow+=20);SCREEN_CLASSIC.press(PRESS_MOVE,60,150);
  SCREEN_CLASSIC.tick(clockNow+=20);SCREEN_CLASSIC.press(PRESS_MOVE,60,140);
  SCREEN_CLASSIC.press(PRESS_UP,60,140);SCREEN_CLASSIC.tick(clockNow+=100);
  check(classic_offset()==(10-L::rows(tft.height()))*L::RowH&&!classic_flingActive()&&classic_flingVelocity()==0,"bottom edge clamps and zeros velocity");
  // Reverse an outward pull while still clamped: the inward release must leave the edge.
  toSongs();SCREEN_CLASSIC.press(PRESS_DOWN,60,140);
  SCREEN_CLASSIC.tick(clockNow+=20);SCREEN_CLASSIC.press(PRESS_MOVE,60,180);
  SCREEN_CLASSIC.tick(clockNow+=20);SCREEN_CLASSIC.press(PRESS_MOVE,60,170);
  SCREEN_CLASSIC.press(PRESS_UP,60,170);SCREEN_CLASSIC.tick(clockNow);
  check(classic_flingActive(),"inward fling at exact boundary is not stopped");
  SCREEN_CLASSIC.tick(clockNow+=20);check(classic_offset()>0,"inward fling leaves boundary");
  toSongs();SCREEN_CLASSIC.press(PRESS_DOWN,60,140);
  SCREEN_CLASSIC.tick(clockNow+=20);SCREEN_CLASSIC.press(PRESS_MOVE,60,150);
  SCREEN_CLASSIC.tick(clockNow+=20);SCREEN_CLASSIC.press(PRESS_MOVE,60,160);
  SCREEN_CLASSIC.press(PRESS_UP,60,160);SCREEN_CLASSIC.tick(clockNow+=100);
  check(classic_offset()==0&&!classic_flingActive(),"top edge clamps");
  check(classic_scrollStats().flings>=initial.flings+7&&classic_scrollStats().frames>initial.frames,"scroll counters record flings and frames");
  check(classic_scrollStats().claimToPaintMs<=41,"claim to first frame measured with fake clock");
  classic_logScrollStats();
  std::cout<<"PASS CY-99 fling: start/finish, tap-to-stop/drag, hard edges, fake-clock frame independence, cache-only fast frames, thumb timeout, stats/underruns\n";
}
int main(){try{
  if(std::getenv("CYDPOD_REGENERATE_CLASSIC")){std::ofstream("../golden/classic_layout.txt")<<classic_geometry();return 0;}

  if(const char* preview=std::getenv("CYDPOD_HP_LIMIT_ONLY")){headphoneLimitRegression(preview);return 0;}

  if(!std::getenv("CYDPOD_VIEWPORT_ONLY"))geometryGolden();
  else {const auto actual=classic_geometry();std::ifstream f("../golden/classic_layout.txt");
    check(actual==std::string((std::istreambuf_iterator<char>(f)),{}),"Classic drag geometry differs from golden");
    std::cout<<"PASS CY-98b Classic layout golden (dialog excluded: baseline failure)\n";}

  g_settings.screenTimeout=0;g_settings.outRoute=OUT_BT;g_settings.rotation=0;
  SD.root=std::filesystem::current_path()/("ui-card-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::filesystem::path uiCardRoot=SD.root;
  std::filesystem::create_directories(SD.root/"music/Example");
  std::ifstream input("metadata-real.m4a",std::ios::binary);std::vector<char> original((std::istreambuf_iterator<char>(input)),{});
  check(!original.empty(),"real metadata fixture required");
  const std::string tag="Actual tagged fixture";auto found=std::search(original.begin(),original.end(),tag.begin(),tag.end());check(found!=original.end(),"locate fixture title");const size_t pos=size_t(found-original.begin());
  for(int i=0;i<10;++i){auto bytes=original;char name[32];snprintf(name,sizeof(name),"Song %02d",i);memset(bytes.data()+pos,0,tag.size());memcpy(bytes.data()+pos,name,strlen(name));
    std::ofstream out(SD.root/(std::string("music/Example/")+name+".m4a"),std::ios::binary);out.write(bytes.data(),bytes.size());}
  check(storage_begin(nullptr),"build UI catalogue");playlist_begin();classic_home();ui_go(SCR_CLASSIC);pump();check(label("Songs")&&label("Artists")&&label("Albums")&&label("Playlists")&&label("Search")&&label("Settings")&&!label("Music"),"home tiles");snapshot("ui-home.svg");
  if(std::getenv("CYDPOD_VIEWPORT_ONLY")){viewportRegression();dragRegression();flingRegression();miniPlayerRegression();return 0;}
  tapTile(1);check(label("Fixture Artist")&&classic_depth()==2,"artist view straight from the tile");snapshot("ui-artists.svg");
  tap(80,66);check(label("All Songs")&&label("Fixture Album"),"artist album view");snapshot("ui-albums.svg");
  tap(80,110);check(label("Song 00"),"album tracks");snapshot("ui-songs.svg");
  int before=calls;tx=80;ty=160;down=true;ui_tick(clockNow+=130);ty=60;ui_tick(clockNow+=5000);
  check(ui_pressClaimed(),"long drag claims recalibration hold");down=false;ui_touch(TOUCH_RECALIBRATE,80,198);pump();check(calls==before,"drag does not play");
  tap(80,66);check(label("Song 02")&&playing,"select track");
  tap(120,246);check(!playing,"pause transport hit target");tap(120,246);check(playing,"play transport hit target");
  int was=current;tap(182,246);check(current!=was,"next transport hit target");tap(40,246);check(playing,"previous transport hit target");snapshot("ui-player.svg");
  int seeks=seekCalls;tap(120,190);check(seekCalls==seeks+1&&lastSeek>119000&&lastSeek<121000,"progress tap seeks halfway");
  seeks=seekCalls;tx=20;ty=190;down=true;ui_tick(clockNow+=130);tx=200;ui_tick(clockNow+=130);
  check(ui_pressClaimed()&&seekCalls==seeks,"seek drag claims hold and defers commit");
  down=false;ui_touch(TOUCH_RECALIBRATE,tx,ty);pump();check(seekCalls==seeks+1&&lastSeek>200000,"seek commits once on release");
  seeks=seekCalls;tx=50;ty=190;down=true;ui_tick(clockNow+=130);ty=140;ui_tick(clockNow+=130);ty=190;ui_tick(clockNow+=130);down=false;ui_touch(TOUCH_TAP,tx,ty);pump();
  check(seekCalls==seeks,"vertical excursion cancels seek permanently");
  seeks=seekCalls;tx=50;ty=190;down=true;ui_tick(clockNow+=130);++serial;down=false;ui_touch(TOUCH_TAP,tx,ty);pump();check(seekCalls==seeks,"track change cancels drag");
  seekAllowed=false;tap(120,190);check(seekCalls==seeks,"unknown duration disables seek");tap(120,190);seekAllowed=true;
  // Transport acts once on touch-down; holding is not a jog and never repeats the command.
  seeks=seekCalls;was=current;tx=182;ty=246;down=true;ui_tick(clockNow+=130);check(current!=was&&seekCalls==seeks,"Next acts on touch-down");
  was=current;std::this_thread::sleep_for(std::chrono::milliseconds(620));ui_tick(clockNow+=130);down=false;ui_touch(TOUCH_LONG_PRESS,tx,ty);pump();
  check(current==was&&relativeSeek==0&&seekCalls==seeks,"holding Next does not jog or advance again");
  tft.setRotation(1);ui_redraw();pump();tap(160,110);check(lastSeek>119000&&lastSeek<121000,"landscape progress geometry");
  tap(12,110);check(lastSeek==0,"progress start clamp");tap(306,110);check(lastSeek==239999,"progress end clamp");tft.setRotation(0);ui_redraw();pump();
  std::cout<<"PASS seek UI: tap, drag release, cancellation, track identity, unknown duration, Next on touch-down and recalibration claim\n";
  tap(45,300);check(volume>30&&volume<40,"Player volume slider action (portrait track is 48 px from x=28 after CY-85: x=45 is 35 %)");snapshot("ui-player-volume.svg");
  tap(20,20);check(classic_top()==2&&label("Song 02"),"restore scrolled browse position");
  // Leaving the original target cancels even when the finger returns.
  before=calls;tx=80;ty=60;down=true;ui_tick(clockNow+=130);tx=220;ui_tick(clockNow+=130);tx=80;ui_tick(clockNow+=130);down=false;ui_touch(TOUCH_TAP,80,66);pump();check(calls==before,"cancelled press stays cancelled");
  home();tapTile(5);check(label("Audio Output"),"settings navigation");snapshot("ui-settings.svg");
  tap(80,216);check(classic_depth()==3&&shown("Library & System")&&label("Web passcode")&&label("Refresh Library")&&label("Upload music"),"Library & System opens from Settings");tap(20,20);check(label("Audio Output")&&classic_depth()==2,"Back returns to Settings");
  // CT-05: an unclaimed 0.6 to 4 s press activates the row like a tap.
  tx=80;ty=60;down=true;ui_tick(clockNow+=130);ui_tick(clockNow+=900);down=false;ui_touch(TOUCH_LONG_PRESS,80,66);pump();
  check(label("Bluetooth")&&label("Selected"),"long press activates the row (CT-05)");
  // CY-71 scope: Bluetooth and Headphones only. No speaker, no line out, no placeholder text, no SPK badge.
  check(!label("On-board speaker")&&!label("Stereo line out")&&!label("Hardware path pending")&&!label("SPK"),"speaker and line out are gone from the Output page and header");
#if CYD_WIRED_OUTPUT
  check(label("Headphones"),"Headphones is offered next to Bluetooth");
  // CY-43: failed output change must not persist/select an unusable route, and says so.
  routeAllowed=false;tap(80,110);check(g_settings.outRoute==OUT_BT,"failed headphones selection retains BT setting");
  check(label("Unable to change output. Try again."),"failed output change is reported");
  routeAllowed=true;tap(20,20);
  const auto restartsBefore=hostRestarts;
  saveAllowed=false;tap(80,110);
  check(g_settings.outRoute==OUT_BT&&liveRoute==OUT_BT&&hostRestarts==restartsBefore,"failed NVS save cannot restart or publish the output");
  saveAllowed=true;tap(20,20);
  tap(80,110);check(g_settings.outRoute==OUT_WIRED,"Headphones selected after a successful switch");
  ui_go(SCR_CLASSIC);pump();check(countLabel("Selected")==1&&countLabel("Headphones")==1&&!label("SPK")&&!label("BT")&&!label("BT..."),"exactly the active entry is selected; the header shows no SPK or BT text for Headphones");
  check(hostRestarts==restartsBefore+1&&savedParked&&g_settings.lastTrack==current&&g_settings.lastPosMs==84000&&!playing,"switch saves exact paused resume then restarts once");
  tft.setRotation(1);ui_go(SCR_CLASSIC);pump();
  check(label("Switch back to Bluetooth if there is no sound"),"full one-line no-module hint below Headphones on W328C");
  snapshot("ui-output-headphones.svg");
  btCalls=0;
  ui_go(SCR_BLUETOOTH);pump();
  check(label("Bluetooth is off (output: Headphones)")&&label("Use Bluetooth"),"Bluetooth off page text and restart action");
  tap(220,20);tap(20,200);pump();
  check(btCalls==0,"off Bluetooth page Pair new, rows, ticks and presses never call bt_*");
  tft.setRotation(0);ui_go(SCR_BLUETOOTH);pump();
  check(label("Bluetooth is off (output: Headphones)"),"off status is complete in portrait too");
  tft.setRotation(0);tap(20,20);pump();tap(120,246);tap(182,246);tap(40,246);pump();
  check(btCalls==0,"Headphones header, connected state, transport and sleep have zero bt_* calls");
  // CY-81: the Settings tree that replaced Setup is radio-free on Headphones too.
  btCalls=0;tft.setRotation(0);home();tapTile(5);
  for(int row:{2,3,4,5}){tapSettingsRow(row);pump();check(classic_depth()==3,"Settings child page opened");tap(20,20);}
  check(btCalls==0,"Playback, Display, Library & System and About make zero bt_* calls on Headphones");
  std::cout<<"HEADPHONES_UI_OK: zero bt_* calls across Bluetooth page, Classic and the Settings tree\n";
  ui_go(SCR_BLUETOOTH);pump();tap(120,126);
  check(g_settings.outRoute==OUT_BT&&hostRestarts==restartsBefore+2,"Use Bluetooth saves Bluetooth then restarts");
  home();tapTile(5);tap(80,66);
  ui_redraw();pump();check(countLabel("Selected")==1&&!label("BT")&&!label("BT..."),"back on Bluetooth: one Selected and no text badge");
#else
  check(!label("Headphones"),"a board without the module offers Bluetooth only");
  routeAllowed=false;tap(80,110);check(g_settings.outRoute==OUT_BT,"the single entry cannot select an unavailable route");routeAllowed=true;
  ui_redraw();pump();check(countLabel("Selected")==1,"one Selected entry");
#endif
  // Folder preview must be dropped above the source page, then rebuilt at its old scroll position.
  home();tapTile(3);tap(80,96);tap(80,66);
  tap(225,220);tap(80,66);check(playing,"folder preview starts playback");
  before=calls;hostFreeHeap=10000;tap(20,20);
  check(label("Unable to reload list. Check SD card or try again."),"Back rebuild is gated after preview release");
  check(calls==before,"failed preview rebuild preserves active playback queue");
  hostFreeHeap=64000;tap(20,20);check(label("Song 04"),"Back rebuild restores source scroll position");
  tap(80,66);check(playing&&calls>before,"rebuilt preview remains playable");
  std::cout<<"PASS CY-43: output failure preserves route; preview release, memory refusal, Back rebuild, scroll and queue preservation\n";
  classic_home();ui_go(SCR_CLASSIC);pump();before=calls;
  tapTile(4);check(label("Search Songs")&&label("a")&&label("z"),"Home Search opens alphabet keyboard");snapshot("ui-search-keyboard.svg");
  typeQuery("song");tapRect(cydclassic::layout::keyAction<Rect>(tft.width(),tft.height(),1));
  tapRect(cydclassic::layout::keyDigits<Rect>(tft.width(),tft.height()));check(label("0")&&label("9"),"digit keyboard page");
  typeQuery("034");tapRect(cydclassic::layout::keyBack<Rect>(tft.width(),tft.height())); // -> song 03
  check(calls==before,"incremental search preserves active queue");snapshot("ui-search-digits.svg");
  tapRect(cydclassic::layout::keyAction<Rect>(tft.width(),tft.height(),3));check(label("Song 03")&&storage_searchCount()==1,"filtered result list");snapshot("ui-search-results.svg");
  tap(80,110);check(playing&&storage_trackLabel(current)=="Song 03"&&calls>before,"result commits playable queue");
  // Existing 52 px transport and 44 px seek targets include the outer edges.
  tap(120,226);check(!playing,"large play target upper edge");tap(120,276);check(playing,"large play target lower edge");
  tx=100;ty=246;down=true;ui_tick(clockNow+=130);tx=130;ui_tick(clockNow+=130);down=false;ui_touch(TOUCH_TAP,tx,ty);pump();check(!playing,"sliding inside Play executes instead of silently cancelling");
  tap(20,20);tap(80,66);
  tapRect(cydclassic::layout::keyAction<Rect>(tft.width(),tft.height(),2));tapRect(cydclassic::layout::keyBack<Rect>(tft.width(),tft.height()));
  typeQuery("zz");tapRect(cydclassic::layout::keyAction<Rect>(tft.width(),tft.height(),3));check(label("No matches"),"empty results are visible");
  tap(80,66);tft.setRotation(1);ui_redraw();pump();
  check(label("q")&&label("z"),"landscape QWERTY has every letter on one page");snapshot("ui-search-landscape.svg");
  tft.setRotation(0);ui_redraw();pump();
  classic_home();ui_go(SCR_CLASSIC);pump();tapTile(5);tap(80,154);check(label("Screen timeout")&&label("Sleep timer"),"Playback exposes both sleep settings");g_settings.screenTimeout=2;ui_redraw();pump();snapshot("ui-playback-sleep.svg");g_settings.screenTimeout=0;
  tap(80,154);check(g_settings.screenTimeout==1,"screen timeout cycles and persists setting");
  tap(80,176);check(g_settings.sleepTimer==1,"sleep timer cycles setting");sleep_setTimer(0,clockNow);
  classic_home();ui_go(SCR_CLASSIC);pump();tapMini();playing=true;
  g_settings.screenTimeout=2;sleep_begin(clockNow);
  g_settings.sleepTimer=2;sleep_begin(clockNow);check(g_settings.sleepTimer==2&&!sleep_remainingMinutes(clockNow),"F-10 boot does not re-arm the persisted sleep timer");sleep_tick(clockNow+31*60000,false);check(playing,"F-10 no sleep at boot+31 min");g_settings.sleepTimer=0;
  ui_tick(clockNow+=29999);check(backlight==100&&playing,"screen awake before default dim deadline");
  ui_tick(++clockNow);check(backlight==20&&playing,"default dim at 30 seconds preserves playback");
  ui_tick(clockNow+=30000);check(backlight==0&&playing,"default screen off at 60 seconds preserves playback");
  tx=120;ty=246;down=true;ui_tick(++clockNow);check(backlight==100&&sleep_touchBlocked(),"touch wakes screen and is claimed");
  down=false;ui_touch(TOUCH_TAP,tx,ty);pump();check(playing,"wake touch does not pause");
  tap(120,246);check(!playing,"next touch operates transport");
  playing=true;const int savedVolume=volume;sleep_setTimer(1,clockNow);
  ui_tick(clockNow+=900000);check(fadeGain==100&&playing,"sleep fade starts at 15 minutes");
  ui_tick(clockNow+=1500);check(fadeGain==50&&playing,"three-second fade midpoint");
  ui_tick(clockNow+=1500);check(!playing&&backlight==0&&volume==savedVolume&&fadeGain==100&&!sleep_remainingMinutes(clockNow),"timer pauses, restores user gain, turns screen off and disarms");
  sleep_tick(++clockNow,true);sleep_tick(++clockNow,false);sleep_tick(++clockNow,false);
  g_settings.screenTimeout=0;ui_tick(clockNow+=600000);check(backlight==100,"Off disables screen timeout");
  compactChromeRegression();
  nowPlayingRegression();
  textq::run();
  iconq::run();
  touchRegression();
  miniPlayerRegression();
  homeTilesRegression();
  settingsRegression();
  themeRegression();
  themePreviews();
  bluetoothReturns();
  renderInventory();
  SD.root=uiCardRoot;check(storage_begin(nullptr),"navigation card catalogue");playlist_begin();
  qwertyRegression();
  searchSessionRegression();
  for(unsigned r:{0u,1u,2u,3u}){navRotation=r;navigationGraph();}navRotation=0;tft.setRotation(0);g_settings.rotation=0;
  g_settings.screenTimeout=2;sleep_begin(UINT32_MAX-1000);sleep_tick(uint32_t(UINT32_MAX-1000)+30000,false);check(backlight==20,"timeout survives millis rollover");
  std::cout<<"PASS CY-50 UI: large targets, keyboard letters/digits/space/delete/clear/done, incremental search and queue commit, sleep settings, dim/off, swallowed wake, timer fade, timeout Off and millis rollover\n";
  std::cout<<"PASS firmware UI: Home, Artist/Album/Song routes, track selection, queue isolation during drag, 5-second drag suppression, button cancellation, volume and Back position; SVG snapshots recorded\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL firmware UI: "<<e.what()<<"\n";return 1;}}

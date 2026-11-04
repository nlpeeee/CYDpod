#include "ui.h"
#include "sleep_mode.h"
#include "display.h"
#include "classic_screen.h"
#include "settings.h"
#include "catalog.h"
#include "playlist.h"
#include "storage.h"
#include "titles.h"
#include "background.h"
#include "bt.h"
#include "speaker.h"
#include "output_switch.h"
#include "audio.h"
#include "layouts.h"
#include "calibrate.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <functional>
#include "touch_gesture.h"
#include "classic_layout.h"
TFT_eSPI tft;
Settings g_settings;
static int current=0,calls=0,volume=32,queueLength=0;
static uint32_t serial=0,clockNow=0,lastSeek=0;
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
void settings_save(){++settingsSaves;}
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
uint32_t audio_positionMs(){return 84000;}
uint32_t audio_trackSerial(){return serial;}
static int btCalls=0;
bool bt_connected(){++btCalls;return true;}
bool bt_connecting(){++btCalls;return false;}
bool bt_pairing(){++btCalls;return false;}
bool bt_choosing(){++btCalls;return false;}
String bt_speakerName(){++btCalls;return "Speaker";}
cyd::PairingSession::Phase bt_pairingPhase(){++btCalls;return cyd::PairingSession::Normal;}
BtFailure bt_failure(){++btCalls;return BT_FAIL_NONE;}
uint32_t bt_passkey(){++btCalls;return 0;}
uint32_t bt_listSerial(){++btCalls;return 0;}
bool bt_requestPairing(){++btCalls;return true;}
bool bt_cancelPairing(){++btCalls;return true;}
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
static int restartSaves=0;
static uint8_t liveRoute=OUT_BT;
static bool savedParked=false;
bool settings_saveOutputResume(uint8_t){++restartSaves;savedParked=liveRoute==OUT_NONE&&!playing;return saveAllowed;}
bool speaker_setRoute(uint8_t r){if(!routeAllowed)return false;liveRoute=r;return true;}
#if CYD_WIRED_OUTPUT
bool wired_active(){return true;}
#endif
bool calibrate_run(){return true;}
void display_setRotation(uint8_t r){tft.setRotation(r);}
void display_setBacklight(uint8_t p){backlight=p;}
int bgStops=0;void bg_stop(){++bgStops;}
int bg_count(){return 0;}
void bg_setView(const Rect&,const Rect*,int){}
bool bg_start(int){return false;}
bool bg_active(){return false;}
void bg_tick(uint32_t){}
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
static void empty(){}
const Screen SCREEN_LIBRARY={empty,nullptr,nullptr,nullptr,nullptr};
const Screen SCREEN_SOURCES={empty,nullptr,nullptr,nullptr,nullptr};
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void pump(){for(int i=0;i<10;++i)ui_tick(clockNow+=130);}
static void tap(int x,int y){tx=x;ty=y;down=true;ui_tick(clockNow+=130);down=false;ui_touch(TOUCH_TAP,x,y);pump();}
static bool label(const char* s){return std::find(tft.labels.begin(),tft.labels.end(),s)!=tft.labels.end();}
static int countLabel(const char* s){return int(std::count(tft.labels.begin(),tft.labels.end(),s));}
static void snapshot(const char* path){tft.save(path);}

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
static void toPlayer(){home();tap(80,110);}
static void toPlayback(){home();tap(80,198);tap(80,154);}
static void toMusic(){home();tap(80,66);}
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
    pressRun(300,10,0,[e](uint32_t t,int& x,int& y){x=80;y=(t>=100&&t<200)?87+e:84;});
    check(shuffleCalls==before+1,"excursion that returns activates");
    toPlayback();before=shuffleCalls;
    pressRun(300,10,0,[e](uint32_t t,int& x,int& y){x=80;y=t>=100?87+e:84;});
    check(shuffleCalls==before+1,"release within the slop of the captured row activates");
  }
  // 14 px out: not within the slop, so releasing there does nothing; coming back re-arms; 40 px is a deliberate exit.
  const uint32_t slopBefore=cydtouch::stats().slop;
  toPlayback();before=shuffleCalls;
  pressRun(300,10,0,[](uint32_t t,int& x,int& y){x=80;y=t>=100?101:84;});
  check(shuffleCalls==before&&cydtouch::stats().slop==slopBefore+1,"release 14 px outside the row cancels and is counted");
  toPlayback();before=shuffleCalls;
  pressRun(400,10,0,[](uint32_t t,int& x,int& y){x=80;y=(t>=100&&t<250)?101:84;});
  check(shuffleCalls==before+1,"returning inside the slop re-arms the press");
  toPlayback();before=shuffleCalls;
  pressRun(400,10,0,[](uint32_t t,int& x,int& y){x=80;y=(t>=100&&t<250)?127:84;});
  check(shuffleCalls==before,"a deliberate far exit cancels for good, even after returning");
  // A footer control: slop reaches past its edge into the neighbouring Up button without retargeting it.
  home();tap(80,198);   // Settings: seven rows, so the footer has Up/Down arrows beside Home
  pressRun(200,10,0,[](uint32_t t,int& x,int& y){x=t<60?60:70;y=300;});   // Home is x 0-63; 70 is inside Up
  check(label("Music")&&label("Search Songs"),"Home released 7 px into the Up button still goes Home");
  std::cout<<"PASS CY-62 tolerant release: 1-8 px excursions return or end near, 14 px outside cancels and re-arms, 40 px exits for good, neighbour not retargeted\n";

  // Lists: row tap with horizontal jitter, sideways and vertical claims, true scrolling.
  toMusic();pressRun(150,10,0,[](uint32_t t,int& x,int& y){x=80+((t/10)%2?-10:10);y=66;});
  check(label("Fixture Artist"),"horizontal jitter in a scrolling list does not suppress the row tap");
  const uint32_t claimBefore=cydtouch::stats().claim;
  toMusic();pressRun(150,10,0,[](uint32_t t,int& x,int& y){x=80+((t/10)%2?-24:24);y=66;});
  check(!label("Fixture Artist")&&cydtouch::stats().claim==claimBefore+1,"a large sideways swing claims the press and is counted");
  toMusic();pressRun(150,10,0,[](uint32_t t,int& x,int& y){x=80+(t>=50?3:0);y=t>=50?52:66;});
  check(!label("Fixture Artist"),"a mostly vertical 14 px move claims the press");
  toMusic();check(!label("Favourites"),"Music page starts at the top");
  pressRun(300,10,0,[](uint32_t t,int& x,int& y){x=80;y=int(150-std::min<uint32_t>(t,200)*120/200);});
  check(label("Favourites")&&!label("Fixture Artist"),"a true vertical drag scrolls the list instead of activating a row");
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
      check(track.x==44&&track.y==216&&track.w==150&&chip.x==238&&chip.y==205&&chip.w==76&&chip.h==30,"bottom row matches proposal A SVG");
      check(bar.x==14&&bar.y==118&&bar.w==292,"seek track matches proposal A SVG");
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
  tft.setRotation(0);home();
  std::cout<<"CY61_NOWPLAYING_OK: rotations 0/1/3, SVG geometry, slider/seek edges and monotonic drag, live shared volume, one deferred save, down-only transport, favourite, shuffle, repeat, Output and Options\n";
}
std::string classic_geometry();std::string classic_dialog_geometry();
static void geometryGolden(){
  const auto dialog=classic_dialog_geometry();if(std::getenv("CYDPOD_REGENERATE_LAYOUT"))std::ofstream("../golden/classic_dialog.txt")<<dialog;
  std::ifstream d("../golden/classic_dialog.txt");check(dialog==std::string((std::istreambuf_iterator<char>(d)),{}),"Confirmation dialog geometry differs");
  const auto actual=classic_geometry();std::ofstream("classic_layout.actual.txt")<<actual;
  if(std::getenv("CYDPOD_REGENERATE_LAYOUT")){std::ofstream("../golden/classic_layout.txt")<<actual;}
  std::ifstream f("../golden/classic_layout.txt");std::string expected((std::istreambuf_iterator<char>(f)),{});
  check(actual==expected,"Classic geometry differs from approved golden table");
  std::cout<<"CLASSIC_GEOMETRY_GOLDEN: 17 modes x 4 variants x rotations 0/1/3, 240x320 and 320x240\n";
}
int main(){try{
  geometryGolden();
  g_settings.screenTimeout=0;g_settings.theme=8;g_settings.style=7;g_settings.outRoute=OUT_BT;g_settings.rotation=0;
  SD.root=std::filesystem::current_path()/("ui-card-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(SD.root/"music/Example");
  std::ifstream input("metadata-real.m4a",std::ios::binary);std::vector<char> original((std::istreambuf_iterator<char>(input)),{});
  check(!original.empty(),"real metadata fixture required");
  const std::string tag="Actual tagged fixture";auto found=std::search(original.begin(),original.end(),tag.begin(),tag.end());check(found!=original.end(),"locate fixture title");const size_t pos=size_t(found-original.begin());
  for(int i=0;i<10;++i){auto bytes=original;char name[32];snprintf(name,sizeof(name),"Song %02d",i);memset(bytes.data()+pos,0,tag.size());memcpy(bytes.data()+pos,name,strlen(name));
    std::ofstream out(SD.root/(std::string("music/Example/")+name+".m4a"),std::ios::binary);out.write(bytes.data(),bytes.size());}
  check(storage_begin(nullptr),"build UI catalogue");playlist_begin();classic_home();ui_go(SCR_CLASSIC);pump();check(label("Music"),"home menu");snapshot("ui-home.svg");
  tap(80,66);check(label("Artists")&&label("Albums")&&label("Songs"),"music routes");snapshot("ui-music.svg");
  tap(80,66);check(label("Fixture Artist"),"artist view");snapshot("ui-artists.svg");
  tap(80,66);check(label("All Songs")&&label("Fixture Album"),"artist album view");snapshot("ui-albums.svg");
  tap(80,110);check(label("Song 00"),"album tracks");snapshot("ui-songs.svg");
  int before=calls;tx=80;ty=180;down=true;ui_tick(clockNow+=130);ty=60;ui_tick(clockNow+=5000);
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
  tap(70,300);check(volume>30&&volume<40,"Player volume slider action");snapshot("ui-player-volume.svg");
  tap(20,20);check(label("Song 02"),"restore scrolled browse position");
  // Leaving the original target cancels even when the finger returns.
  before=calls;tx=80;ty=60;down=true;ui_tick(clockNow+=130);tx=220;ui_tick(clockNow+=130);tx=80;ui_tick(clockNow+=130);down=false;ui_touch(TOUCH_TAP,80,66);pump();check(calls==before,"cancelled press stays cancelled");
  tap(30,300);tap(80,198);check(label("Audio Output"),"settings navigation");snapshot("ui-settings.svg");
  tap(135,300);tap(80,198);check(ui_current()==SCR_SETUP,"advanced settings bridge");ui_go(SCR_PLAYER);pump();check(label("Refresh Library"),"legacy Back returns to scrolled settings parent");tap(88,300);
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
  // Legacy screens are still reachable through saved skins/advanced settings.
  SCREEN_SETUP.enter();SCREEN_SETUP.tick(clockNow+=2500);
  SCREEN_SETUP.touch(TOUCH_TAP,100,UI_HEADER_H+UI_ROW_H+10);pump();
  check(btCalls==0,"legacy Setup status and Bluetooth navigation are radio-free");
  for(uint8_t skin=0;skin<SKIN_COUNT;++skin){
    g_settings.skin=skin;SCREEN_PLAYER.enter();SCREEN_PLAYER.tick(clockNow+=2500);
    const auto& l=layout_player();
    SCREEN_PLAYER.press(PRESS_DOWN,l.status.cx(),l.status.cy());
    SCREEN_PLAYER.press(PRESS_UP,l.status.cx(),l.status.cy());
    SCREEN_PLAYER.touch(TOUCH_TAP,l.buttons[PB_VOL_UP].cx(),l.buttons[PB_VOL_UP].cy());
    SCREEN_PLAYER.leave();
  }
  check(btCalls==0,"all legacy output chips, connected-state polling and volume hooks are radio-free");
  std::cout<<"HEADPHONES_UI_OK: zero bt_* calls across Bluetooth page, Classic, Setup and all legacy player skins\n";
  g_settings.skin=0;
  ui_go(SCR_BLUETOOTH);pump();tap(120,126);
  check(g_settings.outRoute==OUT_BT&&hostRestarts==restartsBefore+2,"Use Bluetooth saves Bluetooth then restarts");
  home();tap(80,198);tap(80,66);
  ui_redraw();pump();check(countLabel("Selected")==1&&label("BT"),"back on Bluetooth: one Selected, BT badge");
#else
  check(!label("Headphones"),"a board without the module offers Bluetooth only");
  routeAllowed=false;tap(80,110);check(g_settings.outRoute==OUT_BT,"the single entry cannot select an unavailable route");routeAllowed=true;
  ui_redraw();pump();check(countLabel("Selected")==1,"one Selected entry");
#endif
  // Folder preview must be dropped above the source page, then rebuilt at its old scroll position.
  tap(30,300);tap(80,66);tap(80,242);tap(80,66);
  tap(135,300);tap(80,66);check(playing,"folder preview starts playback");
  before=calls;hostFreeHeap=10000;tap(20,20);
  check(label("Unable to reload list. Check SD card or try again."),"Back rebuild is gated after preview release");
  check(calls==before,"failed preview rebuild preserves active playback queue");
  hostFreeHeap=64000;tap(20,20);check(label("Song 05"),"Back rebuild restores source scroll position");
  tap(80,66);check(playing&&calls>before,"rebuilt preview remains playable");
  std::cout<<"PASS CY-43: output failure preserves route; preview release, memory refusal, Back rebuild, scroll and queue preservation\n";
  classic_home();ui_go(SCR_CLASSIC);pump();before=calls;
  const int stopsBefore=bgStops;tap(80,242);check(bgStops>stopsBefore,"F-12 background .anim handle is released when Search opens");check(label("Search Songs")&&label("a")&&label("z"),"Home Search opens alphabet keyboard");snapshot("ui-search-keyboard.svg");
  tap(20,216);tap(100,176);tap(60,176);tap(20,136); // song
  tap(40,300);tap(200,256);check(label("0")&&label("9"),"digit keyboard page");
  tap(24,96);tap(168,96);tap(216,96);tap(120,256); // 034, backspace -> 03
  check(calls==before,"incremental search preserves active queue");snapshot("ui-search-digits.svg");
  tap(200,300);check(label("Song 03")&&storage_searchCount()==1,"filtered result list");snapshot("ui-search-results.svg");
  tap(80,110);check(playing&&storage_trackLabel(current)=="Song 03"&&calls>before,"result commits playable queue");
  // Existing 52 px transport and 44 px seek targets include the outer edges.
  tap(120,226);check(!playing,"large play target upper edge");tap(120,276);check(playing,"large play target lower edge");
  tx=100;ty=246;down=true;ui_tick(clockNow+=130);tx=130;ui_tick(clockNow+=130);down=false;ui_touch(TOUCH_TAP,tx,ty);pump();check(!playing,"sliding inside Play executes instead of silently cancelling");
  tap(20,20);tap(80,66);tap(120,300);tap(100,256);tap(200,256); // edit, Clear, Delete on empty, ABC
  tap(60,256);tap(60,256); // z,z
  tap(200,300);check(label("No matches"),"empty results are visible");
  tap(80,66);tft.setRotation(1);ui_redraw();pump();
  tap(150,176);check(label("q")&&label("z"),"landscape alphabet second page");snapshot("ui-search-landscape.svg");
  tft.setRotation(0);ui_redraw();pump();
  classic_home();ui_go(SCR_CLASSIC);pump();tap(80,198);tap(80,154);check(label("Screen timeout")&&label("Sleep timer"),"Playback exposes both sleep settings");g_settings.screenTimeout=2;ui_redraw();pump();snapshot("ui-playback-sleep.svg");g_settings.screenTimeout=0;
  tap(80,154);check(g_settings.screenTimeout==1,"screen timeout cycles and persists setting");
  tap(80,198);check(g_settings.sleepTimer==1,"sleep timer cycles setting");sleep_setTimer(0,clockNow);
  classic_home();ui_go(SCR_CLASSIC);pump();tap(80,110);playing=true;
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
  nowPlayingRegression();
  touchRegression();
  g_settings.screenTimeout=2;sleep_begin(UINT32_MAX-1000);sleep_tick(uint32_t(UINT32_MAX-1000)+30000,false);check(backlight==20,"timeout survives millis rollover");
  std::cout<<"PASS CY-50 UI: large targets, keyboard letters/digits/space/delete/clear/done, incremental search and queue commit, sleep settings, dim/off, swallowed wake, timer fade, timeout Off and millis rollover\n";
  std::cout<<"PASS firmware UI: Home, Artist/Album/Song routes, track selection, queue isolation during drag, 5-second drag suppression, button cancellation, volume and Back position; SVG snapshots recorded\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL firmware UI: "<<e.what()<<"\n";return 1;}}

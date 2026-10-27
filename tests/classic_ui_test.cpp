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
#include "calibrate.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
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
void settings_save(){}
bool audio_ready(){return true;}
uint32_t audio_error(char*,size_t,int*){return 0;}
void audio_setQueue(const uint16_t* p,int n,int,bool){queueData=p;queueLength=n;++calls;}
void audio_playTrack(int n){current=n;playing=true;++serial;++calls;}
int audio_currentTrack(){return current;}
bool audio_isPlaying(){return playing;}
void audio_next(){int pos=0;for(int i=0;i<queueLength;++i)if(queueData[i]==current)pos=i;audio_playTrack(queueData[(pos+1)%queueLength]);}
void audio_prev(){audio_playTrack(current);}
void audio_togglePause(){playing=!playing;}
void audio_setVolume(uint8_t n){volume=n;}
uint8_t audio_volume(){return uint8_t(volume);}
void audio_setShuffle(bool on){shuffle=on;}
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
bool bt_connected(){return true;}
bool speaker_setRoute(uint8_t){return routeAllowed;}
bool calibrate_run(){return true;}
void display_setRotation(uint8_t r){tft.setRotation(r);}
void display_setBacklight(uint8_t p){backlight=p;}
void bg_stop(){}
bool touch_isDown(){return down;}
void touch_position(int& x,int& y){x=tx;y=ty;}
void touch_livePosition(int& x,int& y){x=tx;y=ty;}
bool titles_load(const String&){return false;}
void titles_release(){}
int titles_width(){return 0;}
void titles_draw(const Rect&,int,int,uint16_t,uint16_t){}
static void empty(){}
const Screen SCREEN_PLAYER={empty,nullptr,nullptr,nullptr,nullptr};
const Screen SCREEN_SETUP={empty,nullptr,nullptr,nullptr,nullptr};
const Screen SCREEN_BLUETOOTH={empty,nullptr,nullptr,nullptr,nullptr};
const Screen SCREEN_LIBRARY={empty,nullptr,nullptr,nullptr,nullptr};
const Screen SCREEN_SOURCES={empty,nullptr,nullptr,nullptr,nullptr};
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void pump(){for(int i=0;i<10;++i)ui_tick(clockNow+=130);}
static void tap(int x,int y){tx=x;ty=y;down=true;ui_tick(clockNow+=130);down=false;ui_touch(TOUCH_TAP,x,y);pump();}
static bool label(const char* s){return std::find(tft.labels.begin(),tft.labels.end(),s)!=tft.labels.end();}
static void snapshot(const char* path){tft.save(path);}
int main(){try{
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
  tap(80,66);check(label("Now Playing")&&playing,"select track");
  tap(120,246);check(!playing,"pause transport hit target");tap(120,246);check(playing,"play transport hit target");
  int was=current;tap(200,246);check(current!=was,"next transport hit target");tap(40,246);check(playing,"previous transport hit target");snapshot("ui-player.svg");
  int seeks=seekCalls;tap(120,190);check(seekCalls==seeks+1&&lastSeek>119000&&lastSeek<121000,"progress tap seeks halfway");
  seeks=seekCalls;tx=20;ty=190;down=true;ui_tick(clockNow+=130);tx=200;ui_tick(clockNow+=130);
  check(ui_pressClaimed()&&seekCalls==seeks,"seek drag claims hold and defers commit");
  down=false;ui_touch(TOUCH_RECALIBRATE,tx,ty);pump();check(seekCalls==seeks+1&&lastSeek>200000,"seek commits once on release");
  seeks=seekCalls;tx=50;ty=190;down=true;ui_tick(clockNow+=130);ty=140;ui_tick(clockNow+=130);ty=190;ui_tick(clockNow+=130);down=false;ui_touch(TOUCH_TAP,tx,ty);pump();
  check(seekCalls==seeks,"vertical excursion cancels seek permanently");
  seeks=seekCalls;tx=50;ty=190;down=true;ui_tick(clockNow+=130);++serial;down=false;ui_touch(TOUCH_TAP,tx,ty);pump();check(seekCalls==seeks,"track change cancels drag");
  seekAllowed=false;tap(120,190);check(seekCalls==seeks,"unknown duration disables seek");tap(120,190);seekAllowed=true;
  // Hold timestamp uses the same monotonic clock as firmware.
  tx=200;ty=246;down=true;ui_tick(clockNow+=130);std::this_thread::sleep_for(std::chrono::milliseconds(620));down=false;ui_touch(TOUCH_LONG_PRESS,tx,ty);pump();
  check(relativeSeek==10,"holding Next seeks ten seconds instead of advancing");
  tx=40;ty=246;down=true;ui_tick(clockNow+=130);std::this_thread::sleep_for(std::chrono::milliseconds(620));down=false;ui_touch(TOUCH_LONG_PRESS,tx,ty);pump();check(relativeSeek==-10,"holding Previous seeks backward");
  tft.setRotation(1);ui_redraw();pump();tap(160,110);check(lastSeek>119000&&lastSeek<121000,"landscape progress geometry");
  tap(12,110);check(lastSeek==0,"progress start clamp");tap(308,110);check(lastSeek==239999,"progress end clamp");tft.setRotation(0);ui_redraw();pump();
  std::cout<<"PASS seek UI: tap, drag release, cancellation, track identity, unknown duration, long Next and recalibration claim\n";
  tap(30,300);check(label("Volume"),"footer volume action");tap(180,212);check(volume==37,"volume step");snapshot("ui-volume.svg");
  tap(20,20);check(label("Now Playing"),"back from volume");tap(20,20);check(label("Song 02"),"restore scrolled browse position");
  // Leaving the original target cancels even when the finger returns.
  before=calls;tx=80;ty=60;down=true;ui_tick(clockNow+=130);tx=220;ui_tick(clockNow+=130);tx=80;ui_tick(clockNow+=130);down=false;ui_touch(TOUCH_TAP,80,66);pump();check(calls==before,"cancelled press stays cancelled");
  tap(30,300);tap(80,198);check(label("Audio Output"),"settings navigation");snapshot("ui-settings.svg");
  tap(135,300);tap(80,198);check(ui_current()==SCR_SETUP,"advanced settings bridge");ui_go(SCR_PLAYER);pump();check(label("Refresh Library"),"legacy Back returns to scrolled settings parent");tap(88,300);
  // CT-05: an unclaimed 0.6 to 4 s press activates the row like a tap.
  tx=80;ty=60;down=true;ui_tick(clockNow+=130);ui_tick(clockNow+=900);down=false;ui_touch(TOUCH_LONG_PRESS,80,66);pump();
  check(label("On-board speaker"),"long press activates the row (CT-05)");
  // CY-43: failed output allocation must not persist/select an unusable route.
  routeAllowed=false;tap(80,110);check(g_settings.outRoute==OUT_BT,"failed speaker selection retains BT setting");
  routeAllowed=true;tap(20,20);
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
  tap(80,242);check(label("Search Songs")&&label("a")&&label("z"),"Home Search opens alphabet keyboard");snapshot("ui-search-keyboard.svg");
  tap(20,216);tap(100,176);tap(60,176);tap(20,136); // song
  tap(40,300);tap(200,256);check(label("0")&&label("9"),"digit keyboard page");
  tap(24,96);tap(168,96);tap(216,96);tap(120,256); // 034, backspace -> 03
  check(calls==before,"incremental search preserves active queue");snapshot("ui-search-digits.svg");
  tap(200,300);check(label("Song 03")&&storage_searchCount()==1,"filtered result list");snapshot("ui-search-results.svg");
  tap(80,110);check(playing&&storage_trackLabel(current)=="Song 03"&&calls>before,"result commits playable queue");
  // Existing 52 px transport and 44 px seek targets include the outer edges.
  tap(120,222);check(!playing,"large play target upper edge");tap(120,270);check(playing,"large play target lower edge");
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
  g_settings.screenTimeout=2;sleep_begin(UINT32_MAX-1000);sleep_tick(uint32_t(UINT32_MAX-1000)+30000,false);check(backlight==20,"timeout survives millis rollover");
  std::cout<<"PASS CY-50 UI: large targets, keyboard letters/digits/space/delete/clear/done, incremental search and queue commit, sleep settings, dim/off, swallowed wake, timer fade, timeout Off and millis rollover\n";
  std::cout<<"PASS firmware UI: Home, Artist/Album/Song routes, track selection, queue isolation during drag, 5-second drag suppression, button cancellation, volume and Back position; SVG snapshots recorded\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL firmware UI: "<<e.what()<<"\n";return 1;}}

#include "ui.h"
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
TFT_eSPI tft;
Settings g_settings;
static int current=0,calls=0,volume=32,queueLength=0;
static uint32_t serial=0,clockNow=0;
static bool playing=false,shuffle=false,down=false;
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
uint32_t audio_durationMs(){return 240000;}
uint32_t audio_positionMs(){return 84000;}
uint32_t audio_trackSerial(){return serial;}
bool bt_connected(){return true;}
void speaker_setRoute(uint8_t){}
bool calibrate_run(){return true;}
void display_setRotation(uint8_t r){tft.setRotation(r);}
void display_setBacklight(uint8_t){}
void bg_stop(){}
bool touch_isDown(){return down;}
void touch_position(int& x,int& y){x=tx;y=ty;}
void touch_livePosition(int& x,int& y){x=tx;y=ty;}
bool titles_load(const String&){return false;}
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
  g_settings.theme=8;g_settings.style=7;g_settings.outRoute=OUT_BT;g_settings.rotation=0;
  SD.root=std::filesystem::current_path()/("ui-card-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(SD.root/"music/Example");
  std::ifstream input("metadata-real.m4a",std::ios::binary);std::vector<char> original((std::istreambuf_iterator<char>(input)),{});
  check(!original.empty(),"real metadata fixture required");
  const std::string tag="Actual tagged fixture";auto found=std::search(original.begin(),original.end(),tag.begin(),tag.end());check(found!=original.end(),"locate fixture title");const size_t pos=size_t(found-original.begin());
  for(int i=0;i<10;++i){auto bytes=original;char name[32];snprintf(name,sizeof(name),"Song %02d",i);memset(bytes.data()+pos,0,tag.size());memcpy(bytes.data()+pos,name,strlen(name));
    std::ofstream out(SD.root/(std::string("music/Example/")+name+".m4a"),std::ios::binary);out.write(bytes.data(),bytes.size());}
  check(storage_begin(nullptr),"build UI catalogue");playlist_begin();classic_home();ui_go(SCR_CLASSIC);pump();check(label("Music"),"home menu");snapshot("ui-home.svg");
  tap(80,60);check(label("Artists")&&label("Albums")&&label("Songs"),"music routes");snapshot("ui-music.svg");
  tap(80,60);check(label("Fixture Artist"),"artist view");snapshot("ui-artists.svg");
  tap(80,60);check(label("All Songs")&&label("Fixture Album"),"artist album view");snapshot("ui-albums.svg");
  tap(80,100);check(label("Song 00"),"album tracks");snapshot("ui-songs.svg");
  int before=calls;tx=80;ty=180;down=true;ui_tick(clockNow+=130);ty=60;ui_tick(clockNow+=5000);
  check(ui_pressClaimed(),"long drag claims recalibration hold");down=false;ui_touch(TOUCH_RECALIBRATE,80,180);pump();check(calls==before,"drag does not play");
  tap(80,60);check(label("Now Playing")&&playing,"select track");
  tap(120,256);check(!playing,"pause transport hit target");tap(120,256);check(playing,"play transport hit target");
  int was=current;tap(184,256);check(current!=was,"next transport hit target");tap(48,256);check(playing,"previous transport hit target");snapshot("ui-player.svg");
  tap(30,300);check(label("Volume"),"footer volume action");tap(180,212);check(volume==37,"volume step");snapshot("ui-volume.svg");
  tap(20,20);check(label("Now Playing"),"back from volume");tap(20,20);check(label("Song 03"),"restore scrolled browse position");
  // Leaving the original target cancels even when the finger returns.
  before=calls;tx=80;ty=60;down=true;ui_tick(clockNow+=130);tx=220;ui_tick(clockNow+=130);tx=80;ui_tick(clockNow+=130);down=false;ui_touch(TOUCH_TAP,80,60);pump();check(calls==before,"cancelled press stays cancelled");
  tap(30,300);tap(80,180);check(label("Audio Output"),"settings navigation");snapshot("ui-settings.svg");
  tap(80,260);check(ui_current()==SCR_SETUP,"advanced settings bridge");ui_go(SCR_PLAYER);pump();check(label("Audio Output"),"legacy Back returns to settings parent");
  std::cout<<"PASS firmware UI: Home, Artist/Album/Song routes, track selection, queue isolation during drag, 5-second drag suppression, button cancellation, volume and Back position; SVG snapshots recorded\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL firmware UI: "<<e.what()<<"\n";return 1;}}

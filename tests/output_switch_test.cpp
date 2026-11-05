// CY-84: real output_switch.cpp + speaker.cpp + settings.cpp, deterministic
// external audio/task/NVS fault boundaries. No protocol or physical device.
#include <Arduino.h>
#include <Preferences.h>
#include "../firmware/cydpod_player/app/audio.h"
#include "../firmware/cydpod_player/app/speaker.h"
#include "../firmware/cydpod_player/app/settings.cpp"
#include "../firmware/cydpod_player/app/speaker.cpp"
#ifndef CY84_SOURCE
#define CY84_SOURCE "../firmware/cydpod_player/app/output_switch.cpp"
#endif
#include CY84_SOURCE
#include <iostream>

[[maybe_unused]] static bool playing=true,startup=true,btAlive=true,stuck=false,work=false,task=false;
static bool reserveFails=false,startFails=false;
static unsigned reserves=0,starts=0,deletes=0,cancels=0,pathReads=0;
static int settleAfter=-1,delays=0,track=7;
static uint32_t position=84567;
static AudioCommands commands={-1,-1,1,false,false,false};
bool audio_isPlaying(){return playing;}
void audio_setPlaying(bool on){playing=on;}
AudioCommands audio_commands(){return commands;}
bool audio_startupComplete(){return startup;}
int audio_currentTrack(){return track;}
uint32_t audio_positionMs(){return position;}
void audio_pull(int16_t*,int32_t){}
String storage_trackPath(int n){++pathReads;return n==7?"/music/resume.m4a":"/music/settled.m4a";}
void storage_searchCancel(){++cancels;}
void ui_toast(const char*,uint32_t){}
#if CYD_WIRED_OUTPUT
bool wired_reserve(){++reserves;if(reserveFails&&btAlive)return false;work=true;return true;}
void wired_release(){if(!task)work=false;}
bool wired_startTask(){++starts;if(startFails&&btAlive)return false;task=true;return true;}
void wired_kick(){}
bool wired_parked(){return !task||(!stuck&&speaker_route()!=OUT_WIRED);}
void wired_deleteTask(){if(task)++deletes;task=false;}
#endif
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static void tick(){if(settleAfter>=0&&++delays>=settleAfter){commands={-1,-1,2,false,false,false};startup=true;track=9;position=45678;settleAfter=-1;}}
static void reset(uint8_t old=OUT_BT){
  stuck=false;reserveFails=false;startFails=false;check(speaker_setRoute(OUT_NONE),"cleanup");
  Preferences::failOpen=false;Preferences::failKey.clear();Preferences::keyAttempts=0;Preferences::failKeyAttempt=1;
  Preferences::erase();g_settings=Settings();g_settings.outRoute=old;g_settings.lastTrack=3;g_settings.lastPosMs=1200;g_settings.lastPath="/old.m4a";
  g_settings.srcKind=1;g_settings.srcKey=12345;g_settings.catalogKind=4;g_settings.catalogGroup=12;g_settings.catalogCrc=778899;
  g_settings.btName="Pocket";g_settings.wifiSsid="test-network";g_settings.wifiPass="test-pass";
  check(settings_save(),"initial save");check(speaker_begin(old),"old startup");
  btAlive=old==OUT_BT;playing=true;startup=true;commands={-1,-1,1,false,false,false};
  settleAfter=-1;delays=0;track=7;position=84567;reserves=starts=deletes=restarts=cancels=pathReads=0;serialLog.clear();clockMs=0;delayHook=tick;
}
[[maybe_unused]] static void failed(uint8_t old,const char* stage){
  check(!output_switch(old==OUT_BT?OUT_WIRED:OUT_BT),"failure rejected");
  check(restarts==0&&cancels==0&&playing&&speaker_route()==old&&g_settings.outRoute==old,"failure restores route/playback, never restart");
  check(Preferences::peek("out")==old,"failed save preserves persisted route");
  check(serialLog.find(stage)!=std::string::npos,"diagnostic identifies failure stage");
}
int main(){try{
#if CYD_WIRED_OUTPUT
  // Reproduce both real speaker_setRoute failure sites with Bluetooth alive.
  for(int fault=0;fault<2;++fault){
    reset();reserveFails=fault==0;startFails=fault==1;
    check(!speaker_setRoute(OUT_WIRED),"live destination would fail under BT resource pressure");
    check(speaker_route()==OUT_BT&&!work&&!task,"failed live activation leaves BT intact");
    reserves=starts=0;
    check(output_switch(OUT_WIRED),"restart choice must not depend on live destination allocation");
    check(restarts==1&&cancels==1&&!playing&&speaker_route()==OUT_NONE,"successful switch parks and restarts paused");
    check(reserves==0&&starts==0&&!work&&!task,"no preboot destination reserve/start");
    g_settings=Settings();settings_begin();
    check(g_settings.outRoute==OUT_WIRED&&g_settings.lastTrack==7&&g_settings.lastPosMs==84567&&g_settings.lastPath.s=="/music/resume.m4a","fresh boot restores exact resume");
    check(g_settings.srcKind==1&&g_settings.srcKey==12345&&g_settings.catalogKind==4&&g_settings.catalogGroup==12&&g_settings.catalogCrc==778899,"source/catalog identity preserved");
    check(boot_plan(g_settings.outRoute,true).wired&&!boot_plan(g_settings.outRoute,true).bluetooth,"fresh boot gates BT");
    btAlive=false;check(speaker_begin(g_settings.outRoute),"destination valid after fresh boot releases BT");
  }
  reset(OUT_WIRED);check(output_switch(OUT_BT),"reverse restart");
  check(restarts==1&&!playing&&speaker_route()==OUT_NONE&&reserves==0&&starts==0,"reverse never starts destination");
  g_settings=Settings();settings_begin();check(g_settings.outRoute==OUT_BT&&boot_plan(g_settings.outRoute,true).bluetooth,"reverse saved boot plan");
  for(uint8_t old:{OUT_BT,OUT_WIRED}){
    reset(old);check(output_switch(old)&&restarts==0&&pathReads==0&&playing&&clockMs==0,"same route is no-op");
    reset(old);check(!output_switch(OUT_SPEAKER)&&restarts==0&&pathReads==0&&playing&&Preferences::peek("out")==old,"invalid destination no side effects");
    // Every checked preference flush key, including open and empty-string remove.
    for(const char* key:{"open","uiRev","catKind","catGroup","catCrc","panel","invert","rot","bright","lang","screenTime","sleepTime","vol","out","track","pos","lastPath","shuffle","repeat","srcKind","srcKey","btName","btAddr","known","knownN","ssid","pass","webPin"}){
      reset(old);if(std::string(key)=="open")Preferences::failOpen=true;else Preferences::failKey=key;
      failed(old,"preferences");check(old!=OUT_WIRED||(task&&work&&deletes==0&&reserves==0&&starts==0),"rollback retains old wired work/task without allocation");
      Preferences::failOpen=false;Preferences::failKey.clear();
    }
    // Inject at the second write, inside the resume/output transaction itself.
    for(const char* key:{"track","pos","lastPath","srcKind","srcKey","catKind","catGroup","catCrc","out"}){
      reset(old);Preferences::failKey=key;Preferences::failKeyAttempt=2;failed(old,(std::string("nvs=")+key).c_str());Preferences::failKey.clear();
    }
    for(int p=0;p<5;++p){
      reset(old);if(p==0)commands.track=9;if(p==1)commands.seekMs=45678;if(p==2)commands.opening=true;if(p==3)commands.seeking=true;if(p==4)startup=false;
      const auto saved=Preferences::store();failed(old,"quiesce");check(pathReads==0&&Preferences::store()==saved&&g_settings.lastTrack==3&&clockMs==5000,"timeout bounded; never captures stale identity or writes NVS");
      reset(old);commands.track=9;commands.opening=true;startup=false;settleAfter=3;
      check(output_switch(old==OUT_BT?OUT_WIRED:OUT_BT)&&restarts==1&&!playing&&g_settings.lastTrack==9&&g_settings.lastPosMs==45678&&g_settings.lastPath.s=="/music/settled.m4a","settled pending/startup captures final identity paused");
    }
    reset(old);playing=false;Preferences::failKey="track";check(!output_switch(old==OUT_BT?OUT_WIRED:OUT_BT)&&!playing,"rollback preserves paused intent too");Preferences::failKey.clear();
  }
  reset(OUT_WIRED);stuck=true;failed(OUT_WIRED,"park");check(task&&work&&deletes==0&&clockMs==3000,"park timeout retains old resources");stuck=false;
#else
  reset();const auto saved=Preferences::store();check(!output_switch(OUT_WIRED)&&restarts==0&&playing&&clockMs==0&&Preferences::store()==saved,"unsupported board rejects without effects");
  check(!settings_saveOutputResume(OUT_WIRED),"persistence rejects unsupported destination");
  check(output_switch(OUT_BT)&&restarts==0&&pathReads==0,"same route plain board");
#endif
  Preferences::failKey.clear();check(speaker_setRoute(OUT_NONE),"final cleanup");
  std::cout<<"OUTPUT_SWITCH_GLUE_OK: real orchestration/speaker/settings, resource pressure, rollback, save stages, pending/startup, board validation\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL output switch: "<<e.what()<<"\n"<<serialLog;return 1;}}

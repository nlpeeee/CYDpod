#ifndef CYD_UPLOADER
#include "sleep_mode.h"
#include "settings.h"
#include "display.h"
#include "audio.h"
#include "power.h"
#include "touch_gesture.h"
namespace {
const uint32_t screenSeconds[]={0,30,60,300};
const uint8_t timerMinutes[]={0,15,30,60,90};
uint32_t lastTouch=0,sleepDeadline=0,wakeSwallowUntil=0;
bool initialized=false,swallow=false,blocked=false,forcedOff=false,timerArmed=false,fading=false,timerExpired=false;
int applied=-1;
// The panel sleeps (SLPIN) only when fully dark and wakes before the backlight,
// so a sleeping panel is never lit (power.h, lever "panel").
void light(int level){
  if(applied==level)return;
  if(level>0&&display_panelAsleep())display_panelSleep(false);
  applied=level;display_setBacklight(level);
  if(level==0&&power_lever(PWR_PANEL))display_panelSleep(true);
}
}
void sleep_begin(uint32_t now){
  initialized=true;lastTouch=now;swallow=blocked=forcedOff=fading=false;applied=-1;
  // The timer is per session: a persisted choice stays visible in the menu but is not re-armed at power-on.
  timerArmed=false;sleepDeadline=now;audio_setSleepGain(100);light(g_settings.brightness);
}
void sleep_setTimer(uint8_t choice,uint32_t now){
  choice=choice<5?choice:0;g_settings.sleepTimer=choice;
  timerArmed=choice!=0;fading=false;audio_setSleepGain(100);
  sleepDeadline=now+uint32_t(timerMinutes[choice])*60000;
  Serial.printf("cmd: sleep timer %u minutes %s\n",unsigned(timerMinutes[choice]),timerArmed?"armed":"off");
}
void sleep_tick(uint32_t now,bool down){
  if(!initialized)sleep_begin(now);
  blocked=swallow;
  if(down){
    // The complete wake press, including release and a recalibration hold, is consumed.
    // A press that woke the chip from light sleep finds the screen already lit.
    const bool fromSleep=wakeSwallowUntil&&int32_t(now-wakeSwallowUntil)<0;
    if(forcedOff||applied<int(g_settings.brightness)||fromSleep){swallow=blocked=true;++cydtouch::stats().wake;Serial.println("cmd: wake accepted (touch swallowed)");}
    wakeSwallowUntil=0;forcedOff=false;lastTouch=now;light(g_settings.brightness);
  }else if(swallow){blocked=true;swallow=false;}
  if(timerArmed&&int32_t(now-sleepDeadline)>=0){
    if(!fading){fading=true;Serial.println("cmd: sleep timer fading (3 seconds)");}
    const uint32_t elapsed=now-sleepDeadline;
    audio_setSleepGain(elapsed>=3000?0:100-elapsed/30);
    if(elapsed>=3000){
      if(audio_isPlaying())audio_togglePause();
      audio_setSleepGain(100);timerArmed=fading=false;forcedOff=true;timerExpired=true;
      Serial.println("cmd: sleep timer expired (paused, screen off)");
    }
  }
  const uint32_t off=screenSeconds[g_settings.screenTimeout<4?g_settings.screenTimeout:2]*1000;
  const uint32_t idle=now-lastTouch;
  if(forcedOff||(off&&idle>=off))light(0);
  else if(off&&idle>=off/2)light(max(1,int(g_settings.brightness)/5));
  else light(g_settings.brightness);
}
bool sleep_touchBlocked(){return blocked;}
bool sleep_screenOff(){return applied==0;}
uint32_t sleep_idleMs(uint32_t now){return initialized?now-lastTouch:0;}
void sleep_forceScreenOff(){forcedOff=true;light(0);}
bool sleep_takeTimerExpired(){const bool expired=timerExpired;timerExpired=false;return expired;}
void sleep_wakeNow(uint32_t now,bool pressed){
  forcedOff=false;lastTouch=now;
  wakeSwallowUntil=pressed?((now+1500)|1):0;
  light(g_settings.brightness);
}
uint16_t sleep_remainingMinutes(uint32_t now){return timerArmed?(int32_t(sleepDeadline-now)>0?uint16_t((sleepDeadline-now+59999)/60000):1):0;}
const char* sleep_timeoutLabel(uint8_t c){const char* labels[]={"Off","30 s (dim at 15 s)","1 min (dim at 30 s)","5 min (dim at 2.5 min)"};return labels[c<4?c:2];}
const char* sleep_timerLabel(uint8_t c){const char* labels[]={"Off","15 minutes","30 minutes","60 minutes","90 minutes"};return labels[c<5?c:0];}
#endif

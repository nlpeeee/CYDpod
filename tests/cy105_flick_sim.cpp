// CY-105 device-free audit, extended by CY-107 with the new raw-position estimator, the Coast model and the replay matrix.
// Production decoder, PressTracker, Tracker and Coast; only UI routing/scheduling (changed-coordinate dispatch) is a model.
// g++ -std=c++17 -O2 -Wall -Wextra -Ifirmware/cydpod_player/app tests/cy105_flick_sim.cpp -o /tmp/cy107 && /tmp/cy107
#include "cst820_report.h"
#include "touch_gesture.h"
#include "scroll_core.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
using namespace cydscroll;
struct Sample { uint32_t ms; int y; bool valid=true; };
struct Result {
  bool fling=false,claimed=false;float v=0,dist=0;
  uint32_t pause=0,lag=0;int lastY=0;
  bool monotone=true,framesAgree=true;
};
static const float kRow=40.0f;
// Raw y is the sample coordinate; filtered y is the PressTracker EMA. Dispatch happens when either changed
// (the firmware wiring chunk will pass both into Tracker::move).
Result run(const std::vector<Sample>& samples) {
  cydtouch::PressTracker press;Tracker tracker;Coast coast;Result r;
  bool uiDown=false;int uiY=0,uiRaw=0;uint32_t prevPoll=0;(void)prevPoll;
  for(const auto& s:samples){
    uint8_t b[6]={0,uint8_t(s.valid),0,100,uint8_t((s.y>>8)&15),uint8_t(s.y&255)};
    auto report=cst820::decode(b,6);
    press.update(s.ms,report.state==cst820::State::Contact,report.x,report.y);
    prevPoll=s.ms;
    if(press.down()){
      int x,y;
      if(!uiDown){
        press.landing(x,y);uiDown=true;uiY=y;uiRaw=s.valid?s.y:y;tracker.down(press.lastSeenMs(),float(y),float(uiRaw));
      }else{
        press.live(x,y);const int raw=s.valid?s.y:uiRaw;
        if(y!=uiY||raw!=uiRaw){tracker.move(press.lastSeenMs(),float(y),float(raw));uiY=y;uiRaw=raw;}
      }
    }else if(uiDown){
      uiDown=false;r.pause=press.lastSeenMs()-tracker.samples().lastMs();
      r.lag=s.ms-press.lastSeenMs();r.claimed=tracker.claimed();r.v=tracker.velocity();r.lastY=uiY;
      const uint32_t t0=press.lastSeenMs();
      r.fling=tracker.up(t0,coast,500,0,1e6f,-1.0f,s.ms>t0+cydtouch::PressTracker::ReleaseGraceMs?s.ms-cydtouch::PressTracker::ReleaseGraceMs:t0);
      r.dist=r.fling?coast.finalY()-500:0;
      if(r.fling){  // forward only, monotone; two frame loops agree at common instants
        float prev=500;
        for(uint32_t t=0;t<9000;t+=7){float p=coast.position(t0+t);if(p<prev-1e-3f||p<500)r.monotone=false;prev=p;}
        for(uint32_t t=0;t<=9900;t+=1650)  // 20 fps (50 ms) and 30 fps (33 ms) loops share multiples of 1650 ms
          if(std::fabs(coast.position(t0+(t/50)*50)-coast.position(t0+(t/33)*33))>1.0f)r.framesAgree=false;
      }
    }
  }
  return r;
}
std::vector<Sample> stroke(int speed,int dt,int stall,int phase,int duration=120,int rest=0){
  std::vector<Sample> out;int t=0,i=0;unsigned rng=17+phase*101;
  duration+=phase;
  while(t<duration+rest+180){
    bool valid=t<duration+rest;
    int moving=t<duration?t:duration;
    out.push_back({uint32_t(1000+t),315-int(std::lround(speed*moving/1000.)),valid});
    rng=rng*1664525u+1013904223u;
    int jitter=int((rng>>16)%7)-3;
    t+=dt+jitter+((++i%2==0)?stall:0);
  }
  return out;
}
static void show(const char* label,const Result& r){
  std::printf("%s fling=%d v=%.1f pause=%u lag=%u claim=%d y=%d distance=%.2f\n",label,r.fling,r.v,r.pause,r.lag,r.claimed,r.lastY,r.dist);
}
static void req(bool ok,const std::string& what){if(!ok)throw std::runtime_error(what);}
int main(){
  try{
    // Hz x stall (ms added to every second interval); strokes 120..148 ms; eight phases (stroke length + jitter seed).
    const int hz[]={15,25,30,60},stalls[]={0,25,30},speeds[]={50,100,300,800,2000};
    const int NH=4,NS=3,NSP=5;
    double medDist[NSP][NH*NS];int flung[NSP][NH*NS];
    std::printf("matrix: fling count/8 and median coast rows (hz+stall ms); strokes max(120 ms, 20 px)+0..28 ms, jitter +-3 ms\n\npx/s");
    for(int h:hz)for(int st:stalls)std::printf(" | %d+%d",h,st);
    std::puts("");
    int violations=0,restFlung=0,restCells=0;std::vector<std::string> misses;
    for(int si=0;si<NSP;++si){
      std::printf("%d",speeds[si]);
      for(int hi=0;hi<NH;++hi)for(int sti=0;sti<NS;++sti){
        const int dt=int(1000.0/hz[hi]+0.5),stall=stalls[sti],c=hi*NS+sti;
        std::vector<double> d;int n=0;
        for(int ph=0;ph<8;++ph){
          const int dur=std::max(120,20000/speeds[si]);  // CY-105 strokes (120 ms) are 12 px at 100 px/s: under the 12 px travel minimum
          auto r=run(stroke(speeds[si],dt,stall,ph*4,dur));
          if(r.fling){++n;d.push_back(r.dist/kRow);}
          if(r.fling&&(!r.monotone||!r.framesAgree||r.dist<=0))++violations;
          if(speeds[si]>=100&&!r.fling)misses.push_back(std::to_string(speeds[si])+"@"+std::to_string(hz[hi])+"+"+std::to_string(stall)+"/ph"+std::to_string(ph*4));
          if(speeds[si]>=300){auto q=run(stroke(speeds[si],dt,stall,ph*4,dur,200));++restCells;restFlung+=q.fling;if(q.fling)std::printf("RESTFLING %d@%d+%d/ph%d pause=%u lag=%u\n",speeds[si],hz[hi],stall,ph*4,q.pause,q.lag);}
        }
        std::sort(d.begin(),d.end());
        flung[si][c]=n;medDist[si][c]=d.empty()?0:d[d.size()/2];
        std::printf(" | %d/8 %.1f",n,medDist[si][c]);
      }
      std::puts("");
    }
    for(const auto& m:misses)std::printf("MISS %s\n",m.c_str());
    req(misses.empty(),"a >=100 px/s cell did not fling ("+std::to_string(misses.size())+" runs)");
    req(violations==0,"coast reversed, stalled backwards or frame loops disagree");
    req(restFlung==0,"a 200 ms rest flung ("+std::to_string(restFlung)+"/"+std::to_string(restCells)+")");
    int mono=0;
    for(int c=0;c<NH*NS;++c)for(int si=2;si<NSP;++si)  // 100 px/s travels barely over the 12 px minimum; compare from 300 up
      if(medDist[si][c]<medDist[si-1][c])++mono;
    req(mono==0,"distance not monotone in release speed ("+std::to_string(mono)+" cells)");
    double spread=0;  // release-speed spread across poll rates/stalls, rows (constant-speed resampling)
    for(int si=1;si<NSP;++si){double lo=1e9,hi=0;for(int c=0;c<NH*NS;++c){lo=std::min(lo,medDist[si][c]);hi=std::max(hi,medDist[si][c]);}
      std::printf("spread %d px/s: %.2f rows\n",speeds[si],hi-lo);spread=std::max(spread,hi-lo);}
    req(spread<=1.0,"distance spread across poll rates above 1 row");
    int f50=0;for(int c=0;c<NH*NS;++c)f50+=flung[0][c];
    std::printf("\n50 px/s flings in %d of %d runs (allowed either way); every >=100 px/s run flung; 200 ms rest flung %d/%d\n",f50,NH*NS*8,restFlung,restCells);

    // Audit reproductions with the new behaviour.
    auto gap41=run({{1000,200},{1020,190},{1040,180},{1081,160},{1141,0,false}});
    auto shortR=run({{1000,200},{1020,188},{1080,0,false}});
    auto rest100=run(stroke(500,17,0,0,120,100));
    auto rest200=run(stroke(500,17,0,0,120,200));
    auto decel=run({{1000,200},{1020,180},{1040,170},{1060,168},{1080,166},{1100,164},{1160,0,false}});
    show("gap41",gap41);show("12px/20ms raw",shortR);show("rest100ms",rest100);show("rest200ms",rest200);show("decel",decel);
    req(gap41.fling&&shortR.fling&&!rest200.fling,"audit reproduction behaviour");
    // Reversal: after a fast flick turns back, only the new direction may fling (content opposite to the final finger motion), and a short turn-back does not.
    auto rev=run({{1000,300},{1020,260},{1040,230},{1060,238},{1080,246},{1100,254},{1120,262},{1140,262,false},{1260,262,false}});
    auto revShort=run({{1000,300},{1020,260},{1040,230},{1060,238},{1140,238,false},{1260,238,false}});
    show("reversal long",rev);show("reversal short",revShort);
    req(rev.claimed&&rev.fling&&rev.dist<0&&rev.v>0,"long reversal must fling in the new direction only");
    req(!revShort.fling,"8 px turn-back flung");
    // Stray down stops a running coast and is a drag.
    Coast active;active.start(500,800,0,10000,1000);Tracker stop;stop.down(1020,100,&active);
    std::printf("stray down stops=%d stoppedFling=%d tap=%d\n",active.finished(1020),stop.stoppedFling(),stop.isTap());
    req(active.finished(1020)&&stop.stoppedFling()&&!stop.isTap(),"stray down");
    std::puts("PASS CY-107 replay: matrix, rest, direction, monotone distance, 20/30 fps");
  }catch(const std::exception& e){std::printf("FAIL CY-107 %s\n",e.what());return 1;}
}

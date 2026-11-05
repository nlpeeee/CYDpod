#include "../firmware/cydpod_player/app/output_switch.h"
#include <cassert>
#include <string>
#include <cstdio>
struct Ops {
  std::string events;
  bool captureOK=true, parkOK=true, saveOK=true;
  void failed(const char*){}
  bool capture(){events+='C';return captureOK;}
  bool park(){events+='P';return parkOK;}
  bool save(uint8_t){events+='S';return saveOK;}
  void restore(uint8_t){events+='U';}
  void restart(uint8_t){events+='X';}
};
int main(){
  for(int board=0;board<2;++board)for(int route=0;route<256;++route){
    const auto p=boot_plan(uint8_t(route),board);
    assert(p.wired==(board&&route==OUT_WIRED));assert(p.bluetooth==!p.wired);
  }
#if CYD_WIRED_OUTPUT
  for(auto old:{OUT_BT,OUT_WIRED}){
    const uint8_t next=old==OUT_BT?OUT_WIRED:OUT_BT;
    Ops good;assert(output_switch_run(old,next,good)&&good.events=="CPSX");
    Ops capture;capture.captureOK=false;assert(!output_switch_run(old,next,capture)&&capture.events=="CU");
    Ops park;park.parkOK=false;assert(!output_switch_run(old,next,park)&&park.events=="CPU");
    Ops save;save.saveOK=false;assert(!output_switch_run(old,next,save)&&save.events=="CPSU");
    Ops same;assert(output_switch_run(old,old,same)&&same.events.empty());
    Ops invalid;assert(!output_switch_run(old,255,invalid)&&invalid.events.empty());
  }
#else
  Ops unavailable;
  assert(!output_switch_run(OUT_BT,OUT_WIRED,unavailable)&&unavailable.events.empty());
  Ops same;
  assert(output_switch_run(OUT_BT,OUT_BT,same)&&same.events.empty());
#endif
  puts("BOOT_PLAN_RESTART_OK: 512 plans; route/capture/park/save/restart ordering and failures");
}

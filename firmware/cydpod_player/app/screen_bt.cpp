// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "ui.h"
#include "display.h"
#include "theme.h"
#include "bt.h"
#include "output_switch.h"
#include "classic_screen.h"

static constexpr int STATUS_Y=UI_HEADER_H+4,LIST_Y=UI_HEADER_H+34,ROW_H=28;
static BtUiSnapshot shown{};
static BtUiRow selected{};
static uint32_t lastListPaint=0,rssiPaintAt[BT_MAX_DEVICES]{};
static bool shownValid=false,actionView=false,pairLit=false,waitingToPair=false;
static bool rowCaptured=false;
static uint8_t capturedAddr[6]{};
static_assert(sizeof(shown)+sizeof(selected)+sizeof(rssiPaintAt)+sizeof(capturedAddr)+32<=1024,
              "Bluetooth UI persistent RAM budget");

static Rect pairRect(){return {tft.width()-110,0,110,UI_HEADER_H};}
static Rect actionRect(int row){return {10,LIST_Y+32+48*row,tft.width()-20,40};}
static Rect useBluetoothRect(){return {10,LIST_Y+44,tft.width()-20,40};}
static int maxRows(){return min(BT_MAX_DEVICES,(tft.height()-LIST_Y)/ROW_H);}
static bool sameAddress(const uint8_t* a,const uint8_t* b){return memcmp(a,b,6)==0;}
static bool sameRow(const BtUiRow& a,const BtUiRow& b){
  return sameAddress(a.device.addr,b.device.addr)&&strcmp(a.device.name,b.device.name)==0&&
         a.saved==b.saved&&a.connected==b.connected;
}

static void drawStatus(const BtUiStatus& status){
  const Palette& p=theme();
  const int y=STATUS_Y+13;
  tft.fillRect(0,STATUS_Y,tft.width(),26,p.bg);
  if(status.disconnecting){
    ui_label(T_DISCONNECTING,10,y,ML_DATUM,p.warn,p.bg,tft.width()-20);
  }else if(status.failure!=BT_FAIL_NONE){
    if(status.failure==BT_FAIL_AUTH)ui_label(T_PAIR_REFUSED,10,y,ML_DATUM,p.bad,p.bg,tft.width()-20);
    else ui_text("Bluetooth request failed. Try again.",10,y,ML_DATUM,p.bad,p.bg,tft.width()-20);
  }else if(status.pairing&&!status.passkey){
    const char* text=status.phase==cyd::PairingSession::Disconnecting?"Disconnecting...":
      status.phase==cyd::PairingSession::Cancelling?"Reconnecting previous device...":
      status.phase==cyd::PairingSession::Selecting?"Connecting...":"Searching for devices...";
    ui_text(text,10,y,ML_DATUM,p.text,p.bg,tft.width()-20);
  }else if(status.connected){
    const int width=ui_label(T_CONNECTED,10,y,ML_DATUM,p.good,p.bg);
    ui_text(status.name,16+width,y,ML_DATUM,p.good,p.bg,tft.width()-width-26);
  }else if(status.connecting||status.choosing){
    const int width=ui_label(T_CONNECTING,10,y,ML_DATUM,p.warn,p.bg);
    if(status.passkey){
      char code[8];snprintf(code,sizeof(code),"%06u",unsigned(status.passkey));
      ui_text(code,18+width,y,ML_DATUM,p.text,p.bg,tft.width()-width-28);
    }
  }else{
    ui_label(T_DISCONNECTED,10,y,ML_DATUM,p.dim,p.bg,tft.width()-20);
  }
}

static void drawRow(int row,const BtUiRow* device){
  const Palette& p=theme();
  const int y=LIST_Y+row*ROW_H;
  tft.fillRect(0,y,tft.width(),ROW_H,p.bg);
  if(!device)return;
  const uint16_t face=device->connected?p.btnActive:p.btn;
  const uint16_t ink=device->connected?p.onControlActive:p.text;
  tft.fillRoundRect(6,y+2,tft.width()-12,ROW_H-4,6,face);
  const int right=tft.width()-14;
  ui_text(device->device.name,14,y+ROW_H/2,ML_DATUM,ink,face,right-100);
  if(device->saved){
    ui_label(device->connected?T_CONNECTED:T_SAVED,right,y+ROW_H/2,MR_DATUM,
             device->connected?p.onControlActive:p.dim,face);
  }else{
    ui_text(String(device->device.rssi)+" dBm",right,y+ROW_H/2,MR_DATUM,p.dim,face,0,TYPE_CAPTION);
  }
}

static bool selectedConnected(const BtUiStatus& status){
  return status.connected&&sameAddress(status.peer,selected.device.addr);
}
static bool actionBusy(const BtUiStatus& status){
  return status.disconnecting||status.connecting||status.choosing||
    (status.pairing&&status.phase!=cyd::PairingSession::Scanning);
}
static void drawActions(const BtUiSnapshot& snapshot){
  const Palette& p=theme();
  tft.fillRect(0,LIST_Y,tft.width(),tft.height()-LIST_Y,p.bg);
  ui_text(selected.device.name,10,LIST_Y+12,ML_DATUM,p.text,p.bg,tft.width()-20);
  const bool busy=actionBusy(snapshot.status);
  const Txt action=busy?(snapshot.status.disconnecting?T_DISCONNECTING:T_CONNECTING):
    selectedConnected(snapshot.status)?T_DISCONNECT:T_CONNECT;
  ui_textButton(actionRect(0),action,!busy,false);
  if(selected.saved)ui_textButton(actionRect(1),T_FORGET,false,false);
}

static void drawOff(){
  tft.fillScreen(theme().bg);ui_header(T_BLUETOOTH);
  tft.setTextDatum(TL_DATUM);tft.setTextColor(theme().text,theme().bg);
  tft.drawString("Bluetooth is off (output: Headphones)",10,LIST_Y,1);
  const Rect r=useBluetoothRect();
  tft.fillRoundRect(r.x,r.y,r.w,r.h,6,theme().btn);
  ui_text("Use Bluetooth",r.cx(),r.cy(),MC_DATUM,theme().text,theme().btn);
}
static void renderBase(){
  tft.fillScreen(theme().bg);ui_header(T_BLUETOOTH);
  if(!actionView)ui_textButton(pairRect(),T_PAIR_NEW,true,pairLit,theme().band);
  shownValid=false;rowCaptured=false;
}
static void enter(){
  if(!output_usesBluetooth()){waitingToPair=false;actionView=false;drawOff();return;}
  pairLit=false;renderBase();
  waitingToPair=!actionView&&bt_pairing()&&!bt_connected();
}
static void leave(){actionView=false;rowCaptured=false;waitingToPair=false;}

static void tick(uint32_t now){
  if(!output_usesBluetooth())return;
  BtUiSnapshot next{};bt_uiSnapshot(next);
  const bool statusChanged=!shownValid||memcmp(&next.status,&shown.status,sizeof(next.status))!=0;
  if(statusChanged){
    drawStatus(next.status);
    if(next.status.connected&&!next.status.pairing&&waitingToPair){
      waitingToPair=false;classic_return();return;
    }
  }
  if(actionView){
    bool changed=false;
    for(int i=0;i<next.count;++i){
      if(sameAddress(next.rows[i].device.addr,selected.device.addr)){
        changed=!sameRow(next.rows[i],selected);selected=next.rows[i];break;
      }
    }
    if(statusChanged||changed)drawActions(next);
  }else if(!shownValid||now-lastListPaint>=200){
    const int count=min<int>(next.count,maxRows());
    const int previous=shownValid?min<int>(shown.count,maxRows()):0;
    bool painted=false;
    for(int i=0;i<max(count,previous);++i){
      const bool contentChanged=!shownValid||i>=count||i>=previous||!sameRow(next.rows[i],shown.rows[i]);
      const bool rssiChanged=i<count&&!next.rows[i].saved&&
        abs(next.rows[i].device.rssi-shown.rows[i].device.rssi)>=3&&now-rssiPaintAt[i]>=1000;
      if(contentChanged||rssiChanged){
        drawRow(i,i<count?&next.rows[i]:nullptr);
        if(i<count)shown.rows[i]=next.rows[i];
        rssiPaintAt[i]=now;painted=true;
      }
    }
    if(!count&&(!shownValid||previous))ui_label(next.status.pairing?T_SEARCHING:T_PAIRING_MODE,10,LIST_Y+14,ML_DATUM,theme().dim,theme().bg,tft.width()-20);
    shown.count=count;
    if(painted||!shownValid)lastListPaint=now;
  }
  shown.status=next.status;shownValid=true;
}

static void forgetConfirmed(){
  if(output_usesBluetooth()&&bt_forgetDevice(selected.device.addr))actionView=false;
  else ui_toast("Bluetooth is busy or device unavailable");
}
static void touch(TouchEvent ev,int x,int y){
  if(!output_usesBluetooth()){
    if(ev==TOUCH_TAP){
      if(UI_BACK_RECT.contains(x,y))classic_return();
      else if(useBluetoothRect().contains(x,y))output_switch(OUT_BT);
    }
    return;
  }
  if(ev!=TOUCH_TAP)return;
  if(UI_BACK_RECT.contains(x,y)){
    if(actionView){actionView=false;renderBase();return;}
    if((bt_pairing()||bt_pairingPhase()==cyd::PairingSession::Failed)&&!bt_cancelPairing()){
      ui_toast("Unable to cancel pairing");return;
    }
    classic_return();return;
  }
  if(actionView){
    if(actionRect(0).contains(x,y)){
      BtUiSnapshot next{};bt_uiSnapshot(next);
      if(actionBusy(next.status))return;
      const bool accepted=selectedConnected(next.status)?bt_disconnect(selected.device.addr):bt_connectDevice(selected.device.addr);
      if(!accepted)ui_toast("Bluetooth is busy or device unavailable");
    }else if(selected.saved&&actionRect(1).contains(x,y)){
      BtUiSnapshot next{};bt_uiSnapshot(next);
      if(!actionBusy(next.status))ui_confirm(T_FORGET_Q,T_FORGET_HINT,T_COUNT,T_FORGET,forgetConfirmed);
    }
    return;
  }
  if(pairRect().contains(x,y)){
    if(bt_requestPairing())waitingToPair=true;else ui_toast("Bluetooth is not ready. Try again.");
    return;
  }
  if(y<LIST_Y)return;
  const int row=(y-LIST_Y)/ROW_H;
  if(row<0||row>=shown.count||row>=maxRows())return;
  const uint8_t* address=rowCaptured?capturedAddr:shown.rows[row].device.addr;
  for(int i=0;i<shown.count;++i){
    if(sameAddress(shown.rows[i].device.addr,address)){
      selected=shown.rows[i];actionView=true;renderBase();return;
    }
  }
}
static bool press(PressPhase phase,int x,int y){
  if(!output_usesBluetooth())return false;
  if(phase==PRESS_DOWN){
    rowCaptured=false;
    if(!actionView&&y>=LIST_Y){
      const int row=(y-LIST_Y)/ROW_H;
      if(row>=0&&row<shown.count&&row<maxRows()){
        memcpy(capturedAddr,shown.rows[row].device.addr,6);rowCaptured=true;
      }
    }
  }
  const bool lit=!actionView&&phase==PRESS_DOWN&&pairRect().contains(x,y);
  if(lit!=pairLit&&phase!=PRESS_MOVE){
    pairLit=lit;
    if(!actionView)ui_textButton(pairRect(),T_PAIR_NEW,true,pairLit,theme().band);
  }
  return false;
}

const Screen SCREEN_BLUETOOTH={enter,leave,tick,touch,press};
#endif

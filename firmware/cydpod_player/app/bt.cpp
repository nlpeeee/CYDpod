// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "bt.h"
#include "audio.h"
#include "command_queue.h"
#include "speaker.h"
#include "settings.h"
#include "BluetoothA2DPSource.h"
#include "pairing_session.h"
#include "bt_reconnect_policy.h"
#include "bt_volume_sync.h"
#include <esp_gap_bt_api.h>

// ---------------------------------------------------------------------------
// The library, with two things it does not offer on its own
// ---------------------------------------------------------------------------
// 1. Connecting to a device the owner tapped, at the moment they tap it.
//    The library only ever connects from its own discovery: when the ssid
//    callback returns true it marks the device DISCOVERED and stops the
//    inquiry, and the "discovery stopped" handler is what calls
//    esp_a2d_connect. That works for a speaker, which answers every inquiry.
//    A car head unit announces itself once while its pairing screen is open
//    and then goes quiet, so waiting to see it a second time waits forever.
//    Driving the library's own path -- name it, mark it DISCOVERED, stop the
//    inquiry -- connects immediately and leaves its state machine in step.
//
// 2. Reporting what happened. Pairing results reach ESP_LOG only, which an
//    Arduino build compiles out, so a failure was invisible and the screen
//    just said "connecting" forever.
static portMUX_TYPE pairingMux=portMUX_INITIALIZER_UNLOCKED;
static volatile bool pairingActive=false;
static volatile cyd::PairingSession::Phase pairingPhase=cyd::PairingSession::Normal;
static volatile BtFailure failure=BT_FAIL_NONE;
static bool bluetoothReady=false,pairResumeWanted=false;
static uint32_t pairPauseIntent=0,pairResumeIdleSince=0;
static volatile bool controlPending=false,disconnectPending=false;
static uint8_t disconnectAddr[6]{};
static uint32_t disconnectDeadline=0;
static portMUX_TYPE volumeMux=portMUX_INITIALIZER_UNLOCKED;
static cyd::BtVolumeSync volumeSync;
static uint32_t volumeRequestSeen=0;

// When the player may page its saved speaker unprompted (bt_reconnect_policy.h).
// Written from BtAppT (link events) and the loop task (ticks, owner actions).
static portMUX_TYPE policyMux=portMUX_INITIALIZER_UNLOCKED;
static cyd::ReconnectPolicy policy;
#define POLICY(call) do{portENTER_CRITICAL(&policyMux);policy.call;portEXIT_CRITICAL(&policyMux);}while(0)

// A device the owner has connected to before. Read-only; a torn read during a
// Forget on the loop task can only make one peer-initiated connect fail once.
static bool isKnownPeer(const uint8_t* a){
  if(cyd::PairingSession::any(g_settings.btAddr)&&memcmp(g_settings.btAddr,a,6)==0)return true;
  for(int i=0;i<g_settings.btKnownCount;i++)if(memcmp(g_settings.btKnown[i].addr,a,6)==0)return true;
  return false;
}


// Diagnostic snapshots only; BtAppT writes, loop reads aligned scalar values.
// ESP32-A2DP 1.8.12 BluetoothA2DPSource.cpp (not exported by its header).
static constexpr uint16_t SOURCE_HEART_BEAT_EVT=0xff00;
static volatile esp_a2d_audio_state_t streamAudio=ESP_A2D_AUDIO_STATE_SUSPEND;
static volatile int streamMedia=0;
static uint32_t intentSeen=0,intentPulls=0,intentFrames=0;
static const char* audioName(esp_a2d_audio_state_t s){return s==ESP_A2D_AUDIO_STATE_STARTED?"STARTED":"SUSPEND";}
static const char* mediaName(int s){
  switch(s){case 0:return "IDLE";case 1:return "STARTING";case 2:return "STARTED";case 3:return "STOPPING";default:return "UNKNOWN";}
}
static const char* connName(esp_a2d_connection_state_t s){
  switch(s){case ESP_A2D_CONNECTION_STATE_DISCONNECTED:return "DISCONNECTED";case ESP_A2D_CONNECTION_STATE_CONNECTING:return "CONNECTING";case ESP_A2D_CONNECTION_STATE_CONNECTED:return "CONNECTED";case ESP_A2D_CONNECTION_STATE_DISCONNECTING:return "DISCONNECTING";default:return "UNKNOWN";}
}
static const char* avName(APP_AV_STATE s){
  switch(s){case APP_AV_STATE_IDLE:return "IDLE";case APP_AV_STATE_DISCOVERING:return "DISCOVERING";case APP_AV_STATE_DISCOVERED:return "DISCOVERED";case APP_AV_STATE_UNCONNECTED:return "UNCONNECTED";case APP_AV_STATE_CONNECTING:return "CONNECTING";case APP_AV_STATE_CONNECTED:return "CONNECTED";case APP_AV_STATE_DISCONNECTING:return "DISCONNECTING";default:return "UNKNOWN";}
}

class CydSource : public BluetoothA2DPSource {
 public:
  enum Command:uint16_t { Pair=0xfe00, Cancel, Select, Drop, Scan, ChaseOff, Tick, InquiryStarted, InquiryStopped, Page, UserDrop };
  struct Request {uint32_t generation;uint8_t addr[6];char name[33];};
  CydSource(){owner=this;}
  bool post(Command cmd,const uint8_t* addr=nullptr,const char* name=""){
    Request q{};
    portENTER_CRITICAL(&pairingMux);q.generation=++submitted;portEXIT_CRITICAL(&pairingMux);
    if(addr)memcpy(q.addr,addr,6);strlcpy(q.name,name,sizeof(q.name));
    const bool queued=bt_app_work_dispatch(dispatch,cmd,&q,sizeof(q),nullptr);
    if(!queued){portENTER_CRITICAL(&pairingMux);if(submitted==q.generation)--submitted;portEXIT_CRITICAL(&pairingMux);}
    return queued;
  }
  void poll(){bt_app_work_dispatch(dispatch,Tick,nullptr,0,nullptr);}
  bool needsPoll()const{
    portENTER_CRITICAL(&pairingMux);
    const bool pending=controlPending;
    portEXIT_CRITICAL(&pairingMux);
    return pending;
  }
  bool connectNow(const uint8_t addr[6],const char* name){return post(Select,addr,name);}
  // The saved speaker, unprompted: an idle session only, never drops a link.
  bool pageSaved(const uint8_t addr[6],const char* name){return post(Page,addr,name);}
  const uint8_t* peer()const{return lastPeer;}
  void chaseLastDevice(bool){post(ChaseOff);}
  void disconnectFrom(const uint8_t addr[6]){post(Drop,addr);}
  // Never discoverable outside the Pair screen (see publish()).
  void hide(){discoverability=ESP_BT_NON_DISCOVERABLE;}
  void rediscover(){post(Scan);}
  // PairingSession transport, called only on BtAppT.
  bool linkBusy()const{return connection_state!=ESP_A2D_CONNECTION_STATE_DISCONNECTED||s_a2d_state==APP_AV_STATE_CONNECTING||s_a2d_state==APP_AV_STATE_DISCONNECTING;}
  bool linkConnected()const{return connection_state==ESP_A2D_CONNECTION_STATE_CONNECTED;}
  void savePeer(){write_address(last_bda_nvs_name(),lastPeer);}
  void forgetSavedPeer(){uint8_t none[6]{};write_address(last_bda_nvs_name(),none);clean_last_connection();}
  bool scanning()const{return discovery_active;}
  bool inquiryStarting()const{return inquiryPending;}
  bool drop(){markOurDrop();return esp_a2d_disconnect(lastPeer)==ESP_OK;}
  bool stopScan(){return esp_bt_gap_cancel_discovery()==ESP_OK;}
  bool startScan(){
    s_a2d_state=APP_AV_STATE_DISCOVERING;visible=true;
    set_discoverability(ESP_BT_GENERAL_DISCOVERABLE);set_connectable(true);
    const esp_err_t result=esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,10,0);
    inquiryPending=result==ESP_OK;
    if(result!=ESP_OK)s_a2d_state=APP_AV_STATE_UNCONNECTED;
    return result==ESP_OK;
  }
  bool connect(const uint8_t* addr){
    memcpy(peer_bd_addr,addr,6);memcpy(lastPeer,addr,6);
    s_a2d_state=APP_AV_STATE_CONNECTING;
    const esp_err_t result=esp_a2d_connect(peer_bd_addr);
    Serial.printf("bt: live connect -> %d\n",result);
    if(result!=ESP_OK)s_a2d_state=APP_AV_STATE_UNCONNECTED;
    return result==ESP_OK;
  }
  void failed(const char* operation){failure=strcmp(operation,"timeout")==0?BT_FAIL_TIMEOUT:BT_FAIL_RADIO;Serial.printf("bt: live pairing failed: %s\n",operation);}
  // A background page ended without a link: counted, never shown as an error.
  void pageFailed(const char* operation){POLICY(pageFailed(millis()));Serial.printf("bt: page failed: %s\n",operation);}
 protected:
  int32_t get_audio_data_volume(uint8_t* data,int32_t len) override {
    // Absolute volume is applied by the receiver, not again by this library.
    return audio_remoteVolume()?get_audio_data(data,len):BluetoothA2DPSource::get_audio_data_volume(data,len);
  }
  void bt_av_notify_evt_handler(uint8_t event,esp_avrc_rn_param_t* param) override {
    if(event==ESP_AVRC_RN_VOLUME_CHANGE){
      portENTER_CRITICAL(&volumeMux);volumeSync.notification(param->volume);portEXIT_CRITICAL(&volumeMux);
      bt_av_volume_changed(); // Re-arm the notification without echoing a command.
      return;
    }
    BluetoothA2DPSource::bt_av_notify_evt_handler(event,param);
  }
  void bt_av_hdl_avrc_ct_evt(uint16_t event,void* param) override {
    auto* rc=(esp_avrc_ct_cb_param_t*)param;
    if(event==ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT){
      portENTER_CRITICAL(&volumeMux);volumeSync.response(rc->set_volume_rsp.volume);portEXIT_CRITICAL(&volumeMux);
      return; // The base handler calls set_volume(), sending another command.
    }
    if(event==ESP_AVRC_CT_CONNECTION_STATE_EVT&&memcmp(rc->conn_stat.remote_bda,lastPeer,6)==0){
      portENTER_CRITICAL(&volumeMux);volumeSync.connection(rc->conn_stat.connected);portEXIT_CRITICAL(&volumeMux);
    }
    BluetoothA2DPSource::bt_av_hdl_avrc_ct_evt(event,param);
    if(event==ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT){
      const bool supported=esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST,
        &rc->get_rn_caps_rsp.evt_set,ESP_AVRC_RN_VOLUME_CHANGE);
      const uint8_t initial=audio_volume();
      portENTER_CRITICAL(&volumeMux);const bool started=volumeSync.capabilities(supported,initial);portEXIT_CRITICAL(&volumeMux);
      if(started)Serial.printf("bt: initial volume sync target=%u%%\n",unsigned(initial));
    }
  }
  void bt_app_av_sm_hdlr(uint16_t event,void* param) override {
    rollEventWindow(millis());
    auto* diag=(esp_a2d_cb_param_t*)param;
    if(diag&&event==ESP_A2D_MEDIA_CTRL_ACK_EVT){
      const unsigned cmd=unsigned(diag->media_ctrl_stat.cmd);
      const long delay=cmd>=1&&cmd<=3&&(cmdMask&(1u<<cmd))?long(millis()-cmdAt[cmd-1]):-1L;
      Serial.printf("bt: media ack cmd=%u status=%d ms=%ld\n",cmd,int(diag->media_ctrl_stat.status),delay);
      if(cmd>=1&&cmd<=3)cmdMask&=~(1u<<cmd);
    }
    if(param&&event==ESP_A2D_CONNECTION_STATE_EVT){
      auto* p=(esp_a2d_cb_param_t*)param;const uint8_t* addr=p->conn_stat.remote_bda;
      const auto next=p->conn_stat.state;
      portENTER_CRITICAL(&policyMux);
      const bool permitted=policy.acceptsPeer();
      portEXIT_CRITICAL(&policyMux);
      if(next==ESP_A2D_CONNECTION_STATE_CONNECTED&&(!permitted||!session.accepts(addr,isKnownPeer(addr)))){
        Serial.println("bt: ignoring stale/unselected peer");logEvent(event);markOurDrop();esp_a2d_disconnect((uint8_t*)addr);return;
      }
      if(linkBusy()&&cyd::PairingSession::any(lastPeer)&&memcmp(addr,lastPeer,6)!=0){logEvent(event);return;}
      if(next==ESP_A2D_CONNECTION_STATE_CONNECTING||next==ESP_A2D_CONNECTION_STATE_CONNECTED)memcpy(lastPeer,addr,6);
      // The library pages the saved speaker once at boot and then stays out of it:
      // every later page, with its backoff and cap, is bt_tick's (bt_reconnect_policy.h).
      is_autoreconnect_allowed=false;reconnect_status=NoReconnect;
      if(next==ESP_A2D_CONNECTION_STATE_DISCONNECTED){
        portENTER_CRITICAL(&volumeMux);volumeSync.connection(false);portEXIT_CRITICAL(&volumeMux);
        // connection_state still holds the state before this event.
        const bool wasUp=connection_state==ESP_A2D_CONNECTION_STATE_CONNECTED;
        const bool ours=ourDropUntil&&int32_t(millis()-ourDropUntil)<0;ourDropUntil=0;
        const bool abnormal=p->conn_stat.disc_rsn==ESP_A2D_DISC_RSN_ABNORMAL;
        POLICY(linkDown(millis(),ours,wasUp,abnormal));
        Serial.printf("bt: link ended (%s, %s%s)\n",wasUp?"was up":"page",abnormal?"abnormal":"normal",ours?", ours":"");
      }
      if(next==ESP_A2D_CONNECTION_STATE_CONNECTED){
        if(connection_state!=ESP_A2D_CONNECTION_STATE_CONNECTED){++epoch;cmdMask=0;}
        POLICY(linkUp());
        set_last_connection((uint8_t*)addr);
        session.connected();pairingActive=false;
        if(s_a2d_state==APP_AV_STATE_DISCOVERING||s_a2d_state==APP_AV_STATE_DISCOVERED||s_a2d_state==APP_AV_STATE_UNCONNECTED)s_a2d_state=APP_AV_STATE_CONNECTING;
        if(discovery_active)esp_bt_gap_cancel_discovery();
      }
    }
    // No virtual media-send hook: mirror the pinned library's dispatch branches.
    // rc=-1 means unavailable; these lines infer sends, never issue commands.
    if(s_a2d_state==APP_AV_STATE_CONNECTING&&
       (event==ESP_A2D_AUDIO_STATE_EVT||event==ESP_A2D_AUDIO_CFG_EVT||event==ESP_A2D_MEDIA_CTRL_ACK_EVT||event==SOURCE_HEART_BEAT_EVT))inferCommand(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
    else if(s_a2d_state==APP_AV_STATE_CONNECTED){
      if(event==SOURCE_HEART_BEAT_EVT&&s_media_state==0)inferCommand(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
      if(diag&&event==ESP_A2D_MEDIA_CTRL_ACK_EVT){
        if(s_media_state==0&&diag->media_ctrl_stat.cmd==ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY&&diag->media_ctrl_stat.status==ESP_A2D_MEDIA_CTRL_ACK_SUCCESS)inferCommand(ESP_A2D_MEDIA_CTRL_START);
        if(s_media_state==3&&!(diag->media_ctrl_stat.cmd==ESP_A2D_MEDIA_CTRL_SUSPEND&&diag->media_ctrl_stat.status==ESP_A2D_MEDIA_CTRL_ACK_SUCCESS))inferCommand(ESP_A2D_MEDIA_CTRL_SUSPEND);
      }
    }
    BluetoothA2DPSource::bt_app_av_sm_hdlr(event,param);
    streamMedia=s_media_state;
    if(diag&&(event==ESP_A2D_CONNECTION_STATE_EVT||event==ESP_A2D_AUDIO_STATE_EVT||event==ESP_A2D_MEDIA_CTRL_ACK_EVT))logEvent(event);
    if(diag&&event==ESP_A2D_CONNECTION_STATE_EVT&&diag->conn_stat.state==ESP_A2D_CONNECTION_STATE_DISCONNECTED)cmdMask=0;
    session.drive(millis(),*this);publish();
  }
  void app_gap_callback(esp_bt_gap_cb_event_t event,esp_bt_gap_cb_param_t* param) override;
  esp_err_t esp_a2d_disconnect(esp_bd_addr_t peer) override {
    const esp_err_t result = BluetoothA2DPSource::esp_a2d_disconnect(peer);
    Serial.printf("bt: disconnect rc=%d\n", int(result));
    return result;
  }
 private:
  uint32_t epoch=0,evtWindow=0,evtDropped=0,cmdAt[3]{};
  uint8_t evtCount=0,cmdMask=0;
  void inferCommand(esp_a2d_media_ctrl_t cmd){
    cmdAt[unsigned(cmd)-1]=millis();cmdMask|=1u<<unsigned(cmd);
    Serial.printf("bt: media cmd=%s rc=-1\n",cmd==ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY?"CHECK":cmd==ESP_A2D_MEDIA_CTRL_START?"START":"SUSPEND");
  }
  void rollEventWindow(uint32_t now){
    if(now-evtWindow<1000)return;
    evtWindow=now;evtCount=0;
    if(evtDropped){Serial.printf("bt: evt dropped=%lu\n",(unsigned long)evtDropped);evtCount=1;evtDropped=0;}
  }
  void logEvent(uint16_t event){
    const uint32_t now=millis();
    rollEventWindow(now);
    if(evtCount>=20){++evtDropped;return;}++evtCount;
    Serial.printf("bt: evt ms=%lu ev=%s conn=%s av=%s media=%s audio=%s epoch=%lu\n",
      (unsigned long)now,event==ESP_A2D_CONNECTION_STATE_EVT?"CONNECTION":event==ESP_A2D_AUDIO_STATE_EVT?"AUDIO":"MEDIA_ACK",
      connName(connection_state),avName(s_a2d_state),mediaName(s_media_state),audioName(streamAudio),(unsigned long)epoch);
  }
  inline static CydSource* owner=nullptr;
  uint32_t submitted=0,accepted=0;
  bool inquiryPending=false;
  uint8_t lastPeer[6]{};
  uint32_t ourDropUntil=0;        // a disconnect we asked for is expected until then
  bool visible=false;             // discoverable for the Pair screen
  void markOurDrop(){ourDropUntil=millis()+5000;if(!ourDropUntil)ourDropUntil=1;}
  cyd::PairingSession session;
  static void dispatch(uint16_t event,void* p){owner->command((Command)event,(Request*)p);}
  void publish(){
    portENTER_CRITICAL(&pairingMux);
    pairingPhase=session.phase;
    controlPending=session.needsTick();
    // A queued UI command owns its claim until BtAppT accepts it. An older
    // Tick/connection callback must not reopen saved-device paging meanwhile.
    if(accepted==submitted)pairingActive=session.pairing;
    portEXIT_CRITICAL(&pairingMux);
    // Findable only while a pairing session is running; when it ends (connected,
    // failed, cancelled) the board stays connectable for known devices only.
    if(visible&&!session.pairing){visible=false;hide();esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,ESP_BT_NON_DISCOVERABLE);}
  }
  void fixedGap(esp_bt_gap_cb_event_t event,esp_bt_gap_cb_param_t* param);
  static void dispatchGap(uint16_t event,void* p){owner->fixedGap((esp_bt_gap_cb_event_t)event,(esp_bt_gap_cb_param_t*)p);}
  void command(Command cmd,Request* p){
    rollEventWindow(millis()); // Tick flushes drops even if no later A2DP event arrives.
    if(p){if(p->generation<accepted){Serial.printf("cmd: bt %u ignored (superseded)\n",unsigned(cmd));return;}accepted=p->generation;}
    if(cmd==InquiryStarted){inquiryPending=false;discovery_active=true;if(linkConnected()||!session.pairing)esp_bt_gap_cancel_discovery();}
    else if(cmd==InquiryStopped){inquiryPending=false;discovery_active=false;session.stopped();}
    else if(cmd==Pair){POLICY(userAction(millis()));if(!cyd::PairingSession::any(lastPeer))memcpy(lastPeer,peer_bd_addr,6);is_autoreconnect_allowed=false;reconnect_status=NoReconnect;session.begin(p->addr,millis());}
    else if(cmd==Cancel){is_autoreconnect_allowed=false;reconnect_status=NoReconnect;session.cancel(millis());}
    else if(cmd==Page){
      is_autoreconnect_allowed=false;reconnect_status=NoReconnect;
      if(session.page(p->addr,millis()))strlcpy((char*)s_peer_bdname,p->name,sizeof(s_peer_bdname));
      else{Serial.println("cmd: page skipped (session busy)");pageFailed("busy");}
    }
    else if(cmd==Select){
      POLICY(userAction(millis()));
      set_connectable(true);
      is_autoreconnect_allowed=false;reconnect_status=NoReconnect;
      strlcpy((char*)s_peer_bdname,p->name,sizeof(s_peer_bdname));session.select(p->addr,millis());
    }else if(cmd==UserDrop){
      if(!linkConnected()||memcmp(lastPeer,p->addr,6)!=0){
        portENTER_CRITICAL(&pairingMux);disconnectPending=false;portEXIT_CRITICAL(&pairingMux);
        failure=BT_FAIL_RADIO;
      }else{
        is_autoreconnect_allowed=false;reconnect_status=NoReconnect;
        POLICY(userStop());
        set_connectable(false);
        session=cyd::PairingSession{};
        markOurDrop();
        if(esp_a2d_disconnect(p->addr)!=ESP_OK){
          portENTER_CRITICAL(&pairingMux);disconnectPending=false;portEXIT_CRITICAL(&pairingMux);
          failure=BT_FAIL_RADIO;
        }
      }
    }else if(cmd==Drop){markOurDrop();esp_a2d_disconnect(p->addr);}
    else if(cmd==Scan){const uint8_t none[6]{};session.begin(none,millis());}
    else if(cmd==ChaseOff)is_autoreconnect_allowed=false;
    session.drive(millis(),*this);publish();
  }
};

static CydSource a2dp;

static BtDevice     devs[BT_MAX_DEVICES];
static int          devCount = 0;
static volatile uint32_t listSerial = 0;
static portMUX_TYPE devMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool haveTarget = false;
static uint8_t       targetAddr[6];
static char          targetName[33];

static volatile esp_a2d_connection_state_t state = ESP_A2D_CONNECTION_STATE_DISCONNECTED;

// Nothing connected AND nothing being torn down. Paging during a disconnect is
// refused by the peer, which costs a whole retry cycle.
static bool btIdle() { return state == ESP_A2D_CONNECTION_STATE_DISCONNECTED; }

// Pairing is a live session; bonds and playback survive entry/cancellation.
static volatile bool saveWanted = false;
static volatile bool adoptTarget = false;

// A connection the owner asked for, and how it went.
static const uint32_t CONNECT_TIMEOUT_MS = 20000;
// An owner action re-arms saved-speaker paging with a new budget.
static void ownerActed(){POLICY(userAction(millis()));}

// Who is on the other end. The name cannot come from the tap alone: the car
// connected after the wait for it had already timed out, and a head unit may
// open the link itself, so the device shown was whatever connected last time.
// Ask the stack instead, and believe the answer.
static uint8_t           connectedAddr[6] = { 0 };
static volatile bool     nameWanted = false;      // ask who this is
// A name is only ever learned WITH the address it belongs to. The library asks
// the name of every device an inquiry turns up, so an unattached name is some
// passing device -- a car in the next parking space -- not the one playing.
static uint8_t           learnedAddr[6] = { 0 };
static char              learnedName[33] = { 0 };
static volatile bool     learned = false;
static uint32_t          connectDeadline = 0;      // 0 = nothing pending

static volatile uint32_t  passkey = 0;

// Bluetooth task: pairing events the library only logs.
void CydSource::app_gap_callback(esp_bt_gap_cb_event_t event,esp_bt_gap_cb_param_t* param){
  if(event==ESP_BT_GAP_DISC_STATE_CHANGED_EVT){
    if(!bt_app_work_dispatch(dispatch,param->disc_st_chg.state==ESP_BT_GAP_DISCOVERY_STARTED?InquiryStarted:InquiryStopped,nullptr,0,nullptr))failure=BT_FAIL_TIMEOUT;
    return;
  }
  if(event==ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT)return;
  // These two payloads own inline data, so a queue copy is safe. DISC_RES owns
  // pointer properties and must remain synchronous (onSsid only copies the list).
  if(event==ESP_BT_GAP_AUTH_CMPL_EVT||event==ESP_BT_GAP_READ_REMOTE_NAME_EVT){
    if(!bt_app_work_dispatch(dispatchGap,event,param,sizeof(*param),nullptr))failure=BT_FAIL_TIMEOUT;
    return;
  }
  fixedGap(event,param);
}
void CydSource::fixedGap(esp_bt_gap_cb_event_t event,esp_bt_gap_cb_param_t* param){
  if(event==ESP_BT_GAP_AUTH_CMPL_EVT&&!session.accepts(param->auth_cmpl.bda,isKnownPeer(param->auth_cmpl.bda)))return;
  switch (event) {
    case ESP_BT_GAP_KEY_NOTIF_EVT:
      passkey = param->key_notif.passkey;
      Serial.printf("bt: passkey %06u -- confirm it on the device\n", param->key_notif.passkey);
      break;
    case ESP_BT_GAP_CFM_REQ_EVT:
      passkey = param->cfm_req.num_val;
      Serial.printf("bt: confirm %06u (answered yes)\n", param->cfm_req.num_val);
      break;
    case ESP_BT_GAP_PIN_REQ_EVT:
      Serial.println("bt: the device asked for a PIN (answering with the default)");
      break;
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
      if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS) {
        memcpy(learnedAddr, param->read_rmt_name.bda, 6);
        strlcpy(learnedName, (const char*)param->read_rmt_name.rmt_name, sizeof(learnedName));
        learned = true;
      }
      break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
      if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
        Serial.printf("bt: paired with \"%s\"\n", (const char*)param->auth_cmpl.device_name);
        memcpy(learnedAddr, param->auth_cmpl.bda, 6);
        strlcpy(learnedName, (const char*)param->auth_cmpl.device_name, sizeof(learnedName));
        learned = true;
      } else {
        Serial.printf("bt: pairing REFUSED, status %d\n", param->auth_cmpl.stat);
        failure = BT_FAIL_AUTH;
        connectDeadline = 0;
      }
      passkey = 0;
      break;
    default:
      break;
  }
  BluetoothA2DPSource::app_gap_callback(event, param);
}

// Bluetooth task: every device discovery reports.
static bool onSsid(const char* ssid, esp_bd_addr_t address, int rssi) {
  const char* name = (ssid && *ssid) ? ssid : "(no name)";
  portENTER_CRITICAL(&devMux);
  int i = 0;
  for (; i < devCount; i++)
    if (memcmp(devs[i].addr, address, 6) == 0) break;
  if (i == devCount && devCount < BT_MAX_DEVICES) devCount++;
  if (i < devCount) {
    if(memcmp(devs[i].addr,address,6)!=0||strcmp(devs[i].name,name)!=0||devs[i].rssi!=rssi){
      strlcpy(devs[i].name, name, sizeof(devs[i].name));
      memcpy(devs[i].addr, address, 6);
      devs[i].rssi = rssi;
      ++listSerial;
    }
  }
  const bool tapped = haveTarget && memcmp(targetAddr, address, 6) == 0;
  portEXIT_CRITICAL(&devMux);

  // Selection/paging is driven by UI/loop commands, never by this GAP callback.
  (void)tapped;
  return false;
}

static void onState(esp_a2d_connection_state_t s, void*) {
  static const char* NAMES[] = { "disconnected", "connecting", "connected", "disconnecting" };
  Serial.printf("bt: %s  heap %u\n", s <= 3 ? NAMES[s] : "?", ESP.getFreeHeap());
  portENTER_CRITICAL(&pairingMux);
  state = s;
  if(s==ESP_A2D_CONNECTION_STATE_CONNECTED)memcpy(connectedAddr,a2dp.peer(),6);
  if(s==ESP_A2D_CONNECTION_STATE_DISCONNECTED&&disconnectPending){
    disconnectPending=false;failure=BT_FAIL_NONE;
  }
  portEXIT_CRITICAL(&pairingMux);
  if (s == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    connectDeadline = 0;
    failure = BT_FAIL_NONE;

    passkey = 0;
    const uint8_t* peer = a2dp.peer();
    bool changed = memcmp(g_settings.btAddr, peer, 6) != 0;
    // The device the owner tapped -- by address, because the wait for it may
    // have timed out before it answered, which is what the car does.
    if (targetName[0] && memcmp(targetAddr, peer, 6) == 0) {
      adoptTarget = true;                    // the String is assigned in bt_tick,
      changed = true;                        // on the UI's own task
    } else {
      nameWanted = true;                     // somebody else: ask who
    }
    haveTarget = false;
    if (changed) {
      memcpy(g_settings.btAddr, peer, 6);
      saveWanted = true;                     // NVS write happens in bt_tick, not
    }                                        // on this 3 KB Bluetooth task stack
  }
}

// ---------------------------------------------------------------------------
// Speaker buttons (AVRCP pass-through)
// ---------------------------------------------------------------------------
// The speaker's own play / next / previous buttons arrive here on the
// Bluetooth task. The audio_* calls belong to the loop task (they share the
// play order), so the key is only recorded; bt_tick() acts on it.
static const uint8_t AVRC_VOL_UP = 0x41, AVRC_VOL_DOWN = 0x42, AVRC_PLAY = 0x44,
                     AVRC_STOP = 0x45, AVRC_PAUSE = 0x46, AVRC_FORWARD = 0x4B,
                     AVRC_BACKWARD = 0x4C;
static cyd::CommandQueue<16> speakerKeys;
static portMUX_TYPE keyMux=portMUX_INITIALIZER_UNLOCKED;

static void onSpeakerKey(uint8_t key, bool isReleased) {
  if (!isReleased) return; // one action per press
  portENTER_CRITICAL(&keyMux);
  const bool queued=speakerKeys.push(key);
  portEXIT_CRITICAL(&keyMux);
  Serial.printf("cmd: avrcp 0x%02x %s\n",key,queued?"queued":"ignored (queue full)");
}

static void handleSpeakerKey(uint8_t key) {
  switch (key) {
    case AVRC_PLAY:
      audio_setPlaying(true);
      break;
    case AVRC_PAUSE:
    case AVRC_STOP:
      audio_setPlaying(false);
      break;
    case AVRC_FORWARD:  audio_next(); break;
    case AVRC_BACKWARD: audio_prev(); break;
    // Absolute-volume receivers already changed their own gain and report it.
    // A second pass-through event from the same press must not add another step.
    case AVRC_VOL_UP:   if(!audio_remoteVolume())audio_setVolume(min(100, audio_volume() + 5)); break;
    case AVRC_VOL_DOWN: if(!audio_remoteVolume())audio_setVolume(max(0, audio_volume() - 5)); break;
    default:
      Serial.printf("cmd: avrcp 0x%02x ignored (unsupported)\n", key);
      return;
  }
  Serial.printf("cmd: avrcp 0x%02x executed\n", key);
}

// Frame is {int16_t channel1, channel2}, packed: the interleaved layout
// audio_pull fills.
static int32_t onData(Frame* frames, int32_t count) {
  // Only one consumer may pull from the ring (speaker.h). When the owner has
  // routed audio to the on-board speaker the link stays up and carries
  // silence: tearing the A2DP stack down and back up for a menu tap is a far
  // bigger risk than 44 KB/s of zeroes.
  if (speaker_route() != OUT_BT) {
    memset(frames, 0, count * sizeof(Frame));
    return count;
  }
  audio_pull((int16_t*)frames, count);
  return count;
}

// Registered callback also enables the library's audio_state cache update.
// It runs on BtAppT; the event line is emitted after the library handles it.
static void onAudioState(esp_a2d_audio_state_t s,void*){streamAudio=s;}

void bt_begin() {
  volumeRequestSeen=audio_volumeRequestSerial();
  intentSeen=audio_playIntentSerial();intentPulls=audio_pullCalls();intentFrames=audio_pullFrames();
  a2dp.set_local_name("CYD-MP3");
  a2dp.set_ssid_callback(onSsid);
  a2dp.set_on_connection_state_changed(onState);
  a2dp.set_on_audio_state_changed(onAudioState);
  a2dp.set_data_callback_in_frames(onData);
  a2dp.set_avrc_passthru_command_callback(onSpeakerKey);
  a2dp.set_volume(100); // Preserve fallback gain until the peer reports absolute volume.
  // Secure Simple Pairing: what a car expects. The library answers the
  // "compare this number" request for us; app_gap_callback above keeps the
  // number so the screen can show what the owner is agreeing to.
  a2dp.set_ssp_enabled(true);
  // Connectable for known devices, never findable until the Pair screen asks.
  a2dp.hide();

  // The library pages the saved speaker once when the stack is up (bt_tick
  // leaves it BOOT_PAGE_WINDOW_MS); its own retries and its fallback inquiry
  // are switched off by the first connection event (bt_app_av_sm_hdlr).
  // Anything later is bt_tick's, under bt_reconnect_policy.h.
  a2dp.set_auto_reconnect(true, 1);
  POLICY(boot(millis(),cyd::PairingSession::any(g_settings.btAddr)));
  a2dp.start();
  bluetoothReady=true;
  Serial.printf("bt: started (live pairing), saved speaker \"%s\"  heap %u\n",g_settings.btName.c_str(),ESP.getFreeHeap());
}

// Add a device to the list of those connected before, or refresh its name.
// Matched by address: a device may be renamed, and two may share a name.
//
// The order is the order they were first connected, and it does not change
// when one of them connects again. Sorting by most-recent moved the row under
// the owner's finger and made the highlight look stuck on the first line.
// When the list is full the oldest goes.
static void remember(const uint8_t addr[6], const char* name) {
  Settings& g = g_settings;
  for (int i = 0; i < g.btKnownCount; i++) {
    if (memcmp(g.btKnown[i].addr, addr, 6) == 0) {
      strlcpy(g.btKnown[i].name, name, sizeof(g.btKnown[i].name));
      return;
    }
  }
  if (g.btKnownCount == BT_KNOWN_MAX) {
    for (int i = 0; i + 1 < BT_KNOWN_MAX; i++) g.btKnown[i] = g.btKnown[i + 1];
    g.btKnownCount--;
  }
  BtKnown& slot = g.btKnown[g.btKnownCount++];
  memcpy(slot.addr, addr, 6);
  strlcpy(slot.name, name, sizeof(slot.name));
}

void bt_tick() {
  if (!bluetoothReady) return;
  const uint32_t request=audio_volumeRequestSerial();
  bool publish=false,remote=false,send=false;uint8_t pct=0,raw=0,label=0;
  portENTER_CRITICAL(&volumeMux);
  if(request!=volumeRequestSeen){volumeRequestSeen=request;volumeSync.local(audio_volume());}
  send=volumeSync.command(millis(),raw,label);
  publish=volumeSync.take(remote,pct);
  portEXIT_CRITICAL(&volumeMux);
  if(publish){
    audio_setRemoteVolume(remote&&speaker_route()==OUT_BT,pct);
    if(remote){g_settings.volume=audio_volume();Serial.printf("bt: speaker volume %u%% (absolute)\n",unsigned(pct));}
  }
  if(send){
    const esp_err_t result=esp_avrc_ct_send_set_absolute_volume_cmd(label,raw);
    if(result!=ESP_OK){portENTER_CRITICAL(&volumeMux);volumeSync.failed();portEXIT_CRITICAL(&volumeMux);}
    Serial.printf("bt: set speaker volume raw=%u rc=%d\n",unsigned(raw),int(result));
  }
  if(failure!=BT_FAIL_NONE){haveTarget=false;connectDeadline=0;POLICY(connectFailed());}
  POLICY(expire(millis()));
  // Give up on a connection the owner asked for, rather than saying
  // "connecting" until the board is restarted.
  if (connectDeadline && (int32_t)(millis() - connectDeadline) >= 0) {
    connectDeadline = 0;
    haveTarget = false;

    passkey = 0;
    if (failure == BT_FAIL_NONE) failure = BT_FAIL_TIMEOUT;
    Serial.printf("bt: giving up on \"%s\"\n", targetName);
    a2dp.disconnectFrom(targetAddr);
  }


  static uint32_t pollAt=0;
  if(a2dp.needsPoll()){
    if(int32_t(millis()-pollAt)>=0){pollAt=millis()+250;a2dp.poll();}
  }else pollAt=millis();
  portENTER_CRITICAL(&pairingMux);
  if(disconnectPending&&int32_t(millis()-disconnectDeadline)>=0){
    disconnectPending=false;failure=BT_FAIL_TIMEOUT;
  }
  portEXIT_CRITICAL(&pairingMux);
  if(pairResumeWanted&&!pairingActive&&bt_connected()){
    if(speaker_route()==OUT_BT&&!audio_isPlaying()&&cyd::currentCompletion(pairPauseIntent,audio_playIntentSerial()))audio_resumeAfterPairing();
    else Serial.println("cmd: pairing resume ignored (newer play intent or changed route)");
    pairResumeWanted=false;
  }
  // Pairing ended with nothing connected (cancelled with no former device, or
  // failed): do not let a later, unrelated connection start the music.
  if(pairResumeWanted&&!pairingActive&&!bt_connected()){
    if(!pairResumeIdleSince)pairResumeIdleSince=millis()|1;
    else if(millis()-pairResumeIdleSince>=cyd::ReconnectPolicy::PAIR_RESUME_WINDOW_MS){pairResumeWanted=false;pairResumeIdleSince=0;Serial.println("cmd: pairing resume dropped (no reconnect)");}
  }else pairResumeIdleSince=0;

  // Drain in arrival order, outside the callback lock; producer cannot race a clear.
  for(unsigned i=0;i<16;++i){
    uint8_t key=0;
    portENTER_CRITICAL(&keyMux);const bool have=speakerKeys.pop(key);portEXIT_CRITICAL(&keyMux);
    if(!have)break;
    handleSpeakerKey(key);
  }

  const uint32_t intent=audio_playIntentSerial();
  if(intent!=intentSeen){
    const uint32_t pulls=audio_pullCalls(),frames=audio_pullFrames();
    if(speaker_route()==OUT_BT)Serial.printf("bt: play intent=%lu link=%d audio=%s media=%s playing=%d route=BT pulls=%lu frames=%lu\n",
      (unsigned long)intent,int(bt_connected()),audioName(streamAudio),mediaName(streamMedia),int(audio_isPlaying()),
      (unsigned long)(pulls-intentPulls),(unsigned long)(frames-intentFrames));
    intentSeen=intent;intentPulls=pulls;intentFrames=frames;
  }

  // Reach for the saved device rather than waiting to stumble across it.
  // Discovery only finds what is advertising, and a car advertises only while
  // its pairing screen is open. A known device is paged by address, which is
  // what a phone does -- but only as often as bt_reconnect_policy.h allows:
  // never after the other side ended the link on purpose, never while a link
  // exists, and at most MAX_FAILED_PAGES times with growing gaps until the
  // owner acts. The session must be idle so a page cannot cut across a pairing.
  bool page = false;
  if (!pairingActive && !haveTarget && btIdle() &&
      (pairingPhase == cyd::PairingSession::Normal || pairingPhase == cyd::PairingSession::Failed)) {
    portENTER_CRITICAL(&policyMux);
    page = policy.mayPage(millis(), bt_connected(), false, cyd::PairingSession::any(g_settings.btAddr));
    if (page) policy.pageStarted(millis());
    portEXIT_CRITICAL(&policyMux);
  }
  if (page) {
    Serial.printf("bt: paging saved device \"%s\"\n", g_settings.btName.c_str());
    if (!a2dp.pageSaved(g_settings.btAddr, g_settings.btName.c_str())) POLICY(pageFailed(millis()));
  }

  // Connectable so a known speaker or car can open the link itself, but findable
  // only while a pairing session is up. Ten seconds after it starts, the library
  // turns the scan mode off entirely; this puts connectable back. The pairing
  // screen needs "CYD-MP3" in the other device's own list, hence GENERAL there.
  static uint32_t nextVisible = 0;
  portENTER_CRITICAL(&policyMux);
  const bool acceptingPeer=policy.acceptsPeer();
  portEXIT_CRITICAL(&policyMux);
  if (acceptingPeer && !bt_connected() && (int32_t)(millis() - nextVisible) >= 0) {
    nextVisible = millis() + 5000;
    if (pairingActive) a2dp.set_discoverability(ESP_BT_GENERAL_DISCOVERABLE);
    else             a2dp.set_connectable(true);   // discoverability stays NON_DISCOVERABLE
  }

  // Find out who connected, when it was not the device that was tapped. The
  // list from the last scan usually knows; otherwise ask over the air.
  if (nameWanted) {
    nameWanted = false;
    bool found = false;
    BtDevice d;
    for (int i = 0; i < bt_deviceCount() && !found; i++) {
      if (bt_device(i, d) && memcmp(d.addr, connectedAddr, 6) == 0) {
        memcpy(learnedAddr, d.addr, 6);
        strlcpy(learnedName, d.name, sizeof(learnedName));
        learned = found = true;
      }
    }
    if (!found) esp_bt_gap_read_remote_name(connectedAddr);
  }

  if (learned) {
    learned = false;
    // Only the device actually on the other end may rename what is shown.
    if (bt_connected() && memcmp(learnedAddr, connectedAddr, 6) == 0 && learnedName[0]) {
      if (g_settings.btName != learnedName) {
        g_settings.btName = learnedName;
        Serial.printf("bt: connected device is \"%s\"\n", learnedName);
      }
      remember(connectedAddr, learnedName);
      saveWanted = true;
    }
  }

  if (saveWanted) {
    saveWanted = false;
    if (adoptTarget) {
      g_settings.btName = targetName;
      remember(connectedAddr, targetName);
      adoptTarget = false;
    }
    a2dp.savePeer(); // NVS work stays off the 3 KiB Bluetooth event task
    settings_save();
    Serial.printf("bt: saved speaker \"%s\"\n", g_settings.btName.c_str());
  }
}

bool   bt_connected()   { return state == ESP_A2D_CONNECTION_STATE_CONNECTED; }
bool   bt_connecting()  { return state == ESP_A2D_CONNECTION_STATE_CONNECTING; }
String bt_speakerName() { return g_settings.btName; }
cyd::PairingSession::Phase bt_pairingPhase(){return pairingPhase;}
bool   bt_pairing() { return pairingActive; }
bool   bt_choosing()    { return haveTarget; }
BtFailure bt_failure()  { return failure; }
uint32_t  bt_passkey()  { return passkey; }
uint32_t bt_listSerial() { return listSerial; }

bool bt_requestPairing() {
  if(!bluetoothReady){Serial.println("cmd: pair ignored (Bluetooth not ready)");return false;}
  const bool fresh=!pairingActive,pauseForPair=speaker_route()==OUT_BT&&audio_isPlaying();
  if(fresh)pairResumeWanted=pauseForPair;
  if(!a2dp.post(CydSource::Pair,g_settings.btAddr)){failure=BT_FAIL_TIMEOUT;Serial.println("cmd: pair ignored (queue busy)");return false;}
  portENTER_CRITICAL(&pairingMux);pairingActive=true;portEXIT_CRITICAL(&pairingMux);
  haveTarget=false;connectDeadline=0;passkey=0;failure=BT_FAIL_NONE;ownerActed();
  if(pauseForPair){audio_pauseForPairing();pairResumeWanted=true;pairPauseIntent=audio_playIntentSerial();}
  else if(fresh)pairPauseIntent=audio_playIntentSerial();
  portENTER_CRITICAL(&devMux);devCount=0;++listSerial;portEXIT_CRITICAL(&devMux);
  portENTER_CRITICAL(&pairingMux);disconnectPending=false;portEXIT_CRITICAL(&pairingMux);
  Serial.println("cmd: pair queued");return true;
}
bool bt_cancelPairing() {
  if (!bluetoothReady) return false;
  if(!a2dp.post(CydSource::Cancel)){failure=BT_FAIL_TIMEOUT;Serial.println("cmd: cancel pair ignored (queue busy)");return false;}
  haveTarget=false;connectDeadline=0;passkey=0;ownerActed();
  Serial.println("cmd: cancel pair queued");return true;
}

int bt_deviceCount() {
  portENTER_CRITICAL(&devMux);
  const int n = devCount;
  portEXIT_CRITICAL(&devMux);
  return n;
}

bool bt_device(int i, BtDevice& out) {
  portENTER_CRITICAL(&devMux);
  const bool ok = i >= 0 && i < devCount;
  if (ok) out = devs[i];
  portEXIT_CRITICAL(&devMux);
  return ok;
}

bool bt_isConnectedTo(const uint8_t addr[6]) {
  portENTER_CRITICAL(&pairingMux);
  const bool connected=state==ESP_A2D_CONNECTION_STATE_CONNECTED&&memcmp(connectedAddr,addr,6)==0;
  portEXIT_CRITICAL(&pairingMux);
  return connected;
}

int bt_knownCount() { return g_settings.btKnownCount; }

bool bt_known(int i, BtDevice& out) {
  if (i < 0 || i >= g_settings.btKnownCount) return false;
  memcpy(out.addr, g_settings.btKnown[i].addr, 6);
  strlcpy(out.name, g_settings.btKnown[i].name, sizeof(out.name));
  out.rssi = 0;                          // not seen, remembered
  return true;
}

static bool requestConnect(const BtDevice& device) {
  if(!bluetoothReady||disconnectPending)return false;
  if(bt_isConnectedTo(device.addr))return true;
  uint8_t previousAddr[6];char previousName[33];
  portENTER_CRITICAL(&devMux);
  memcpy(previousAddr,targetAddr,6);memcpy(previousName,targetName,sizeof(previousName));
  const bool previousTarget=haveTarget;
  memcpy(targetAddr,device.addr,6);strlcpy(targetName,device.name,sizeof(targetName));haveTarget=true;
  portEXIT_CRITICAL(&devMux);
  if(!a2dp.connectNow(device.addr,device.name)){
    portENTER_CRITICAL(&devMux);
    memcpy(targetAddr,previousAddr,6);memcpy(targetName,previousName,sizeof(targetName));haveTarget=previousTarget;
    portEXIT_CRITICAL(&devMux);
    Serial.println("cmd: connect ignored (Bluetooth queue busy)");return false;
  }
  failure=BT_FAIL_NONE;passkey=0;connectDeadline=millis()+CONNECT_TIMEOUT_MS;
  ownerActed();
  Serial.printf("cmd: connect queued for %s\n",device.name);
  return true;
}

bool bt_connectDevice(const uint8_t addr[6]) {
  BtDevice device;
  for(int i=0;i<bt_knownCount();++i)if(bt_known(i,device)&&memcmp(device.addr,addr,6)==0)return requestConnect(device);
  for(int i=0;i<bt_deviceCount();++i)if(bt_device(i,device)&&memcmp(device.addr,addr,6)==0)return requestConnect(device);
  return false;
}

void bt_connectKnown(int i) {
  BtDevice device;
  if(bt_known(i,device))requestConnect(device);
}

bool bt_disconnect(const uint8_t addr[6]) {
  if(!bluetoothReady)return false;
  portENTER_CRITICAL(&pairingMux);
  const bool matching=state==ESP_A2D_CONNECTION_STATE_CONNECTED&&memcmp(connectedAddr,addr,6)==0;
  if(!matching||disconnectPending){
    const bool already=matching&&disconnectPending&&memcmp(disconnectAddr,addr,6)==0;
    portEXIT_CRITICAL(&pairingMux);return already;
  }
  memcpy(disconnectAddr,addr,6);disconnectDeadline=millis()+CONNECT_TIMEOUT_MS;disconnectPending=true;
  portEXIT_CRITICAL(&pairingMux);
  if(!a2dp.post(CydSource::UserDrop,addr)){
    portENTER_CRITICAL(&pairingMux);disconnectPending=false;portEXIT_CRITICAL(&pairingMux);
    return false;
  }
  audio_setPlaying(false);
  haveTarget=false;connectDeadline=0;pairResumeWanted=false;pairResumeIdleSince=0;
  POLICY(userStop());
  Serial.println("cmd: disconnect queued; bond retained");
  return true;
}

static bool forgetKnown(int i) {
  if (!bluetoothReady) return false;
  if (i < 0 || i >= g_settings.btKnownCount) return false;
  uint8_t addr[6];
  memcpy(addr, g_settings.btKnown[i].addr, 6);
  Serial.printf("bt: forgetting \"%s\"\n", g_settings.btKnown[i].name);

  if(bt_isConnectedTo(addr)&&!disconnectPending&&!bt_disconnect(addr))return false;

  for (int j = i; j + 1 < g_settings.btKnownCount; j++)
    g_settings.btKnown[j] = g_settings.btKnown[j + 1];
  g_settings.btKnownCount--;

  // The queued disconnect owns the address before the last-device record is erased.
  // Remove that record and the bond so the deleted device cannot reconnect at boot.

  if (memcmp(g_settings.btAddr, addr, 6) == 0) {
    static const uint8_t NONE[6] = { 0 };
    memcpy(g_settings.btAddr, NONE, 6);
    g_settings.btName = "";
    haveTarget = false;
    targetName[0] = 0;
    connectDeadline = 0;
    a2dp.forgetSavedPeer();
  }

  // And the pairing itself: with the bond still in the stack the two would
  // know each other, and either side could open the link again.
  const esp_err_t err = esp_bt_gap_remove_bond_device((uint8_t*)addr);
  if (err != ESP_OK) Serial.printf("bt: remove bond -> %s\n", esp_err_to_name(err));

  settings_save();
  return true;
}

void bt_forget(int i){forgetKnown(i);}

void bt_choose(int i) {
  BtDevice device;
  if(bt_device(i,device))requestConnect(device);
}

bool bt_forgetDevice(const uint8_t addr[6]) {
  if(!bluetoothReady||bt_connecting()||haveTarget||disconnectPending)return false;
  BtDevice device;
  for(int i=0;i<bt_knownCount();++i){
    if(bt_known(i,device)&&memcmp(device.addr,addr,6)==0){
      return forgetKnown(i);
    }
  }
  return false;
}

void bt_uiSnapshot(BtUiSnapshot& out) {
  memset(&out,0,sizeof(out));
  portENTER_CRITICAL(&pairingMux);
  out.status.connected=state==ESP_A2D_CONNECTION_STATE_CONNECTED;
  out.status.connecting=state==ESP_A2D_CONNECTION_STATE_CONNECTING;
  out.status.disconnecting=disconnectPending||state==ESP_A2D_CONNECTION_STATE_DISCONNECTING;
  memcpy(out.status.peer,connectedAddr,6);
  out.status.phase=pairingPhase;out.status.pairing=pairingActive;
  out.status.failure=failure;out.status.passkey=passkey;
  portEXIT_CRITICAL(&pairingMux);
  strlcpy(out.status.name,g_settings.btName.c_str(),sizeof(out.status.name));
  for(int i=0;i<g_settings.btKnownCount&&out.count<BT_MAX_DEVICES;++i){
    auto& row=out.rows[out.count++];
    memcpy(row.device.addr,g_settings.btKnown[i].addr,6);
    strlcpy(row.device.name,g_settings.btKnown[i].name,sizeof(row.device.name));row.saved=true;
  }
  portENTER_CRITICAL(&devMux);
  out.status.choosing=haveTarget;
  for(int i=0;i<devCount&&out.count<BT_MAX_DEVICES;++i){
    bool saved=false;
    for(int j=0;j<out.count;++j)if(memcmp(out.rows[j].device.addr,devs[i].addr,6)==0){saved=true;break;}
    if(!saved)out.rows[out.count++].device=devs[i];
  }
  portEXIT_CRITICAL(&devMux);
  for(int i=0;i<out.count;++i)out.rows[i].connected=out.status.connected&&memcmp(out.status.peer,out.rows[i].device.addr,6)==0;
}

#endif  // !CYD_UPLOADER

// Player image only (firmware.h).
#ifndef CYD_UPLOADER

#include "bt.h"
#include "audio.h"
#include "speaker.h"
#include "settings.h"
#include "BluetoothA2DPSource.h"
#include "pairing_session.h"
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
static volatile bool pairingActive=false;
static volatile BtFailure failure=BT_FAIL_NONE;
static bool bluetoothReady=false,pairResumeWanted=false;


class CydSource : public BluetoothA2DPSource {
 public:
  enum Command:uint16_t { Pair=0xfe00, Cancel, Select, Drop, Scan, ChaseOff, Tick, InquiryStarted, InquiryStopped };
  struct Request {uint32_t generation;uint8_t addr[6];char name[33];};
  CydSource(){owner=this;}
  bool post(Command cmd,const uint8_t* addr=nullptr,const char* name=""){
    Request q{};q.generation=++submitted;
    if(addr)memcpy(q.addr,addr,6);strlcpy(q.name,name,sizeof(q.name));
    return bt_app_work_dispatch(dispatch,cmd,&q,sizeof(q),nullptr);
  }
  void poll(){bt_app_work_dispatch(dispatch,Tick,nullptr,0,nullptr);}
  bool connectNow(const uint8_t addr[6],const char* name){return post(Select,addr,name);}
  const uint8_t* peer()const{return lastPeer;}
  void chaseLastDevice(bool){post(ChaseOff);}
  void disconnectFrom(const uint8_t addr[6]){post(Drop,addr);}
  void rediscover(){post(Scan);}
  // PairingSession transport, called only on BtAppT.
  bool linkBusy()const{return connection_state!=ESP_A2D_CONNECTION_STATE_DISCONNECTED||s_a2d_state==APP_AV_STATE_CONNECTING||s_a2d_state==APP_AV_STATE_DISCONNECTING;}
  bool linkConnected()const{return connection_state==ESP_A2D_CONNECTION_STATE_CONNECTED;}
  void savePeer(){write_address(last_bda_nvs_name(),lastPeer);}
  void forgetSavedPeer(){uint8_t none[6]{};write_address(last_bda_nvs_name(),none);clean_last_connection();}
  bool scanning()const{return discovery_active;}
  bool drop(){return esp_a2d_disconnect(lastPeer)==ESP_OK;}
  bool stopScan(){return esp_bt_gap_cancel_discovery()==ESP_OK;}
  bool startScan(){
    s_a2d_state=APP_AV_STATE_DISCOVERING;
    set_discoverability(ESP_BT_GENERAL_DISCOVERABLE);set_connectable(true);
    return esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,10,0)==ESP_OK;
  }
  bool connect(const uint8_t* addr){
    memcpy(peer_bd_addr,addr,6);memcpy(lastPeer,addr,6);
    s_a2d_state=APP_AV_STATE_CONNECTING;
    const esp_err_t result=esp_a2d_connect(peer_bd_addr);
    Serial.printf("bt: live connect -> %d\n",result);return result==ESP_OK;
  }
  void failed(const char* operation){failure=BT_FAIL_TIMEOUT;Serial.printf("bt: live pairing failed: %s\n",operation);}
 protected:
  void app_a2d_callback(esp_a2d_cb_event_t event,esp_a2d_cb_param_t* param) override {
    if(param&&event==ESP_A2D_AUDIO_STATE_EVT)Serial.printf("bt: audio state %d  heap %u\n",int(param->audio_stat.state),ESP.getFreeHeap());
    if(param&&event==ESP_A2D_MEDIA_CTRL_ACK_EVT)Serial.printf("bt: media ack cmd %d status %d  heap %u\n",int(param->media_ctrl_stat.cmd),int(param->media_ctrl_stat.status),ESP.getFreeHeap());
    BluetoothA2DPSource::app_a2d_callback(event,param); // copied into BtAppT's queue
  }
  void bt_app_av_sm_hdlr(uint16_t event,void* param) override {
    if(param&&event==ESP_A2D_CONNECTION_STATE_EVT){
      auto* p=(esp_a2d_cb_param_t*)param;const uint8_t* addr=p->conn_stat.remote_bda;
      const auto next=p->conn_stat.state;
      if(next==ESP_A2D_CONNECTION_STATE_CONNECTED&&!session.accepts(addr)){
        Serial.println("bt: ignoring stale/unselected peer");esp_a2d_disconnect((uint8_t*)addr);return;
      }
      if(linkBusy()&&cyd::PairingSession::any(lastPeer)&&memcmp(addr,lastPeer,6)!=0)return;
      if(next==ESP_A2D_CONNECTION_STATE_CONNECTING||next==ESP_A2D_CONNECTION_STATE_CONNECTED)memcpy(lastPeer,addr,6);
      if(next==ESP_A2D_CONNECTION_STATE_CONNECTED){
        // Disable library paging; the loop owns reconnects and persistence.
        is_autoreconnect_allowed=false;reconnect_status=NoReconnect;set_last_connection((uint8_t*)addr);
        session.connected();pairingActive=false;
        if(s_a2d_state==APP_AV_STATE_DISCOVERING||s_a2d_state==APP_AV_STATE_DISCOVERED||s_a2d_state==APP_AV_STATE_UNCONNECTED)s_a2d_state=APP_AV_STATE_CONNECTING;
        if(discovery_active)esp_bt_gap_cancel_discovery();
      }
    }
    BluetoothA2DPSource::bt_app_av_sm_hdlr(event,param);
    session.drive(millis(),*this);publish();
  }
  void app_gap_callback(esp_bt_gap_cb_event_t event,esp_bt_gap_cb_param_t* param) override;
 private:
  inline static CydSource* owner=nullptr;
  uint32_t submitted=0,accepted=0;
  uint8_t lastPeer[6]{};
  cyd::PairingSession session;
  static void dispatch(uint16_t event,void* p){owner->command((Command)event,(Request*)p);}
  void publish(){pairingActive=session.pairing;}
  void fixedGap(esp_bt_gap_cb_event_t event,esp_bt_gap_cb_param_t* param);
  static void dispatchGap(uint16_t event,void* p){owner->fixedGap((esp_bt_gap_cb_event_t)event,(esp_bt_gap_cb_param_t*)p);}
  void command(Command cmd,Request* p){
    if(p){if(p->generation<accepted)return;accepted=p->generation;}
    if(cmd==InquiryStarted)discovery_active=true;
    else if(cmd==InquiryStopped){discovery_active=false;session.stopped();}
    else if(cmd==Pair){if(!cyd::PairingSession::any(lastPeer))memcpy(lastPeer,peer_bd_addr,6);is_autoreconnect_allowed=false;reconnect_status=NoReconnect;session.begin(p->addr,millis());}
    else if(cmd==Cancel){is_autoreconnect_allowed=false;reconnect_status=NoReconnect;session.cancel(millis());}
    else if(cmd==Select){
      is_autoreconnect_allowed=false;reconnect_status=NoReconnect;
      strlcpy((char*)s_peer_bdname,p->name,sizeof(s_peer_bdname));session.select(p->addr,millis());
    }else if(cmd==Drop){esp_a2d_disconnect(p->addr);}
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
// Switching from one remembered device to another: the link has to be dropped
// first, so the request is held until the radio is free (bt_tick).
static int               pendingKnown = -1;

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
  if(event==ESP_BT_GAP_AUTH_CMPL_EVT&&!session.accepts(param->auth_cmpl.bda))return;
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
    strlcpy(devs[i].name, name, sizeof(devs[i].name));
    memcpy(devs[i].addr, address, 6);
    devs[i].rssi = rssi;
    listSerial++;
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
  state = s;
  if (s == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    connectDeadline = 0;
    failure = BT_FAIL_NONE;

    passkey = 0;
    const uint8_t* peer = a2dp.peer();
    memcpy(connectedAddr, peer, 6);
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
static volatile uint8_t pendingKey = 0;

static void onSpeakerKey(uint8_t key, bool isReleased) {
  if (isReleased) pendingKey = key;          // one action per press
}

static void handleSpeakerKey(uint8_t key) {
  switch (key) {
    case AVRC_PLAY:
      if (!audio_isPlaying()) audio_togglePause();
      break;
    case AVRC_PAUSE:
    case AVRC_STOP:
      if (audio_isPlaying()) audio_togglePause();
      break;
    case AVRC_FORWARD:  audio_next(); break;
    case AVRC_BACKWARD: audio_prev(); break;
    case AVRC_VOL_UP:   audio_setVolume(min(100, audio_volume() + 5)); break;
    case AVRC_VOL_DOWN: audio_setVolume(max(0, audio_volume() - 5)); break;
    default:
      Serial.printf("bt: speaker key 0x%02x ignored\n", key);
      return;
  }
  Serial.printf("bt: speaker key 0x%02x\n", key);
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

void bt_begin() {
  a2dp.set_local_name("CYD-MP3");
  a2dp.set_ssid_callback(onSsid);
  a2dp.set_on_connection_state_changed(onState);
  a2dp.set_data_callback_in_frames(onData);
  a2dp.set_avrc_passthru_command_callback(onSpeakerKey);
  a2dp.set_volume(100);                       // gain is applied in audio_pull
  // Secure Simple Pairing: what a car expects. The library answers the
  // "compare this number" request for us; app_gap_callback above keeps the
  // number so the screen can show what the owner is agreeing to.
  a2dp.set_ssp_enabled(true);

  // Normal boot: page the saved speaker first. Pairing boot: go straight to
  // discovery, then turn auto-reconnect back on so the speaker the user picks
  // is written to the library's NVS for next time.
  // One attempt, not three: a device that does not answer the first page is
  // off or out of range, and three immediate tries only delay the fallback to
  // scanning. Anything later is handled by the 15 s paging in bt_tick().
  a2dp.set_auto_reconnect(true, 1);
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
  if(failure!=BT_FAIL_NONE){haveTarget=false;connectDeadline=0;}
  static uint32_t pollAt=0;
  if(int32_t(millis()-pollAt)>=0){pollAt=millis()+250;a2dp.poll();}
  if(pairResumeWanted&&!pairingActive&&bt_connected()){if(speaker_route()==OUT_BT&&!audio_isPlaying())audio_togglePause();pairResumeWanted=false;}

  const uint8_t key = pendingKey;
  if (key) {
    pendingKey = 0;
    handleSpeakerKey(key);
  }

  // The link asked to be dropped is gone: now page what was asked for.
  if (pendingKnown >= 0 && btIdle()) {
    const int i = pendingKnown;
    pendingKnown = -1;
    bt_connectKnown(i);
  }

  // Reach for the saved device rather than waiting to stumble across it.
  // Discovery only finds what is advertising, and a car advertises only while
  // its pairing screen is open -- so "looking for MBUX" would wait for ever
  // once it had been disconnected. A known device is paged by address, which
  // is what a phone does.
  static uint32_t nextReconnect = 0;
  static const uint8_t NO_ADDR[6] = { 0 };
  if (!pairingActive && !haveTarget && btIdle() &&
      memcmp(g_settings.btAddr, NO_ADDR, 6) != 0 &&
      (int32_t)(millis() - nextReconnect) >= 0) {
    nextReconnect = millis() + 15000;
    Serial.printf("bt: paging saved device \"%s\"\n", g_settings.btName.c_str());
    a2dp.connectNow(g_settings.btAddr, g_settings.btName.c_str());
  }

  // Be findable, not just looking. Ten seconds after it starts, the library
  // turns the scan mode off entirely -- neither discoverable nor connectable --
  // to keep a peer-initiated connection from confusing its state machine. On
  // the pairing screen that is backwards: a car head unit expects to find
  // "CYD-MP3" in its OWN list, and offers no other way in. So re-assert it
  // while that screen is up, and only there: a pairing boot, nothing connected.
  static uint32_t nextVisible = 0;
  if (!bt_connected() && (int32_t)(millis() - nextVisible) >= 0) {
    nextVisible = millis() + 5000;
    // Connectable always, so the car can open the link itself when the engine
    // starts; findable as well only while the pairing screen is up.
    if (pairingActive) a2dp.set_discoverability(ESP_BT_GENERAL_DISCOVERABLE);
    else             a2dp.set_connectable(true);
  }

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
bool   bt_pairing() { return pairingActive; }
bool   bt_choosing()    { return haveTarget; }
BtFailure bt_failure()  { return failure; }
uint32_t  bt_passkey()  { return passkey; }
uint32_t bt_listSerial() { return listSerial; }

bool bt_requestPairing() {
  if(!bluetoothReady)return false;
  if(!pairingActive)pairResumeWanted=speaker_route()==OUT_BT&&audio_isPlaying();
  if(!a2dp.post(CydSource::Pair,g_settings.btAddr)){failure=BT_FAIL_TIMEOUT;return false;}
  pairingActive=true;haveTarget=false;pendingKnown=-1;connectDeadline=0;passkey=0;failure=BT_FAIL_NONE;
  if(speaker_route()==OUT_BT&&audio_isPlaying())audio_togglePause();
  portENTER_CRITICAL(&devMux);devCount=0;++listSerial;portEXIT_CRITICAL(&devMux);
  Serial.println("bt: pair new requested (no restart)");return true;
}
bool bt_cancelPairing() {
  if(!a2dp.post(CydSource::Cancel)){failure=BT_FAIL_TIMEOUT;return false;}
  haveTarget=false;connectDeadline=0;passkey=0;
  Serial.println("bt: cancel pairing requested (no restart)");return true;
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
  return bt_connected() && memcmp(connectedAddr, addr, 6) == 0;
}

int bt_knownCount() { return g_settings.btKnownCount; }

bool bt_known(int i, BtDevice& out) {
  if (i < 0 || i >= g_settings.btKnownCount) return false;
  memcpy(out.addr, g_settings.btKnown[i].addr, 6);
  strlcpy(out.name, g_settings.btKnown[i].name, sizeof(out.name));
  out.rssi = 0;                          // not seen, remembered
  return true;
}

void bt_connectKnown(int i) {
  BtDevice d;
  if (!bt_known(i, d)) return;
  if (bt_connected()) {
    if (memcmp(connectedAddr, d.addr, 6) == 0) return;     // already on it
    // Claim the target before dropping the link: it stops the library chasing
    // what we are leaving, and stops our own paging (which still points at the
    // old address until the new one connects) from doing the same.
    portENTER_CRITICAL(&devMux);
    memcpy(targetAddr, d.addr, 6);
    strlcpy(targetName, d.name, sizeof(targetName));
    haveTarget = true;
    portEXIT_CRITICAL(&devMux);
    pendingKnown = i;
    failure = BT_FAIL_NONE;
    connectDeadline = millis() + CONNECT_TIMEOUT_MS;
    Serial.printf("bt: switching to \"%s\"\n", d.name);
    a2dp.chaseLastDevice(false);
    a2dp.disconnectFrom(connectedAddr);
    return;
  }
  portENTER_CRITICAL(&devMux);
  memcpy(targetAddr, d.addr, 6);
  strlcpy(targetName, d.name, sizeof(targetName));
  haveTarget = true;
  portEXIT_CRITICAL(&devMux);
  failure = BT_FAIL_NONE;
  passkey = 0;
  connectDeadline = millis() + CONNECT_TIMEOUT_MS;
  Serial.printf("bt: paging \"%s\" on request\n", d.name);
  if(!a2dp.connectNow(d.addr,d.name)){haveTarget=false;connectDeadline=0;failure=BT_FAIL_TIMEOUT;}
}

void bt_forget(int i) {
  if (i < 0 || i >= g_settings.btKnownCount) return;
  uint8_t addr[6];
  memcpy(addr, g_settings.btKnown[i].addr, 6);
  Serial.printf("bt: forgetting \"%s\"\n", g_settings.btKnown[i].name);

  for (int j = i; j + 1 < g_settings.btKnownCount; j++)
    g_settings.btKnown[j] = g_settings.btKnown[j + 1];
  g_settings.btKnownCount--;

  // Forgetting has to mean forgetting. Dropped from the list but still the
  // "last device", it would be paged again every 15 s and reconnected to at
  // the next boot -- so the link goes, our record goes, and the library's own
  // stored address goes with them.
  pendingKnown = -1;

  // The link goes first, by address: the library's disconnect() reads the very
  // record the next few lines erase, and would otherwise hang up on nobody
  // while the music kept playing through the device just deleted.
  if (bt_isConnectedTo(addr)) {
    Serial.println("bt: it was the one connected -- dropping the link");
    a2dp.disconnectFrom(addr);
  }

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
}

void bt_choose(int i) {
  BtDevice d;
  if (!bt_device(i, d)) return;
  portENTER_CRITICAL(&devMux);
  memcpy(targetAddr, d.addr, 6);
  memcpy(targetName, d.name, sizeof(targetName));
  haveTarget = true;
  portEXIT_CRITICAL(&devMux);
  failure = BT_FAIL_NONE;
  passkey = 0;
  connectDeadline = millis() + CONNECT_TIMEOUT_MS;
  Serial.printf("bt: chose \"%s\" -- connecting now\n", d.name);
  // onSsid() still connects if this fails and the device turns up again.
  if(!a2dp.connectNow(d.addr,d.name)){haveTarget=false;connectDeadline=0;failure=BT_FAIL_TIMEOUT;}
}

#endif  // !CYD_UPLOADER

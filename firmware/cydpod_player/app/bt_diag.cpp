#ifndef CYD_UPLOADER
// AirPods investigation: see bt_diag.h and docs/research/airpods-diag.md.
#include "bt_diag.h"
#include <Arduino.h>
#include <esp_bt.h>
#include <string.h>

// Counters from tools/bluedroid/diag/0002-a2dp-src-diagnostics.patch. Weak, so
// an ordinary libbt still links; their addresses are then null.
extern "C" {
extern volatile uint32_t cydpod_a2d_src_txq_drops __attribute__((weak));
extern volatile uint32_t cydpod_a2d_src_txq_max __attribute__((weak));
extern volatile uint32_t cydpod_a2d_src_limited __attribute__((weak));
extern volatile uint16_t cydpod_a2d_src_rate_kbps __attribute__((weak));
extern volatile int16_t cydpod_a2d_src_bitpool __attribute__((weak));
extern volatile uint16_t cydpod_a2d_src_bitrate __attribute__((weak));
extern volatile uint8_t cydpod_a2d_src_peer_minbp __attribute__((weak));
extern volatile uint8_t cydpod_a2d_src_peer_maxbp __attribute__((weak));
extern volatile uint16_t cydpod_a2d_src_mtu __attribute__((weak));
extern volatile uint8_t cydpod_a2d_src_edr __attribute__((weak));
extern volatile uint32_t cydpod_av_l2c_bufs_max __attribute__((weak));
extern volatile uint32_t cydpod_av_l2c_stalls __attribute__((weak));
extern volatile uint32_t cydpod_av_drops __attribute__((weak));
extern volatile uint32_t cydpod_av_writes __attribute__((weak));
}

static bool stackDiag() { return &cydpod_a2d_src_txq_drops && &cydpod_av_writes; }

// Written on the Bluetooth task, read on the loop task.
static volatile uint8_t linkMode = 0xff;      // ESP_BT_PM_MD_*, 0xff = not reported
static volatile uint16_t linkInterval = 0;
static volatile uint32_t modeChanges = 0, sniffEntries = 0;
static volatile uint32_t modeSeen = 0;        // bumps on each mode event, for the loop to print
static volatile int16_t rssiDelta = 0x7fff;   // 0x7fff = not read
static volatile uint32_t rssiSeen = 0;
static volatile int8_t qosStat = -1; static volatile uint32_t qosPoll = 0, qosSeen = 0;
static volatile int8_t pktStat = -1; static volatile uint16_t pktTypes = 0; static volatile uint32_t pktSeen = 0;

static uint8_t peerAddr[6]; static bool peerValid = false;

void btdiag_gap(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* p) {
  switch (event) {
    case ESP_BT_GAP_MODE_CHG_EVT:
      if (p->mode_chg.mode == ESP_BT_PM_MD_SNIFF && linkMode != ESP_BT_PM_MD_SNIFF) sniffEntries = sniffEntries + 1;
      linkMode = p->mode_chg.mode; linkInterval = p->mode_chg.interval;
      modeChanges = modeChanges + 1; modeSeen = modeSeen + 1;
      break;
    case ESP_BT_GAP_READ_RSSI_DELTA_EVT:
      rssiDelta = p->read_rssi_delta.stat == ESP_BT_STATUS_SUCCESS ? p->read_rssi_delta.rssi_delta : 0x7ffe;
      rssiSeen = rssiSeen + 1;
      break;
    case ESP_BT_GAP_QOS_CMPL_EVT:
      qosStat = int8_t(p->qos_cmpl.stat); qosPoll = p->qos_cmpl.t_poll; qosSeen = qosSeen + 1;
      break;
    case ESP_BT_GAP_ACL_PKT_TYPE_CHANGED_EVT:
      pktStat = int8_t(p->set_acl_pkt_types.status); pktTypes = p->set_acl_pkt_types.pkt_types; pktSeen = pktSeen + 1;
      break;
    default:
      break;
  }
}

static const char* modeName(uint8_t m) {
  switch (m) { case ESP_BT_PM_MD_ACTIVE: return "active"; case ESP_BT_PM_MD_HOLD: return "hold";
               case ESP_BT_PM_MD_SNIFF: return "sniff"; case ESP_BT_PM_MD_PARK: return "park"; default: return "?"; }
}

static void readTx(int& mn, int& mx) {
  esp_power_level_t a, b;
  if (esp_bredr_tx_power_get(&a, &b) == ESP_OK) { mn = a; mx = b; } else { mn = mx = -1; }
}

static uint32_t lastDrops = 0, lastLimited = 0, lastStalls = 0, lastAvDrops = 0, lastWrites = 0;

static void printStat() {
  int tmin, tmax; readTx(tmin, tmax);
  char rssi[8];
  if (rssiDelta == 0x7fff) strcpy(rssi, "?"); else if (rssiDelta == 0x7ffe) strcpy(rssi, "err"); else snprintf(rssi, sizeof rssi, "%d", int(rssiDelta));
  if (!stackDiag()) {
    Serial.printf("stat: a2dlink mode=%s(%u) sniffs=%lu modeChanges=%lu rssiDelta=%s txpwr=%d..%d stack=no-diag-libbt\n",
                  modeName(linkMode), unsigned(linkInterval), (unsigned long)sniffEntries, (unsigned long)modeChanges, rssi, tmin, tmax);
    return;
  }
  const uint32_t drops = cydpod_a2d_src_txq_drops, limited = cydpod_a2d_src_limited, stalls = cydpod_av_l2c_stalls,
                 avDrops = cydpod_av_drops, writes = cydpod_av_writes;
  const uint32_t txqMax = cydpod_a2d_src_txq_max, l2cMax = cydpod_av_l2c_bufs_max;
  cydpod_a2d_src_txq_max = 0; cydpod_av_l2c_bufs_max = 0;
  Serial.printf("stat: a2dlink mode=%s(%u) sniffs=%lu rssiDelta=%s txpwr=%d..%d rate=%u%s bitpool=%d peerBp=%u..%u mtu=%u edr=%u "
                "writes=+%lu txqMax=%lu txqDrops=+%lu limited=+%lu l2cMax=%lu l2cStalls=+%lu avDrops=+%lu\n",
                modeName(linkMode), unsigned(linkInterval), (unsigned long)sniffEntries, rssi, tmin, tmax,
                unsigned(cydpod_a2d_src_bitrate), cydpod_a2d_src_rate_kbps ? "(set)" : "", int(cydpod_a2d_src_bitpool),
                unsigned(cydpod_a2d_src_peer_minbp), unsigned(cydpod_a2d_src_peer_maxbp), unsigned(cydpod_a2d_src_mtu),
                unsigned(cydpod_a2d_src_edr),
                (unsigned long)(writes - lastWrites), (unsigned long)txqMax, (unsigned long)(drops - lastDrops),
                (unsigned long)(limited - lastLimited), (unsigned long)l2cMax, (unsigned long)(stalls - lastStalls),
                (unsigned long)(avDrops - lastAvDrops));
  lastDrops = drops; lastLimited = limited; lastStalls = stalls; lastAvDrops = avDrops; lastWrites = writes;
}

void btdiag_tick(bool connected, const uint8_t* peer) {
  static uint32_t statAt = 0, rssiAt = 0, modeShown = 0, rssiShown = 0, qosShown = 0, pktShown = 0;
  static bool wasConnected = false;
  if (connected && peer) { memcpy(peerAddr, peer, 6); peerValid = true; }
  if (connected != wasConnected) {
    wasConnected = connected;
    if (!connected) { peerValid = false; linkMode = 0xff; rssiDelta = 0x7fff; }
  }
  if (modeSeen != modeShown) {
    modeShown = modeSeen;
    Serial.printf("bt: link mode %s interval %u slots (%u.%03u ms)\n", modeName(linkMode), unsigned(linkInterval),
                  unsigned(linkInterval * 625u / 1000u), unsigned(linkInterval * 625u % 1000u));
  }
  if (qosSeen != qosShown) { qosShown = qosSeen; Serial.printf("btd: qos status=%d t_poll=%lu slots\n", int(qosStat), (unsigned long)qosPoll); }
  if (pktSeen != pktShown) { pktShown = pktSeen; Serial.printf("btd: acl packet types status=%d types=0x%04x\n", int(pktStat), unsigned(pktTypes)); }
  if (rssiSeen != rssiShown) rssiShown = rssiSeen;  // shown on the stat line
  if (!connected) return;
  const uint32_t now = millis();
  if (now - rssiAt >= 2000) { rssiAt = now; esp_bt_gap_read_rssi_delta(peerAddr); }
  if (now - statAt >= 10000) { statAt = now; printStat(); }
}

static bool needPeer() {
  if (peerValid) return true;
  Serial.println("btd: needs a connected speaker");
  return false;
}

bool btdiag_command(const char* line) {
  if (strncmp(line, "btd", 3) != 0 || (line[3] != 0 && line[3] != ' ')) return false;
  char verb[12] = {0}, a[16] = {0}, b[16] = {0};
  sscanf(line + 3, "%11s %15s %15s", verb, a, b);
  if (!verb[0]) { printStat(); return true; }
  if (strcmp(verb, "rate") == 0) {
    if (!&cydpod_a2d_src_rate_kbps) { Serial.println("btd: rate needs the diagnostic libbt"); return true; }
    const int kbps = atoi(a);
    if (kbps != 0 && (kbps < 100 || kbps > 400)) { Serial.println("btd: rate 0 (stock 328) or 100..400 kbps"); return true; }
    cydpod_a2d_src_rate_kbps = uint16_t(kbps);
    Serial.printf("btd: SBC target bitrate %s; takes effect when the stream next starts (disconnect and reconnect)\n",
                  kbps ? a : "stock (328 kbps)");
    return true;
  }
  if (strcmp(verb, "txpwr") == 0) {
    const int mn = atoi(a), mx = b[0] ? atoi(b) : mn;
    if (mn < 0 || mx > 7 || mx < mn) { Serial.println("btd: txpwr <min> <max>, levels 0..7 (0=-12, 4=0, 5=+3, 6=+6, 7=+9 dBm)"); return true; }
    const esp_err_t rc = esp_bredr_tx_power_set(esp_power_level_t(mn), esp_power_level_t(mx));
    int rmn = -1, rmx = -1;
    for (int i = 0; i < 200; ++i) { readTx(rmn, rmx); if (rmn == mn && rmx == mx) break; delay(1); }
    Serial.printf("btd: txpwr set %d..%d rc=%d readback %d..%d\n", mn, mx, int(rc), rmn, rmx);
    return true;
  }
  if (strcmp(verb, "pkt") == 0) {
    if (!needPeer()) return true;
    uint16_t types;
    // EDR bits are "do not use" bits: all clear = every EDR type allowed.
    if (strcmp(a, "all") == 0) types = ESP_BT_ACL_PKT_TYPES_MASK_DH5 | ESP_BT_ACL_PKT_TYPES_MASK_DM5 |
                                       ESP_BT_ACL_PKT_TYPES_MASK_DH3 | ESP_BT_ACL_PKT_TYPES_MASK_DM3 |
                                       ESP_BT_ACL_PKT_TYPES_MASK_DH1 | ESP_BT_ACL_PKT_TYPES_MASK_DM1;
    else if (strcmp(a, "2dh5") == 0) types = ESP_BT_ACL_2_DH5_ONLY | ESP_BT_ACL_PKT_TYPES_MASK_DH5 | ESP_BT_ACL_PKT_TYPES_MASK_DH1 | ESP_BT_ACL_PKT_TYPES_MASK_DM1;
    else if (strcmp(a, "3dh5") == 0) types = ESP_BT_ACL_3_DH5_ONLY | ESP_BT_ACL_PKT_TYPES_MASK_DH5 | ESP_BT_ACL_PKT_TYPES_MASK_DH1 | ESP_BT_ACL_PKT_TYPES_MASK_DM1;
    else if (strcmp(a, "basic") == 0) types = 0x330e | ESP_BT_ACL_PKT_TYPES_MASK_DH5 | ESP_BT_ACL_PKT_TYPES_MASK_DH3 | ESP_BT_ACL_PKT_TYPES_MASK_DH1 | ESP_BT_ACL_PKT_TYPES_MASK_DM1;
    else types = uint16_t(strtoul(a, nullptr, 16));
    const esp_err_t rc = esp_bt_gap_set_acl_pkt_types(peerAddr, types);
    Serial.printf("btd: acl packet types 0x%04x rc=%d (reply follows)\n", unsigned(types), int(rc));
    return true;
  }
  if (strcmp(verb, "qos") == 0) {
    if (!needPeer()) return true;
    const uint32_t slots = strtoul(a, nullptr, 10);
    if (slots < 2 || slots > 0x1000) { Serial.println("btd: qos <t_poll slots>, 2..4096 (1 slot = 0.625 ms)"); return true; }
    Serial.printf("btd: qos t_poll %lu rc=%d (reply follows)\n", (unsigned long)slots, int(esp_bt_gap_set_qos(peerAddr, slots)));
    return true;
  }
  Serial.println("btd: commands are btd | btd rate <kbps|0> | btd txpwr <min> <max> | btd pkt all|basic|2dh5|3dh5|<hex> | btd qos <slots>");
  return true;
}
#endif  // !CYD_UPLOADER

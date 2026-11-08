#ifndef CYD_UPLOADER
// A2DP link manager: telemetry, the adaptive SBC rate, per-device link memory
// and the quiet-UI gate (bt_diag.h, docs/specs/bt-rate-and-earbuds.md).
#include "bt_diag.h"
#include "bt_rate_policy.h"
#include "settings.h"
#include <Arduino.h>
#include <esp_bt.h>
#include <string.h>

// Counters and the rate control from tools/bluedroid/patches/0002. Weak, so a
// libbt without the patch still links; their addresses are then null.
extern "C" {
extern volatile uint32_t cydpod_a2d_src_txq_drops __attribute__((weak));
extern volatile uint32_t cydpod_a2d_src_txq_max __attribute__((weak));
extern volatile uint32_t cydpod_a2d_src_limited __attribute__((weak));
extern volatile uint16_t cydpod_a2d_src_rate_kbps __attribute__((weak));
extern volatile uint8_t cydpod_a2d_src_apply __attribute__((weak));
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
extern volatile uint8_t cydpod_a2d_src_full_packets __attribute__((weak));  // patch 0003
extern volatile uint32_t cydpod_a2d_src_pkts __attribute__((weak));
extern volatile uint32_t cydpod_a2d_src_frames __attribute__((weak));
}
static bool packing() { return &cydpod_a2d_src_full_packets && &cydpod_a2d_src_pkts && &cydpod_a2d_src_frames; }

static bool stackDiag() { return &cydpod_a2d_src_txq_drops && &cydpod_av_writes; }
static bool rateControl() { return &cydpod_a2d_src_rate_kbps && &cydpod_a2d_src_apply; }

// Written on the Bluetooth task, read on the loop task.
static volatile uint8_t linkMode = 0xff;      // ESP_BT_PM_MD_*, 0xff = not reported
static volatile uint16_t linkInterval = 0;
static volatile uint32_t modeChanges = 0, sniffEntries = 0;
static volatile uint32_t modeSeen = 0;        // bumps on each mode event, for the loop to print
static volatile int16_t rssiDelta = 0x7fff;   // 0x7fff = not read
static volatile int8_t qosStat = -1; static volatile uint32_t qosPoll = 0, qosSeen = 0;
static volatile int8_t pktStat = -1; static volatile uint16_t pktTypes = 0; static volatile uint32_t pktSeen = 0;

static uint8_t peerAddr[6]; static bool peerValid = false;
static cyd::BtRatePolicy rate;
static uint8_t uiMode = 0;   // btd ui: 0 auto, 1 always quiet, 2 never quiet
static bool memoDirty = false;

// ---------------------------------------------------------------------------
// Per-device link memory (Settings::btLinks)
// ---------------------------------------------------------------------------
static BtLinkMemo* memoFind(const uint8_t addr[6]) {
  for (auto& m : g_settings.btLinks) if (memcmp(m.addr, addr, 6) == 0) return &m;
  return nullptr;
}
static BtLinkMemo& memoFor(const uint8_t addr[6]) {
  if (BtLinkMemo* m = memoFind(addr)) return *m;
  static const uint8_t NONE[6] = {0};
  BtLinkMemo* slot = memoFind(NONE);
  if (!slot) {  // full: the oldest entry goes
    for (int i = 0; i + 1 < BT_KNOWN_MAX; ++i) g_settings.btLinks[i] = g_settings.btLinks[i + 1];
    slot = &g_settings.btLinks[BT_KNOWN_MAX - 1];
  }
  memset(slot, 0, sizeof(*slot)); memcpy(slot->addr, addr, 6);
  return *slot;
}
static void memoSet(const uint8_t addr[6], uint16_t kbps, uint8_t setFlags) {
  BtLinkMemo* m = memoFind(addr);
  if (!m && !kbps && !setFlags) return;
  BtLinkMemo& w = m ? *m : memoFor(addr);
  const uint8_t flags = w.flags | setFlags;
  if (w.rateKbps != kbps || w.flags != flags) { w.rateKbps = kbps; w.flags = flags; memoDirty = true; }
}

// ---------------------------------------------------------------------------
// GAP events (Bluetooth task)
// ---------------------------------------------------------------------------
void btdiag_gap(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* p) {
  switch (event) {
    case ESP_BT_GAP_MODE_CHG_EVT:
      if (p->mode_chg.mode == ESP_BT_PM_MD_SNIFF && linkMode != ESP_BT_PM_MD_SNIFF) sniffEntries = sniffEntries + 1;
      linkMode = p->mode_chg.mode; linkInterval = p->mode_chg.interval;
      modeChanges = modeChanges + 1; modeSeen = modeSeen + 1;
      break;
    case ESP_BT_GAP_READ_RSSI_DELTA_EVT:
      rssiDelta = p->read_rssi_delta.stat == ESP_BT_STATUS_SUCCESS ? p->read_rssi_delta.rssi_delta : 0x7ffe;
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

// Hand the policy's rate to Bluedroid. 328 is the stock target, so it is sent
// as 0 (no override). The media task applies it between packets.
static void applyRate(const char* why) {
  if (!rateControl()) return;
  const uint16_t k = rate.kbps();
  cydpod_a2d_src_rate_kbps = k == cyd::BtRatePolicy::stepKbps(0) ? 0 : k;
  cydpod_a2d_src_apply = 1;
  Serial.printf("bt: SBC rate %u kbps (%s)\n", unsigned(k), why);
}

static uint32_t lastDrops = 0, lastLimited = 0, lastStalls = 0, lastAvDrops = 0, lastWrites = 0;
static uint32_t statDrops = 0, statLimited = 0, statStalls = 0, statAvDrops = 0, statWrites = 0;
static uint32_t statTxqMax = 0, statL2cMax = 0;
static uint32_t packPkts = 0, packFrames = 0;   // patch 0003 counters at the last stat line

static void printStat() {
  int tmin, tmax; readTx(tmin, tmax);
  char rssi[8];
  if (rssiDelta == 0x7fff) strcpy(rssi, "?"); else if (rssiDelta == 0x7ffe) strcpy(rssi, "err"); else snprintf(rssi, sizeof rssi, "%d", int(rssiDelta));
  if (!stackDiag()) {
    Serial.printf("stat: a2dlink mode=%s(%u) sniffs=%lu rssiDelta=%s txpwr=%d..%d stack=no-link-counters\n",
                  modeName(linkMode), unsigned(linkInterval), (unsigned long)sniffEntries, rssi, tmin, tmax);
    return;
  }
  const BtLinkMemo* m = peerValid ? memoFind(peerAddr) : nullptr;
  char fpp[24] = "fpp=? pack=?";
  if (packing()) {
    const uint32_t pk = cydpod_a2d_src_pkts - packPkts, fr = cydpod_a2d_src_frames - packFrames;
    packPkts = cydpod_a2d_src_pkts; packFrames = cydpod_a2d_src_frames;
    snprintf(fpp, sizeof fpp, "fpp=%lu.%lu pack=%s", (unsigned long)(pk ? fr / pk : 0),
             (unsigned long)(pk ? (fr * 10 / pk) % 10 : 0), cydpod_a2d_src_full_packets ? "on" : "off");
  }
  Serial.printf("stat: a2dlink mode=%s(%u) sniffs=%lu rssiDelta=%s txpwr=%d..%d rate=%u%s policy=%u down=%u up=%u "
                "bitpool=%d peerBp=%u..%u mtu=%u edr=%u writes=+%lu txqMax=%lu txqDrops=+%lu limited=+%lu "
                "l2cMax=%lu l2cStalls=+%lu avDrops=+%lu memo=%u/%u quietUi=%d %s\n",
                modeName(linkMode), unsigned(linkInterval), (unsigned long)sniffEntries, rssi, tmin, tmax,
                unsigned(cydpod_a2d_src_bitrate), rate.pinned ? "(pinned)" : "", unsigned(rate.kbps()),
                unsigned(rate.downs), unsigned(rate.ups), int(cydpod_a2d_src_bitpool),
                unsigned(cydpod_a2d_src_peer_minbp), unsigned(cydpod_a2d_src_peer_maxbp), unsigned(cydpod_a2d_src_mtu),
                unsigned(cydpod_a2d_src_edr), (unsigned long)statWrites, (unsigned long)statTxqMax, (unsigned long)statDrops,
                (unsigned long)statLimited, (unsigned long)statL2cMax, (unsigned long)statStalls, (unsigned long)statAvDrops,
                m ? unsigned(m->rateKbps) : 0u, m ? unsigned(m->flags) : 0u, int(btlink_quietUi()), fpp);
  statDrops = statLimited = statStalls = statAvDrops = statWrites = statTxqMax = statL2cMax = 0;
}

// Once a second while connected: deltas for the 10 s stat line and the rate policy.
static void sampleLink(uint32_t now) {
  if (!stackDiag()) return;
  const uint32_t drops = cydpod_a2d_src_txq_drops, limited = cydpod_a2d_src_limited, stalls = cydpod_av_l2c_stalls,
                 avDrops = cydpod_av_drops, writes = cydpod_av_writes;
  const uint32_t txqMax = cydpod_a2d_src_txq_max, l2cMax = cydpod_av_l2c_bufs_max;
  cydpod_a2d_src_txq_max = 0; cydpod_av_l2c_bufs_max = 0;
  const uint32_t dDrops = drops - lastDrops, dStalls = stalls - lastStalls, dWrites = writes - lastWrites,
                 dAvDrops = avDrops - lastAvDrops;
  statDrops += dDrops; statLimited += limited - lastLimited; statStalls += dStalls;
  statAvDrops += dAvDrops; statWrites += dWrites;
  if (txqMax > statTxqMax) statTxqMax = txqMax;
  if (l2cMax > statL2cMax) statL2cMax = l2cMax;
  lastDrops = drops; lastLimited = limited; lastStalls = stalls; lastAvDrops = avDrops; lastWrites = writes;
  if (!dWrites) return;  // not streaming: neither congestion nor clean time
  const uint16_t before = rate.kbps();
  if (rate.sample(now, dStalls, txqMax, dDrops + dAvDrops)) {
    applyRate(rate.kbps() < before ? "link backed up" : "link clean");
    if (rate.kbps() < cyd::BtRatePolicy::stepKbps(0)) memoSet(peerAddr, rate.rememberKbps(), BT_LINK_EARBUDS);
  }
}

static void linkStarted(uint32_t now) {
  const BtLinkMemo* m = memoFind(peerAddr);
  const uint16_t remembered = m ? m->rateKbps : 0;
  rate.start(now, remembered);
  // Fresh baselines: the link counters are cumulative across links.
  if (stackDiag()) {
    lastDrops = cydpod_a2d_src_txq_drops; lastLimited = cydpod_a2d_src_limited; lastStalls = cydpod_av_l2c_stalls;
    lastAvDrops = cydpod_av_drops; lastWrites = cydpod_av_writes;
    cydpod_a2d_src_txq_max = 0; cydpod_av_l2c_bufs_max = 0;
  }
  applyRate(rate.pinned ? "pinned" : remembered ? "remembered for this device" : "stock");
}

static void linkEnded() {
  rate.stop();
  if (peerValid && !rate.pinned) {
    const BtLinkMemo* m = memoFind(peerAddr);
    memoSet(peerAddr, rate.rememberKbps(), 0);
    if (m || rate.rememberKbps()) Serial.printf("bt: remembered SBC rate %u kbps for this device\n", unsigned(rate.rememberKbps() ? rate.rememberKbps() : 328));
  }
}

void btdiag_tick(bool connected, const uint8_t* peer) {
  static uint32_t statAt = 0, rssiAt = 0, sampleAt = 0, modeShown = 0, qosShown = 0, pktShown = 0;
  static bool wasConnected = false;
  const uint32_t now = millis();
  if (connected != wasConnected) {
    wasConnected = connected;
    if (connected && peer) { memcpy(peerAddr, peer, 6); peerValid = true; linkStarted(now); statAt = now; }
    if (!connected) { linkEnded(); peerValid = false; linkMode = 0xff; rssiDelta = 0x7fff; }
  }
  if (memoDirty) { memoDirty = false; settings_save(); }
  if (modeSeen != modeShown) {
    modeShown = modeSeen;
    Serial.printf("bt: link mode %s interval %u slots (%u.%03u ms)\n", modeName(linkMode), unsigned(linkInterval),
                  unsigned(linkInterval * 625u / 1000u), unsigned(linkInterval * 625u % 1000u));
  }
  if (qosSeen != qosShown) { qosShown = qosSeen; Serial.printf("btd: qos status=%d t_poll=%lu slots\n", int(qosStat), (unsigned long)qosPoll); }
  if (pktSeen != pktShown) { pktShown = pktSeen; Serial.printf("btd: acl packet types status=%d types=0x%04x\n", int(pktStat), unsigned(pktTypes)); }
  if (!connected) return;
  if (now - sampleAt >= 1000) { sampleAt = now; sampleLink(now); }
  if (now - rssiAt >= 2000) { rssiAt = now; esp_bt_gap_read_rssi_delta(peerAddr); }
  if (now - statAt >= 10000) { statAt = now; printStat(); }
}

bool btlink_quietUi() {
  if (uiMode) return uiMode == 1;
  if (!peerValid) return false;
  const BtLinkMemo* m = memoFind(peerAddr);
  return (m && (m->flags & BT_LINK_EARBUDS)) || rate.congested(millis());
}

uint32_t btlink_idleSuspendMs() {
  const BtLinkMemo* m = peerValid ? memoFind(peerAddr) : nullptr;
  return (m && (m->flags & BT_LINK_DROPS_WHEN_IDLE)) ? 60000 : 10000;
}

void btlink_droppedWhenIdle(const uint8_t addr[6]) {
  const BtLinkMemo* m = memoFind(addr);
  if (m && (m->flags & BT_LINK_DROPS_WHEN_IDLE)) return;
  memoSet(addr, m ? m->rateKbps : 0, BT_LINK_DROPS_WHEN_IDLE);
  Serial.println("bt: this device ends the link when idle; waiting 60 s before suspending it next time");
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
    if (!rateControl()) { Serial.println("btd: rate needs the rate-control libbt (patch 0002)"); return true; }
    const int kbps = strcmp(a, "auto") == 0 ? 0 : atoi(a);
    if (strcmp(a, "auto") != 0 && (kbps < 100 || kbps > 400)) { Serial.println("btd: rate auto | 100..400 kbps (pins until reboot)"); return true; }
    rate.pin(uint16_t(kbps));
    if (!kbps && peerValid) { const BtLinkMemo* m = memoFind(peerAddr); rate.start(millis(), m ? m->rateKbps : 0); }
    applyRate(kbps ? "pinned by btd" : "automatic");
    return true;
  }
  if (strcmp(verb, "pack") == 0) {
    if (!packing()) { Serial.println("btd: pack needs the full-packet libbt (patch 0003)"); return true; }
    if (strcmp(a, "on") == 0 || strcmp(a, "off") == 0) cydpod_a2d_src_full_packets = strcmp(a, "on") == 0;
    Serial.printf("btd: full A2DP packets %s (until reboot)\n", cydpod_a2d_src_full_packets ? "on" : "off");
    return true;
  }
  if (strcmp(verb, "forget") == 0) {
    memset(g_settings.btLinks, 0, sizeof(g_settings.btLinks)); memoDirty = true;
    Serial.println("btd: link memory cleared (rates and earbud flags)");
    return true;
  }
  if (strcmp(verb, "ui") == 0) {
    uiMode = strcmp(a, "quiet") == 0 || strcmp(a, "off") == 0 ? 1 : strcmp(a, "on") == 0 ? 2 : 0;
    Serial.printf("btd: ui animations %s\n", uiMode == 1 ? "always quiet" : uiMode == 2 ? "always on" : "automatic");
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
  Serial.println("btd: commands are btd | btd rate auto|<kbps> | btd ui auto|quiet|on | btd pack on|off | btd forget | btd txpwr <min> <max> | btd pkt all|basic|2dh5|3dh5|<hex> | btd qos <slots>");
  return true;
}
#endif  // !CYD_UPLOADER

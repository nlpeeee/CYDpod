#include "i18n.h"
#include "settings.h"

struct Entry { const char* en; const char* zh; };

// Order must match enum Txt; the static_assert below catches a missed row.
static const Entry TABLE[] = {
  { "English",               "中文" },
  { "Bluetooth",             "藍牙" },
  { "Pair new",              "配對新裝置" },
  { "Connected:",            "已連接：" },
  { "Connecting...",         "連接中…" },
  { "Pairing: put the speaker in pairing mode", "配對中：請將喇叭設為配對模式" },
  { "Looking for",           "正在尋找" },
  { "Put your speaker in pairing mode", "請將喇叭設為配對模式" },
  { "searching...",          "搜尋中…" },
  { "Touch calibration",     "觸控校正" },
  { "press each target as it appears", "依次按住每個目標" },
  { "Top left",              "左上" },
  { "Top right",             "右上" },
  { "Bottom right",          "右下" },
  { "Bottom left",           "左下" },
  { "press and hold the target", "按住目標" },
  { "tap the centre to confirm", "點擊中心確認" },
  { "Calibration skipped",   "已略過校正" },
  { "keeping the previous mapping", "保留原有設定" },
  { "Calibrated",            "校正完成" },
  { "Not quite",             "不太準確" },
  { "try again",             "請再試一次" },
  { "Roughly calibrated",    "大致完成校正" },
  { "hold 4 s anywhere to redo", "長按 4 秒可重新校正" },
  { "Upload mode",           "上傳模式" },
  { "Open in a browser:",    "用瀏覽器打開：" },
  { "1. Join WiFi:",         "1. 連接 WiFi：" },
  { "2. Open:",              "2. 打開：" },
  { "Connecting to WiFi...", "正在連接 WiFi…" },
  { "WiFi failed - using hotspot", "WiFi 連接失敗，改用熱點" },
  { "Done",                  "完成" },
  { "Hotspot",               "熱點" },
  { "Receiving",             "正在接收" },
  { "Restarting...",         "重新啟動中…" },
  { "Cancel",                "取消" },
  { "Restart",               "重新啟動" },
  { "Uploader not installed", "未安裝上傳程式" },
  { "No answer - tap it to try again", "沒有回應，請再按一次" },
  { "Confirm this code:",     "請核對此代碼：" },
  { "Refused - forget it there, try again", "被拒絕，請在該裝置刪除後再試" },
  { "saved",                 "已儲存" },
  { "Forget",                "刪除" },
  { "Forget this device?",   "刪除此裝置？" },
  { "It can be paired again later.", "之後可以重新配對。" },
  { "Reset the web passcode?", "重設網頁密碼？" },
  { "It goes back to 000000.", "密碼會變回 000000。" },
  { "Change it on the WiFi tab.", "可在 WiFi 分頁更改。" },
  { "Reset",                 "重設" },
  { "Connect",               "連接" },
  { "Disconnect",            "中斷連接" },
  { "Disconnecting...",      "中斷連接中…" },
  { "Disconnected",          "未連接" },
};
static_assert(sizeof(TABLE) / sizeof(TABLE[0]) == T_COUNT, "i18n TABLE does not match enum Txt");

const char* tr_en(Txt id) { return id < T_COUNT ? TABLE[id].en : ""; }

const char* tr(Txt id) {
  if (id >= T_COUNT) return "";
  return g_settings.lang == LANG_ZH ? TABLE[id].zh : TABLE[id].en;
}

void i18n_forEachChinese(void (*fn)(const char* text)) {
  for (int i = 0; i < T_COUNT; i++) fn(TABLE[i].zh);
}

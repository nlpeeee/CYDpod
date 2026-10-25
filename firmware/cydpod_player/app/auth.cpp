// WiFi image only (firmware.h).
#ifdef CYD_UPLOADER

#include <WiFi.h>
#include "auth.h"
#include "settings.h"

static WebServer* srv = nullptr;
static String   token;                      // the live session, "" = none
static uint32_t lastSeen = 0;
static int      fails = 0;
static uint32_t lockUntil = 0;              // millis; tries before this are refused

static const int      FREE_TRIES = 3;       // typos before any wait
static const uint32_t FIRST_WAIT_MS = 30 * 1000;
static const uint32_t MAX_WAIT_MS = 15 * 60 * 1000;
static const char*    COOKIE = "cyd_s";

static const char LOGIN_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD MP3</title>
<style>
body{font-family:system-ui,-apple-system,sans-serif;background:#111;color:#eee;margin:0;padding:24px;display:flex;justify-content:center}
.w{max-width:340px;width:100%;margin-top:8vh;text-align:center}
h1{font-size:22px;margin:0 0 6px}.sub{color:#888;font-size:14px;margin-bottom:22px}
input{width:100%;box-sizing:border-box;font-size:32px;letter-spacing:12px;text-align:center;padding:12px;border-radius:10px;border:1px solid #444;background:#1c1c1c;color:#eee}
button{width:100%;margin-top:14px;padding:14px;border:0;border-radius:10px;background:#0aa;color:#fff;font-size:17px;font-weight:600}
button:disabled{background:#2a2a2a;color:#777}
.msg{min-height:44px;margin-top:14px;font-size:14px;color:#f66;line-height:1.4}
.hint{color:#666;font-size:12px;margin-top:24px;line-height:1.5}
</style></head><body><div class="w">
<h1>CYD MP3</h1><div class="sub" id="sub"></div>
<form onsubmit="go();return false">
<input id="pin" type="password" inputmode="numeric" pattern="[0-9]*" maxlength="6" autocomplete="current-password" autofocus>
<button id="btn"></button></form>
<div class="msg" id="msg"></div><div class="hint" id="hint"></div>
</div><script>
function T(e,z){return Z?z:e}function $(i){return document.getElementById(i)}
$('sub').textContent=T('Enter the 6-digit passcode','請輸入 6 位數字密碼');$('btn').textContent=T('Sign in','登入');
$('hint').textContent=T('The passcode is shown on the player under Setup ▸ Web passcode.','密碼可在播放器的 設定 ▸ 網頁密碼 查看。');
var t=null;
function wait(s){clearInterval(t);$('btn').disabled=true;var end=Date.now()+s*1000;
 var f=()=>{var l=Math.ceil((end-Date.now())/1000);if(l<=0){clearInterval(t);$('btn').disabled=false;$('msg').textContent='';return}
  $('msg').textContent=T('Too many wrong tries. Try again in ','錯誤次數太多，請於 ')+l+T(' s.',' 秒後再試。')};f();t=setInterval(f,500)}
function go(){var p=$('pin').value;if(!/^\d{6}$/.test(p)){$('msg').textContent=T('6 digits, please.','請輸入 6 位數字。');return}
 fetch('/api/login',{method:'POST',body:new URLSearchParams({pin:p})}).then(r=>r.json()).then(j=>{
  if(j.ok){location.reload();return}
  $('pin').value='';
  if(j.error=='busy')$('msg').textContent=T('In use on another device. It frees up when that device signs out, or ','另一部裝置正在使用。該裝置登出後，或停用 ')+Math.ceil(j.left/60)+T(' min after it stops using it.',' 分鐘後即可使用。');
  else if(j.wait)wait(j.wait);
  else $('msg').textContent=T('Wrong passcode.','密碼錯誤。')+(j.tries>0?T(' Tries left before a wait: ',' 剩餘嘗試次數：')+j.tries:'');
 }).catch(()=>$('msg').textContent=T('Player not reachable','無法連接播放器'))}
</script></body></html>)HTML";

bool auth_validPin(const String& pin) {
  if (pin.length() != 6) return false;
  for (unsigned i = 0; i < 6; i++) if (!isDigit(pin[i])) return false;
  return true;
}

// Same time whatever matches, so the reply's timing says nothing.
static bool pinEquals(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (unsigned i = 0; i < a.length(); i++) diff |= a[i] ^ b[i];
  return diff == 0;
}

static String cookieToken() {
  const String c = srv->header("Cookie");
  const String key = String(COOKIE) + "=";
  int at = c.indexOf(key);
  while (at > 0 && c[at - 1] != ' ' && c[at - 1] != ';') at = c.indexOf(key, at + 1);
  if (at < 0) return String();
  const int end = c.indexOf(';', at);
  return c.substring(at + key.length(), end < 0 ? c.length() : end);
}

static bool sessionLive() {
  if (token.length() && millis() - lastSeen >= AUTH_IDLE_MS) {
    Serial.println("auth: session timed out");
    token = "";
  }
  return token.length() > 0;
}

bool auth_ok() {
  if (!sessionLive()) return false;
  if (!pinEquals(cookieToken(), token)) return false;
  lastSeen = millis();
  return true;
}

static void sendJson(int code, const String& body) {
  srv->sendHeader("Cache-Control", "no-store");
  srv->send(code, "application/json", body);
}

static void sendLogin() {
  srv->sendHeader("Cache-Control", "no-store");
  srv->setContentLength(CONTENT_LENGTH_UNKNOWN);
  srv->send(200, "text/html; charset=utf-8", "");
  srv->sendContent(g_settings.lang == LANG_ZH ? "<script>var Z=true</script>"
                                              : "<script>var Z=false</script>");
  srv->sendContent_P(LOGIN_PAGE);
  srv->sendContent("");
}

WebServer::THandlerFunction auth_guard(WebServer::THandlerFunction fn, bool page) {
  return [fn, page]() {
    if (auth_ok()) { fn(); return; }
    if (page) sendLogin();
    else sendJson(401, "{\"ok\":false,\"error\":\"sign in\"}");
  };
}

// A wrong passcode: after FREE_TRIES, a wait that doubles each time.
static uint32_t failed() {
  fails++;
  if (fails < FREE_TRIES) return 0;
  const int doublings = min(fails - FREE_TRIES, 10);
  const uint32_t waitMs = min<uint32_t>(FIRST_WAIT_MS << doublings, MAX_WAIT_MS);
  lockUntil = millis() + waitMs;
  Serial.printf("auth: wrong passcode #%d, next try in %u s\n", fails, (unsigned)(waitMs / 1000));
  return waitMs;
}

static bool locked(uint32_t* leftS) {
  const int32_t left = (int32_t)(lockUntil - millis());
  if (left <= 0) return false;
  *leftS = (left + 999) / 1000;
  return true;
}

static void handleLogin() {
  uint32_t left;
  if (locked(&left)) { sendJson(429, "{\"ok\":false,\"error\":\"wait\",\"wait\":" + String(left) + "}"); return; }
  if (!pinEquals(srv->arg("pin"), g_settings.webPin)) {
    const uint32_t w = failed();
    sendJson(403, "{\"ok\":false,\"error\":\"wrong\",\"wait\":" + String((w + 999) / 1000) +
                  ",\"tries\":" + String(max(0, FREE_TRIES - fails)) + "}");
    return;
  }
  // Right passcode, but someone else holds the session.
  if (sessionLive() && !pinEquals(cookieToken(), token)) {
    const uint32_t leftS = (AUTH_IDLE_MS - (millis() - lastSeen) + 999) / 1000;
    sendJson(409, "{\"ok\":false,\"error\":\"busy\",\"left\":" + String(leftS) + "}");
    return;
  }
  fails = 0;
  lockUntil = 0;
  char t[33];
  for (int i = 0; i < 4; i++) snprintf(t + i * 8, 9, "%08x", (unsigned)esp_random());
  token = t;
  lastSeen = millis();
  srv->sendHeader("Set-Cookie", String(COOKIE) + "=" + token + "; Path=/; HttpOnly; SameSite=Strict");
  Serial.printf("auth: signed in from %s\n", srv->client().remoteIP().toString().c_str());
  sendJson(200, "{\"ok\":true}");
}

static void handleLogout() {
  if (auth_ok()) {
    token = "";
    Serial.println("auth: signed out");
  }
  srv->sendHeader("Set-Cookie", String(COOKIE) + "=; Path=/; Max-Age=0");
  sendJson(200, "{\"ok\":true}");
}

// Change the passcode: the current one again, then the new one.
static void handlePasscode() {
  uint32_t left;
  if (locked(&left)) { sendJson(429, "{\"ok\":false,\"error\":\"too many wrong tries, wait " + String(left) + " s\"}"); return; }
  if (!pinEquals(srv->arg("cur"), g_settings.webPin)) {
    failed();
    sendJson(403, "{\"ok\":false,\"error\":\"the current passcode is wrong\"}");
    return;
  }
  const String pin = srv->arg("pin");
  if (!auth_validPin(pin)) { sendJson(400, "{\"ok\":false,\"error\":\"the passcode must be 6 digits\"}"); return; }
  g_settings.webPin = pin;
  settings_save();
  Serial.println("auth: passcode changed");
  sendJson(200, "{\"ok\":true}");
}

static void handlePing() { sendJson(200, "{\"ok\":true}"); }

void auth_begin(WebServer& server) {
  srv = &server;
  static const char* keys[] = { "Cookie" };
  server.collectHeaders(keys, 1);
  if (!auth_validPin(g_settings.webPin)) g_settings.webPin = "000000";
  server.on("/api/login",    HTTP_POST, handleLogin);
  server.on("/api/logout",   HTTP_POST, handleLogout);
  server.on("/api/passcode", HTTP_POST, auth_guard(handlePasscode));
  server.on("/api/ping",     HTTP_GET,  auth_guard(handlePing));
}

#endif  // CYD_UPLOADER

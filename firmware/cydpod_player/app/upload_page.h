// ===========================================================================
// upload_page.h -- the web page served in upload mode
// ===========================================================================
// One self-contained page: no external scripts or fonts, because in hotspot
// mode the phone has no internet. It talks to the JSON endpoints in
// uploadmode.cpp. The server prepends "<script>var Z=true|false</script>" so
// the page follows the player's language setting.
#pragma once

#include <Arduino.h>

static const char UPLOAD_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD MP3</title>
<style>
body{font-family:system-ui,-apple-system,sans-serif;background:#111;color:#eee;margin:0;padding:14px}
.w{max-width:640px;margin:0 auto}
h1{font-size:20px;margin:0 0 2px}
.sub{color:#888;font-size:13px;margin-bottom:12px}
.tabs{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap}
.tabs button{flex:1;min-width:90px}
button{padding:9px 12px;border:0;border-radius:6px;background:#2a2a2a;color:#ddd;font-size:14px;cursor:pointer}
button.on,button.pri{background:#0aa;color:#fff;font-weight:600}
button.bad{background:#7a2020;color:#fff}
.box{border:1px solid #333;border-radius:8px;padding:12px;margin-bottom:12px}
.row{display:flex;align-items:center;gap:8px;padding:7px 2px;border-bottom:1px solid #222}
.row:last-child{border-bottom:0}
.nm{flex:1;word-break:break-all}
.sz{color:#888;font-size:12px;white-space:nowrap}
.ic{background:none;padding:4px 6px;font-size:16px}
.dir{color:#0dd;cursor:pointer}
.crumb{color:#0dd;margin-bottom:8px;font-size:14px}
.drop{border:2px dashed #444;border-radius:8px;padding:18px;text-align:center;color:#888;margin:10px 0}
.drop.hot{border-color:#0aa;color:#0dd}
.bar{height:6px;background:#333;border-radius:3px;overflow:hidden;margin-top:4px}
.bar i{display:block;height:100%;background:#0aa;width:0}
.q{font-size:13px;margin:6px 0}
.err{color:#f66}.ok{color:#6d6}
input{width:100%;box-sizing:border-box;padding:9px;border-radius:6px;border:1px solid #444;background:#1c1c1c;color:#eee;font-size:15px;margin:4px 0 10px}
img.th{width:56px;height:56px;object-fit:cover;border-radius:4px;background:#000}
.hint{color:#777;font-size:12px;line-height:1.5}
</style></head><body><div class="w">
<h1>CYD MP3</h1><div class="sub" id="info">…</div>
<div style="text-align:right;margin-bottom:10px">
 <a href="#" onclick="signout();return false" id="b_signout" style="color:#0dd;text-decoration:none;font-size:14px"></a>
</div>
<div class="tabs">
 <button id="t_music" onclick="tab('music')"></button>
 <button id="t_pl" onclick="tab('pl')"></button>
 <button id="t_bg" onclick="tab('bg')"></button>
 <button id="t_font" onclick="tab('font')"></button>
 <button id="t_wifi" onclick="tab('wifi')"></button>
</div>

<div id="p_music" class="box">
 <div class="crumb" id="crumb"></div>
 <div id="list"></div>
 <div class="drop" id="drop_music"></div>
 <input type="file" id="f_music" accept=".mp3,audio/mpeg" multiple style="display:none">
 <button onclick="$('f_music').click()" class="pri" id="b_pick"></button>
 <button onclick="mkdir()" id="b_mkdir"></button>
 <div id="q_music"></div>
</div>

<div id="p_pl" class="box" style="display:none">
 <div id="plmain">
  <div id="pllist"></div>
  <button onclick="pledit(null)" class="pri" id="b_newpl"></button>
  <button onclick="$('f_pl').click()" id="b_pickpl"></button>
  <input type="file" id="f_pl" accept=".m3u,.m3u8" multiple style="display:none">
  <div id="q_pl"></div>
  <p class="hint" id="h_pl"></p>
 </div>
 <div id="pled" style="display:none">
  <div id="plnamebox"><label id="l_plname"></label><input id="plname" autocomplete="off"></div>
  <div class="q" id="plcount"></div>
  <div id="plsongs"></div>
  <div class="q" style="margin-top:14px"><b id="l_pladd"></b></div>
  <input id="plsearch" autocomplete="off" oninput="plpick()">
  <div id="plpick" style="max-height:320px;overflow:auto"></div>
  <button onclick="pladdShown()" id="b_pladdall"></button>
  <div style="margin-top:14px"><button class="pri" onclick="plsave()" id="b_plsave"></button>
   <button onclick="plclose()" id="b_plcancel"></button></div>
  <p class="hint" id="h_pled"></p>
 </div>
</div>

<div id="p_bg" class="box" style="display:none">
 <div id="bglist"></div>
 <div class="drop" id="drop_bg"></div>
 <input type="file" id="f_bg" accept=".gif,image/gif" multiple style="display:none">
 <button onclick="$('f_bg').click()" class="pri" id="b_pickgif"></button>
 <div id="q_bg"></div>
 <p class="hint" id="h_bg"></p>
</div>

<div id="p_font" class="box" style="display:none">
 <div id="fontinfo" class="q"></div>
 <input type="file" id="f_font" accept=".vlw" style="display:none">
 <button onclick="$('f_font').click()" class="pri" id="b_pickfont"></button>
 <div id="q_font"></div>
 <p class="hint" id="h_font"></p>
</div>

<div id="p_wifi" class="box" style="display:none">
 <div id="wifiinfo" class="q"></div>
 <label id="l_ssid"></label>
 <input id="ssid" list="nets" autocomplete="off"><datalist id="nets"></datalist>
 <button onclick="scan()" id="b_scan"></button> <span id="scanmsg" class="hint"></span>
 <label id="l_pass" style="display:block;margin-top:10px"></label>
 <input id="pass" type="password">
 <button class="pri" onclick="saveWifi()" id="b_savewifi"></button>
 <button class="bad" onclick="forgetWifi()" id="b_forget"></button>
 <p class="hint" id="h_wifi"></p>
 <div style="margin-top:18px;border-top:1px solid #333;padding-top:12px">
  <b id="l_pc"></b>
  <label id="l_pccur" style="display:block;margin-top:8px"></label><input id="pccur" type="password" inputmode="numeric" maxlength="6" autocomplete="current-password">
  <label id="l_pcnew"></label><input id="pcnew" type="password" inputmode="numeric" maxlength="6" autocomplete="new-password">
  <label id="l_pcnew2"></label><input id="pcnew2" type="password" inputmode="numeric" maxlength="6" autocomplete="new-password">
  <button class="pri" onclick="savePc()" id="b_savepc"></button> <span id="pcmsg" class="q"></span>
  <p class="hint" id="h_pc"></p>
 </div>
</div>

<button class="pri" style="width:100%;padding:13px" onclick="done()" id="b_done"></button>
<p class="hint" id="h_done"></p>
</div>
<script>
// Signed out (timed out, or signed out elsewhere): back to the sign-in page.
var _fetch=window.fetch;window.fetch=function(){return _fetch.apply(this,arguments).then(r=>{if(r.status==401){location.reload();throw new Error('signed out')}return r})};
function signout(){_fetch('/api/logout',{method:'POST'}).then(()=>location.reload())}
function T(e,z){return Z?z:e}
function $(i){return document.getElementById(i)}
function kb(n){return n>1048576?(n/1048576).toFixed(1)+' MB':Math.ceil(n/1024)+' KB'}
function esc(s){return s.replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';')}
var dir='/music';
var L={t_music:T('Music','音樂'),t_pl:T('Playlists','播放清單'),t_bg:T('Backgrounds','背景動畫'),
 b_newpl:T('New playlist','新增播放清單'),b_pickpl:T('Upload .m3u','上傳 .m3u'),
 h_pl:T('Folders under /music are playlists by themselves. Changes reach the player when it restarts (Done, below).','/music 內的資料夾會自動成為播放清單。按下方「完成」重新啟動後，播放器才會套用更改。'),
 l_plname:T('Name','名稱'),l_pladd:T('Add songs','加入歌曲'),b_pladdall:T('Add all shown','加入全部顯示的歌曲'),
 b_plsave:T('Save','儲存'),b_plcancel:T('Cancel','取消'),
 h_pled:T('Tip: type a folder or artist name to find songs, then "Add all shown".','提示：輸入資料夾或歌手名稱搜尋，再按「加入全部顯示的歌曲」。'),t_font:T('Title font','標題字型'),t_wifi:'WiFi',
 b_pick:T('Upload MP3s','上傳 MP3'),b_mkdir:T('New folder','新增資料夾'),drop_music:T('or drop MP3 files here','或把 MP3 拖放到這裡'),
 b_pickgif:T('Upload GIFs','上傳 GIF'),drop_bg:T('or drop GIF files here','或把 GIF 拖放到這裡'),
 h_bg:T('GIFs are saved as bg1.gif, bg2.gif… in upload order. Any size works: the player crops or shrinks them to fit when it restarts.','GIF 會依上傳次序儲存為 bg1.gif、bg2.gif…。任何尺寸都可以：播放器重新啟動時會自動裁切或縮小。'),
 b_pickfont:T('Upload font (.vlw)','上傳字型 (.vlw)'),
 h_font:T('Song titles and Chinese menus are drawn with this font. Make it on a computer with tools/gen_cjk_font.py.','歌名及中文選單使用此字型。請在電腦上用 tools/gen_cjk_font.py 製作。'),
 l_ssid:T('Network','網絡名稱'),l_pass:T('Password','密碼'),b_scan:T('Scan','掃描'),b_savewifi:T('Save and reconnect','儲存並重新連接'),
 b_forget:T('Forget WiFi','刪除 WiFi 設定'),h_wifi:T('2.4 GHz networks only. Saved on the player, not shown again.','只支援 2.4 GHz 網絡。密碼儲存在播放器內，不會再顯示。'),
 b_signout:T('Sign out','登出'),l_pc:T('Web passcode','網頁密碼'),l_pccur:T('Current passcode','目前密碼'),
 l_pcnew:T('New passcode (6 digits)','新密碼（6 位數字）'),l_pcnew2:T('New passcode again','再次輸入新密碼'),b_savepc:T('Change passcode','更改密碼'),
 h_pc:T('Asked for by this page. Forgot it? Setup ▸ Web passcode on the player shows it and can reset it to 000000.','此頁需要此密碼。忘記密碼？可在播放器的 設定 ▸ 網頁密碼 查看或重設為 000000。'),
 b_done:T('Done — restart the player','完成 — 重新啟動播放器'),h_done:T('New songs and backgrounds are prepared when the player restarts.','播放器重新啟動時會準備新歌曲及背景。')};
for(var k in L)$(k).textContent=L[k];

function tab(t){['music','pl','bg','font','wifi'].forEach(x=>{$('p_'+x).style.display=x==t?'':'none';$('t_'+x).className=x==t?'on':''});
 if(t=='music')list();if(t=='pl')pllist();if(t=='bg')bglist();if(t=='font')fontinfo();if(t=='wifi')wifiinfo();}

function info(){fetch('/api/info').then(r=>r.json()).then(j=>{$('info').textContent=
 T('Free space ','剩餘空間 ')+kb(j.free)+' / '+kb(j.total)+'  ·  '+(j.ap?T('hotspot ','熱點 ')+j.ssid:j.ssid+'  '+j.ip)})}

function list(){fetch('/api/list?dir='+encodeURIComponent(dir)).then(r=>r.json()).then(j=>{
 var parts=dir.split('/').filter(x=>x),h='',p='';
 parts.forEach((x,i)=>{p+='/'+x;h+=(i?' / ':'')+'<span class="dir" onclick="go(\''+esc(p)+'\')">'+esc(x)+'</span>'});
 $('crumb').innerHTML=h;
 var o='';
 if(dir!='/music')o+='<div class="row"><span class="nm dir" onclick="go(\''+esc(dir.replace(/\/[^\/]*$/,''))+'\')">⬑ ..</span></div>';
 j.items.forEach(e=>{var full=dir+'/'+e.name;
  o+='<div class="row">'+(e.dir?'<span class="nm dir" onclick="go(\''+esc(full)+'\')">📁 '+esc(e.name)+'</span>'
   :'<span class="nm">'+esc(e.name)+'</span><span class="sz">'+kb(e.size)+'</span>')+
   '<button class="ic" title="rename" onclick="ren(\''+esc(full)+'\')">✎</button><button class="ic" title="delete" onclick="del(\''+esc(full)+'\','+(e.dir?1:0)+')">✕</button></div>'});
 if(!j.items.length)o+='<div class="hint">'+T('Empty','沒有檔案')+'</div>';
 $('list').innerHTML=o;})}
function go(d){dir=d;list()}

function bglist(){fetch('/api/list?dir=/bg').then(r=>r.json()).then(j=>{var o='';
 j.items.filter(e=>!e.dir).forEach(e=>{o+='<div class="row"><img class="th" src="/file?path='+encodeURIComponent('/bg/'+e.name)+'"><span class="nm">'+esc(e.name)+
  '</span><span class="sz">'+kb(e.size)+'</span><button class="ic" onclick="del(\'/bg/'+esc(e.name)+'\',0)">✕</button></div>'});
 $('bglist').innerHTML=o||'<div class="hint">'+T('No backgrounds yet','尚未有背景動畫')+'</div>';})}

function pllist(){plclose();fetch('/api/list?dir=/playlists').then(r=>r.json()).then(j=>j.items).catch(()=>[]).then(items=>{
 var o='<div class="row"><span class="nm">★ '+T('Favourites','我的最愛')+'</span><button class="ic" title="edit" onclick="pledit(\'fav\')">✎</button></div>';
 items.filter(e=>!e.dir).forEach(e=>{var n=esc(e.name);o+='<div class="row"><span class="nm">'+n+'</span><span class="sz">'+kb(e.size)+
  '</span><button class="ic" title="edit" onclick="pledit(\''+n+'\')">✎</button><button class="ic" title="delete" onclick="del(\'/playlists/'+n+'\',0)">✕</button></div>'});
 if(!items.filter(e=>!e.dir).length)o+='<div class="hint">'+T('No playlists yet','尚未有播放清單')+'</div>';
 $('pllist').innerHTML=o;})}

// ---- playlist editor: songs are paths inside /music, as the player reads them
var SONGS=null,LOW={},TITLE={},ED=null;
function songsReady(){if(SONGS)return Promise.resolve();
 return Promise.all([fetch('/api/songs').then(r=>r.json()),fetch('/api/titles').then(r=>r.text())]).then(([s,t])=>{
  t.split('\n').slice(1).forEach(l=>{var p=l.split('\t');if(p.length>1&&p[0].startsWith('/music/')&&p[1]){
   TITLE[p[0].slice(7).toLowerCase()]=p[1]+(p[2]?' - '+p[2]:'')}});
  SONGS=s.songs.sort((a,b)=>a.localeCompare(b));SONGS.forEach(x=>LOW[x.toLowerCase()]=x)})}
function title(rel){return TITLE[rel.toLowerCase()]||rel.split('/').pop().replace(/\.mp3$/i,'')}
function folder(rel){var i=rel.lastIndexOf('/');return i>0?rel.slice(0,i):''}
// The same reading of an entry as the player's (playlist.cpp resolveEntry).
function norm(p){var o=[];p.split('/').forEach(x=>{if(x=='..')o.pop();else if(x&&x!='.')o.push(x)});return '/'+o.join('/')}
function resolve(e){e=e.replace(/\\/g,'/');if(e.length>1&&e[1]==':')e=e.slice(2);var r;
 if(e[0]=='/'){var m=e.toLowerCase().indexOf('/music/');r=norm(m>0?e.slice(m):e)}
 else{var n=norm('/playlists/'+e);r=n.toLowerCase().startsWith('/music/')?n:norm('/music/'+e)}
 return r.toLowerCase().startsWith('/music/')?LOW[r.slice(7).toLowerCase()]:undefined}

function pledit(name){songsReady().then(()=>name?fetch('/api/playlist?name='+encodeURIComponent(name)).then(r=>r.text()):'').then(txt=>{
 var items=[];txt.split('\n').forEach(l=>{l=l.replace(/^﻿/,'').trim();if(!l||l[0]=='#')return;
  var rel=resolve(l);items.push(rel?{rel:rel}:{raw:l})});
 ED={name:name,items:items};
 $('plmain').style.display='none';$('pled').style.display='';
 $('plnamebox').style.display=name=='fav'?'none':'';
 $('plname').value=name&&name!='fav'?name.replace(/\.m3u8?$/i,''):'';
 $('plsearch').placeholder=T('Search songs, artists, folders','搜尋歌曲、歌手或資料夾');$('plsearch').value='';
 plrender();plpick();if(!name)$('plname').focus()}).catch(()=>alert(T('Could not read the song list','無法讀取歌曲清單')))}

function plclose(){ED=null;$('pled').style.display='none';$('plmain').style.display='';}
function plrender(){var o='';ED.items.forEach((it,i)=>{
 o+='<div class="row"><span class="sz">'+(i+1)+'</span><span class="nm">'+(it.rel?esc(title(it.rel))+' <span class="sz">'+esc(folder(it.rel))+'</span>'
  :'<span class="err">'+T('missing: ','找不到：')+esc(it.raw)+'</span>')+'</span>'+
  '<button class="ic" onclick="plmove('+i+',-1)">↑</button><button class="ic" onclick="plmove('+i+',1)">↓</button>'+
  '<button class="ic" onclick="plrm('+i+')">✕</button></div>'});
 $('plsongs').innerHTML=o||'<div class="hint">'+T('No songs yet — add some below.','尚未有歌曲，請在下方加入。')+'</div>';
 $('plcount').textContent=(ED.name=='fav'?'★ '+T('Favourites','我的最愛')+' · ':'')+ED.items.length+T(' songs',' 首歌');}
function plmove(i,d){var j=i+d;if(j<0||j>=ED.items.length)return;var t=ED.items[i];ED.items[i]=ED.items[j];ED.items[j]=t;plrender()}
function plrm(i){ED.items.splice(i,1);plrender();plpick()}
function inList(rel){return ED.items.some(it=>it.rel==rel)}
var SHOWN=[];
function plpick(){var q=$('plsearch').value.trim().toLowerCase();
 SHOWN=SONGS.filter(x=>!q||x.toLowerCase().includes(q)||title(x).toLowerCase().includes(q)).slice(0,300);
 var o='';SHOWN.forEach((x,i)=>{var on=inList(x);o+='<div class="row"><span class="nm">'+esc(title(x))+' <span class="sz">'+esc(folder(x))+'</span></span>'+
  (on?'<span class="ok">✓</span>':'<button class="ic" onclick="pladd('+i+')">＋</button>')+'</div>'});
 $('plpick').innerHTML=o||'<div class="hint">'+T('No matching songs','沒有相符的歌曲')+'</div>'}
function pladd(i){ED.items.push({rel:SHOWN[i]});plrender();plpick()}
function pladdShown(){SHOWN.forEach(x=>{if(!inList(x))ED.items.push({rel:x})});plrender();plpick()}
function plsave(){var fav=ED.name=='fav',name='fav';
 if(!fav){name=$('plname').value.trim();if(!name){alert(T('Give the playlist a name','請輸入播放清單名稱'));return}
  if(!/\.m3u8?$/i.test(name))name+=(ED.name&&/\.m3u$/i.test(ED.name))?'.m3u':'.m3u8'}
 // Favourites are full paths; playlists are paths inside /music, readable on a PC too.
 var lines=ED.items.map(it=>it.rel?(fav?'/music/'+it.rel:it.rel):it.raw);
 var text=fav?lines.join('\n')+'\n':'#EXTM3U\n'+lines.join('\n')+'\n',b={name:name,text:text};
 if(ED.name)b.old=ED.name;
 post('/api/playlist',b).then(j=>{if(!j.ok){alert(j.error);return}pllist();info()})}

function fontinfo(){fetch('/api/font').then(r=>r.json()).then(j=>{$('fontinfo').innerHTML=j.size?
 '<span class="ok">'+T('Installed','已安裝')+'</span> '+kb(j.size):'<span class="err">'+T('Not installed — titles show English letters only','未安裝 — 歌名只會顯示英文字母')+'</span>'})}

function wifiinfo(){fetch('/api/info').then(r=>r.json()).then(j=>{$('wifiinfo').textContent=j.ap?
 T('Using the player\'s own hotspot. Save a network to upload over your home WiFi.','正在使用播放器的熱點。儲存家中 WiFi 後可經家中網絡上傳。'):T('Connected to ','已連接到 ')+j.ssid;
 if(j.saved)$('ssid').value=j.saved;})}

function scan(){$('scanmsg').textContent=T('Scanning…','掃描中…');
 fetch('/api/scan').then(r=>r.json()).then(n=>{var d=$('nets');d.innerHTML='';n.forEach(s=>{var o=document.createElement('option');o.value=s;d.appendChild(o)});
 $('scanmsg').textContent=n.length+T(' found',' 個網絡')}).catch(()=>$('scanmsg').textContent=T('Scan failed','掃描失敗'))}

function post(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(b)}).then(r=>r.json())}
function saveWifi(){post('/api/wifi',{ssid:$('ssid').value,pass:$('pass').value}).then(j=>{
 document.body.innerHTML='<div class="w"><h1>CYD MP3</h1><p>'+T('Saved. The player is joining ','已儲存。播放器正在連接 ')+esc($('ssid').value)+
 T('. Its screen shows the new address to open.','。請在播放器螢幕上查看新的網址。')+'</p></div>'})}
function forgetWifi(){if(confirm(T('Forget the saved WiFi network?','刪除已儲存的 WiFi 設定？')))post('/api/wifi',{ssid:'',pass:''}).then(()=>
 document.body.innerHTML='<div class="w"><h1>CYD MP3</h1><p>'+T('Forgotten. The player restarts its hotspot.','已刪除。播放器會重新開啟熱點。')+'</p></div>')}
function savePc(){var c=$('pccur').value,n=$('pcnew').value,m=$('pcmsg');
 if(!/^\d{6}$/.test(n)){m.innerHTML='<span class="err">'+T('6 digits, please','請輸入 6 位數字')+'</span>';return}
 if(n!=$('pcnew2').value){m.innerHTML='<span class="err">'+T('The two new passcodes differ','兩次輸入的新密碼不同')+'</span>';return}
 post('/api/passcode',{cur:c,pin:n}).then(j=>{m.innerHTML=j.ok?'<span class="ok">✓ '+T('Changed','已更改')+'</span>':'<span class="err">'+esc(j.error)+'</span>';
  if(j.ok){$('pccur').value=$('pcnew').value=$('pcnew2').value=''}})}
function del(p,isDir){if(!confirm(T('Delete ','刪除 ')+p+'?'))return;post('/api/delete',{path:p}).then(j=>{if(!j.ok)alert(j.error);
 if(p.startsWith('/bg/'))bglist();else if(p.startsWith('/playlists/'))pllist();else list();info()})}
function ren(p){var old=p.split('/').pop(),n=prompt(T('New name','新名稱'),old);if(!n||n==old)return;
 post('/api/rename',{path:p,name:n}).then(j=>{if(!j.ok)alert(j.error);list()})}
function mkdir(){var n=prompt(T('Folder name','資料夾名稱'));if(!n)return;post('/api/mkdir',{path:dir+'/'+n}).then(j=>{if(!j.ok)alert(j.error);list()})}
function done(){post('/api/done',{}).then(()=>document.body.innerHTML='<div class="w"><h1>CYD MP3</h1><p>'+
 T('Restarting the player. You can close this page.','播放器正在重新啟動，可以關閉此頁面。')+'</p></div>')}

// GIF width and height live in bytes 6-9 of the file, little-endian.
function gifSize(f){return f.slice(0,10).arrayBuffer().then(b=>{var d=new DataView(b);
 if(String.fromCharCode(d.getUint8(0),d.getUint8(1),d.getUint8(2))!='GIF')return null;return [d.getUint16(6,true),d.getUint16(8,true)]})}

// One file at a time: the player has one SD card and a small buffer.
async function upload(files,target,qid,after){
 var q=$(qid);
 for(const f of files){
  var line=document.createElement('div');line.className='q';line.innerHTML=esc(f.name)+' <span class="sz">'+kb(f.size)+'</span><div class="bar"><i></i></div>';q.appendChild(line);
  if(target=='/bg'){var s=await gifSize(f);if(!s){line.innerHTML+='<span class="err">'+T('not a GIF','不是 GIF')+'</span>';continue}
   if(s[0]>2048){line.innerHTML+='<span class="err">'+T('too wide (max 2048 px)','太闊（最多 2048 px）')+'</span>';continue}}
  await new Promise(res=>{var x=new XMLHttpRequest(),fd=new FormData();fd.append('file',f,f.name);
   x.upload.onprogress=e=>{if(e.lengthComputable)line.querySelector('i').style.width=(100*e.loaded/e.total)+'%'};
   x.onload=()=>{if(x.status==401){location.reload();return}var j={};try{j=JSON.parse(x.responseText)}catch(e){}
    line.innerHTML+=j.ok?'<span class="ok">✓ '+esc(j.name||'')+'</span>':'<span class="err">'+esc(j.error||('HTTP '+x.status))+'</span>';res()};
   x.onerror=()=>{line.innerHTML+='<span class="err">'+T('connection lost','連線中斷')+'</span>';res()};
   x.open('POST','/api/upload?dir='+encodeURIComponent(target));x.send(fd)});
 }
 after();info();
}
$('f_music').onchange=e=>upload([...e.target.files],dir,'q_music',list);
$('f_pl').onchange=e=>upload([...e.target.files],'/playlists','q_pl',pllist);
$('f_bg').onchange=e=>upload([...e.target.files],'/bg','q_bg',bglist);
$('f_font').onchange=e=>upload([...e.target.files],'/.sys','q_font',fontinfo);
function dropper(id,fn){var d=$(id);d.ondragover=e=>{e.preventDefault();d.classList.add('hot')};d.ondragleave=()=>d.classList.remove('hot');
 d.ondrop=e=>{e.preventDefault();d.classList.remove('hot');fn([...e.dataTransfer.files])}}
dropper('drop_music',f=>upload(f,dir,'q_music',list));
dropper('drop_bg',f=>upload(f,'/bg','q_bg',bglist));
tab('music');info();
setInterval(()=>fetch('/api/ping').catch(()=>{}),30000);   // keeps the session while the page is open
</script></body></html>)HTML";

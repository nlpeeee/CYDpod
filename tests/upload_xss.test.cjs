// Focused regression check: execute the actual embedded page logic without dependencies.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const root = path.join(__dirname, '..');
const page = fs.readFileSync(path.join(root, 'firmware/cydpod_player/app/upload_page.h'), 'utf8');
const script = page.match(/<script>\r?\n([\s\S]*?)<\/script>/)[1];
const hostile = ["a'b", '\");alert(1);//', '<img src=x onerror=alert(1)>',
  '</script><script>alert(1)</script>', 'quote" & < > \\ ` 中文 café space'];
class Element {
  constructor(tag) { this.tagName = tag; this.childNodes = []; this.dataset = {}; this.style = {}; this.listeners = {}; this.value = ''; this.classList = {add(){}, remove(){}}; }
  appendChild(e) { this.childNodes.push(e); return e; }
  replaceChildren() { this.childNodes = []; this.html = ''; }
  set textContent(text) { this.text = String(text); this.childNodes = []; }
  get textContent() { return (this.text || '') + this.childNodes.map(e => e.textContent).join(''); }
  set innerHTML(html) {
    // Escaped text must never become an element or supply an event attribute.
    assert(!/<\/?(?:script|img)\b/i.test(html), 'name became executable markup');
    for (const m of html.matchAll(/\bonclick="([^"]*)"/g))
      assert(/^(?:plmove\(\d+,-?1\)|plrm\(\d+\)|pladd\(\d+\))$/.test(m[1]), 'dynamic inline handler');
    this.html = html; this.childNodes = [];
  }
  get innerHTML() { return this.html || ''; }
  querySelector() { return {style:{}}; }
  focus() {}
}
const ids = new Map();
const el = id => { if (!ids.has(id)) ids.set(id, new Element('div')); return ids.get(id); };
let items = [], requests = [], confirms = [], prompts = [], response = {ok:true};
const context = vm.createContext({Z:false, console, URLSearchParams, encodeURIComponent,
  document: {getElementById:el, createElement:tag=>new Element(tag), createTextNode:text=>{const e=new Element('#text');e.textContent=text;return e;}, body:new Element('body')},
  location:{reload(){throw Error('unexpected reload');}}, setInterval(){},
  alert(){throw Error('unexpected alert');}, confirm:p=>{confirms.push(p);return true;}, prompt:(p,n)=>{prompts.push(n);return n+'new';},
  window:{fetch:async u=>({status:200,json:async()=>u.startsWith('/api/list')?{items}:u==='/api/info'?{free:1,total:2}:response})}
});
// The script wraps window.fetch, as it would in a browser.
context.fetch = (...args)=>context.window.fetch(...args);
vm.runInContext(script.replace("tab('music');info();", ''), context);
const walk = e => [e, ...e.childNodes.flatMap(walk)];
const click = e => { assert(e.listeners.click); return e.listeners.click(); };
Element.prototype.addEventListener = function(type, fn) { this.listeners[type] = fn; };
function safeTree(id) {
  for (const e of walk(el(id))) {
    assert(!e.onclick && !e.onerror, 'inline executable handler');
    assert(!e.innerHTML, 'dynamic listing uses HTML');
    assert(!['script','iframe'].includes(e.tagName));
  }
}
(async()=>{
  for (const name of hostile) {
    items = [{name, dir:true, size:123}, {name, dir:false, size:123}];
    const folderName=name.replace(/\//g,'-');context.dir='/music/'+folderName;
    await context.list(); safeTree('list'); safeTree('crumb');
    const full=context.dir+'/'+name, calls=[];
    context.go=p=>calls.push(['go',p]);context.ren=p=>calls.push(['ren',p]);context.del=(p,d)=>calls.push(['del',p,d]);
    // list() binds current function references; render with spies before clicking.
    await context.list();
    const actions=walk(el('list')).filter(e=>e.listeners.click);
    actions.forEach(click);
    assert(calls.some(c=>c[0]==='go'&&c[1]===full));
    assert(calls.some(c=>c[0]==='ren'&&c[1]===full));
    assert(calls.some(c=>c[0]==='del'&&c[1]===full&&c[2]===1));
    assert(calls.some(c=>c[0]==='del'&&c[1]===full&&c[2]===0));
    assert(el('list').textContent.includes(name));
    calls.length=0;await context.list();walk(el('crumb')).filter(e=>e.listeners.click).forEach(click);
    assert(calls.some(c=>c[1]==='/music/'+folderName), 'breadcrumb path changed');
    await context.bglist();safeTree('bglist');walk(el('bglist')).filter(e=>e.listeners.click).forEach(click);
    assert(calls.some(c=>c[0]==='del'&&c[1]==='/bg/'+name));
    assert.equal(walk(el('bglist')).find(e=>e.tagName==='img').src,'/file?path='+encodeURIComponent('/bg/'+name));
    context.pledit=p=>calls.push(['edit',p]);await context.pllist();safeTree('pllist');
    walk(el('pllist')).filter(e=>e.listeners.click).forEach(click);
    assert(calls.some(c=>c[0]==='edit'&&c[1]===name));
    assert(calls.some(c=>c[0]==='del'&&c[1]==='/playlists/'+name));
    context.SONGS=[name];context.ED={name,items:[{rel:name},{raw:name}]};context.plrender();context.plpick();
    assert(el('plsongs').innerHTML.includes(context.esc(name)));
    assert(el('plpick').innerHTML.includes(context.esc(context.title(name))));
  }
  // Restore real actions and verify request bodies and native dialog strings.
  vm.runInContext(script.slice(script.indexOf('function del('),script.indexOf('function mkdir(')), context);
  context.post=async(u,b)=>{requests.push([u,b]);return {ok:true};};
  context.list=context.bglist=context.pllist=context.info=()=>{};
  for(const name of hostile){const p='/music/'+name;context.del(p,0);context.ren(p);
    assert.equal(confirms.at(-1),'Delete '+p+'?');assert.equal(prompts.at(-1),p.split('/').pop());
    assert.equal(requests.at(-2)[1].path,p);assert.equal(requests.at(-1)[1].name,p.split('/').pop()+'new');
    const encoded=new URLSearchParams(requests.at(-1)[1]);assert.equal(encoded.get('path'),p);
  }
  const server=fs.readFileSync(path.join(root,'firmware/cydpod_player/app/uploadmode.cpp'),'utf8');
  assert(server.includes("c == '/'"), 'JSON slash escaping missing');
  assert(server.includes('server.sendHeader("X-Content-Type-Options", "nosniff")'));
  assert.equal((server.match(/server\.send\([^\n]*"application\/json"/g)||[]).length,1,'streamed JSON bypasses headers');
  console.log('PASS upload XSS: hostile names stay inert; listing, breadcrumbs, background, playlist and action paths preserved');
})().catch(e=>{console.error(e);process.exitCode=1;});

// No browser, clipboard or external request is used by these flow checks.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const script = fs.readFileSync(path.join(__dirname,'../site/download.js'),'utf8');
const url = 'https://github.com/thomasthzh/TUnpack/releases/download/v0.13.0/TUnpack-0.13.0-windows-x64.zip';
function setup({agent='Windows NT 10.0', reject=false, clipboard=true}={}) {
  const elements = {};
  for(const id of ['download-primary','download-link','copy-download','download-status','platform-note']) elements[id]={handlers:{},dataset:{},hidden:true,href:url,value:url,addEventListener(name,fn){this.handlers[name]=fn;},focus(){this.focused=true;},select(){this.selected=true;}};
  let writes=[];
  const navigator={userAgent:agent};
  if(clipboard)navigator.clipboard={async writeText(text){if(reject)throw new Error('denied'); writes.push(text);}};
  vm.runInNewContext(script,{document:{querySelector(selector){return elements[selector.slice(1)];}},navigator});
  return {...elements,writes};
}
const tick=()=>new Promise(resolve=>setImmediate(resolve));
test('Mobile and non-Windows platforms receive guidance while keeping the real download available',()=>{
  for(const agent of ['Mozilla iPhone','Mozilla Macintosh','Android Linux','Linux x86_64']){
    const e=setup({agent});assert.equal(e['platform-note'].dataset.platform,'other');assert.match(e['platform-note'].textContent,/Windows 10 \/ 11 x64/);assert.equal(e['download-primary'].href,url);assert.equal(e.writes.length,0);
  }
});
test('A download click reports opening, never a completed or verified download',()=>{
  const e=setup();assert.equal(e['download-status'].textContent,undefined);e['download-primary'].handlers.click();assert.match(e['download-status'].textContent,/正在打开下载链接/);assert.match(e['download-status'].textContent,/GitHub 发布页重试/);assert.doesNotMatch(e['download-status'].textContent,/下载成功|已下载|已校验/);assert.equal(e['download-primary'].href,url);
});
test('Copy occurs only on deliberate request and copies the exact public package URL',async()=>{
  const e=setup();assert.equal(e.writes.length,0);assert.equal(e['copy-download'].hidden,false);e['copy-download'].handlers.click();await tick();assert.deepEqual(e.writes,[url]);assert.match(e['download-status'].textContent,/已复制/);
});
test('Clipboard rejection or absence selects the visible read-only URL for manual copy',async()=>{
  for(const option of [{reject:true},{clipboard:false}]){
    const e=setup(option);e['copy-download'].handlers.click();await tick();assert.equal(e['download-link'].focused,true);assert.equal(e['download-link'].selected,true);assert.match(e['download-status'].textContent,/手动复制/);assert.equal(e.writes.length,0);
  }
});

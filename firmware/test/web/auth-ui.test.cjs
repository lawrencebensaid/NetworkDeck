// No dependencies: node --test firmware/test/web/auth-ui.test.cjs
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const root = path.resolve(__dirname, '../../components/web');
function script(file) { return fs.readFileSync(path.join(root, file), 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1]; }
function page(file, setup = false, response = {ok:true}) {
  const elements = new Map();
  function element(id) {
    if (!elements.has(id)) elements.set(id, {value:'', textContent:'', innerHTML:'', classList:{toggle(){},remove(){}}, files:[]});
    return elements.get(id);
  }
  const requests = [], navigations = [];
  let xhr;
  const context = {
    document:{getElementById:element,querySelectorAll:()=>[],querySelector:()=>({checked:true,value:'dhcp'})},
    location:{replace:url=>navigations.push(url),reload(){}},
    TextEncoder, URLSearchParams, setInterval(){},setTimeout(){},
    fetch:async (...args)=>{requests.push(args);return {status:response.status||200,json:async()=>response};},
    XMLHttpRequest: class {
      constructor(){xhr=this;this.upload={};this.headers={};}
      open(method,url){this.method=method;this.url=url;}
      setRequestHeader(key,value){this.headers[key]=value;}
      send(body){this.body=body;}
    },
  };
  context.window = {fetch:context.fetch};
  vm.createContext(context);
  vm.runInContext(script(file).replace('AUTH_MODE', String(setup)),context);
  // In a browser window.fetch and the global fetch are the same property.
  context.fetch = context.window.fetch;
  return {element,requests,navigations,context,get xhr(){return xhr;}};
}
const submit = page => page.element('login').onsubmit({preventDefault(){}});

test('setup screen has confirmation, no private network requests before login',()=>{
  const p=page('login.html',true);
  assert.equal(p.element('repeat').hidden,false);
  assert.equal(p.element('confirmation').required,true);
  assert.equal(p.element('password').autocomplete,'new-password');
  assert.equal(p.requests.length,0);
});
test('setup rejects mismatched passwords without sending',async()=>{
  const p=page('login.html',true);
  p.element('password').value='long-passphrase';p.element('confirmation').value='different';
  await submit(p);
  assert.equal(p.requests.length,0);
  assert.match(p.element('message').textContent,/not match/);
});
test('password byte limit handles Unicode before sending',async()=>{
  const p=page('login.html');
  p.element('password').value='é'.repeat(65);
  await submit(p);assert.equal(p.requests.length,0);
});
test('login posts password with CSRF header, clears fields, redirects on success',async()=>{
  const p=page('login.html');p.element('password').value='long-passphrase';
  await submit(p);
  assert.equal(p.requests[0][0],'/api/login');
  assert.equal(p.requests[0][1].headers['X-NetworkDeck'],'1');
  assert.equal(p.requests[0][1].body,'long-passphrase');
  assert.equal(p.element('password').value,'');
  assert.deepEqual(p.navigations,['/']);
});
test('failed or rate limited login stays on sign-in and shows server message',async()=>{
  const p=page('login.html',false,{ok:false,status:429,message:'Wait before trying again'});
  p.element('password').value='long-passphrase';await submit(p);
  assert.deepEqual(p.navigations,[]);
  assert.equal(p.element('message').textContent,'Wait before trying again');
  assert.equal(p.element('submit').disabled,false);
});
test('console logout carries CSRF header and navigates back to login',async()=>{
  const p=page('index.html');await p.element('logout').onclick();
  const request=p.requests.find(r=>r[0]==='/api/logout');
  assert.equal(request[1].headers['X-NetworkDeck'],'1');
  assert.deepEqual(p.navigations,['/']);
});
test('Wi-Fi and network mutations carry the CSRF header',async()=>{
  const p=page('index.html');p.element('ssid').value='home';
  await p.element('join').onclick();p.element('savenet').onclick();
  for(const url of ['/api/wifi','/api/network']) {
    const request=p.requests.find(r=>r[0]===url);
    assert.equal(request[1].headers['X-NetworkDeck'],'1');
  }
});
test('firmware upload carries CSRF header; expired auth returns to sign-in',()=>{
  const p=page('index.html');p.element('fw').files=[{name:'firmware.bin'}];
  p.element('upload').onclick();
  assert.equal(p.xhr.url,'/api/update');
  assert.equal(p.xhr.headers['X-NetworkDeck'],'1');
  p.xhr.status=401;p.xhr.onload();assert.deepEqual(p.navigations,['/']);
});
test('expired API session returns to sign-in',async()=>{
  const p=page('index.html',false,{status:401});
  await p.context.fetch('/api/status');assert.ok(p.navigations.includes('/'));
});
test('network-controlled text is escaped for both text and attribute contexts',()=>{
  const p=page('index.html');
  assert.equal(vm.runInContext('escapeHTML(\'&<img src=x onerror="x">\')',p.context),'&amp;&lt;img src=x onerror=&quot;x&quot;&gt;');
});

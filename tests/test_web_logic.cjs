// Run the exact generated HTML script with real SHA-256/Blob support and a
// minimal DOM harness. This validates patching logic, not browser rendering.
const fs=require('fs'),path=require('path'),vm=require('vm'),crypto=require('crypto');
const root=path.resolve(__dirname,'..'),stock=fs.readFileSync(process.argv[2]);
const manifest=JSON.parse(fs.readFileSync(path.join(root,'patch_manifest.json')));
const html=fs.readFileSync(path.join(root,'patch_bluebox.html'),'utf8');
const script=html.match(/<script>([\s\S]*)<\/script>/)[1];
const ids=[...new Set([...html.matchAll(/\bid="([^"]+)"/g)].map(x=>x[1]))];
const elements=Object.fromEntries(ids.map(id=>[id,{files:[],checked:false,hidden:true,disabled:true,textContent:'',value:'',style:{props:{},setProperty(k,v){this.props[k]=v;}}}]));
for(const f of manifest.theme.fields)elements[f.key].value=elements[f.key+'Hex'].value=f.default;
elements.themePreset.value='Default';
const ctx={document:{getElementById:id=>elements[id]},crypto:crypto.webcrypto,Blob,URL,atob};ctx.globalThis=ctx;
vm.createContext(ctx);vm.runInContext(script,ctx);
function select(buf){elements.file.files=[{arrayBuffer:async()=>Uint8Array.from(buf).buffer}];elements.file.onchange();}
let count=0;
function check(cond,msg){count++;if(!cond)throw Error(msg);console.log('PASS',msg);}
async function output(){const response=await fetch(elements.download.href);return Buffer.from(await response.arrayBuffer());}
(async()=>{
select(stock);const pending=elements.go.onclick();check(elements.file.disabled&&elements.restore.disabled&&elements.accent.disabled&&elements.accentHex.disabled,'web locks input and colour changes during generation');await pending;check(!elements.file.disabled&&!elements.restore.disabled&&!elements.accent.disabled,'web releases controls after generation');check(!elements.download.hidden,'web script accepts exact stock input');const patched=await output();
check(crypto.createHash('sha256').update(patched).digest('hex')===manifest.patched_sha256,'web output matches expected patched hash');
select(patched);await elements.go.onclick();check(elements.download.hidden&&elements.status.textContent.startsWith('ERROR:'),'web script rejects already-patched input');
elements.restore.checked=true;elements.restore.onchange();await elements.go.onclick();check(!elements.download.hidden&&(await output()).equals(stock),'web restoration reproduces exact original');
elements.restore.checked=false;select(Buffer.from('wrong firmware'));await elements.go.onclick();check(elements.download.hidden&&elements.status.textContent.startsWith('ERROR:'),'web script rejects wrong size/hash');
select(Buffer.from(stock));elements.file.files[0].arrayBuffer=async()=>{const b=Uint8Array.from(stock);b[0]^=1;return b.buffer;};await elements.go.onclick();check(elements.download.hidden&&elements.status.textContent.startsWith('ERROR:'),'web script rejects one-byte corruption');
check(!/<(?:script|link|iframe)[^>]+(?:src|href)\s*=\s*"(?!data:)/i.test(html)&&!script.includes('fetch('),'HTML has no external resources or network calls');
for(const color of ['#9455fa','#6b03fc','#ff6af3','#000000','#ffffff','#12ab90']){
 elements.restore.checked=false;select(stock);elements.accentHex.value=color;elements.accentHex.oninput();
 await elements.go.onclick();check(!elements.download.hidden,'web accepts chosen accent '+color);
 const custom=await output(),expected=Buffer.from(patched);
 for(const offset of manifest.accent.offsets)expected.writeUInt32LE((0xff000000+parseInt(color.slice(1),16))>>>0,offset);
 check(custom.equals(expected),'web changes only the exact palette words '+color);
 const {execFileSync}=require('child_process');
 const py=execFileSync('python3',['-c',"import importlib.util,pathlib,sys;s=importlib.util.spec_from_file_location('p',sys.argv[1]);p=importlib.util.module_from_spec(s);s.loader.exec_module(p);sys.stdout.buffer.write(p.transform(pathlib.Path(sys.argv[2]).read_bytes(),accent=sys.argv[3]))",path.join(root,'patch_bluebox.py'),path.resolve(process.argv[2]),color],{maxBuffer:2*1024*1024});
 check(custom.equals(py),'browser and Python output agree byte for byte '+color);
 elements.restore.checked=true;elements.restore.onchange();select(custom);await elements.go.onclick();
 check(!elements.download.hidden&&(await output()).equals(stock),'web restores selected colour without manual input '+color);
 check(elements.accent.disabled&&elements.accentHex.disabled,'restore disables accent editing '+color);
 const corrupt=Buffer.from(custom);corrupt[0x10d240]^=1;select(corrupt);await elements.go.onclick();
 check(elements.download.hidden&&elements.status.textContent.startsWith('ERROR:'),'web rejects unmatched palette words '+color);
}
for(const color of ['#fff','#1234567','#zzzzzz','#123456\n']){
 elements.restore.checked=false;select(stock);elements.accentHex.value=color;elements.accentHex.oninput();await elements.go.onclick();
 check(elements.download.hidden&&elements.status.textContent.startsWith('ERROR:'),'web rejects malformed colour '+JSON.stringify(color));
}
elements.restore.checked=false;select(stock);elements.accent.value='#79f7e0';elements.accent.oninput();
check(elements.accentHex.value==='#79f7e0'&&!elements.accent.disabled,'colour picker updates the hex field');
await elements.go.onclick();check(!elements.download.hidden,'valid picker colour generates download');
elements.accent.value='#9455fa';elements.accent.oninput();check(elements.download.hidden&&elements.status.textContent==='','changing the colour invalidates the prior download');
const corrupt=Buffer.from(patched);corrupt[0x78000]^=1;elements.restore.checked=true;select(corrupt);await elements.go.onclick();
check(elements.download.hidden&&elements.status.textContent.startsWith('ERROR:'),'web refuses modified executable bytes during custom restore');
// Theme presets and native palette variants: compare real browser script to Python.
for(const name of Object.keys(manifest.theme.presets))check(html.includes('<option>'+name+'</option>'),'preset is selectable in actual HTML '+name);
for(const name of [...Object.keys(manifest.theme.presets).filter(n=>n!=='Amber'),'Amber']){
 elements.restore.checked=false;elements.themePreset.value=name;elements.themePreset.onchange();
 check(manifest.theme.fields.every(f=>elements[f.key+'Hex'].value===manifest.theme.presets[name][f.key]),'preset populates every colour '+name);
 check(manifest.theme.fields.every(f=>elements.themePreview.style.props['--'+f.key]===manifest.theme.presets[name][f.key]),'preview uses every selected role '+name);
 select(stock);await elements.go.onclick();const custom=await output();
 const py=require('child_process').execFileSync('python3',['-c',"import importlib.util,json,pathlib,sys;s=importlib.util.spec_from_file_location('p',sys.argv[1]);p=importlib.util.module_from_spec(s);s.loader.exec_module(p);sys.stdout.buffer.write(p.transform(pathlib.Path(sys.argv[2]).read_bytes(),theme=json.loads(sys.argv[3])))",path.join(root,'patch_bluebox.py'),path.resolve(process.argv[2]),JSON.stringify(manifest.theme.presets[name])],{maxBuffer:2*1024*1024});
 check(custom.equals(py),'complete theme agrees with Python '+name);
 elements.restore.checked=true;select(custom);await elements.go.onclick();check(!elements.download.hidden&&(await output()).equals(stock),'complete theme restores without manual colours '+name);
}
elements.restore.checked=false;elements.restore.onchange();elements.themeExport.onclick();
const exported=await (await fetch(elements.themeDownload.href)).json();
check(exported.schema===1&&JSON.stringify(exported.colors)===JSON.stringify(manifest.theme.presets.Amber),'export preserves all exact selected colours');
elements.themeReset.onclick();check(manifest.theme.fields.every(f=>elements[f.key+'Hex'].value===f.default),'reset restores all defaults');
async function importTheme(value){const s=JSON.stringify(value);elements.themeFile.files=[{size:s.length,text:async()=>s}];await elements.themeFile.onchange();}
await importTheme(exported);check(elements.themeMessage.textContent==='Theme imported.'&&JSON.stringify(vm.runInContext('currentTheme()',ctx))===JSON.stringify(exported.colors),'theme import/export round trip');
const retained=JSON.stringify(vm.runInContext('currentTheme()',ctx));
for(const bad of [{schema:2,colors:{}},{schema:1,colors:{background:'#fff'}},{schema:1,colors:{unknown:'#123456'}},{schema:1,colors:null},{schema:1,colors:{},extra:1}]){
 await importTheme(bad);check(elements.themeMessage.textContent.startsWith('ERROR:')&&JSON.stringify(vm.runInContext('currentTheme()',ctx))===retained,'invalid import is rejected atomically '+JSON.stringify(bad));
}
elements.themeFile.files=[{size:65537,text:async()=>{throw Error('must not read oversized file');}}];await elements.themeFile.onchange();check(elements.themeMessage.textContent.startsWith('ERROR:'),'oversized theme rejected before reading');
await importTheme({schema:1,colors:{background:'#152437'}});check(elements.backgroundHex.value==='#152437'&&elements.textHex.value==='#fcfcfc','partial theme fills omitted roles with documented defaults');
elements.textHex.value='#bad';elements.textHex.oninput();elements.themeExport.onclick();check(elements.themeDownload.hidden&&elements.themeMessage.textContent.startsWith('ERROR:'),'invalid colour cannot export a theme');
// Older theme files: alpha.5 nine-role files and alpha.6 files with stock-only entries.
const legacy={schema:1,colors:{accent:"#277c4e",background:"#12201a",text:"#edfff4",button_idle:"#345444",button_active:"#256949",button_pressed:"#2c8059",button_border:"#8cf0b6",control_active:"#8cf0b6",knob:"#8cf0b6"}};
await importTheme(legacy);check(elements.themeMessage.textContent==='Theme imported.'&&manifest.theme.fields.filter(f=>f.key.startsWith('slot')).every(f=>elements[f.key+'Hex'].value===f.default),'alpha.5 nine-role theme imports with stock palette entries');
await importTheme({schema:1,colors:Object.assign({},manifest.theme.presets.Lagoon,...manifest.theme.legacy_keys.map(k=>({[k]:'#123456'})))});
check(elements.themeMessage.textContent==='Theme imported.'&&JSON.stringify(vm.runInContext('currentTheme()',ctx))===JSON.stringify(manifest.theme.presets.Lagoon),'alpha.6 theme with stock-only entries imports, ignoring them');
// Every offered entry set at once: browser and Python agree, and restore is exact.
const vivid=Object.fromEntries(manifest.theme.fields.map((f,i)=>[f.key,'#'+((0x3a1f7*(i+1))&0xffffff).toString(16).padStart(6,'0')]));
elements.restore.checked=false;elements.restore.onchange();await importTheme({schema:1,colors:vivid});select(stock);await elements.go.onclick();const custom=await output();
const tf=path.join(require('os').tmpdir(),'bluebox-vivid.json');fs.writeFileSync(tf,JSON.stringify(vivid));
const py=require('child_process').execFileSync('python3',['-c',"import importlib.util,json,pathlib,sys;s=importlib.util.spec_from_file_location('p',sys.argv[1]);p=importlib.util.module_from_spec(s);s.loader.exec_module(p);sys.stdout.buffer.write(p.transform(pathlib.Path(sys.argv[2]).read_bytes(),theme=json.loads(pathlib.Path(sys.argv[3]).read_text())))",path.join(root,'patch_bluebox.py'),process.argv[2],tf],{maxBuffer:64<<20});
check(custom.equals(py),'every offered entry agrees with Python');
elements.restore.checked=true;select(custom);await elements.go.onclick();check(!elements.download.hidden&&(await output()).equals(stock),'every offered entry restores exact stock');
console.log(count+' web-logic checks passed. Real browser rendering/download UI not exercised.');
})().catch(e=>{console.error(e);process.exitCode=1;});

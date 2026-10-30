// Execute the exact page script and render its SVG output locally. Browser
// CSS/layout is not modeled here; patching is covered by test_web_logic.cjs.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
let sharp;try{sharp=require('sharp');}catch{console.log('INFO Sharp unavailable; SVG raster output skipped, logic assertions still run.');}
const root=path.resolve(__dirname,'..'),html=fs.readFileSync(path.join(root,'patch_bluebox.html'),'utf8'),manifest=JSON.parse(fs.readFileSync(path.join(root,'patch_manifest.json')));
const ids=[...new Set([...html.matchAll(/\bid="([^"]+)"/g)].map(x=>x[1]))];
const elements=Object.fromEntries(ids.map(id=>[id,{id,files:[],checked:false,hidden:true,disabled:false,value:'',textContent:'',innerHTML:'',attrs:{},setAttribute(k,v){this.attrs[k]=v;},style:{props:{},setProperty(k,v){this.props[k]=v;}}}]));
for(const f of manifest.theme.fields)elements[f.key].value=elements[f.key+'Hex'].value=f.default;
elements.previewDepth.value='rgb888';
const ctx={document:{getElementById:id=>elements[id]},URL,Blob,atob};ctx.globalThis=ctx;vm.createContext(ctx);vm.runInContext(html.match(/<script>([\s\S]*)<\/script>/)[1],ctx);
let n=0;function check(c,msg){assert(c,msg);n++;console.log('PASS',msg);}
function run(s){return vm.runInContext(s,ctx);}
(async()=>{
check(html.includes('bluebox desktop firmware 1.5.2 · v'+manifest.version)&&manifest.version==='1.1.0-midi.m9'&&!html.includes('Tools shows')&&!html.includes('__MOD_VERSION__'),'page shows the firmware and Cobalt versions, without the TOOLS label');
check(!html.includes('43.8%')&&!html.includes('CPU %'),'preview header has no CPU percentage');
const views=run('SCREEN_LIST.map(x=>x[0])'),show=v=>run('setView('+JSON.stringify(v)+')');
check(views.length===27&&!views.includes('delay'),'27 photo-based screens, no invented FX1 Delay page');
const keysUsed=new Set();
for(const view of views){
 show(view);const svg=elements.blueboxScreen.innerHTML;
 check(svg.includes('<rect')&&svg.includes('<text')&&!svg.includes('undefined')&&!svg.includes('NaN'),'complete screen recreation '+view);
 check(svg.includes('#00c850')&&svg.includes('data-k="compressor"'),'fixed CPU meter and themed compressor bar '+view);
 const keys=[...svg.matchAll(/data-k="([^"]+)"/g)].flatMap(x=>x[1].split(' '));keys.forEach(k=>keysUsed.add(k));
 check(keys.every(k=>manifest.theme.fields.some(f=>f.key===k)),'every tagged element maps to a theme control '+view);
 check(elements.screenName.textContent.length>0&&elements.blueboxScreen.ariaLabel.includes('Bluebox'),'screen name and label update '+view);
 const full='<svg xmlns="http://www.w3.org/2000/svg" width="960" height="720" viewBox="0 0 320 240" font-family="monospace" font-weight="700">'+svg+'</svg>';
 if(sharp)await sharp(Buffer.from(full)).png().toFile(path.join(root,'tests','theme-preview-'+view+'.png'));
}
const core=manifest.theme.fields.filter(f=>!f.key.startsWith('slot')).map(f=>f.key);
check(core.filter(k=>k!=='button_border').every(k=>keysUsed.has(k))&&!keysUsed.has('button_border'),'every traced role except the unidentified outline role appears on a screen');
check(manifest.theme.fields.filter(f=>f.key.startsWith('slot')).every(f=>keysUsed.has(f.key)),'every offered palette entry is drawn');
check(manifest.theme.legacy_keys.length===12&&manifest.theme.legacy_keys.every(k=>!keysUsed.has(k)&&!manifest.theme.fields.some(f=>f.key===k)),'stock-only entries are neither offered nor drawn');
const vivid={accent:'#f055d2',background:'#101820',text:'#e0f8ff',button_idle:'#304050',button_active:'#205f78',button_pressed:'#b04020',button_border:'#a0d0e0',control_active:'#d5b050',knob:'#70e0c0',slot15:'#ffe0a0',slot11:'#40a0ff',slot23:'#30ff90',slot24:'#ff8000',slot07:'#ff4060',slot08:'#202a10',slot06:'#ff2020',slot13:'#ff00aa',slot30:'#00ff00',slot16:'#4040a0',slot25:'#5a3070',slot20:'#ffd000',compressor:'#ff00ff'};
run('applyTheme('+JSON.stringify(vivid)+')');
show('track');let svg=elements.blueboxScreen.innerHTML;
check(['text','knob','background','accent','button_idle','slot15','slot11','slot06','compressor'].every(k=>svg.includes(vivid[k])),'TRACK follows text, knob, base, selection, button, label, REC and record-arm colours');
show('fx1');svg=elements.blueboxScreen.innerHTML;
check(['button_idle','button_active','control_active','accent','knob'].every(k=>svg.includes(vivid[k])),'FX1 Delay panel shows lit toggles with FX-active labels');
show('mixer');svg=elements.blueboxScreen.innerHTML;
check([...svg.matchAll(new RegExp('fill="'+vivid.button_active+'"','g'))].length>=5,'level meters draw as paired bars');
check(svg.includes('fill="'+vivid.text+'"')&&svg.includes('>Gain<'),'idle tab labels use primary text');
show('mute');svg=elements.blueboxScreen.innerHTML;check(svg.includes('fill="'+vivid.slot08+'"')&&svg.includes('data-k="button_idle"'),'muted track number uses slot 8, muted meters the idle fill');
for(const [v,ks] of [['solo',['slot23']],['tracksolo',['slot23','slot15']],['rec',['slot06']],['tunerfar',['slot06','slot15']],['tunernear',['slot20','slot15']],['sidechain',['slot07']],['tools',['slot30']],['mainmenu',['slot30','accent']],['learn',['slot25','accent']],['load',['slot13','button_active']],['keys',['button_pressed','slot15']],['scope',['slot07','slot08','slot16','accent']],['tuner',['slot11','slot15']]]){show(v);check(ks.every(k=>elements.blueboxScreen.innerHTML.includes(vivid[k])),v+' follows hardware-mapped entries '+ks.join(','));}
const tickBefore=elements.blueboxScreen.innerHTML;elements.slot08Hex.value='#ff7a00';elements.slot08Hex.oninput();show('tuner');check(!tickBefore.includes('#ff7a00')&&(elements.blueboxScreen.innerHTML.match(/stroke="#ff7a00"/g)||[]).length===13,'tuner ticks follow a visible slot 8, else read as primary text');
elements.slot08Hex.value=vivid.slot08;elements.slot08Hex.oninput();
show('eq');svg=elements.blueboxScreen.innerHTML;
check(['slot07','slot08','slot15','slot24'].every(k=>svg.includes(vivid[k])),'EQ follows spectrum, background, node and curve entries');
const click=v=>elements.themePreview.onclick({target:{closest:s=>s==='[data-view]'?{dataset:{view:v}}:null}});
check(['MIXER','TRACK','MAIN','EQ','FX','EDIT','PROJ','TOOLS'].every(s=>elements.sectionTabs.innerHTML.includes('>'+s+'<')),'section tabs list every hardware key');
click('eq');check(run('view')==='eq'&&elements.pageTabs.innerHTML.includes('data-view="eqfull"')&&elements.screenName.textContent==='EQ · Tracks','section tab opens its first page and lists its pages');
click('eqfull');check(elements.blueboxScreen.ariaLabel.includes('EQ · Full screen')&&/data-view="eqfull" aria-pressed="true"/.test(elements.pageTabs.innerHTML),'page tab switches screen');
elements.modeGrid.onclick();check(views.every(v=>elements.gridView.innerHTML.includes('data-view="'+v+'"'))&&!elements.gridView.hidden&&elements.singleView.hidden,'all-screens grid renders every screen');
click('solo');check(run('mode')==='single'&&run('view')==='solo'&&elements.gridView.innerHTML===''&&!elements.singleView.hidden,'clicking a grid screen opens it in the single view');
elements.modeGrid.onclick();elements.modeSingle.onclick();check(elements.gridView.innerHTML===''&&!elements.singleView.hidden,'single mode restores the large screen');
check(/<button id="themeImport" class="ghost" type="button">Import<\/button><input id="themeFile"[^>]* hidden/.test(html),'Import is a ghost button like Reset and Export; the file input is hidden');
let picked=0;elements.themeFile.click=()=>picked++;elements.themeImport.onclick();check(picked===1,'Import button opens the file picker');
show('track');elements.compare.onpointerdown({});check(elements.blueboxScreen.innerHTML.includes('#9455fa')&&!elements.blueboxScreen.innerHTML.includes(vivid.accent),'hold-to-compare shows stock colours');
elements.compare.onpointerup();check(!elements.blueboxScreen.innerHTML.includes('#9455fa'),'releasing compare returns to the chosen theme');
check(run("displayColor('#010203','rgb565')")==='#000000','low-level RGB values can collapse to black in RGB565 approximation');
check(run("displayColor('#ff0000','rgb555')")==='#ff0000','full-intensity red stays red in RGB555 approximation');
check(run("displayColor('#123456','rgb888')")==='#123456','RGB888 preview preserves exact input RGB');
elements.previewDepth.value='rgb565';elements.previewDepth.onchange();check(!elements.blueboxScreen.innerHTML.includes(vivid.knob),'display-depth selection rerenders quantized colors');
run("applyTheme({background:'#141414',text:'#141414',button_idle:'#141414',slot15:'#141414',knob:'#0b0b0b'})");check(elements.contrast.textContent.includes('low contrast'),'low-contrast feedback covers invisible text/knobs');
elements.knobHex.value='#fff';elements.knobHex.oninput();check(elements.knobHex.ariaInvalid==='true'&&elements.contrast.textContent.includes('complete #RRGGBB'),'invalid hex is explained beside retained valid preview');
check(!/<(?:script|link|iframe)[^>]+(?:src|href)\s*=\s*"(?!data:)/i.test(html)&&!html.includes('fetch('),'preview introduces no remote assets or network requests');
check(html.includes('<title>Bluebox Cobalt mod</title>')&&html.includes('<h1>Bluebox Cobalt mod</h1>')&&!/Theme Studio|slot finder|not seen|experimental/i.test(html.replace(/<script>[\s\S]*<\/script>/,'')),'page is titled Bluebox Cobalt mod and carries no finder or experimental wording');
check(/<div class="toolbar"><label>Preset <select id="themePreset">[\s\S]*?<\/select><\/label><\/div>\n<div class="toolbar actions"><button id="themeReset"[^>]*>Reset<\/button><button id="themeExport"[^>]*>Export<\/button><button id="themeImport"/.test(html),'Reset, Export and Import sit in a row under the preset selector');
check(/<details class="contrast"><summary>Contrast check<\/summary><small id="contrast">/.test(html),'contrast report is collapsible');
check(html.includes('max-width:1500px')&&!html.includes('class="badge'),'page is up to 1500px wide with no status badges');
let picks=0;elements.file.click=()=>picks++;elements.dropzone.onclick();check(picks===1,'clicking the dropzone opens the firmware picker');
let prevented=0;const ev={preventDefault:()=>prevented++,dataTransfer:{files:[{name:'BLUEBOX.BIN',size:2097152}]}};elements.dropzone.ondragover(ev);elements.dropzone.ondrop(ev);
check(prevented===2&&elements.file.files[0].name==='BLUEBOX.BIN'&&elements.fileName.textContent==='BLUEBOX.BIN · 2.00 MB','dropping a firmware file selects it and shows its name');
const docs=path.join(root,'docs','index.html');check(fs.existsSync(docs)&&fs.readFileSync(docs,'utf8')===html&&fs.existsSync(path.join(root,'docs','.nojekyll')),'GitHub Pages copy in docs/ matches the patcher page');
console.log(n+' page-preview mapping, navigation, compare, quantization and input-feedback checks passed. Full browser layout is not verified here.');
})().catch(e=>{console.error(e);process.exitCode=1;});

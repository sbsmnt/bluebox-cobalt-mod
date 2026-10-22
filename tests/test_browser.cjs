const { chromium } = require('playwright');
const fs = require('fs');const path = require('path');const os=require('os');const crypto=require('crypto');
(async()=>{
const root=path.resolve(__dirname,'..');const stock=path.resolve(process.argv[2]);const manifest=JSON.parse(fs.readFileSync(path.join(root,'patch_manifest.json')));
const browser=await chromium.launch({headless:true,args:['--no-sandbox']});
const tmp=fs.mkdtempSync(path.join(os.tmpdir(),'bluebox-browser-'));
try{const page=await browser.newPage();await page.goto('file://'+path.join(root,'patch_bluebox.html'));
await page.locator('#file').setInputFiles(stock);await page.locator('#go').click();await page.locator('#download').waitFor({state:'visible'});
const [dl]=await Promise.all([page.waitForEvent('download'),page.locator('#download').click()]);const mod=path.join(tmp,'BLUEBOX.BIN');await dl.saveAs(mod);
if(crypto.createHash('sha256').update(fs.readFileSync(mod)).digest('hex')!==manifest.patched_sha256)throw Error('Browser patch checksum mismatch');console.log('PASS browser patch produces exact expected bytes');
await page.locator('#file').setInputFiles(mod);await page.locator('#go').click();await page.waitForFunction(()=>document.querySelector('#status').textContent.startsWith('ERROR:'));
if(await page.locator('#download').isVisible())throw Error('Downloaded for wrong input');console.log('PASS browser refuses already-patched input');
await page.locator('#restore').check();await page.locator('#go').click();await page.locator('#download').waitFor({state:'visible'});
const [restoreDl]=await Promise.all([page.waitForEvent('download'),page.locator('#download').click()]);const original=path.join(tmp,'restored.bin');await restoreDl.saveAs(original);
if(!fs.readFileSync(original).equals(fs.readFileSync(stock)))throw Error('Restore differs');console.log('PASS browser restore reconstructs original byte-for-byte');
const bad=path.join(tmp,'bad.bin');fs.writeFileSync(bad,'wrong firmware');await page.locator('#restore').uncheck();await page.locator('#file').setInputFiles(bad);await page.locator('#go').click();await page.waitForFunction(()=>document.querySelector('#status').textContent.startsWith('ERROR:'));
if(await page.locator('#download').isVisible())throw Error('Downloaded for bad input');console.log('PASS browser refuses unsupported firmware');
}finally{await browser.close();fs.rmSync(tmp,{recursive:true,force:true});}
})().catch(e=>{console.error(e);process.exitCode=1;});

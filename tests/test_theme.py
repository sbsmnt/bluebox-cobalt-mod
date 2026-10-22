"""Theme allowlist, native widget paths and automatic restoration."""
import hashlib, importlib.util, json, pathlib, struct, subprocess, sys, tempfile
import firmware_harness as H
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('p',ROOT/'patch_bluebox.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
stock=pathlib.Path(sys.argv[1]).read_bytes();base=p.transform(stock);fields=p.MANIFEST['theme']['fields'];count=0
def check(ok,msg):
 global count
 assert ok,msg
 count+=1;print('PASS',msg)
def refuse(fn,msg):
 try:fn()
 except ValueError:check(True,msg)
 else:raise AssertionError(msg)
slots={f['key']:f['slots'] for f in fields}
core={k:v for k,v in slots.items() if not k.startswith('slot')}
check(core==dict(accent=[4,32],background=[2],text=[3],button_idle=[1],button_active=[5],button_pressed=[31],button_border=[10],control_active=[27],knob=[],compressor=[]),'core theme roles remain the independently traced shared roles')
extended=[f for f in fields if f['key'].startswith('slot')]
check(all(f['slots']==[int(f['key'][4:])] and len(f['offsets'])==2 for f in extended),'palette entries are one slot each, palette plus CLUT')
check({f['slots'][0] for f in extended}=={6,7,8,11,13,15,16,20,23,24,25,30},'twelve hardware-mapped palette entries are offered')
legacy=p.MANIFEST['theme']['legacy_keys']
check(sorted([s for f in fields for s in f['slots']]+[int(k[4:]) for k in legacy])==list(range(1,34)),'offered fields plus stock-only legacy entries cover palette entries 1..33 once')
comp=[f for f in fields if f['key']=='compressor'][0]
# alpha.6: the literal's address follows the rebuilt M4 code; it is found by value.
cave=base[0x78000:0x80000];hits=[i for i in range(0,len(cave)-3,4) if cave[i:i+4]==bytes.fromhex('e0f77900')]
check(comp.get('alpha')==0 and len(hits)==1 and comp['offsets']==[0x78000+hits[0]],'compressor colour is the single compiled 0x00RRGGBB literal in the M4 cave')
fac=p.MANIFEST['theme']['presets']['Factory default']
check(fac['accent']=='#c24e9b' and all(fac[f['key']]==f['default'] for f in fields if f['key']!='accent'),'Factory default is Default with accent #c24e9b')
check('probes' not in p.MANIFEST['theme'] and not list((ROOT/'themes').glob('slot-finder-*.json')),'no slot finder themes are shipped')
allowed={o+i for f in fields for o in f['offsets'] for i in range(3)}
def selected_background(u,selected):
 row=0x24010000;ctx=0x24020000;clip=0x24020100;surface=0x24020200
 u.mem_write(row+4,struct.pack('<4i',0,40,320,28));u.mem_write(row+0x322,bytes([selected]));u.mem_write(row+0x32f,b'\x02')
 u.mem_write(ctx,b'\0\1\0\0'+struct.pack('<I',surface));u.mem_write(clip,struct.pack('<4i',0,0,320,240));u.mem_write(surface,struct.pack('<IHHBBH',0xc0180000,320,240,4,1,2))
 seen=[];a=H.stub(u,0x8101408,lambda u:seen.append(u.reg_read(H.UC_ARM_REG_R1)));b=H.stub(u,0x813a1f8,lambda u:None)
 H.call(u,0x813d3a8,row,ctx,clip);u.hook_del(a);u.hook_del(b);return seen
def button_colors(u,state,border=False):
 w=0x24040000;ctx=0x24050000;surface=ctx+0x100
 H.call(u,0x8138154,w)
 u.mem_write(w+4,struct.pack('<4i',20,40,80,28));u.mem_write(ctx,b'\1\1\0\0'+struct.pack('<I',surface));u.mem_write(surface,struct.pack('<IHHBBH',0xc0180000,320,240,1,1,4))
 if state=='active':u.mem_write(w+0x96,b'\1')
 if state=='pressed':H.w32(u,w+0x98,1)
 if border:u.mem_write(w+0x9f,b'\1')
 seen=[];a=H.stub(u,0x8101408,lambda u:seen.append(u.reg_read(H.UC_ARM_REG_R1)));b=H.stub(u,0x813a1f8,lambda u:None)
 # Outlines use the native per-pixel path rather than the filled-rectangle
 # hardware boundary. Observe its actual getter without stubbing it.
 c=u.hook_add(H.UC_HOOK_CODE,lambda u,a,n,d:seen.append(H.u32(u,0x30000000+4*u.reg_read(H.UC_ARM_REG_R1))),begin=0x8101064,end=0x8101064)
 u.ctl_flush_tb() # Refresh translated blocks after changing scoped hook callbacks.
 H.call(u,0x8137fbc,w,ctx);u.hook_del(a);u.hook_del(b);u.hook_del(c);return seen
def luminance(value):
 rgb=[int(value[i:i+2],16)/255 for i in [1,3,5]]
 rgb=[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb]
 return sum(v*w for v,w in zip(rgb,[.2126,.7152,.0722]))
for name in [n for n in p.MANIFEST['theme']['presets'] if n not in ('Default','Factory default')]:
 theme=p.MANIFEST['theme']['presets'][name]
 check(json.loads((ROOT/'themes'/(name.lower().replace(' ','-')+'.json')).read_text())==dict(schema=1,colors=theme),'new preset JSON matches generated page '+name)
 def ratio(a,b):x,y=sorted([luminance(a),luminance(b)]);return (y+.05)/(x+.05)
 for fg,bg,minimum in [('text','background',4.5),('text','button_idle',4.5),('text','accent',3),('text','slot23',3),('text','slot06',3),('slot15','button_idle',4.5),('slot15','button_active',3),('slot15','button_pressed',3),('slot15','slot11',3),('slot15','slot23',3),('slot30','accent',3),('slot13','button_active',3),('slot07','slot08',3),('slot24','slot08',3),('slot15','slot08',3)]:
  check(ratio(theme[fg],theme[bg])>=minimum,'preset contrast '+name+' '+fg+'/'+bg+' at least '+str(minimum))
 check(not set(theme)&set(legacy),'preset carries no stock-only legacy entries '+name)
 check(ratio(theme['compressor'],'#000000')>=4.5,'compressor bar readable on black header '+name)
for name,theme in p.MANIFEST['theme']['presets'].items():
 custom=p.transform(stock,theme=theme)
 check(all(i in allowed for i,(a,b) in enumerate(zip(base,custom)) if a!=b),'complete preset changes only allowed RGB channels: '+name)
 check(p.transform(custom,True)==stock and hashlib.sha256(p.normalize_theme(custom)).hexdigest()==p.MANIFEST['patched_sha256'],'complete preset restores and verifies whole image: '+name)
 u=H.machine(custom);native=H.machine(stock)
 for q in [u,native]:H.call(q,0x81004d4,0x814d230,0x30000000,0x300000ec)
 wanted={slot:0xff000000|int(theme[f['key']][1:],16) for f in fields for slot in f['slots']}
 check(all(H.call(u,0x8101064,0,i)==(wanted[i] if i in wanted else H.call(native,0x8101064,0,i)) for i in range(34)),'actual startup and getter preserve every unexposed palette entry: '+name)
 clut=p.MANIFEST['symbols']['4']['theme_knob_palette']
 check(all(H.u32(u,clut+4*i)==H.call(u,0x8101064,0,i) for i in range(34)),'conversion CLUT mirrors all original colors and chosen roles '+name)
 check(H.u32(u,clut+34*4)==(0xff000000|int(theme['knob'][1:],16)),'dedicated indexed knob entry matches picker '+name)
 check(selected_background(u,0)==[wanted[2]] and selected_background(u,1)==[wanted[4]],'native row renderer uses themed background and selection: '+name)
 for state,slot in [('idle',1),('active',5),('pressed',31)]:check(wanted[slot] in button_colors(u,state),'native '+state+' button fill uses traced role: '+name)
 check(wanted[10] in button_colors(u,'active',True),'native outlined button uses themed border: '+name)
 # Native constructor binds label colour index3; native getter supplies RGB.
 H.call(u,0x8138154,0x24040000)
 check(bytes(u.mem_read(0x24040000+0x87,1))==b'\3' and H.call(u,0x8101064,0,3)==wanted[3],'native primary label uses themed text: '+name)
for f in fields:
 custom=p.transform(stock,theme={f['key']:'#123456'})
 check(all(custom[o:o+4]==bytes([0x56,0x34,0x12,f.get('alpha',255)]) for o in f['offsets']),'each role independently accepts exact colour word: '+f['key'])
 check(all(i in {o+j for o in f['offsets'] for j in range(3)} for i,(a,b) in enumerate(zip(base,custom)) if a!=b),'each role has an isolated delta: '+f['key'])
 bad=bytearray(custom);bad[f['offsets'][0]+3]^=1
 refuse(lambda:p.transform(bad,True),'invalid alpha rejected for '+f['key'])
for bad in [[],42,{'unknown':'#123456'},{'text':'#fff'},{'button_border':None}]:refuse(lambda:p.transform(stock,theme=bad),'invalid theme rejected: '+repr(bad))
refuse(lambda:p.transform(base,True,theme={}), 'restore refuses theme overrides')
for offset in [0,0x78000,0x10d248,0x10d2b8]:
 bad=bytearray(p.transform(stock,theme=p.MANIFEST['theme']['presets']['Ocean']));bad[offset]^=1
 refuse(lambda:p.transform(bad,True),'non-theme code/status/palette corruption rejected at '+hex(offset))
with tempfile.TemporaryDirectory(prefix='bluebox-theme-') as td:
 theme=pathlib.Path(td)/'theme.json';out=pathlib.Path(td)/'custom.bin'
 theme.write_text(json.dumps(dict(schema=1,colors=p.MANIFEST['theme']['presets']['Ocean'])))
 args=[sys.executable,str(ROOT/'patch_bluebox.py'),str(pathlib.Path(sys.argv[1]).resolve())]
 q=subprocess.run(args+['--theme',str(theme),'--output',str(out)],capture_output=True)
 check(q.returncode==0 and out.read_bytes()==p.transform(stock,theme=p.MANIFEST['theme']['presets']['Ocean']),'CLI applies exported theme without compilation')
 q=subprocess.run(args+['--theme',str(theme),'--accent','#abcdef','--check'],capture_output=True)
 expected=p.transform(stock,theme=p.MANIFEST['theme']['presets']['Ocean'],accent='#abcdef')
 check(q.returncode==0 and hashlib.sha256(expected).hexdigest().encode() in q.stdout,'explicit CLI accent overrides imported accent')
 theme.write_text('{"schema":2,"colors":{}}');q=subprocess.run(args+['--theme',str(theme),'--check'],capture_output=True)
 check(q.returncode!=0,'CLI refuses unsupported theme schema')
old=dict(p.MANIFEST['theme']['presets']['Ocean'],**{k:'#123456' for k in legacy})
check(p.transform(stock,theme=old)==p.transform(stock,theme=p.MANIFEST['theme']['presets']['Ocean']),'older theme files with legacy entries patch as if those entries were absent')
print(str(count)+' theme checks passed; physical shared-role coverage remains a hardware test.')

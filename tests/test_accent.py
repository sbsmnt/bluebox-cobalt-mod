"""Palette-only custom firmware variants retain full normalized image guards."""
import hashlib, importlib.util, pathlib, subprocess, sys, tempfile, struct
import firmware_harness as H
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('patcher',ROOT/'patch_bluebox.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
stock=pathlib.Path(sys.argv[1]).read_bytes();base=p.transform(stock);count=0
def check(condition,message):
 global count
 assert condition,message
 count+=1;print('PASS',message)
def refused(fn,message):
 try:fn()
 except ValueError:check(True,message)
 else:raise AssertionError(message)
offsets=[0x10d240,0x10d2b0]+[p.MANIFEST['symbols']['4']['theme_knob_palette']-0x08040000+4*i for i in [4,32]]
check(p.MANIFEST['accent']['offsets']==offsets,'customization is restricted to the two traced accents and their conversion-CLUT mirrors')
allowed={i for o in offsets for i in range(o,o+3)}
for color in ['#9455fa','#6b03fc','#ff6af3','#000000','#ffffff','#12Ab90']:
 custom=p.transform(stock,accent=color);word=(0xff000000|int(color[1:],16)).to_bytes(4,'little')
 check(all(custom[o:o+4]==word for o in offsets),'exact opaque ARGB entries for '+color)
 check(all(i in allowed for i,(a,b) in enumerate(zip(base,custom)) if a!=b),'only the accent RGB fields and mirrors change for '+color)
 check(hashlib.sha256(p.normalize_accent(custom)).hexdigest()==p.MANIFEST['patched_sha256'],'complete normalized image hash verified for '+color)
 check(p.transform(custom,True)==stock,'exact original restored automatically from '+color)
 refused(lambda:p.transform(custom),'custom output refused as patch input '+color)
# Execute actual startup palette copy/getter and selected-row rendering for
# custom variants. Only final hardware fill and generic children are boundaries.
def row_colors(u,selected):
 row=0x24010000;context=0x24020000;clip=0x24020100;surface=0x24020200
 u.mem_write(row+4,struct.pack('<4i',0,40,320,28));u.mem_write(row+0x322,bytes([selected]));u.mem_write(row+0x32f,b'\x02')
 u.mem_write(context,b'\0\1\0\0'+struct.pack('<I',surface));u.mem_write(clip,struct.pack('<4i',0,0,320,240))
 u.mem_write(surface,struct.pack('<IHHBBH',0xc0180000,320,240,4,1,2))
 seen=[]
 hook=H.stub(u,0x8101408,lambda u:seen.append(u.reg_read(H.UC_ARM_REG_R1)))
 children=H.stub(u,0x813a1f8,lambda u:None)
 H.call(u,0x813d3a8,row,context,clip)
 u.hook_del(hook);u.hook_del(children)
 return seen
for color in ['#9455fa','#6b03fc','#ff6af3','#000000','#ffffff','#12Ab90']:
 u=H.machine(p.transform(stock,accent=color));native=H.machine(stock)
 for q in [u,native]:H.call(q,0x81004d4,0x814d230,0x30000000,0x300000ec)
 want=0xff000000|int(color[1:],16)
 check(all(H.call(u,0x8101064,0,i)==(want if i in [4,32] else H.call(native,0x8101064,0,i)) for i in range(34)),'native palette getter uses chosen accent and preserves all other colours '+color)
 check(row_colors(u,1)==[want],'native selected-row path renders chosen accent '+color)
 check(row_colors(u,0)==[0xff141414],'native unselected background preserved '+color)
for color in ['9455fa','#fff','#1234567','#zzzzzz','#12345g','#123456\n',42]:
 refused(lambda:p.transform(stock,accent=color),'malformed accent refused: '+repr(color))
custom=bytearray(p.transform(stock,accent='#123456'))
for offset in [0,0x78000,offsets[0]-1,0x10d248]:
 corrupt=custom.copy();corrupt[offset]^=1
 refused(lambda:p.transform(corrupt,True),'restoration rejects non-palette corruption at '+hex(offset))
for offset in offsets:
 corrupt=custom.copy();corrupt[offset+3]=0
 refused(lambda:p.transform(corrupt,True),'invalid palette alpha rejected at '+hex(offset))
 corrupt=custom.copy();corrupt[offset]^=1
 refused(lambda:p.transform(corrupt,True),'mismatched palette RGB rejected at '+hex(offset))
refused(lambda:p.transform(custom[:-1],True),'truncated custom firmware rejected')
refused(lambda:p.transform(custom,True,accent='#123456'),'accent option rejected during restoration')
with tempfile.TemporaryDirectory(prefix='bluebox-accent-') as td:
 out=pathlib.Path(td)/'custom.bin';restored=pathlib.Path(td)/'restored.bin'
 args=[sys.executable,str(ROOT/'patch_bluebox.py'),str(pathlib.Path(sys.argv[1]).resolve())]
 q=subprocess.run(args+['--accent','#123456','--output',str(out)],capture_output=True)
 check(q.returncode==0 and out.read_bytes()==custom,'CLI writes the selected variant without compilation')
 q=subprocess.run([sys.executable,str(ROOT/'patch_bluebox.py'),str(out),'--restore','--output',str(restored)],capture_output=True)
 check(q.returncode==0 and restored.read_bytes()==stock,'CLI restores custom accent without knowing its selected colour')
 q=subprocess.run(args+['--accent','#ffffff','--output',str(out)],capture_output=True)
 check(q.returncode!=0 and out.read_bytes()==custom,'custom CLI output cannot overwrite an existing file')
 q=subprocess.run(args+['--accent','#ff','--output',str(pathlib.Path(td)/'bad.bin')],capture_output=True)
 check(q.returncode!=0 and not (pathlib.Path(td)/'bad.bin').exists(),'invalid CLI accent writes no output')
print(str(count)+' accent-customization checks passed.')

"""Desktop compressor telemetry and UI integration under Unicorn.
Real stock compressor, constructor, queues and buffers are executed. An
unsupported Unicorn float32->float64 comparison is modeled in the harness.
This model changes no firmware bytes and is also used for the stock baseline.
"""
import importlib.util,pathlib,sys,struct,math,hashlib
from unicorn import *
from unicorn.arm_const import *
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('patcher',ROOT/'patch_bluebox.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
stock=pathlib.Path(sys.argv[1]).read_bytes();patched=p.transform(stock);M=p.MANIFEST
RET=0x080dfff0;NEXT_FN=0x080dff00;checks=0

def check(cond,msg):
 global checks
 if not cond:raise AssertionError(msg)
 checks+=1;print('PASS',msg)
def u32(u,a):return struct.unpack('<I',u.mem_read(a,4))[0]
def w32(u,a,v):u.mem_write(a,struct.pack('<I',v&0xffffffff))
def f32(u,a):return struct.unpack('<f',u.mem_read(a,4))[0]
def machine(image=patched):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M7)
 for a,n in [(0x08000000,0x200000),(0x20000000,0x20000),(0x24000000,0x80000),(0x30000000,0x48000),(0x38000000,0x10000),(0x38800000,0x1000),(0x58024000,0x1000),(0xe000e000,0x1000),(0xc0000000,0x2000000)]:u.mem_map(a,n)
 u.mem_write(0x8040000,image);u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);return u
def call(u,a,*args,end=RET):
 for r,v in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):u.reg_write(r,v)
 u.reg_write(UC_ARM_REG_SP,0x2407f000);u.reg_write(UC_ARM_REG_LR,RET|1)
 u.emu_start(a|1,end,count=3000000)
 if u.reg_read(UC_ARM_REG_PC)!=end:raise AssertionError('Function timeout: '+hex(a))
 return u.reg_read(UC_ARM_REG_R0)
def stub(u,a,fn):
 def h(u,addr,size,data):fn(u);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
 return u.hook_add(UC_HOOK_CODE,h,begin=a,end=a)
OBJ=0xc0000000;CTX=0x30000000;BUF=0x24020000;QUEUE=CTX+12*0x604+4
NEXT=0x24030000;VT=NEXT+0x100

def setup(image=patched,chain=True):
 u=machine(image);heap=[0xc0010000];nexts=[]
 def alloc(u):
  size=u.reg_read(UC_ARM_REG_R0);a=heap[0];heap[0]=(a+size+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x80412a4,alloc)
 def double_compare(u,a,n,_):
  value=struct.unpack('<f',struct.pack('<I',u.reg_read(UC_ARM_REG_S14)))[0]
  threshold=struct.unpack('<d',struct.pack('<Q',u.reg_read(UC_ARM_REG_D5)))[0]
  flags=3 if math.isnan(value) or math.isnan(threshold) else 2 if value>threshold else 6 if value==threshold else 8
  u.reg_write(UC_ARM_REG_FPSCR,(u.reg_read(UC_ARM_REG_FPSCR)&0x0fffffff)|(flags<<28));u.reg_write(UC_ARM_REG_PC,0x8042825)
 u.hook_add(UC_HOOK_CODE,double_compare,begin=0x804281c,end=0x804281c)
 call(u,0x8042364,OBJ,48000,32)
 w32(u,OBJ+0x18,12);u.mem_write(OBJ+0x1e,struct.pack('<H',12))
 if chain:
  w32(u,OBJ+8,NEXT);w32(u,NEXT,VT);w32(u,VT+12,NEXT_FN|1)
  def nextnode(u):
   nexts.append((u.reg_read(UC_ARM_REG_R0),u.reg_read(UC_ARM_REG_R1),u32(u,OBJ+8)));u.reg_write(UC_ARM_REG_R0,7)
  stub(u,NEXT_FN,nextnode)
 return u,nexts

def events(u,evs):
 for i,(pid,val) in enumerate(evs):u.mem_write(QUEUE+i*24,struct.pack('<B7xIHHiI',0x39,12,pid,0,val,0))
 w32(u,QUEUE+0x600,len(evs))
def block(u,l,r,evs=(),process=None):
 events(u,evs);u.mem_write(CTX+0x150e4+12*20,struct.pack('<IIIIBBxx',32,32,BUF,BUF+0x100,0,1))
 u.mem_write(BUF,struct.pack('<32f',*l));u.mem_write(BUF+0x100,struct.pack('<32f',*r))
 fn=process if process is not None else u32(u,u32(u,OBJ)+12)
 ret=call(u,fn,OBJ,CTX)
 return bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128)),ret

def signal(amp,b,frequency=1000):return [amp*math.sin(2*math.pi*frequency*(b*32+i)/48000) for i in range(32)]
def report(u):return struct.unpack('<IHBBI',u.mem_read(0x38800f40,12))
check(u32(machine(),0x806a24c)==M['symbols']['gr7']['gr_process']|1,'desktop compressor vtable points to new telemetry wrapper')
check(hashlib.sha256((ROOT/'build'/'cm7.bin').read_bytes()).hexdigest()=='04d2cd048283d02db2bb90d6f29c6b729a38fa8e3032dc6a5e5dd6dcef686579','hardware-tested CPU M7 code is byte-identical')
# Exercise changing compressor parameters, lookahead, stereo levels, bypass,
# silence and chain returns while comparing against the original firmware.
for label,initial,ampL,ampR in [
 ('bypass',[(0xb5,0)],0.7,0.2),
 ('active/no makeup',[(0xb5,1),(0x135,0)],0.7,0.2),
 ('active/auto makeup',[(0xb5,1),(0x135,1)],0.7,0.05),
 ('right channel silent',[(0xb5,1),(0x135,0)],0.7,0),
 ('quiet input',[(0xb5,1),(0x135,0)],0.00001,0.00001),
 ('silence',[(0xb5,1)],0,0)]:
 a,an=setup(stock);b,bn=setup();same=True
 for n in range(160):
  evs=initial if n==0 else [(0x132,-40000)] if n==60 else [(0xb5,0)] if n==100 else [(0xb5,1),(0x132,-20000)] if n==110 else []
  l,r=signal(ampL,n),signal(ampR,n,700)
  oa,ra=block(a,l,r,evs);ob,rb=block(b,l,r,evs)
  same=same and oa==ob and ra==rb==7
  same=same and a.mem_read(OBJ,0x130)==b.mem_read(OBJ,0x130) and a.mem_read(0xc0010000,0x2020)==b.mem_read(0xc0010000,0x2020)
 check(same,label+': 160 blocks of PCM and compressor state exactly match stock')
 check(an==bn and len(bn)==160 and all(x==(NEXT,CTX,NEXT) for x in bn),label+': downstream node runs once, linkage/return preserved')
# Stationary signal, predictable reduction and makeup-independent measurement.
for auto in [0,1]:
 u,_=setup()
 for n in range(400):block(u,signal(0.3,n),signal(0.1,n),[(0xb5,1),(0x132,-40000),(0x135,auto)] if n==0 else [])
 magic,gr,on,reserved,seq=report(u)
 actual=(f32(u,OBJ+0xfc)-min(f32(u,OBJ+0xc0),f32(u,OBJ+0xd8)))*6.0206
 print('  steady: auto=',auto,'meter=',gr/10,'state=',actual)
 check(magic==0x44475232 and on==1 and seq>=12,'real compressor publishes enabled meter reports')
 check(abs(gr/10-actual)<1.0 and gr>100,'meter tracks the stronger channel/applied gain with makeup '+str(auto))
 # bypass update must not retain old reduction until a full 32-block window.
 block(u,signal(.3,401),signal(.1,401),[(0xb5,0)])
 check(report(u)[1]==0 and report(u)[2]==0,'bypass immediately clears published reduction')
# Quiet/silent long-run detector should settle without persistent fake reduction.
for amp in [0,0.00001]:
 u,_=setup(chain=False)
 for n in range(500):_,ret=block(u,signal(amp,n),signal(amp,n),[(0xb5,1),(0x135,0)] if n==0 else [])
 print('  quiet',amp,report(u),'gain',f32(u,OBJ+0xc0),'env',f32(u,OBJ+0xbc))
 check(report(u)[1]<=5 and ret==1,'quiet/silent settled reduction is zero; no chain returns stock 1 (amp '+str(amp)+')')
# Function-level telemetry rejects NaN, clamps extremes and has no state writes
# into the compressor object when called after a minimal stock-return stub.
for left,right,makeup,on,want in [(0,0,0,1,0),(-1,-2,0,1,120),(-1,-2,1,1,181),(float('nan'),float('nan'),0,1,0),(-100,-100,0,1,1200),(-10,-10,0,0,0)]:
 u=machine();stub(u,0x8042648,lambda u:u.reg_write(UC_ARM_REG_R0,42))
 u.mem_write(OBJ+0xbc,struct.pack('<f',1.0));u.mem_write(OBJ+0xd4,struct.pack('<f',1.0));u.mem_write(OBJ+0xc0,struct.pack('<f',left));u.mem_write(OBJ+0xd8,struct.pack('<f',right));u.mem_write(OBJ+0xfc,struct.pack('<f',makeup));u.mem_write(OBJ+0x28,bytes([not on]));before=bytes(u.mem_read(OBJ,0x130))
 for _ in range(32):ret=call(u,M['symbols']['gr7']['gr_process'],OBJ,CTX)
 check(report(u)[1]==want and report(u)[2]==on and ret==42 and bytes(u.mem_read(OBJ,0x130))==before,'telemetry units/edge case '+str((left,right,makeup,on)))
# UI tests execute actual stock presenter, buffer getter and flip. DMA/hardware
# are stubbed as for v0.1.0. Settings rows use the mapped desktop layout.
APP=0x30010000;PAGE=APP+0x1e978;LIST=PAGE+0x9c8;DISP=0x30001d80;SURF=0x24030000;FB=0xc1000000
THR=LIST+0xe8+27*0x330

def ui(gr=100,on=1,visible=True,hidden=False,y=40,kind=12,fmt=4,bpp=2):
 u=machine();w32(u,0x3000d9fc,APP);w32(u,APP+0x2b4,PAGE if visible else APP+0x1000)
 u.mem_write(PAGE+0xa3e0,struct.pack('<H',kind));u.mem_write(LIST+4,struct.pack('<iiii',0,0,320,180));w32(u,LIST+0x99e8,38)
 for k in range(38):u.mem_write(LIST+0xe8+k*0x330+0x32c,struct.pack('<H',0x100+k))
 u.mem_write(THR+0x32c,struct.pack('<H',0x132));u.mem_write(THR+4,struct.pack('<iiii',0,y,320,28));u.mem_write(THR+0x30,bytes([hidden]))
 u.mem_write(0x38800f40,struct.pack('<IHBBI',0x44475232,gr,on,0,1));u.mem_write(0x38800f00,struct.pack('<IHHI',0x44504355,500,800,1))
 for i,off in enumerate([0x120,0x12c,0x138,0x144]):u.mem_write(DISP+off,struct.pack('<IHHBBH',FB+i*0x50000,320,240,fmt,0,bpp))
 u.mem_write(SURF,struct.pack('<IHHBBH',FB,320,240,fmt,0,bpp))
 stub(u,0x8101660,lambda u:None);stub(u,0x8112b9a,lambda u:None)
 return u

def render(u):call(u,0x8137458,SURF,1)
def px(u,x,y):return struct.unpack('<H',u.mem_read(FB+(y*320+x)*2,2))[0]
for gr,want in [(0,0),(20,71),(100,160),(250,252),(400,320),(1200,320)]:
 u=ui(gr);render(u);row=[px(u,x,199) for x in range(320)]
 check(row.count(0x7fbc)==want,'threshold bar correctly displays '+str(gr/10)+' dB')
 check(px(u,315,16)==0x064a,'CPU meter remains visible alongside compressor meter')
 data=u.mem_read(FB,320*240*2);changed=[i//2 for i,v in enumerate(data) if v]
 check(all((314<=i%320<=316 and 3<=i//320<=16) or (308<=i%320<=310 and 3<=i//320<=16) or (126<=i%320<=165 and 3<=i//320<=16) or 197<=i//320<=199 for i in changed),'writes confined to CPU bar/readout, compact GR bar and threshold underline')
for kwargs in [dict(visible=False),dict(hidden=True),dict(y=-1),dict(y=160),dict(kind=11),dict(on=0)]:
 u=ui(**kwargs);render(u)
 check(bytes(u.mem_read(FB+20*320*2,220*320*2))==bytes(220*320*2),'no compressor drawing for '+str(kwargs))
# Both pixels-per-format and row geometry bounds are checked before drawing.
for fmt,bpp in [(1,4),(2,3),(3,2)]:
 u=ui(fmt=fmt,bpp=bpp);render(u)
 check(bytes(u.mem_read(FB+(199*320)*bpp,bpp))!=bytes(bpp),'compressor bar draws in pixel format '+str(fmt))
# Use actual stock dirty check; stub only its page/widget polling subcalls.
FLAGS=0x24001000
u=ui();polls=[];stub(u,0x81397b8,lambda u:polls.append(1));PAGEVT=0x24001100;POLL_FN=0x080dfe00
w32(u,PAGE,PAGEVT);w32(u,PAGEVT,POLL_FN|1);stub(u,POLL_FN,lambda u:None)
def tick(seq,gr,on=1):
 u.mem_write(0x38800f40,struct.pack('<IHBBI',0x44475232,gr,on,0,seq));u.mem_write(FLAGS,bytes(2))
 call(u,0x813408e,APP+0x280,FLAGS,end=0x8134092)
 return bytes(u.mem_read(FLAGS,2))
# Static check of the actual loop call plus execution through the new wrapper.
check(patched[0xf408e:0xf4092]!=stock[0xf408e:0xf4092],'UI loop redraw-check call is patched')
forced=[tick(s,50+s) for s in range(1,31) for _ in range(4)]
check(9<=forced.count(b'\1\1')<=11 and len(polls)==120,'moving reduction redraws at most once per third report; stock poll still runs')
check(bytes(u.mem_read(APP+0x2b8,2))==bytes(2),'actual stock dirty checker clears force flags')
steady=[tick(s,80) for s in range(31,61) for _ in range(4)]
check(steady.count(b'\1\1')<=1,'steady reduction does not force continuous redraw')
check(tick(61,0,0)==b'\1\1','bypass transition redraws once to erase old meter')
check(tick(61,0,0)==bytes(2),'bypassed steady state forces no redraw')
w32(u,APP+0x1000,PAGEVT);w32(u,APP+0x2b4,APP+0x1000)
check(tick(62,100)==b'\1\1','compact bar refreshes on another page when enabled')
check(all(tick(s,100)==bytes(2) for s in range(63,70)),'steady compact bar on other pages forces no redraw')
w32(u,APP+0x2b4,PAGE);u.mem_write(THR+0x30,b'\1')
check(tick(72,400)==b'\1\1','compact bar refreshes while threshold row is hidden')
check(all(tick(s,400)==bytes(2) for s in range(73,80)),'steady compact bar with hidden threshold forces no redraw')
u.mem_write(THR+0x30,b'\0')
check(tick(80,80)==b'\1\1','returning to visible threshold row refreshes its meter')
print(str(checks)+' compressor integration checks passed. Hardware behaviour remains untested for v0.4.0.')
# Instruction-count comparison is a relative emulator estimate, not cycle timing.
a,_=setup(stock);b,_=setup()
for n in range(63):
 l,r=signal(.3,n),signal(.1,n)
 evs=[(0xb5,1),(0x135,0)] if n==0 else []
 block(a,l,r,evs);block(b,l,r,evs)
for n,label in [(63,'publishing block'),(64,'ordinary block')]:
 counts=[]
 for u in [a,b]:
  steps=[0]
  def instruction(u,addr,size,data):steps[0]+=1
  handle=u.hook_add(UC_HOOK_CODE,instruction)
  block(u,signal(.3,n),signal(.1,n));u.hook_del(handle);counts.append(steps[0])
 overhead=counts[1]-counts[0]
 print('  '+label+' extra instructions:',overhead)
 check(0<overhead<250,label+': telemetry overhead below 250 emulated instructions')
print(str(checks)+' compressor integration checks passed including instruction-overhead checks.')
# v0.4.0: common top-bar reference meter, preserved row meter and real palette.
ui_before=checks
for gr,mini_fill,row_fill in [(0,0,0),(20,3,71),(100,7,160),(250,11,252),(400,14,320),(1200,14,320)]:
 u=ui(gr=gr);render(u)
 column=[px(u,309,y) for y in range(3,17)]
 check(column==[0x7fbc]*mini_fill+[0x3186]*(14-mini_fill),'compact meter fill for '+str(gr/10)+' dB')
 check(sum(px(u,x,199)==0x7fbc for x in range(320))==row_fill,'large Thresh meter preserved at same reading')
 check(all(px(u,x,10)==0 for x in [307,311,312,313,317]),'clear spacing around both top-bar meters')
for state in [dict(visible=False),dict(kind=11),dict(kind=0),dict(hidden=True),dict(y=-1),dict(y=160)]:
 u=ui(gr=100,**state);render(u)
 check(sum(px(u,309,y)==0x7fbc for y in range(3,17))==7,'compact reference remains visible for '+str(state))
 check(bytes(u.mem_read(FB+20*320*2,220*320*2))==bytes(220*320*2),'Thresh overlay remains absent when not fully visible')
u=ui(gr=100,visible=False,on=0);render(u)
check(all(px(u,309,y)==0x3186 for y in range(3,17)),'bypassed compact meter remains as a dark reference rail')
u=ui(gr=100,visible=False);w32(u,0x38800f00,0);render(u)
check(sum(px(u,309,y)==0x7fbc for y in range(3,17))==7 and all(px(u,315,y)==0 for y in range(3,17)),'compact GR meter is independent of CPU report availability')
u=ui(gr=100);w32(u,0x38800f40,0);render(u)
check(all(px(u,309,y)==0 for y in range(3,17)) and px(u,315,16)==0x064a,'no valid GR report: CPU still works without false compressor data')
for fmt,bpp in [(1,4),(2,3),(3,2),(4,2)]:
 u=ui(gr=100,visible=False,fmt=fmt,bpp=bpp);render(u)
 on_pixels=[bytes(u.mem_read(FB+(y*320+309)*bpp,bpp)) for y in range(3,10)]
 expected={1:bytes.fromhex('e0f779ff'),2:bytes.fromhex('e0f779'),3:bytes.fromhex('dcbf'),4:bytes.fromhex('bc7f')}[fmt]
 check(on_pixels==[expected]*7,'compact mint bar pixel encoding, format '+str(fmt))
# Startup initializes .data with this exact copy routine, and the color getter
# loads the requested indexed ARGB word from SRAM. Neither routine is stubbed.
def load_palette(u):call(u,0x081004d4,0x0814d230,0x30000000,0x300000ec)
a=machine(stock);b=machine();load_palette(a);load_palette(b)
for index in range(34):
 old=call(a,0x08101064,DISP,index);new=call(b,0x08101064,DISP,index)
 check(new==(0xff9455fa if index in [4,32] else old),'actual stock palette getter: '+str(index)+(' is requested purple' if index in [4,32] else ' unchanged'))
check([i for i in range(0xec) if a.mem_read(0x30000000+i,1)!=b.mem_read(0x30000000+i,1)]==[16,17,18,128,129,130],'startup .data differs only in two accent RGB triplets; alpha and pointers preserved')
# Execute the actual selected-row renderer and stock rectangle/palette path.
# Only final display-hardware fill and generic child iteration are stubbed.
def row_color(image,selected):
 u=machine(image);load_palette(u)
 row=0x24010000;context=0x24020000;clip=0x24020100;surface=0x24020200
 u.mem_write(row+4,struct.pack('<iiii',0,40,320,28));u.mem_write(row+0x322,bytes([selected]));u.mem_write(row+0x32f,b'\x02')
 u.mem_write(context,b'\0\1\0\0'+struct.pack('<I',surface));u.mem_write(clip,struct.pack('<iiii',0,0,320,240))
 u.mem_write(surface,struct.pack('<IHHBBH',FB,320,240,4,1,2))
 seen=[]
 stub(u,0x08101408,lambda u:seen.append(u.reg_read(UC_ARM_REG_R1)))
 stub(u,0x0813a1f8,lambda u:None)
 call(u,0x0813d3a8,row,context,clip)
 return seen
check(row_color(stock,1)==[0xffff6af3] and row_color(patched,1)==[0xff9455fa],'real selected-row render path uses purple instead of pink')
check(row_color(stock,0)==row_color(patched,0)==[0xff141414],'real unselected-row background is unchanged')
check(patched[0xf9790:0xf9792]==bytes.fromhex('8023'),'clock label narrowed to 128 pixels for two separate indicators')
# The CPU audio payload is preserved; saturation extends the GR wrapper.
# Audio wrapper now includes optional saturation; zero-drive equivalence is tested above.
print(str(checks-ui_before)+' new v0.4.0 overlay/palette checks passed.')
print(str(checks)+' combined compressor/UI checks passed.')

# Simulate the audio core publishing a new value during the long row draw.
u=ui(gr=100);published=[False]
def publish_during_row(u,access,address,size,value,data):
 if not published[0]:
  published[0]=True;u.mem_write(0x38800f44,struct.pack('<H',400))
u.hook_add(UC_HOOK_MEM_WRITE,publish_during_row,begin=FB+197*320*2,end=FB+200*320*2-1)
render(u)
check(published[0] and sum(px(u,x,199)==0x7fbc for x in range(320))==160 and sum(px(u,309,y)==0x7fbc for y in range(3,17))==7,'both meters share one frame snapshot despite a mid-render report update')
print(str(checks)+' combined compressor/UI checks passed, including concurrent-publication fixture.')

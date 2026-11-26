"""XY DELAY pad targets per FX1 mode, on the real native value lookup
(0x8121b50) with the real native delay parameter list: the pad's X Param /
Y Param resolve to Stasis Tone / Wandr, Flux Blur / Wandr, Spiral Shift /
Cutoff, and stock Cutoff / Width everywhere else. The FX2 pad is untouched.
Touch handling and drawing of the pad are native and not run here."""
from firmware_harness import *
import firmware_harness as H
APP=0x24040000;FX1=0x14;FX2=0x15;GMODE=0x1a0
def lookup(u,layer,pid):
 u.mem_write(0x24000100,struct.pack('<HH',layer,pid));w32(u,0x24000104,0x7fffffff)
 r=call(u,0x8121b50,APP,0x24000100,0x24000102,0x24000104)
 return u.reg_read(UC_ARM_REG_R0),struct.unpack('<i',u.mem_read(0x24000104,4))[0]
def setup(image):
 u=machine(image);heap=[0xc0010000]
 def allocate(u):
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,allocate);u.mem_write(0x24000000,b'\x04');call(u,0x81202b8,APP+0xe48,0x24000000)
 return u
def set_mode(u,mode):
 n=struct.unpack('<H',u.mem_read(APP+0xe48+10,2))[0]
 for i in range(n):
  a=0xc0010000+i*8
  if struct.unpack('<H',u.mem_read(a,2))[0]==GMODE:w32(u,a+4,mode);return
 raise AssertionError('no FX1 mode entry')
u=setup(patched);s=setup(stock)
EXPECT={5:(0x0e,0xcb,'Delay'),0:(0x0e,0xcb,'Warp'),1:(0x0e,0xcb,'Granulr'),2:(0x0e,0xcb,'Tape'),3:(0x0e,0xcb,'Ducked'),4:(0x0e,0xcb,'Reverse'),
 6:(0x19f,0x19e,'Stasis'),7:(0x1ec,0x1ed,'Flux'),8:(0x1df,0x0e,'Spiral')}
for mode,(x,y,name) in EXPECT.items():
 set_mode(u,mode)
 got=(lookup(u,FX1,0x179),lookup(u,FX1,0x17a))
 check(got==((1,x),(1,y)),'%s: XY DELAY pad X -> %#x, Y -> %#x (got %s)'%(name,x,y,got))
check((lookup(s,FX1,0x179),lookup(s,FX1,0x17a))==((1,0x0e),(1,0xcb)),'stock reference: FX1 pad is Cutoff / Width')
# The pad's dot reads its targets' values through the same lookup.
set_mode(u,6)
check(lookup(u,FX1,0x19f)==(1,750) and lookup(u,FX1,0x19e)==(1,150),'Stasis: the pad reads Tone (750) and Wandr (150) for its position')
set_mode(u,7)
check(lookup(u,FX1,0x1ec)==(1,500) and lookup(u,FX1,0x1ed)==(1,0),'Flux: the pad reads Blur (500) and Wandr (0)')
set_mode(u,8)
check(lookup(u,FX1,0x1df)==(1,832),'Spiral: the pad reads Shift (832, about +77 Hz)')
# FX2 (reverb) pad unchanged in every mode.
for mode in (5,6,8):
 set_mode(u,mode)
 check((lookup(u,FX2,0x179),lookup(u,FX2,0x17a))==(lookup(s,FX2,0x179),lookup(s,FX2,0x17a))==((1,0x159),(1,0x15a)),'FX2 pad unchanged with FX1 mode %d'%mode)
print(H.checks,'XY pad checks passed; touch and drawing on the device are a hardware test.')
# FX1 pad label: the page's pad refresh dispatcher (0x8133bf0, native) now
# calls xy_pad_refresh, which runs the native refresh and then sets the FX1
# label. Native refresh and label setter are recorded here, not run.
SESSION=0x300173d0;PAGE=0x24100000
def label_run(mode,layer):
 v=setup(patched)
 # the hook reads the FX1 mode from the session's FX1 list
 v.mem_write(SESSION+0xe48,bytes(v.mem_read(APP+0xe48,16)));set_mode(v,mode)
 refresh=[];label=[]
 stub(v,0x8133b48,lambda q:refresh.append((q.reg_read(UC_ARM_REG_R0),struct.unpack('<H',q.mem_read(q.reg_read(UC_ARM_REG_R1),2))[0],q.reg_read(UC_ARM_REG_R2))))
 stub(v,0x813fe4a,lambda q:label.append((q.reg_read(UC_ARM_REG_R0),bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),16)).split(b'\0')[0])))
 v.mem_write(0x24000200,struct.pack('<H',layer));call(v,0x8133bf0,PAGE,0x24000200)
 return refresh,label
for mode,name,text in [(5,'Delay',b'DELAY'),(0,'Warp',b'DELAY'),(6,'Stasis',b'SPECTRAL'),(7,'Flux',b'SPECTRAL'),(8,'Spiral',b'SPECTRAL')]:
 r,l=label_run(mode,FX1)
 check(r==[(PAGE,FX1,0)] and l==[(PAGE+0xbefc,text)],'%s: FX1 pad refreshes natively, label %s'%(name,text.decode()))
for layer in (0x15,0x16):
 r,l=label_run(6,layer)
 check(r==[(PAGE,layer,layer-0x14)] and l==[],'pad %#x refreshes natively and its label is not touched'%layer)
print(H.checks,'XY pad target and label checks passed.')

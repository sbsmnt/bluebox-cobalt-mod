"""Native enum definition, conditional display lists, and compact widget geometry."""
from firmware_harness import *
import firmware_harness as H
APP=0x300173d0;SET=APP+0xe48;LIST=0x24050000;SLOT=0x24009000
EXTRA=[(0x1a0,5,b'Mode:',0,0,b'dlymode'),(0x1a1,8,b'Position:',0,1000,b'grnposition'),(0x1a2,1,b'Size ms:',20,250,b'grnsize'),(0x1a3,1,b'Density:',1,4,b'grndensity'),(0x1a4,8,b'Jitter:',0,1000,b'grnjitter'),(0x1a5,4,b'Freeze:',0,1,b'grnfreeze'),(0x1a6,4,b'Reverse:',0,1,b'grnreverse')]
def cs(u,a):return bytes(u.mem_read(a,64)).split(b'\0')[0]
def init(image=patched):
 u=machine(image);heap=[0xc0010000]
 def allocate(u):
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,allocate);call(u,0x8134cb8,0)
 return u
old=init(stock);u=init()
for id,kind,label,lo,hi,key in EXTRA:
 a=0xc0010000+id*28
 check(bytes(old.mem_read(a,28))==bytes(28),'granular id '+hex(id)+' was undefined in stock')
 check(struct.unpack('<HB',u.mem_read(a,3))==(id,kind) and cs(u,u32(u,a+4))==label and struct.unpack('<ii',u.mem_read(a+16,8))==(lo,hi) and cs(u,u32(u,a+24))==key,'native granular definition '+hex(id)+' type/label/range/key')
a=0xc0010000+0x1a0*28;names=u32(u,a+8)
check(u32(u,a+12)==6 and [cs(u,u32(u,names+4*i)) for i in range(6)]==[b'Warp delay',b'Granular',b'Tape',b'Ducked',b'Reverse',b'Delay'],'Mode uses the native five-choice string enum, like reverb modes')
u.mem_write(0x24010000,b'\x04');call(u,0x81202b8,SET,0x24010000)
w32(u,SET,0x24054000);w32(u,0x24054000,0x80dfe21);stub(u,0x80dfe20,lambda u:u.reg_write(UC_ARM_REG_R0,4))
# Minimal real global get structure with the QUAD-visibility parameter.
w32(u,APP+0xe78+4,0x24052000);u.mem_write(APP+0xe78+10,struct.pack('<HH',1,68))
u.mem_write(0x24052000,struct.pack('<H2xi',0x18c,0))
u.mem_write(SLOT,struct.pack('<H',20))
def visible(u):
 call(u,M['symbols']['4']['dly_parameters'],APP,SLOT,LIST)
 return [(struct.unpack('<H',u.mem_read(LIST+4+i*12,2))[0],struct.unpack('<i',u.mem_read(LIST+12+i*12,4))[0]) for i in range(u32(u,LIST))]
call(u,0x8120264,SET,0x1a0,0)
for quad_visible in [0,1]:
 w32(u,0x24052004,quad_visible)
 for beat in [0,1]:
  call(u,0x8120264,SET,0x35,beat);v=visible(u);ids=[i for i,_ in v]
  check(ids==[0x34 if beat else 0x33,0x39,0xe,0xcb,0x4a,0x3a,0x3d,0xca,0x35,0x36,0x4b]+([0x37] if quad_visible else []),'Normal display order preserves native Beat/QUAD conditions '+str((quad_visible,beat)))
  check(len(v)<=12,'Normal fits all twelve compact widget slots '+str((quad_visible,beat)))
call(u,0x8120264,SET,0x1a0,1)
check([i for i,_ in visible(u)]==[0x1a1,0x1a2,0x1a3,0x4a,0x1a4,0x39,0x3d,0x1a5,0x1a6],'Granular shows seven knobs plus Freeze/Reverse, without a Mode knob')
check([v for _,v in visible(u)]==[250,80,4,0,0,400,0,0,0],'Granular visible controls use native saved/default values')
# Mode switches preserve hidden Normal and granular settings.
for id,value in [(0x33,123),(0x3a,678),(0x1a2,180),(0x1a6,1)]:call(u,0x8120264,SET,id,value)
for mode in [0,1,0,1]:
 call(u,0x8120264,SET,0x1a0,mode);visible(u)
check(all(call(u,0x8120234,SET,id)==v for id,v in [(0x33,123),(0x3a,678),(0x1a2,180),(0x1a6,1)]),'switching modes preserves each mode\'s hidden controls')
# Native layout with actual production model address, no model getter stub.
P=0x24010000;V=0x24008000;RECT=0x80dfe00;HIDE=0x80dfe10
KW=lambda i:P+0xc3b0+i*0x3d0;TW=lambda i:P+0x100b0+i*0x1d4
w32(u,V+0x20,RECT|1);w32(u,V+0x2c,HIDE|1)
for i in range(16):
 for w in [KW(i),TW(i)]:w32(u,w,V)
for off in [0x11df0,0x11e48,0x11ed8,0x11f18]:w32(u,P+off,V)
u.mem_write(P+4,struct.pack('<ii',0,100));w32(u,P+0x38,320);u.mem_write(P+0xc3a4,struct.pack('<H',20))
stub(u,RECT,lambda u:u.mem_write(u.reg_read(UC_ARM_REG_R0)+4,bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),16))))
stub(u,HIDE,lambda u:None);stub(u,0x8141548,lambda u:None);stub(u,0x813f496,lambda u:None)
stub(u,0x8121b50,lambda u:w32(u,u.reg_read(UC_ARM_REG_R3),0))
for mode in [0,1,2,3,4,5,0]:
 call(u,0x8120264,SET,0x1a0,mode);call(u,M['symbols']['4']['dly_layout'],P)
 count=6 if mode==4 else 9 if mode==1 else 11 if mode==2 else 8 if mode==5 else 12;rects=[]
 for i in range(count):
  r=struct.unpack('<4i',u.mem_read(TW(i)+4,16));rects.append(r)
  assert r==struct.unpack('<4i',u.mem_read(KW(i)+4,16)) and 0<=r[0]<=272 and 100<=r[1] and r[0]+r[2]<=320 and r[1]+r[3]<=189
 for i,(x,y,w,h) in enumerate(rects):
  for xx,yy,ww,hh in rects[i+1:]:assert x+w<=xx or xx+ww<=x or y+h<=yy or yy+hh<=y
 check(True,'all '+str(count)+' controls fit without overlap or off-screen bounds, mode '+str(mode))
# Actual event wrapper forwards Mode once, then refreshes via native populate boundary.
seen=[];pop=[];EV=0x24062000
u.mem_write(EV,struct.pack('<H6xH2xIII',0x6e,20,0x1a0,1,0))
call(u,0x8120264,SET,0x1a0,0)
# Avoid recursive call() resetting the in-flight wrapper stack: update value in place.
def bubble(u):
 seen.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),24)))
 data=u32(u,SET+4)
 for i in range(struct.unpack('<H',u.mem_read(SET+10,2))[0]):
  if struct.unpack('<H',u.mem_read(data+8*i,2))[0]==0x1a0:w32(u,data+8*i+4,u32(u,u.reg_read(UC_ARM_REG_R1)+16))
stub(u,0x813a414,bubble);stub(u,0x812abe0,lambda u:pop.append(u.reg_read(UC_ARM_REG_R0)))
ret=call(u,M['symbols']['4']['dly_event'],P,EV)
check(ret==0 and len(seen)==1 and pop==[P] and call(u,0x8120234,SET,0x1a0)==1,'Mode edit bubbles once, changes native model and refreshes the FX panel immediately')
call(u,0x8120264,SET,0x1a0,2)
u.mem_write(EV,struct.pack('<H6xH2xIII',0x6e,20,0x1a0,3,0));seen.clear();pop.clear()
ret=call(u,M['symbols']['4']['dly_event'],P,EV)
check(ret==0 and len(seen)==1 and pop==[P] and call(u,0x8120234,SET,0x1a0)==3,'Tape-to-Ducked mode edit repopulates even though both are non-granular')
oldcalls=[]
stub(u,0x812b460,lambda u:(oldcalls.append(u.reg_read(UC_ARM_REG_R1)),u.reg_write(UC_ARM_REG_R0,7)))
u.mem_write(EV,struct.pack('<H6xH2xIII',0x6e,20,0x39,700,0))
ret=call(u,M['symbols']['4']['dly_event'],P,EV)
check(ret==7 and oldcalls==[EV] and len(seen)==1 and pop==[P],'unrelated control edit preserves original handler, return and propagation')
check(u32(u,0x814b5c4)==M['symbols']['4']['dly_event']|1,'native FX event vtable points to the guarded Mode refresh wrapper')
print(str(H.checks)+' granular native UI checks passed; physical display/touch still requires hardware validation.')

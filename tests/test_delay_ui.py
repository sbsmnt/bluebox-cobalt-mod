"""Native desktop parameter definitions/paging and FX layout wrapper checks.
Widget setRect/display child callbacks are emulated; native compact layout runs.
"""
from firmware_harness import *
import firmware_harness as H
PARAMS=[(0x33,400),(0x39,400),(0xe,120),(0xcb,1000),(0x4a,0),(0x3a,0),(0x3d,0),(0xca,1),(0x35,1),(0x36,1),(0x4b,0),(0x37,0),(0x34,6),(0x1a0,5),(0x1a1,250),(0x1a2,80),(0x1a3,4),(0x1a4,0),(0x1a5,0),(0x1a6,0),(0x1a7,350),(0x1a8,700),(0x1a9,300),(0x1aa,400),(0x1ab,650),(0x1ac,6000),(0x1ad,0),(0x1b9,20)]
def defs(image):
 u=machine(image);stub(u,0x8103128,lambda u:u.reg_write(UC_ARM_REG_R0,0xc0010000));call(u,0x8134cb8,0);return u
old=defs(stock);u=defs(patched)
def cs(u,a):return bytes(u.mem_read(a,64)).split(b'\0')[0]
for id,typ,label,lo,hi,key in [(0x3a,8,b'Drift:',0,1000,b'dlyflutter'),(0x3d,8,b'To Rev:',0,1000,b'dlysend'),(0x4a,1,b'Pitch:',-12,12,b'dlypitch'),(0x4b,4,b'Pitch:',0,1,b'dlypitchon')]:
 a=0xc0010000+id*28
 check(bytes(old.mem_read(a,28))==bytes(28),'desktop parameter id '+hex(id)+' was unused')
 check(struct.unpack('<HB',u.mem_read(a,3))==(id,typ) and cs(u,u32(u,a+4))==label and struct.unpack('<ii',u.mem_read(a+16,8))==(lo,hi) and cs(u,u32(u,a+24))==key,'native definition '+hex(id)+' type/label/range/project key')
# Execute complete native kind-4 initializer, including the inline replacement.
u=machine();stub(u,0x8103128,lambda u:u.reg_write(UC_ARM_REG_R0,0xc0010000));u.mem_write(0x24010000,b'\x04');call(u,0x81202b8,0x24020000,0x24010000)
n=struct.unpack('<H',u.mem_read(0x2402000a,2))[0];actual=[struct.unpack('<H2xi',u.mem_read(0xc0010000+i*8,8)) for i in range(n)]
check(actual==PARAMS,'complete native delay initializer creates exact 28-entry list and safe defaults (exp.8 adds Tape Lo C Off)')
check(n<struct.unpack('<H',u.mem_read(0x2402000c,2))[0],'delay entries fit native allocation')
# Actual native encoder page selectors now treat delay like the two-page reverb.
P=0x24010000;KW=lambda i:P+0xc3b0+i*0x3d0;TW=lambda i:P+0x100b0+i*0x1d4
for fn in [0x812af04,0x812af42]:
 for initial,expected in [(0,0 if fn==0x812af04 else 1),(1,1 if fn==0x812af04 else 0)]:
  u=machine();u.mem_write(P+0xc3a4,struct.pack('<H',0x14));w32(u,P+0xc3a0,initial);pages=[]
  stub(u,0x812ab7c,lambda u:pages.append(u.reg_read(UC_ARM_REG_R1)))
  call(u,fn,P)
  check(pages==[expected],'native delay encoder paging '+hex(fn)+' from '+str(initial))
# Real stock compact layout + added placement; only display/widget boundaries stubbed.
u=machine();V=0x24008000;RECT=0x80dfe00;HIDE=0x80dfe10
w32(u,V+0x20,RECT|1);w32(u,V+0x2c,HIDE|1)
for i in range(16):
 for w in [KW(i),TW(i)]:w32(u,w,V)
for off in [0x11df0,0x11e48,0x11ed8,0x11f18]:w32(u,P+off,V)
u.mem_write(P+4,struct.pack('<ii',10,100));w32(u,P+0x38,320);u.mem_write(P+0xc3a4,struct.pack('<H',0x14))
moves=[]
def rect(u):
 w=u.reg_read(UC_ARM_REG_R0);r=bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),16));u.mem_write(w+4,r);moves.append(w)
stub(u,RECT,rect);stub(u,HIDE,lambda u:None)
stub(u,0x8141548,lambda u:None);stub(u,0x813f496,lambda u:None)
stub(u,0x8121b50,lambda u:w32(u,u.reg_read(UC_ARM_REG_R3),0))
try:call(u,M['symbols']['4']['dly_layout'],P)
except UcError:
 print('ERROR layout pc',hex(u.reg_read(UC_ARM_REG_PC)));raise
for i in range(12):
 col=(-1 if i in [8,9] else 4 if i in [10,11] else i//2)
 expected=(10+48*(col+1),100+45-44*(i&1),48,44)
 check(all(struct.unpack('<4i',u.mem_read(w+4,16))==expected for w in [KW(i),TW(i)]),'native compact layout plus wrapper places delay widget '+str(i))
check(all(u32(u,TW(i)+0x3c)==0 for i in range(16)),'layout preserves button label coordinates')
# populate wrapper moves before the native populate boundary and avoids repeats.
seen=[]
stub(u,0x812abe0,lambda u:seen.append(list(moves)))
SLOT=0x24009000;u.mem_write(SLOT,struct.pack('<H',0x14));moves.clear();call(u,M['symbols']['4']['dly_populate'],P,SLOT)
check(seen==[[]],'already-positioned delay widgets are not moved again')
u.mem_write(SLOT,struct.pack('<H',0x15));moves.clear();call(u,M['symbols']['4']['dly_populate'],P,SLOT)
check(seen[-1] and all(struct.unpack('<4i',u.mem_read(TW(i)+4,16))==(10+48*(i//2+1),100+45-44*(i&1),48,44) for i in range(8,13)),'switching to reverb restores native widget positions before populate')
# Native save/load loops, with XML object interfaces stubbed as a storage boundary.
u=machine();heap=[0xc0010000]
def allocate(u):
 a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,allocate);call(u,0x8134cb8,0);call(u,0x8120abc)
SET=0x24020000;XML=0x24050000;VTY=0x24021000;VXML=0x24051000
u.mem_write(0x24010000,b'\x04');call(u,0x81202b8,SET,0x24010000)
values={0x1ad:1,0x1ac:4200,0x1a7:420,0x1a8:810,0x1a9:750,0x1aa:530,0x1ab:990,0x3a:400,0x3d:650,0x4a:-7,0x4b:1,0x1a0:1,0x1a1:700,0x1a2:120,0x1a3:3,0x1a4:400,0x1a5:1,0x1a6:1}
for id,v in values.items():call(u,0x8120264,SET,id,v&0xffffffff)
w32(u,SET,VTY);w32(u,VTY,0x80dfe01);stub(u,0x80dfe00,lambda u:u.reg_write(UC_ARM_REG_R0,4))
w32(u,XML,VXML);w32(u,VXML,0x80dfe11);stub(u,0x80dfe10,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
saved={};last=['']
stub(u,0x8107b16,lambda u:u.reg_write(UC_ARM_REG_R0,XML));stub(u,0x8106c92,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
def property_key(u):last[0]=cs(u,u.reg_read(UC_ARM_REG_R1)).decode();u.reg_write(UC_ARM_REG_R0,XML)
stub(u,0x81074dc,property_key);stub(u,0x8106dc6,lambda u:saved.update({last[0]:u.reg_read(UC_ARM_REG_R1)}));stub(u,0x8106dba,lambda u:None)
call(u,0x8120b84,XML,XML,SET)
check(all(saved.get(k)==v for k,v in {'tapewild':1,'dlyflutter':400,'dlysend':650,'dlypitch':0xfffffff9,'dlypitchon':1,'dlymode':1,'grnposition':700,'grnsize':120,'grndensity':3,'grnjitter':400,'grnfreeze':1,'grnreverse':1,'tapedrive':420,'dlyduck':810,'duckrelease':750,'revsegment':530,'revtone':990,'tapehicut':4200}.items()),'native project writer emits all eighteen added FX1 keys, including signed pitch')
for id in values:call(u,0x8120264,SET,id,0)
stub(u,0x81074a6,lambda u:u.reg_write(UC_ARM_REG_R0,0));stub(u,0x8106ae8,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
def find_property(u):
 key=cs(u,u.reg_read(UC_ARM_REG_R1)).decode();last[0]=key;u.reg_write(UC_ARM_REG_R0,XML if key in saved else 0)
stub(u,0x810747c,find_property)
def read_integer(u):w32(u,u.reg_read(UC_ARM_REG_R1),saved[last[0]]);u.reg_write(UC_ARM_REG_R0,0)
stub(u,0x8106d9e,read_integer);call(u,0x8120c3c,XML,SET)
check(all(call(u,0x8120234,SET,id)==(v&0xffffffff) for id,v in values.items()),'native project reader restores all eighteen FX1 controls')
for selected in [0,2,3,4,5]:
 call(u,0x8120264,SET,0x1a0,selected);call(u,0x8120b84,XML,XML,SET)
 call(u,0x8120264,SET,0x1a0,0);call(u,0x8120c3c,XML,SET)
 check(call(u,0x8120234,SET,0x1a0)==selected and all(call(u,0x8120234,SET,pid)==value for pid,value in [(0x1a7,420),(0x1a8,810),(0x1a9,750),(0x1aa,530),(0x1ab,990)]),'native project save/load preserves new type '+str(selected)+' and all added settings')
for id,key in [(0x3a,'dlyflutter'),(0x3d,'dlysend'),(0x4a,'dlypitch'),(0x4b,'dlypitchon'),(0x1a0,'dlymode'),(0x1a1,'grnposition'),(0x1a2,'grnsize'),(0x1a3,'grndensity'),(0x1a4,'grnjitter'),(0x1a5,'grnfreeze'),(0x1a6,'grnreverse'),(0x1a7,'tapedrive'),(0x1a8,'dlyduck'),(0x1a9,'duckrelease'),(0x1aa,'revsegment'),(0x1ab,'revtone'),(0x1ac,'tapehicut'),(0x1ad,'tapewild')]:saved.pop(key);call(u,0x8120264,SET,id,0)
call(u,0x8120c3c,XML,SET)
check(all(call(u,0x8120234,SET,id)==0 for id in values),'old project without delay keys preserves new zero/off defaults')

# Loading an old project into a freshly initialized delay retains the actual
# new defaults (rather than relying on a pre-zeroed test object).
FRESH=0x24022000
call(u,0x81202b8,FRESH,0x24010000);w32(u,FRESH,VTY)
call(u,0x8120c3c,XML,FRESH)
check(all(call(u,0x8120234,FRESH,id)==v for id,v in [(0x1a0,5),(0x1a1,250),(0x1a2,80),(0x1a3,4),(0x1a4,0),(0x1a5,0),(0x1a6,0),(0x1a7,350),(0x1a8,700),(0x1a9,300),(0x1aa,400),(0x1ab,650),(0x1ac,6000),(0x1ad,0)]),'old project on a fresh delay retains plain Delay, 80ms, four-grain and unarmed Freeze defaults')

print(str(H.checks)+' desktop delay UI checks passed; complete native populate/display interaction remains a hardware test.')

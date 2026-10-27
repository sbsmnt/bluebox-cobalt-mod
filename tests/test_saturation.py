"""Execute desktop parameter registration and master DSP under Unicorn.
The shared harness models one unsupported stock double comparison; no device,
SD filesystem, USB hardware or wall-clock timing is emulated.
"""
from firmware_harness import *
import firmware_harness as H

def definitions(image):
 u=machine(image);heap=[0xc0010000]
 def allocate(u):  # bump allocator: the definitions table is the first block
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,allocate)
 call(u,0x8134cb8,0)
 return u,bytes(u.mem_read(0xc0010000,0x3934))
a,old=definitions(stock);b,new=definitions(patched)
lo=0x43*28;hi=lo+28
check(old[lo:hi]==bytes(28),'native desktop definition slot 0x43 is unused in stock')
check(all(old[i*28:(i+1)*28]==new[i*28:(i+1)*28] for i in range(523) if i not in [0x43,0x3a,0x3d,0x4a,0x4b,0x155,*range(0x1a0,0x1ba)]),'native parameter definitions outside the declared extensions (incl. exp.8 Tape Lo C 0x1b9) are preserved')
id,kind=struct.unpack('<HB',new[lo:lo+3]);label=u32(b,0xc0010000+lo+4);key=u32(b,0xc0010000+lo+24)
def string(u,p):return bytes(u.mem_read(p,64)).split(b'\0')[0]
check(id==0x43 and kind==8 and string(b,label)==b'Saturate:','native knob definition has expected id/type/label')
check(struct.unpack('<ii',new[lo+16:lo+24])==(0,1000) and string(b,key)==b'mstdrive','native knob range and project key are registered')

def parameters(image,kind):
 u=machine(image);stub(u,0x8103128,lambda u:u.reg_write(UC_ARM_REG_R0,0xc0010000))
 u.mem_write(0x24010000,bytes([kind]));call(u,0x81202b8,0x24020000,0x24010000)
 n=struct.unpack('<H',u.mem_read(0x2402000a,2))[0];cap=struct.unpack('<H',u.mem_read(0x2402000c,2))[0]
 return u,[struct.unpack('<H2xi',u.mem_read(0xc0010000+8*i,8)) for i in range(n)],cap
for kind in range(1,9):
 a,old,cap=parameters(stock,kind);b,new,cap=parameters(patched,kind)
 expected=([(0x33,400),(0x39,400),(0xe,120),(0xcb,1000),(0x4a,0),(0x3a,0),(0x3d,0),(0xca,1),(0x35,1),(0x36,1),(0x4b,0),(0x37,0),(0x34,6),(0x1a0,5),(0x1a1,250),(0x1a2,80),(0x1a3,4),(0x1a4,0),(0x1a5,0),(0x1a6,0),(0x1a7,350),(0x1a8,700),(0x1a9,300),(0x1aa,400),(0x1ab,650),(0x1ac,6000),(0x1ad,0),(0x1b9,20)] if kind==4 else old+([(0x43,0),(0x1b6,0),(0x1b7,500),(0x1b8,0)] if kind==2 else [(0x1ae,-96000)] if kind==1 else list(zip(range(0x1af,0x1b6),[350,600,750,250,200,5500,120])) if kind==5 else []))
 check(new==expected,'native set kind '+str(kind)+': correct master/delay defaults and preserved other kinds')
 check(len(new)<=cap,'native set kind '+str(kind)+': entries stay within allocated capacity')
 if kind==2:
  call(b,0x8120264,0x24020000,0x43,730)
  check(call(b,0x8120234,0x24020000,0x43)==730,'actual native parameter set/get stores saturation changes')
ids=list(struct.unpack('<6H',patched[0x10aea0:0x10aeac]))
check(ids==[0x13b,0x43,0x1b6,0x1b7,0x1b8,0],'master menu appends saturation after last compressor row and retains terminator')

# Execute native parameter save/load loops. Only the XML object/storage
# boundary is stubbed; enumeration, key lookup and set updates execute stock.
u=machine();heap=[0xc0010000]
def allocate(u):
 a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,allocate);call(u,0x8134cb8,0);call(u,0x8120abc)
SET=0x24020000;TYPE=0x24010000;XML=0x24050000;VTYPE=0x24021000;VXML=0x24051000
u.mem_write(TYPE,b'\x02');call(u,0x81202b8,SET,TYPE);call(u,0x8120264,SET,0x43,730)
w32(u,SET,VTYPE);w32(u,VTYPE,0x80dfe01);stub(u,0x80dfe00,lambda u:u.reg_write(UC_ARM_REG_R0,2))
w32(u,XML,VXML);w32(u,VXML,0x80dfe11);stub(u,0x80dfe10,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
saved={};last=['']
stub(u,0x8107b16,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
stub(u,0x8106c92,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
def property_key(u):
 last[0]=string(u,u.reg_read(UC_ARM_REG_R1)).decode();u.reg_write(UC_ARM_REG_R0,XML)
stub(u,0x81074dc,property_key)
stub(u,0x8106dc6,lambda u:saved.update({last[0]:u.reg_read(UC_ARM_REG_R1)}))
stub(u,0x8106dba,lambda u:None)
for pid,value in [(0x1b6,1),(0x1b7,275),(0x1b8,-3000)]:call(u,0x8120264,SET,pid,value&0xffffffff)
call(u,0x8120b84,XML,XML,SET)
check(saved.get('mstdrive')==730,'actual native parameter writer emits mstdrive=730 through XML boundary')
check([saved.get(k) for k in ('mstsatmode','msttapetone','mstsatout')]==[1,275,(-3000)&0xffffffff],'native XML writer includes Tape controls')
for pid,value in [(0x1b6,0),(0x1b7,500),(0x1b8,0)]:call(u,0x8120264,SET,pid,value)
call(u,0x8120264,SET,0x43,0)
stub(u,0x81074a6,lambda u:u.reg_write(UC_ARM_REG_R0,0))
stub(u,0x8106ae8,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
def find_property(u):
 key=string(u,u.reg_read(UC_ARM_REG_R1)).decode();last[0]=key;u.reg_write(UC_ARM_REG_R0,XML if key in saved else 0)
stub(u,0x810747c,find_property)
def read_integer(u):
 w32(u,u.reg_read(UC_ARM_REG_R1),saved[last[0]]);u.reg_write(UC_ARM_REG_R0,0)
stub(u,0x8106d9e,read_integer)
call(u,0x8120c3c,XML,SET)
check(call(u,0x8120234,SET,0x43)==730,'actual native parameter reader restores mstdrive through XML boundary')
check([call(u,0x8120234,SET,pid) for pid in (0x1b6,0x1b7,0x1b8)]==[1,275,(-3000)&0xffffffff],'native XML reader restores Tape controls')
for key,pid,value in [('mstsatmode',0x1b6,0),('msttapetone',0x1b7,500),('mstsatout',0x1b8,0)]:
 saved.pop(key);call(u,0x8120264,SET,pid,value)
saved.pop('mstdrive');call(u,0x8120264,SET,0x43,0);call(u,0x8120c3c,XML,SET)
check(call(u,0x8120234,SET,0x43)==0,'older project without mstdrive retains safe zero default')

check([call(u,0x8120234,SET,pid) for pid in (0x1b6,0x1b7,0x1b8)]==[0,500,0],'alpha.5 project without new keys retains Soft/neutral Tone/unity trim')

# Saturation scalar reference, independent Python implementation.
def curve(x):return 1. if x>3 else -1. if x< -3 else x*(27+x*x)/(27+9*x*x)
def reference(x,drive):
 d=max(0,min(1000,drive))/1000;g=2**(6*d);mix=min(d*20,1);k=.2/curve(g*.2)
 return x+mix*(curve(x*g)*k-x)
for bypass in [0,1]:
 for drive in [0,1,10,50,300,700,1000]:
  u,nodes=setup()
  for n in range(8):
   l=signal(.8,n);r=signal(.3,n,700)
   out,ret=block(u,l,r,[(0xb5,bypass),(0x43,drive)] if n==0 else [])
  values=struct.unpack('<64f',out)
  check(ret==7 and len(nodes)==8 and u32(u,OBJ+8)==NEXT,'drive '+str(drive)+' comp '+str(bypass)+': chain restored and runs exactly once')
  check(all(math.isfinite(x) for x in values),'drive '+str(drive)+' comp '+str(bypass)+': output finite')
  if not bypass:
   expected=[reference(x,drive) for x in l+r]
   check(max(abs(x-y) for x,y in zip(values,expected))<.00006,'drive '+str(drive)+': steady stereo audio matches scalar curve')
# Reference amplitude, symmetry, clamp and nonzero drive with compressor disabled.
for drive in [50,300,700,1000]:
 u,_=setup()
 for i in range(3):out,_=block(u,[.2]*32,[-.2]*32,[(0xb5,0),(0x43,drive)] if i==0 else [])
 values=struct.unpack('<64f',out)
 check(max(abs(x-.2) for x in values[:32])<1e-6 and max(abs(x+.2) for x in values[32:])<1e-6,'drive '+str(drive)+': +/-0.2 reference level preserved')
for drive in [-999,2000]:
 u,_=setup()
 for i in range(3):out,_=block(u,[.8]*32,[-.8]*32,[(0xb5,0),(0x43,drive)] if i==0 else [])
 vals=struct.unpack('<64f',out)
 check(max(abs(x-reference(.8,drive)) for x in vals[:32])<1e-6 and max(abs(x-reference(-.8,drive)) for x in vals[32:])<1e-6,'out-of-range drive '+str(drive)+' is clamped')
# Smooth per-sample change rather than an instantaneous block-wide jump.
u,_=setup();block(u,[.7]*32,[-.7]*32,[(0xb5,0)])
out,_=block(u,[.7]*32,[-.7]*32,[(0x43,1000)])
vals=struct.unpack('<64f',out)
check(.2<vals[0]<.7 and abs(vals[31]-.2)<1e-5 and all(abs(vals[i]+vals[32+i])<1e-6 for i in range(32)),'drive up ramps across a block with symmetric channels')
out,_=block(u,[.7]*32,[-.7]*32,[(0x43,0)])
check(abs(struct.unpack('<64f',out)[31]-.7)<1e-5,'drive down reaches dry audio at end of ramp')
out,_=block(u,[.7]*32,[-.7]*32)
check(out==struct.pack('<64f',*([.7]*32+[-.7]*32)),'after ramp to zero audio is bit-exact dry input')
# Check downstream sees saturated samples, not the original unsaturated bus.
u,_=setup(chain=False);w32(u,OBJ+8,NEXT);w32(u,NEXT,VT);w32(u,VT+12,NEXT_FN|1);seen=[]
def downstream(u):
 seen.append(bytes(u.mem_read(BUF,128)));u.reg_write(UC_ARM_REG_R0,7)
stub(u,NEXT_FN,downstream)
for i in range(3):out,ret=block(u,[.8]*32,[-.5]*32,[(0xb5,0),(0x43,1000)] if i==0 else [])
check(len(seen)==3 and seen[-1]==out[:128] and max(struct.unpack('<32f',seen[-1]))<.201,'downstream master node receives already-saturated audio')
# No downstream node: stock-compatible return and intact null linkage.
u,_=setup(chain=False);out,ret=block(u,[.7]*32,[-.2]*32,[(0xb5,0),(0x43,500)])
check(ret==1 and u32(u,OBJ+8)==0,'saturation without a downstream node preserves terminal return')
# Meter follows compressor gain only; saturation does not rewrite that reading.
a,_=setup();b,_=setup()
for n in range(96):
 ev=[(0xb5,1),(0x132,-40000),(0x135,0)] if n==0 else []
 block(a,signal(.3,n),signal(.1,n),ev)
 block(b,signal(.3,n),signal(.1,n),ev+([(0x43,700)] if n==0 else []))
check(report(a)==report(b) and a.mem_read(OBJ,0x130)==b.mem_read(OBJ,0x130),'saturation leaves compressor state and both meters reading unchanged')
# Native stereo helper expands mono before processing; silent and empty buses
# must remain untouched, and short buffers must not write beyond their frames.
for frames,cap,silent,stereo in [(4,32,0,1),(32,32,0,0),(32,32,1,1),(0,0,0,1)]:
 u,_=setup();block(u,[.8]*32,[-.5]*32,[(0xb5,0),(0x43,1000)])
 events(u,[])
 desc=CTX+0x150e4+12*20
 u.mem_write(desc,struct.pack('<IIIIBBxx',cap,frames,BUF,BUF+0x100,silent,stereo))
 u.mem_write(BUF,struct.pack('<32f',*([.8]*32)));u.mem_write(BUF+0x100,struct.pack('<32f',*([-.5]*32)))
 before=bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128))
 call(u,u32(u,u32(u,OBJ)+12),OBJ,CTX)
 out=bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128))
 vals=struct.unpack('<64f',out)
 if silent or not cap:
  check(out==before,'silent/empty bus skips saturation: '+str((frames,cap,silent,stereo)))
 elif not stereo:
  check(max(abs(x-.2) for x in vals)<1e-5,'native mono bus expands to identical saturated stereo channels')
 else:
  check(all(abs(x-.2)<1e-5 for x in vals[:4]) and all(abs(x+.2)<1e-5 for x in vals[32:36]) and out[16:128]==before[16:128] and out[144:]==before[144:],'short audio block saturates only its valid frames')
# Instruction counts are not CPU cycle/timing measurements.
for drive in [0,700]:
 u,_=setup()
 for n in range(4):block(u,signal(.7,n),signal(.2,n),[(0xb5,1),(0x43,drive)] if n==0 else [])
 steps=[0]
 def count(u,addr,size,data):steps[0]+=1
 h=u.hook_add(UC_HOOK_CODE,count);block(u,signal(.7,5),signal(.2,5));u.hook_del(h)
 print('INFO complete master callback instructions, drive',drive,':',steps[0])
 check(steps[0]<18000,'drive '+str(drive)+': callback completes within emulator instruction budget')
print(str(H.checks)+' saturation/registration checks passed. Physical save/reload, routing, recordings and CPU headroom require hardware tests.')

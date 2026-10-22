"""Real desktop constructor, queues, ARM DSP, stock tank preservation and CPU
instruction estimates. No hardware CPU/cycle claim is made by this fixture.
"""
from reverb_fixture import *
SG=M['symbols']['delay7'];SL=0x38800100
PARAMS=list(range(0x1af,0x1b6));DEFAULTS=[350,600,750,250,200,5500,120]
def sgsetup():
 u,nodes,heap=rsetup();w32(u,0x24000004,heap);w32(u,0x24000000,0xc2000000)
 call(u,SG['sg_allocate']);ptr=u32(u,SL+4)
 assert ptr and u32(u,ptr)==0x53474732
 return u,nodes,ptr
u,nodes,ptr=sgsetup()
check(0xc0000000<=ptr<0xc2000000,'constructor reserves independent, initialized Shoegaze SDRAM')
check(list(struct.unpack('<7i',u.mem_read(ptr+52,28)))==DEFAULTS,'ARM engine defaults match all seven UI values')
# The stock 1 MiB reverb tank and ITCM are not shared with Shoegaze.
native_end=ptr;before=bytes(u.mem_read(0xc0010000,native_end-0xc0010000));itcm=bytes(u.mem_read(0,0x10000))
samples=[]
for n in range(1600):
 x=signal(.25,n,311) if 60<=n<240 else [0.]*32
 y=signal(.15,n,499) if 60<=n<240 else [0.]*32
 out,ret=rblock(u,x,y,[(0x155,15),(0x15c,0),(0x15a,1000)] if n==0 else [])
 v=struct.unpack('<64f',out);samples.append(v)
 assert all(math.isfinite(z) and abs(z)<2.001 for z in v) and ret==7
check(len(nodes)==1600,'Shoegaze calls the downstream graph node exactly once per block')
check(before==bytes(u.mem_read(0xc0010000,native_end-0xc0010000)) and itcm==bytes(u.mem_read(0,0x10000)),'Shoegaze never overwrites the stock reverb tank or ITCM')
energy=[sum(v[i]*v[i] for v in samples[400:] for i in range(a,a+32)) for a in [0,32]]
print('INFO ARM Shoegaze stereo tail energies',energy)
check(min(energy)>1e-6 and abs(10*math.log10(energy[0]/energy[1]))<12,'both wet channels emit a sustained, finite stereo tail')
check(any(abs(v[i]-v[i+32])>1e-5 for v in samples[400:] for i in range(32)),'stereo output remains spatially distinct')
# Real parameter queue edits, including malformed bounds.
for pid,v,expected in [(0x1af,840,840),(0x1b0,1000,1000),(0x1b1,0,0),(0x1b2,1000,1000),(0x1b3,2000,2000),(0x1b4,16000,16000),(0x1b5,2000,2000),(0x1b3,-300,10),(0x1b4,0,500),(0x1b5,999999,2000)]:
 rblock(u,[0.]*32,evs=[(pid,v)])
 check(u32(u,ptr+52+4*(pid-0x1af))==expected,'native M7 FX2 queue applies control '+hex(pid)+' = '+str(v))
# The pending FX1 send reaches the custom reverb input, not a parallel dry path.
a,_,pa=sgsetup();b,_,pb=sgsetup();same=True;energy=0.
for n in range(650):
 e=[(0x155,15),(0x15c,0),(0x15a,1000)] if n==0 else []
 x=signal(.2,n,711) if 50<=n<180 else [0.]*32
 aa,_=rblock(a,x,evs=e)
 w32(b,DS,0x44454c35);w32(b,DS+56,32);b.mem_write(DS+60,struct.pack('<64f',*(x+x)))
 bb,_=rblock(b,[0.]*32,evs=e,silent=1)
 same &= aa==bb and u32(b,DS+56)==0
 energy+=sum(v*v for v in struct.unpack('<64f',bb))
check(same and energy>1e-5,'delayed FX1 send produces exactly the same Shoegaze audio as explicit input')
# Bypass is dry (native semantics), and pending delay send is discarded.
for bypass in [1,1,0,1,0]:
 x=signal(.13,0);w32(u,DS,0x44454c35);w32(u,DS+56,32);u.mem_write(DS+60,struct.pack('<64f',*([.2]*64)))
 out,ret=rblock(u,x,evs=[(0x15c,bypass)])
 if bypass:check(out==struct.pack('<64f',*(x+x)) and u32(u,DS+56)==0,'native bypass preserves dry input and discards pending send')
# Short/empty blocks and silent input must not truncate a live tail or overrun.
for n in range(200):rblock(u,signal(.2,n),evs=[(0x15c,0)] if n==0 else [])
def custom(frames,silent=0,stereo=1):
 w32(u,QUEUE+0x600,0);u.mem_write(BUS,struct.pack('<IIIIBBxx',32 if frames else 0,frames,BUF,BUF+0x100,silent,stereo))
 inp=struct.pack('<64f',*([.123]*64));u.mem_write(BUF,inp[:128]);u.mem_write(BUF+0x100,inp[128:])
 call(u,u32(u,u32(u,REV)+12),REV,CTX)
 return inp,bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128))
aa,bb=custom(1);check(aa[4:128]==bb[4:128] and aa[132:]==bb[132:],'one-frame callback does not write outside either channel')
aa,bb=custom(0);check(aa==bb,'zero-frame callback leaves the bus untouched')
aa,bb=custom(32,1,0);check(any(v!=0 for v in struct.unpack('<64f',bb)) and u.mem_read(BUS+16,1)==b'\0','silent mono input still publishes a live stereo reverb tail')
# No callback allocation. Style switching never indexes stock table at 15.
stub(u,0x80412a4,lambda u:(_ for _ in ()).throw(AssertionError('callback allocation')))
seen=[]
def setter(q,a,n,d):
 if q.reg_read(UC_ARM_REG_R1)==0x155:
  f=struct.unpack('<f',struct.pack('<I',q.reg_read(UC_ARM_REG_S0)))[0];seen.append(f)
h=u.hook_add(UC_HOOK_CODE,setter,begin=0x8044da4,end=0x8044da4)
for n in range(80):rblock(u,signal(.2,n),evs=[(0x155,15 if n%2 else n%15),(0x1b0,1000 if n%3 else 0)])
u.hook_del(h)
check(seen and all(0<=v<15 for v in seen),'rapid style changes keep Shoegaze out of the native fifteen-entry preset table')
check(True,'audio callbacks and rapid control/style changes never allocate')
# Compare stock styles even after constructing the extra custom buffer.
for style in [0,7,14]:
 a,_,_=rsetup(stock);b,_,_=sgsetup();same=True
 for n in range(650):
  e=[(0x155,style),(0x159,1000),(0x15a,1000),(0x15c,0)] if n==0 else []
  x=signal(.2,n) if 100<=n<150 else [0.]*32
  aa,ra=rblock(a,x,evs=e);bb,rb=rblock(b,x,evs=e);same &= aa==bb and ra==rb
 check(same,'stock style '+str(style)+' remains bit-identical with Shoegaze buffer allocated')
# OOM returns a valid style through the native notification queue; no style15
# ever enters the native setter or allocator, even for hostile retained state.
z,_,_=rsetup();w32(z,SL,0x53474c32);w32(z,SL+4,0)
out,ret=rblock(z,[.1]*32,evs=[(0x155,15),(0x15c,0)])
q=CTX+0x1529c
check(ret==7 and not any(struct.unpack('<64f',out)) and bytes(z.mem_read(q,24))==struct.pack('<6I',0x39,0,21,0x155,0,0),'missing buffer mutes custom wet and acknowledges fallback to stock style zero')
# Real allocator preflight (custom constructor only; native graph not called).
for available,ok in [(205056,True),(205055,False),(0,False)]:
 z=machine();w32(z,0x24000004,0xc0100000);w32(z,0x24000000,0xc0100000+available)
 call(z,SG['sg_allocate'])
 check(bool(u32(z,SL+4))==ok,'real allocator safely handles custom remaining bytes '+str(available))
# Instruction counts include full native wrapper, event scans and graph chain.
u,_,ptr=sgsetup()
for n in range(250):rblock(u,signal(.4,n),evs=[(0x155,15),(0x15c,0),(0x1af,1000),(0x1b0,1000),(0x1b1,1000),(0x1b2,1000),(0x1b3,2000)] if n==0 else [])
count=[0];h=u.hook_add(UC_HOOK_CODE,lambda q,a,n,d:count.__setitem__(0,count[0]+1))
out,_=rblock(u,signal(.4,251));u.hook_del(h)
print('INFO steady Shoegaze callback ARM instruction estimate',count[0])
check(count[0]<60000,'Shoegaze steady callback completes within the emulator instruction budget')
print(str(H.checks)+' Shoegaze native integration checks passed; physical CPU, recording and sound remain hardware tests.')

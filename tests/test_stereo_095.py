"""WILD stereo gain staging in actual desktop PING/QUAD feedback paths."""
from delay_fixture import *
import importlib.util,os
oldroot=(ROOT/'tests'/'references'/'v0.9.5')
spec=importlib.util.spec_from_file_location('v095',oldroot/'patch_bluebox.py');before=importlib.util.module_from_spec(spec);spec.loader.exec_module(before)
oldimage=before.transform(stock)
# Measured reproduction: mono/centered, quiet priming followed by silence.
# PING inherently has unequal path lengths; test window energy, not per-sample
# equality. Native calls and stereo paths are retained rather than summed.
for quad in [0,1]:
 a,_,_=dsetup(oldimage);b,nodes,heap=dsetup();energy=[[0.,0.],[0.,0.]];reads={}
 for n in range(3000):
  ev=INITIAL+[(0x1a0,2),(0x1ad,1),(0x1a7,1000),(0x39,650),(0x1ac,16000),(0x36,1),(0x37,quad)] if n==0 else []
  for j,u in enumerate([a,b]):
   out,ret=dblock(u,[.00001]*32 if n<120 else ZERO,evs=ev)
   if n==0:steady(u)
   v=struct.unpack('<64f',out);assert all(math.isfinite(x) and abs(x)<4.01 for x in v)
   if n>2300:
    energy[j][0]+=sum(x*x for x in v[:32]);energy[j][1]+=sum(x*x for x in v[32:])
 db=[10*math.log10(e[0]/e[1]) for e in energy]
 print('INFO PING/QUAD',quad,'v0.9.5/1.0.0-alpha.4 energies',energy,'L/R dB',db)
 check(energy[0]==energy[1],'retain v0.9.5 corrected WILD stereo output, QUAD'+str(quad))
 check(abs(db[1])<3 and db[1]==db[0],'corrected WILD buildup-window imbalance under3dB, QUAD'+str(quad))
 check(min(energy[1])>1e-6,'WILD still regenerates audible energy on both channels, QUAD'+str(quad))
 check(ret==7 and len(nodes)==3000 and u32(b,0x24000004)==heap,'corrected WILD keeps graph/heap ownership, QUAD'+str(quad))
 def trace(u,a,n,d):
  line=(u.reg_read(UC_ARM_REG_R0)-(DOBJ+0x30))//28;ch=u.reg_read(UC_ARM_REG_R2)
  reads[line,ch]=reads.get((line,ch),0)+1
 h=b.hook_add(UC_HOOK_CODE,trace,begin=SYMS['dly_read'],end=SYMS['dly_read']);dblock(b,ZERO);b.hook_del(h)
 expected={(1,1):1,(3 if quad else 2,0):1,(0,1):1}
 if quad:expected.update({(2,1):1,(4,1):1,(5,1):1})
 check(reads==expected,'actual native PING topology matches the corrected serial-stage mapping, QUAD'+str(quad))
# Ten-second runs also catch merely moving the sustained dominance to the
# right. Compare the final999 blocks, with centered quiet priming then silence.
for feedback,drive,quad in [(650,1000,0),(650,1000,1),(800,1000,0),(900,350,0)]:
 u,_,_=dsetup();energy=[0.,0.]
 for n in range(15000):
  ev=INITIAL+[(0x1a0,2),(0x1ad,1),(0x1a7,drive),(0x39,feedback),(0x1ac,6000),(0x36,1),(0x37,quad)] if n==0 else []
  out,_=dblock(u,[.00001]*32 if n<120 else ZERO,evs=ev)
  if n==0:steady(u)
  v=struct.unpack('<64f',out);assert all(math.isfinite(x) and abs(x)<4.01 for x in v)
  if n>14000:
   energy[0]+=sum(x*x for x in v[:32]);energy[1]+=sum(x*x for x in v[32:])
 db=10*math.log10(energy[0]/energy[1])
 print('INFO sustained WILD',feedback,drive,quad,energy,'L/R dB',db)
 check(min(energy)>1 and abs(db)<.5,'sustained WILD retains regeneration with under0.5dB imbalance '+str((feedback,drive,quad)))
# PING off WILD should keep its existing waveform, including QUAD flag on/off.
for quad in [0,1]:
 a,_,_=dsetup(oldimage);b,_,_=dsetup();same=True
 for n in range(1000):
  ev=INITIAL+[(0x1a0,2),(0x1ad,1),(0x1a7,1000),(0x39,800),(0x36,0),(0x37,quad)] if n==0 else [(0x1ac,1500),(0x3a,300)] if n==450 else []
  oa,ra=dblock(a,signal(.17,n,411),signal(.08,n,733),ev);ob,rb=dblock(b,signal(.17,n,411),signal(.08,n,733),ev)
  if n==0:steady(a);steady(b)
  same &= oa==ob and ra==rb
 check(same,'WILD with PING off remains bit-exact to0.9.5 including stereo/Drift/HiCut, QUAD'+str(quad))
# A one-sided source remains one-sided in ordinary stereo delay: this is not
# a mono sum, cross-channel gain rider, or forced output balance.
for left in [True,False]:
 u,_,_=dsetup();el=er=0.
 for n in range(650):
  ev=INITIAL+[(0x1a0,2),(0x1ad,1),(0x1a7,800),(0x39,650),(0x36,0)] if n==0 else []
  s=signal(.13,n,733);out,_=dblock(u,s if left else ZERO,ZERO if left else s,ev)
  if n==0:steady(u)
  v=struct.unpack('<64f',out);el+=sum(x*x for x in v[:32]);er+=sum(x*x for x in v[32:])
 check((el>1e-3 and er==0) if left else (er>1e-3 and el==0),'independent '+('left' if left else 'right')+' input preserves stereo isolation with PING off')
# Native option events apply within the same callback; all switches/Drive/
# HiCut extremes remain finite and do not allocate or alter chain ownership.
u,nodes,heap=dsetup()
for n in range(1200):
 ev=INITIAL+[(0x1a0,2),(0x1ad,1),(0x1a7,1000),(0x39,1000)] if n==0 else []
 if n%17==0:ev += [(0x36,(n//17)&1),(0x37,(n//34)&1),(0x1ac,500 if n&1 else 16000)]
 out,ret=dblock(u,signal(.8,n,1703),signal(.4,n,431),ev)
 if n==0:steady(u)
 assert all(math.isfinite(v) and abs(v)<4.01 for v in struct.unpack('<64f',out))
check(ret==7 and len(nodes)==1200 and u32(u,0x24000004)==heap,'rapid native PING/QUAD switches with WILD retain bounded stereo, graph and heap')
print(str(H.checks)+' v1.0.0-alpha.4 stereo checks passed; physical balance and CPU headroom require hardware tests.')

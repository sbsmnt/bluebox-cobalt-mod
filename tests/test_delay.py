from delay_fixture import *

check(stock[0x10f52:0x10f54]==bytes.fromhex('0023') and stock[0x10f58:0x10f5c]==bytes.fromhex('87f87332'),'native desktop graph configures delay wet-only at object +0x273')
check(u32(machine(),0x806b014)==SYMS['dly_process']|1,'desktop delay process pointer targets wrapper')
check(u32(machine(),0x806a33c)==SYMS['delay_reverb_process']|1,'desktop reverb process pointer targets send wrapper')
# A fresh graph must not inherit backup-SRAM audio or control values.
u=machine();u.mem_write(DS,bytes([0x77])*348);w32(u,DS,0x44454c35)
stub(u,0x8052478,lambda u:u.reg_write(UC_ARM_REG_R0,42))
ret=call(u,SYMS['dly_construct'],DOBJ)
check(ret==42 and u32(u,DS)==0x44454c35 and bytes(u.mem_read(DS+4,40))==bytes(40) and u32(u,DS+56)==0,'graph constructor preserves stock return and clears retained delay/send state')
# Disabled additions preserve native PCM, object state, line memory and chain.
for ping,quad,filt in [(0,0,0),(1,0,0),(1,1,0),(0,0,1),(1,0,1)]:
 a,an,ae=dsetup(stock);b,bn,be=dsetup();same=True
 evs=[('on',0),(0x35,0),(0x36,ping),(0x37,quad),(0x33,10),(0x39,500),(0xca,filt)]
 for n in range(220):
  ev=evs if n==0 else [(0x33,20),(0x39,700)] if n==90 else [('off',0)] if n==150 else [('on',0)] if n==170 else []
  x,y=signal(.3,n),signal(.1,n,700)
  oa,ra=dblock(a,x,y,ev);ob,rb=dblock(b,x,y,ev)
  same=same and oa==ob and ra==rb==7 and a.mem_read(DOBJ,0x278)==b.mem_read(DOBJ,0x278)
 check(same and an==bn and len(bn)==220,'default-off: stock PCM/object/chain exact for ping/quad/filter '+str((ping,quad,filt)))
 check(a.mem_read(0xc0010000,ae-0xc0010000)==b.mem_read(0xc0010000,ae-0xc0010000),'default-off: all native delay line/filter memory identical '+str((ping,quad,filt)))
# Audible impulse topology at a settled 60 ms delay, stock and patched.
for ping in [0,1]:
 a,_,_=dsetup(stock);b,_,_=dsetup()
 for u in [a,b]:
  dblock(u,ZERO,evs=INITIAL+[(0x36,ping)]);steady(u)
  for _ in range(100):dblock(u,ZERO)
 left=[];right=[];same=True
 for n in range(375):
  sig=[1. if n*32+i==0 else 0. for i in range(32)]
  aa,_=dblock(a,sig);bb,_=dblock(b,sig);same=same and aa==bb;v=struct.unpack('<64f',bb);left.extend(v[:32]);right.extend(v[32:])
 print('INFO impulse max/same',ping,same,max(map(abs,left)),max(map(abs,right)),[u32(b,DOBJ+0x30+i*0x1c+16) for i in range(6)])
 check(same and max(map(abs,left))>.4 and max(map(abs,right))>.4,'default-off settled audible echoes match stock, ping '+str(ping))
 print('INFO impulse peaks',ping,[(i,round(v,3)) for i,v in enumerate(left) if abs(v)>.01][:4],[(i,round(v,3)) for i,v in enumerate(right) if abs(v)>.01][:4])
# Semitone pitch, including negative values and ping C's squared ratio.
for pitch,ping in [(12,0),(-7,0),(7,0),(12,1)]:
 u,_,_=dsetup();dblock(u,ZERO,evs=INITIAL+[(0x39,0),(0x36,ping),(0x4a,pitch),(0x4b,1)]);steady(u)
 l,r=run(u,700)
 fl,fr=freq(l[-6000:]),freq(r[-6000:]);expect=480*2**(pitch/12);er=expect*2**(pitch/12) if ping else expect
 print('INFO pitch frequencies',pitch,ping,fl,fr)
 check(abs(fl-expect)<12 and abs(fr-er)<24,'native feedback-line resampling pitch '+str(pitch)+' ping '+str(ping))
 check(all(math.isfinite(x) and abs(x)<1.1 for x in l+r),'pitch output finite and bounded '+str((pitch,ping)))
 dblock(u,ZERO,evs=[(0x4b,0)]);l,r=run(u,700)
 check(abs(freq(l[-6000:])-480)<1 and abs(freq(r[-6000:])-480)<1,'pitch bypass restores source frequency '+str((pitch,ping)))
# Drift should visibly modulate period, with no nonfinite/unbounded values.
u,_,_=dsetup();dblock(u,ZERO,evs=INITIAL+[(0x39,0),(0x3a,1000)]);steady(u)
l,r=run(u,4500,hz=1000)
x=l[24000:];z=[i+x[i]/(x[i]-x[i+1]) for i in range(len(x)-1) if x[i]<=0<x[i+1]];period=[b-a for a,b in zip(z,z[1:])];mean=sum(period)/len(period);dev=max(abs(x-mean) for x in period)/mean
print('INFO drift relative period modulation',dev)
check(.01<dev<.06,'drift produces slow audible pitch modulation')
check(all(math.isfinite(x) and abs(x)<1.1 for x in l+r),'drift output remains bounded')
# Send: proportional wet output, one consumption before native reverb call.
u,_,_=dsetup();dblock(u,ZERO,evs=INITIAL+[(0x39,0),(0x3d,500)]);steady(u)
for n in range(140):out,_=dblock(u,signal(.5,n))
# layout: stash_n offset 56, stereo stash at 60.
check(u32(u,DS+56)==32,'delay-to-reverb stores one 32-frame block')
stash=struct.unpack('<64f',u.mem_read(DS+60,256));v=struct.unpack('<64f',out)
check(max(abs(x-y*.5) for x,y in zip(stash,v))<1e-7,'send captures scaled wet delay output, both channels')
REV=0x24050000;RVT=0x24051000;RFN=0x80dfe00;RBUF=BUF+0x1000;RBUS=CTX+0x150e4+14*20
w32(u,REV,RVT);w32(u,RVT+0x54,RFN|1);stub(u,RFN,lambda u:u.reg_write(UC_ARM_REG_R0,14))
u.mem_write(RBUS,struct.pack('<IIIIBBxx',32,32,RBUF,RBUF+0x100,1,1));u.mem_write(RBUF,bytes(512));seen=[]
def reverb_input(u):
 seen.append(bytes(u.mem_read(RBUF,128))+bytes(u.mem_read(RBUF+0x100,128)));u.reg_write(UC_ARM_REG_R0,9)
stub(u,0x80489a0,reverb_input)
ret=call(u,SYMS['delay_reverb_process'],REV,CTX)
check(ret==9 and struct.unpack('<64f',seen[-1])==stash and u32(u,DS+56)==0,'pending send reaches reverb input before stock process, cleared once, return preserved')
u.mem_write(RBUF,bytes(512));call(u,SYMS['delay_reverb_process'],REV,CTX)
check(seen[-1]==bytes(256),'same delay block is never sent twice')
dblock(u,ZERO,evs=[(0x3d,0)]);dblock(u,ZERO)
check(u32(u,DS+56)==0,'zero send clears pending storage after gain ramp')
# Feedback shifts each repeat again, rather than only transposing the return.
import cmath
u,_,_=dsetup();dblock(u,ZERO,evs=INITIAL+[(0x39,700),(0x4a,12),(0x4b,1)]);steady(u)
l,r=run(u,1500)
x=l[-6000:]
def magnitude(hz):return abs(sum(v*cmath.exp(-2j*math.pi*hz*i/48000) for i,v in enumerate(x)))
mags=[magnitude(hz) for hz in [480,960,1920,3840]]
print('INFO feedback spectrum at 480/960/1920/3840 Hz',mags)
check(mags[2]>.2*mags[1] and mags[3]>.05*mags[1] and mags[0]<.1*mags[1],'feedback produces successive octave-shifted repeats')
# Combined path exercises all quad/ping read sites and measures an instruction estimate.
u,_,_=dsetup();dblock(u,ZERO,evs=INITIAL+[(0x37,1),(0x36,1),(0x4b,1),(0x4a,12),(0x3a,1000),(0x3d,500),(0xca,1)]);steady(u)
l,r=run(u,700)
check(all(math.isfinite(x) and abs(x)<4 for x in l+r),'quad/ping/filter/pitch/drift/send combined output is finite and bounded')
steps=[0]
def instruction(u,a,n,_):steps[0]+=1
h=u.hook_add(UC_HOOK_CODE,instruction);dblock(u,signal(.3,701));u.hook_del(h)
print('INFO combined delay callback instructions',steps[0])
check(steps[0]<50000,'combined delay callback completes within emulator instruction budget (not hardware cycles)')

print(str(H.checks)+' desktop delay checks passed. Real device, full UI and timing remain untested.')

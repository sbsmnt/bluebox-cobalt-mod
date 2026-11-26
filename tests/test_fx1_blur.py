"""FX1 mode 7, Blur (spectral blur), on the real delay node: it shares the
Freeze state and memory, smears the send continuously, idles when the send
and the tail are silent, and moving between Freeze and Blur starts each
clean. The DSP is checked on the host as well (forward real FFT vs a direct
DFT, tails, full-Blur hold)."""
from delay_fixture import *
import math
GMODE=0x1a0;F_FREEZE=0x19c;F_TONE=0x19f;B_BLUR=0x1ec;B_DRIFT=0x1ed;B_TONE=0x1ee;B_LEVEL=0x1ef
LINK=0x38800e80
def sine(b,hz=440.,amp=.3):return [amp*math.sin(2*math.pi*hz*(b*32+i)/48000) for i in range(32)]
def block(u,l,evs=()):
 out,_=dblock(u,l,evs=evs);return struct.unpack('<64f',out)
def silent_block(u,evs=()):
 devents(u,evs)
 u.mem_write(DBUS,struct.pack('<IIIIBBxx',32,32,BUF,BUF+0x100,1,1))
 u.mem_write(BUF,struct.pack('<32f',*ZERO));u.mem_write(BUF+0x100,struct.pack('<32f',*ZERO))
 call(u,u32(u,u32(u,DOBJ)+12),DOBJ,CTX,count=20000000)
 return struct.unpack('<64f',bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128)))
def counted(u,fn):
 c=[0];h=u.hook_add(UC_HOOK_CODE,lambda q,a,s,d:c.__setitem__(0,c[0]+1),begin=0x08000000,end=0x081fffff)
 r=fn();u.hook_del(h);return r,c[0]
u,_,_=dsetup();st=u32(u,LINK+4)
out=[];cost=[]
for b in range(700):
 o,c=counted(u,lambda:block(u,sine(b),evs=INITIAL+[(GMODE,7),(B_BLUR,300),(B_TONE,500)] if b==0 else []))
 out+=o[:32];cost.append(c)
w=out[-9600:]
rms=math.sqrt(sum(v*v for v in w)/len(w))
print('INFO Blur 300 on a steady 0.3 sine: RMS %.3f, pitch %.1f Hz; %d emulator instructions per block (avg), max %d'%(rms,freq(w),sum(cost[200:])//500,max(cost[200:])))
check(u32(u,st+4*0)==0x46525a32 and abs(20*math.log10(rms/.4243))<2. and abs(freq(w)-440)<1.,'Blur smears the send: a steady sine comes back at its pitch, at about +6 dB (Level 500)')
check(o[32:]==o[:32],'Blur is mono: the same wash on both outputs')
check(sum(cost[200:])//500<45000,'Blur costs %d emulator instructions per block on average (whole FX1 node)'%(sum(cost[200:])//500))
avg=sum(cost[200:])/500
check(max(cost[200:])<1.25*avg and u32(u,st+0x1c904)==0,'G14: Blur paced by cost: the busiest block is within 25%% of the average (%d vs %.0f), no hop finished late'%(max(cost[200:]),avg))
# G14: Granular's history is not primed in Blur (and emptied once).
G_RENDER=SYMS['g_render_o2']&~1;hits=[0]
h=u.hook_add(UC_HOOK_CODE,lambda q,a,s,d:hits.__setitem__(0,hits[0]+1),begin=G_RENDER,end=G_RENDER)
block(u,sine(700));u.hook_del(h)
check(hits[0]==0 and u32(u,0x38800000+0x34)==0,'G14: Granular does not record in the background while Blur runs (its history is emptied)')
# Input stops: the tail decays, then Blur idles with exact silence and costs little.
tail=[]
for b in range(700,4200):tail+=silent_block(u)[:32]
early=math.sqrt(sum(v*v for v in tail[4800:9600])/4800)
_,idle=counted(u,lambda:silent_block(u))
check(early>.01 and all(v==0.0 for v in tail[-3200:]) and idle<8000,'after the send stops, the tail rings on (RMS %.3f at 0.1 s), then ends in exact silence; idle costs %d instructions (Freeze mode not frozen: about 7.3k)'%(early,idle))
# Blur to Freeze and back: each starts clean.
for b in range(4200,4300):block(u,sine(b),evs=[(GMODE,6)] if b==4200 else [])
fz=[]
for b in range(4300,4340):fz+=block(u,sine(b))
check(all(v==0.0 for v in fz),'Blur to Freeze: Freeze mode is silent until a press (no Blur left over)')
back=[]
for b in range(4340,4440):back+=block(u,sine(b),evs=[(GMODE,7)] if b==4340 else [])[:32]
check(max(abs(v) for v in back[:32*20])<.05 and max(abs(v) for v in back[-32*10:])>.01,'Freeze to Blur: Blur starts clean (silent for its first hops) and builds up again')
# Controls reach the state and the shadow copy.
block(u,sine(4440),evs=[(B_BLUR,777),(B_DRIFT,250),(B_TONE,600),(B_LEVEL,400)])
f=struct.unpack('<4i',u.mem_read(st+4*(1+5+6+3),16))
shadow=struct.unpack('<11h',u.mem_read(0x38800ec4,22))
check(f==(777,250,600,400) and shadow[7:]==(777,250,600,400),'Blur controls reach the state and the shadow kept for a re-setup %s %s'%(f,shadow[7:]))
# G14: Blur to Granular: Granular starts from fresh input and plays.
gr=[]
for b in range(4441,4900):gr+=block(u,sine(b),evs=[(GMODE,1)] if b==4441 else [])[:32]
check(max(abs(v) for v in gr[-32*100:])>.01 and u32(u,0x38800000+0x34)>10000,'G14: after Blur, Granular records again and plays (history rebuilt from fresh input)')
# G24/G25 per-block cycle cap: with CYCCNT running (here 1.8 cycles per
# emulated instruction, as measured on the device (G22): the cap is about
# 11,700 instructions) a block stops
# starting job units past the cap, yet every hop still finishes on time.
LATE=116996;HOPS=119108
def busiest(counting):
 u3,_,_=dsetup();ic=[0];per=[]
 u3.hook_add(UC_HOOK_CODE,lambda q,a,s,d:ic.__setitem__(0,ic[0]+1),begin=0x08000000,end=0x081fffff)
 if counting:u3.hook_add(UC_HOOK_MEM_READ,lambda q,acc,a,s,v,d:q.mem_write(0xe0001004,struct.pack('<I',int(1.8*ic[0])&0xffffffff)),begin=0xe0001004,end=0xe0001007)
 for b in range(700):
  i0=ic[0];block(u3,sine(b),evs=INITIAL+[(GMODE,7),(B_BLUR,300),(B_TONE,500),(B_DRIFT,800)] if b==0 else []);per.append(ic[0]-i0)
 st=u32(u3,LINK+4)
 return max(per[100:]),u32(u3,st+LATE),u32(u3,st+HOPS)
free,_,_=busiest(False);capped,late,hops=busiest(True)
print('INFO busiest Blur block (instructions): uncapped %d, capped %d; %d hops, %d late'%(free,capped,hops,late))
check(late==0 and hops>15 and capped<=free,'G24: with the cycle counter running at the device rate, no hop finishes late and the busiest Blur block is no heavier (%d -> %d instructions; the device showed the drop, 37-54%% -> 16-18%% of a block)'%(free,capped))
# Delay mode untouched.
u2,_,_=dsetup();block(u2,ZERO,evs=INITIAL+[(GMODE,5)]);steady(u2)
rep=[]
for b in range(200):rep+=block(u2,sine(b) if b<5 else ZERO)
check(max(abs(v) for v in rep[32*10:])>.01,'Delay mode still repeats (Blur only acts in mode 7)')
print(H.checks,'FX1 Blur checks passed')

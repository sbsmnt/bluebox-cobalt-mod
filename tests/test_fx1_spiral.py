"""G30: FX1 mode 8, Spiral. The stock delay with the SPEC insert's frequency
shifter on each line's read, inside the feedback loop: every repeat moves by
Shift Hz again (barber-pole). Runs the real delay node: constructor, events,
line reads, feedback."""
from delay_fixture import *
import math
GMODE=0x1a0;S_SHIFT=0x1df;S_SPREAD=0x1e0;B_BLUR=0x1ec
def knob_hz(k):
 d=(k-500)/500;u=abs(d)-.02
 if u<=0:return 0.
 hz=.1*2**(u*14.6/.98);return -hz if d<0 else hz
def sine(b,hz=440.,amp=.3):return [amp*math.sin(2*math.pi*hz*(b*32+i)/48000) for i in range(32)]
def echoes(knob,spread=0,fb=500,blocks=330):
 u,_,_=dsetup()
 dblock(u,ZERO,evs=INITIAL+[(0x39,fb),(GMODE,8),(S_SHIFT,knob),(S_SPREAD,spread)]);steady(u)
 l=[];r=[]
 for b in range(1,blocks):
  out,_=dblock(u,sine(b) if b<46 else ZERO);v=struct.unpack('<64f',out);l+=v[:32];r+=v[32:]
 return l,r
def fwin(x,b0,b1):return freq(x[b0*32:b1*32])
# Time 60 ms (Time 10 = 2880 samples = 90 blocks): the burst (blocks 1..45)
# comes back at 91..135, again at 181..225.
for knob in (845,200):
 hz=knob_hz(knob);l,r=echoes(knob)
 f1,f2=fwin(l,98,128),fwin(l,188,218)
 print('INFO Shift %d (%+.1f Hz): echo 1 %.1f Hz, echo 2 %.1f Hz'%(knob,hz,f1,f2))
 check(abs(f1-(440+hz))<3 and abs(f2-(440+2*hz))<4,'Spiral: each repeat is shifted again by Shift (%+.1f Hz): 1st %.1f, 2nd %.1f'%(hz,f1,f2))
l,r=echoes(500)
f1=fwin(l,98,128)
check(abs(f1-440)<1.5 and max(abs(v) for v in l[98*32:128*32])>.05,'Spiral at Shift 0 (centre): plain repeats, pitch kept (%.1f Hz)'%f1)
hz=knob_hz(845);l,r=echoes(845,spread=1)
fl,fr=fwin(l,98,128),fwin(r,98,128)
print('INFO SPRD: left %.1f Hz, right %.1f Hz'%(fl,fr))
check(abs(fl-(440+hz))<3 and abs(fr-(440-hz))<3,'SPRD: the left repeats rise and the right ones fall by the same Shift')
# Feedback 0: one repeat, no second one (the shifter adds no loop of its own;
# its allpass filters ring faintly after the echo stops, as on the SPEC insert).
l,r=echoes(845,fb=0)
check(max(abs(v) for v in l[98*32:128*32])>.05 and max(abs(v) for v in l[185*32:320*32])<.01 and max(abs(v) for v in l[270*32:320*32])<1e-3,'Spiral with FEEDB 0: one shifted repeat, then only the shifter\'s allpass ringing, under -30 dB and gone below -60 dB (no second repeat)')
# Long feedback stays bounded.
l,r=echoes(845,fb=1000,blocks=1200)
check(max(abs(v) for v in l)<1.5 and all(v==v for v in l),'Spiral at full FEEDB stays finite and bounded')
# Loop limit (dev.4): Shift at max with FEEDB near or at full. The allpass
# pair smears attacks into higher peaks on every pass and a top Shift used to
# lose less than a plain repeat, so peaks clipped and full FEEDB crept up.
import random
def burst(knob,fb,amp,blocks=1500):
 random.seed(3);u,_,_=dsetup()
 dblock(u,ZERO,evs=INITIAL+[(0x39,fb),(GMODE,8),(S_SHIFT,knob)]);steady(u)
 l=[]
 for b in range(1,blocks):
  out,_=dblock(u,[random.uniform(-amp,amp) for _ in range(32)] if b<46 else ZERO);l+=struct.unpack('<64f',out)[:32]
 T=90*32;return l,[math.sqrt(sum(v*v for v in l[k*T:(k+1)*T])/T) for k in range(1,len(l)//T)]
for fb in (980,1000):
 l,rms=burst(1000,fb,.9)
 print('INFO Shift max, FEEDB %d, noise at -1 dBFS: peak %.2f, repeat 1 %.1f dB, repeat %d %.1f dB'%(fb,max(abs(v) for v in l),20*math.log10(rms[0]),len(rms),20*math.log10(rms[-1])))
 check(max(abs(v) for v in l)<1.,'Spiral, Shift max, FEEDB %d: the repeats never reach full scale (peak %.2f)'%(fb,max(abs(v) for v in l)))
 check(rms[-1]<rms[0],'Spiral, Shift max, FEEDB %d: the repeats fade instead of building up'%fb)
# G31: Spiral has no PING / QUAD. Set before or during Spiral, they are held
# off (the same repeats as without them); leaving Spiral puts the saved
# settings back.
plain,_=echoes(845)
u,_,_=dsetup();dblock(u,ZERO,evs=INITIAL+[(0x36,1),(0x37,1),(GMODE,8),(S_SHIFT,845)]);steady(u)
pq=[]
for b in range(1,330):out,_=dblock(u,sine(b) if b<46 else ZERO);pq+=struct.unpack('<64f',out)[:32]
flags=u.mem_read(DOBJ+0x274,3)
check(flags[0]==0 and flags[2]==0 and max(abs(x-y) for x,y in zip(pq,plain))<1e-6,'G31: PING and QUAD on in the project: Spiral runs without them (the same repeats as with them off)')
dblock(u,ZERO,evs=[(GMODE,0)]);dblock(u,ZERO)
flags=u.mem_read(DOBJ+0x274,3)
check(flags[0]==1 and flags[2]==1,'G31: leaving Spiral for Warp puts the saved PING and QUAD back')
dblock(u,ZERO,evs=[(GMODE,8)]);dblock(u,ZERO,evs=[(0x36,1)]);dblock(u,ZERO)
check(u.mem_read(DOBJ+0x274,1)[0]==0,'G31: a PING sent while in Spiral (a mapped CC) is held off too, and kept for later')
dblock(u,ZERO,evs=[(GMODE,5)]);dblock(u,ZERO)
check(u.mem_read(DOBJ+0x274,1)[0]==1,'G31: ...and comes back in Delay')
# Leaving Spiral for Flux and back: no crash, Spiral repeats again.
u,_,_=dsetup();dblock(u,ZERO,evs=INITIAL+[(GMODE,8),(S_SHIFT,845)]);steady(u)
for b in range(1,60):dblock(u,sine(b),evs=[(GMODE,7)] if b==20 else [(GMODE,8)] if b==40 else [])
print(H.checks,'FX1 Spiral checks passed')

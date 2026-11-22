"""S8 insert filter on executed ARM through the real native EQ channel/queue:
LP/BP/HP/DJ response, exact DJ centre bypass, envelope follower, family
crossfades, Insert On/Off, legacy SIN mapping and the nine native mode labels."""
from gate_fixture import *
import firmware_harness as H
import gc
MODE,DRIVE,TONE,MIX,OUT=0x1ba,0x1bb,0x1bc,0x1bd,0x1be
ON=0x1d5
def sine(hz,amp,b,phase=0.):
 return [amp*math.sin(2*math.pi*hz*((b*32+i)/48000.)+phase) for i in range(32)]
def mix(*parts):return [sum(p[i] for p in parts) for i in range(32)]
def rms(xs):return math.sqrt(sum(x*x for x in xs)/max(1,len(xs)))
def run(events,blocks,source,stereo=True):
 u,_,_=gsetup();outs=[]
 for b in range(blocks):
  l=source(b);r=source(b) if stereo else None
  out,ret=gblock(u,l,r,events if b==0 else [])
  check_ok=ret==7
  outs.append(struct.unpack('<64f',out))
 return u,outs
def tail(outs,n=40):return [x for o in outs[-n:] for x in o[:32]]
def band(outs,hz,n=40):
 # Correlate the last n blocks (left) with sin/cos at hz: amplitude of that partial.
 xs=tail(outs,n);start=(len(outs)-n)*32
 s=sum(x*math.sin(2*math.pi*hz*(start+i)/48000.) for i,x in enumerate(xs))
 c=sum(x*math.cos(2*math.pi*hz*(start+i)/48000.) for i,x in enumerate(xs))
 return 2*math.sqrt(s*s+c*c)/len(xs)
src=lambda b:mix(sine(110,.2,b),sine(5000,.2,b))
# LP at a low cutoff keeps 110 Hz and removes 5 kHz; HP does the opposite.
_,lp=run([(MODE,5),(DRIVE,350),(TONE,0),(MIX,500)],160,src)
check(band(lp,110)>.15 and band(lp,5000)<.01,'LP at ~220 Hz passes 110 Hz and removes 5 kHz (%.3f / %.4f)'%(band(lp,110),band(lp,5000)))
_,hp=run([(MODE,7),(DRIVE,700),(TONE,0),(MIX,500)],160,src)
check(band(hp,5000)>.15 and band(hp,110)<.01,'HP at ~2.5 kHz passes 5 kHz and removes 110 Hz')
_,bp=run([(MODE,6),(DRIVE,525),(TONE,500),(MIX,500)],160,lambda b:mix(sine(110,.2,b),sine(760,.2,b),sine(9000,.2,b)))
check(band(bp,760)>2*band(bp,110) and band(bp,760)>2*band(bp,9000),'BP centred near 760 Hz favours it over 110 Hz and 9 kHz')
# DJ centre is exactly dry once faded in; left is low-pass, right high-pass.
u,dj=run([(MODE,8),(DRIVE,500),(TONE,350),(MIX,500)],80,src)
dry=[x for b in range(70,80) for x in src(b)]
check(tail(dj,10)==[struct.unpack('<f',struct.pack('<f',x))[0] for x in dry],'DJ centre is bit-exact dry')
_,djl=run([(MODE,8),(DRIVE,120),(TONE,350),(MIX,500)],160,src)
_,djr=run([(MODE,8),(DRIVE,880),(TONE,350),(MIX,500)],160,src)
check(band(djl,5000)<.02 and band(djl,110)>.15,'DJ left is a low-pass')
check(band(djr,110)<.02 and band(djr,5000)>.15,'DJ right is a high-pass')
# Envelope follower: loud bursts open a closed LP when Env is positive.
burst=lambda b:mix(sine(110,.35,b),sine(3000,.35,b)) if (b//20)%2==0 else [0.]*32
_,closed=run([(MODE,5),(DRIVE,300),(TONE,200),(MIX,500)],180,burst)
_,opened=run([(MODE,5),(DRIVE,300),(TONE,200),(MIX,1000)],180,burst)
check(band(opened,3000,10)>4*band(closed,3000,10)+.01,'positive Env opens the low-pass on loud input')
# Family changes fade through dry: drive -> filter -> filter type -> drive -> Off.
u,_,_=gsetup();running=[];jumps=0.;prev=None;finite=True
plan={0:[(MODE,1),(DRIVE,600)],150:[(MODE,5),(DRIVE,600),(TONE,300),(MIX,500)],300:[(MODE,8),(DRIVE,500)],450:[(MODE,3),(DRIVE,350),(TONE,650),(MIX,1000)],600:[(MODE,0)]}
for b in range(750):
 l=sine(220,.3,b);out,_=gblock(u,l,l,plan.get(b,[]))
 o=struct.unpack('<64f',out);finite&=all(math.isfinite(x) and abs(x)<2 for x in o)
 for x in o[:32]:
  if prev is not None:jumps=max(jumps,abs(x-prev))
  prev=x
 if b in (149,299,449,599,749):running.append(u32(u,EQ+0x1d0+64))
check(finite,'all family transitions stay finite and bounded')
check(running[:4]==[1,5,8,3] and u32(u,EQ+0x1d0+4)==0,'running algorithm follows CLIP, LP, DJ, FOLD, then Off is requested '+str(running))
check(jumps<.12,'no step larger than the 220 Hz signal slope across transitions (%.3f)'%jumps)
check(out==struct.pack('<64f',*(sine(220,.3,749)*2)) or tail([o],1)==[struct.unpack('<f',struct.pack('<f',x))[0] for x in sine(220,.3,749)],'Off after the transitions settles to exact dry')
u,_,_=gsetup()
for b in range(300):out,_=gblock(u,sine(330,.25,b),None,[(MODE,6),(DRIVE,600),(TONE,400),(MIX,500)] if b==0 else [(MODE,0)] if b==150 else [])
check(u32(u,EQ+0x1d0+64)==0 and struct.unpack('<32f',out[:128])==tuple(struct.unpack('<f',struct.pack('<f',x))[0] for x in sine(330,.25,299)),'a filter switched Off hands back to the drive state and ends exactly dry')
# Insert Off fades the running type to exact dry and keeps the chosen type;
# a type chosen while Off is the one that comes back with On.
def exact(xs):return tuple(struct.unpack('<f',struct.pack('<f',x))[0] for x in xs)
for first,later in ((1,1),(5,7),(3,8)):
 u,_,_=gsetup();outs={}
 plan={0:[(MODE,first),(DRIVE,700),(TONE,400),(MIX,800)],100:[(ON,0)],150:[(MODE,later),(DRIVE,600)],250:[(ON,1)]}
 for b in range(400):
  out,_=gblock(u,sine(220,.3,b),None,plan.get(b,[]));outs[b]=struct.unpack('<32f',out[:128])
  if b==249:off_state=(u32(u,EQ+0x1d0+4),u32(u,EQ+0x1d0+88),u32(u,EQ+0x1d0+92))
 check(outs[99]!=exact(sine(220,.3,99)),'insert is audible before Off '+str((first,later)))
 check(outs[249]==exact(sine(220,.3,249)) and off_state==(0,later,0),'Off settles to exact dry and keeps the chosen type '+str((first,later,off_state)))
 check(u32(u,EQ+0x1d0+4)==later and u32(u,EQ+0x1d0+64)==later and outs[399]!=exact(sine(220,.3,399)),'On brings back the type chosen while Off '+str((first,later)))
 jumps=max(abs(outs[b][i]-(outs[b][i-1] if i else outs[b-1][31])) for b in range(1,400) for i in range(32))
 check(jumps<.12,'Off/On transitions have no step larger than the signal slope (%.3f)'%jumps)
# Legacy SIN (2) from old projects plays as CLIP.
u,_,_=gsetup()
for b in range(40):gblock(u,sine(220,.3,b),None,[(MODE,2),(DRIVE,700)] if b==0 else [])
check(u32(u,EQ+0x1d0+4)==1 and u32(u,EQ+0x1d0+64)==1,'legacy SIN mode loads as CLIP')
del u;gc.collect()
# Native labels: nine ordinals, old ones kept.
u=machine();heap=[0xc0010000]
def alloc(q):
 a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,alloc);call(u,0x8134cb8,0)
meta=0xc0010000+28*0x1ba;choices=u32(u,meta+8)
names=[bytes(u.mem_read(u32(u,choices+4*i),20)).split(b'\0')[0].decode() for i in range(u32(u,meta+12))]
check(names==['CLIP','CLIP','CLIP','FOLD','WRAP','LP','BP','HP','DJ','SHFT','SPRD','RING','HARM','CHRD','BELL','SAT'],'native mode labels: 0 and 2 show CLIP, LP/BP/HP/DJ added (spectral F1: + SHFT/SPRD/RING)')
print(H.checks,'insert filter ARM checks passed; device sound and CPU remain hardware checks.')

"""Spectral F1 frequency shifter on executed ARM through the real native EQ
channel/queue: single-sideband up/down, SPRD's opposite directions, RING's two
sidebands, mono tracks, centre = 0 Hz, feedback bounded, family crossfades
with the drive and the filter, Insert On/Off back to exact dry, state inside
the enlarged 0x300-byte allocation and the cost per block."""
from gate_fixture import *
import firmware_harness as H
import gc,random
MODE,DRIVE,TONE,MIX,OUT=0x1ba,0x1bb,0x1bc,0x1bd,0x1be
ON=0x1d5;SHFT,SPRD,RING=9,10,11
def sine(hz,amp,b,phase=0.):
 return [amp*math.sin(2*math.pi*hz*((b*32+i)/48000.)+phase) for i in range(32)]
def exact(xs):return tuple(struct.unpack('<f',struct.pack('<f',x))[0] for x in xs)
FSH_LINK=0x38800ce0;BANK=0xc0400000;FSH=192
def link(u,bank=BANK):
 if bank:u.mem_write(bank,bytes(12*FSH))
 u.mem_write(FSH_LINK,struct.pack('<4I',0x46534831 if bank else 0,bank,12 if bank else 0,0))
def gs(**k):
 u,a,b=gsetup(**k);link(u);return u,a,b
def run(events,blocks,source,stereo=True,bank=BANK):
 u,_,_=gsetup();link(u,bank);outs=[]
 for b in range(blocks):
  l=source(b);out,ret=gblock(u,l,source(b) if stereo else None,events if b==0 else [])
  outs.append(struct.unpack('<64f',out))
 return u,outs
def band(outs,hz,ch=0,n=60):
 xs=[x for o in outs[-n:] for x in o[ch*32:ch*32+32]];start=(len(outs)-n)*32
 s=sum(x*math.sin(2*math.pi*hz*(start+i)/48000.) for i,x in enumerate(xs))
 c=sum(x*math.cos(2*math.pi*hz*(start+i)/48000.) for i,x in enumerate(xs))
 return 2*math.sqrt(s*s+c*c)/len(xs)
def knob(hz):
 u=math.log2(abs(hz)/.1)/(14.6/.98)+.02;k=round(500*u);return 500+k if hz>0 else 500-k
K=knob(200);HZ=.1*2**((abs(K-500)/500-.02)*14.6/.98)
src=lambda b:sine(1000,.3,b)
# Fully wet SHFT moves 1 kHz up by the knob's shift; the lower sideband and
# the original stay far down.
_,up=run([(MODE,SHFT),(DRIVE,K),(TONE,0),(MIX,1000)],200,src)
w,uw,o=band(up,1000+HZ),band(up,1000-HZ),band(up,1000)
check(w>.27 and uw<w*.02 and o<w*.02,'SHFT up: 1 kHz -> +%.0f Hz, wanted %.3f, other sideband %.4f, original %.4f'%(HZ,w,uw,o))
check(abs(band(up,1000+HZ,1)-w)<.01,'stereo SHFT shifts both channels the same way')
_,down=run([(MODE,SHFT),(DRIVE,1000-K),(TONE,0),(MIX,1000)],200,src)
check(band(down,1000-HZ)>.27 and band(down,1000+HZ)<.006,'SHFT left of centre shifts down')
_,spread=run([(MODE,SPRD),(DRIVE,K),(TONE,0),(MIX,1000)],200,src)
check(band(spread,1000+HZ,0)>.27 and band(spread,1000-HZ,1)>.27 and band(spread,1000+HZ,1)<.006,'SPRD: left up, right down')
_,ring=run([(MODE,RING),(DRIVE,1000-K),(TONE,0),(MIX,1000)],200,src)
check(band(ring,1000+HZ)>.12 and band(ring,1000-HZ)>.12 and band(ring,1000)<.01,'RING gives both sidebands, either knob side')
_,mono=run([(MODE,SPRD),(DRIVE,K),(TONE,0),(MIX,1000)],200,src,stereo=False)
check(band(mono,1000+HZ)>.27 and band(mono,1000-HZ)<.006,'a mono track plays SPRD as a plain upward shift')
# Centre is 0 Hz: level kept (only the allpass phase differs); half mix gives
# dry + shifted, the classic phaser-like beating.
_,centre=run([(MODE,SHFT),(DRIVE,500),(TONE,0),(MIX,1000)],200,src)
check(abs(band(centre,1000)-.3)<.01,'centre knob is 0 Hz at unity level (%.3f)'%band(centre,1000))
_,half=run([(MODE,SHFT),(DRIVE,K),(TONE,0),(MIX,500)],200,src)
check(abs(band(half,1000)-.15)<.01 and abs(band(half,1000+HZ)-.15)<.01,'Mix 50%: half dry, half shifted')
# F2: a DC offset on the input (VA's default patch has one) must not become
# a steady tone at the shift frequency.
_,dcin=run([(MODE,SHFT),(DRIVE,K),(TONE,0),(MIX,1000)],300,lambda b:[x+.2 for x in sine(1000,.3,b)])
check(band(dcin,HZ)<.004 and band(dcin,1000+HZ)>.27,'DC offset is blocked before the shifter (tone at the shift %.4f)'%band(dcin,HZ))
# Feedback: bounded and finite on loud noise at full feedback, all types.
rng=random.Random(9)
for mode in (SHFT,SPRD,RING):
 u,_,_=gs();peak=0.;finite=True
 for b in range(600):
  l=[rng.uniform(-1,1) for _ in range(32)];r=[rng.uniform(-1,1) for _ in range(32)]
  out,_=gblock(u,l,r,[(MODE,mode),(DRIVE,620),(TONE,1000),(MIX,1000)] if b==0 else [])
  o=struct.unpack('<64f',out);finite&=all(math.isfinite(x) for x in o);peak=max(peak,max(abs(x) for x in o))
 check(finite and peak<2.,'full feedback on full-scale noise stays finite and under the knee (%d: %.3f)'%(mode,peak))
_,fb=run([(MODE,SHFT),(DRIVE,K),(TONE,800),(MIX,1000)],300,src)
check(band(fb,1000+2*HZ)>.02,'feedback re-shifts the shifted signal (1 kHz + 2x shift present)')
# Family changes fade through dry: drive -> shifter -> filter -> shifter type -> Off.
u,_,_=gs();running=[];jumps=0.;prev=None;finite=True
plan={0:[(MODE,1),(DRIVE,600)],150:[(MODE,SHFT),(DRIVE,K),(TONE,300),(MIX,700)],300:[(MODE,5),(DRIVE,600),(TONE,300),(MIX,500)],
      450:[(MODE,RING),(DRIVE,K),(TONE,0),(MIX,700)],600:[(MODE,SPRD)],750:[(MODE,0)]}
for b in range(900):
 l=sine(220,.3,b);out,_=gblock(u,l,l,plan.get(b,[]))
 o=struct.unpack('<64f',out);finite&=all(math.isfinite(x) and abs(x)<2 for x in o)
 for x in o[:32]:
  if prev is not None:jumps=max(jumps,abs(x-prev))
  prev=x
 if b in (149,299,449,599,749,899):running.append(u32(u,EQ+0x1d0+64))
check(finite,'all family transitions with the shifter stay finite and bounded')
check(running[:5]==[1,SHFT,5,RING,SPRD] and u32(u,EQ+0x1d0+4)==0 and running[5]==0,'running algorithm follows CLIP, SHFT, LP, RING, SPRD, then Off '+str(running))
check(jumps<.12,'no step larger than the 220 Hz signal slope across transitions (%.3f)'%jumps)
check(exact(struct.unpack('<32f',out[:128]))==exact(sine(220,.3,899)),'Off after the shifter settles to exact dry')
# Insert Off fades the shifter to exact dry; On brings it back.
u,_,_=gs();outs={}
plan={0:[(MODE,SHFT),(DRIVE,K),(TONE,500),(MIX,1000)],100:[(ON,0)],250:[(ON,1)]}
for b in range(400):
 out,_=gblock(u,sine(220,.3,b),None,plan.get(b,[]));outs[b]=struct.unpack('<32f',out[:128])
check(outs[99]!=exact(sine(220,.3,99)) and outs[249]==exact(sine(220,.3,249)) and outs[399]!=exact(sine(220,.3,399)),'INSRT Off is exact dry, On brings the shifter back')
jumps=max(abs(outs[b][i]-(outs[b][i-1] if i else outs[b-1][31])) for b in range(1,400) for i in range(32))
check(jumps<.15,'Off/On transitions have no step larger than the signal slope (%.3f)'%jumps)
# F3: the state is the track's slot in the SDRAM bank; the EQ object keeps
# S28's 0x240 bytes and nothing past it is touched.
u,_,_=gsetup(ch=3);link(u);u.mem_write(EQ+0x240,b'GUARD123')
for b in range(60):gblock(u,sine(440,.3,b),sine(660,.3,b),[(MODE,SPRD),(DRIVE,K),(TONE,900),(MIX,1000)] if b==0 else [],ch=3)
st=struct.unpack('<48f',u.mem_read(BANK+3*FSH,FSH))
others=bytes(u.mem_read(BANK,3*FSH))+bytes(u.mem_read(BANK+4*FSH,8*FSH))
check(bytes(u.mem_read(EQ+0x240,8))==b'GUARD123' and any(x!=0 for x in st[:32]) and all(math.isfinite(x) for x in st) and others==bytes(11*FSH),'track 4 uses its own bank slot; other slots and the EQ object end are untouched')
# No bank (arena full): the shifter types are exact dry, nothing crashes.
_,nob=run([(MODE,SHFT),(DRIVE,K),(TONE,500),(MIX,1000)],80,src,bank=0)
check(all(exact(o[:32])==exact(src(b)) for b,o in enumerate(nob)),'without a shifter bank the shifter plays exact dry')
# The real delay constructor allocates and initialises the bank at boot.
from delay_fixture import dsetup
d=dsetup()[0]
magic,bank,tracks,_=struct.unpack('<4I',d.mem_read(FSH_LINK,16))
check(magic==0x46534831 and tracks==12 and 0xc0000000<=bank<0xc2000000 and bank%16==0,'delay constructor links a 12-track shifter bank from the SDRAM arena '+hex(bank))
check(all(struct.unpack('<2f',d.mem_read(bank+ch*FSH+144,8))==(1.,0.) for ch in range(12)),'every bank slot starts initialised (phasor 1, 0)')
# Cost of one steady stereo block, from the native process call.
for stereo in (False,True):
 for mode in (SHFT,RING):
  u,_,_=gs()
  for b in range(60):gblock(u,sine(440,.3,b),sine(660,.3,b) if stereo else None,[(MODE,mode),(DRIVE,K),(TONE,300),(MIX,700)] if b==0 else [])
  count=[0];h=u.hook_add(UC_HOOK_CODE,lambda q,a,n,_:count.__setitem__(0,count[0]+1))
  gblock(u,sine(440,.3,60),sine(660,.3,60) if stereo else None,[])
  u.hook_del(h)
  print('INFO native EQ+insert block, %s %s: %d instructions'%('stereo' if stereo else 'mono',{SHFT:'SHFT',RING:'RING'}[mode],count[0]))
  check(count[0]<(7000 if stereo else 4200),'shifter block cost stays near the bench (%d)'%count[0])
# F4: resonator bank (HARM / CHRD / BELL, modes 12-14) in the same bank slot.
HARM,CHRD,BELL=12,13,14
def note_knob(n):return round((n-24)*1000/72)
ROOT=440*2**((36-69)/12) # C2
imp=lambda b:[.8 if b%400==2 and i==0 else 0. for i in range(32)]
for mode,ratios in ((HARM,[1,2,3,4,5,6,7,8]),(CHRD,[1,1.189207,1.498307,1.781797,2,2.244924,2.996614,4]),(BELL,[.5,1,1.189207,1.498307,2,2.514,2.662,3.011])):
 R4=440*2**((60-69)/12) # C4: partials far enough apart for a 0.8 s window
 _,o=run([(MODE,mode),(DRIVE,note_knob(60)),(TONE,800),(MIX,1000)],900,imp)
 def amp(hz):return band(o,hz,0,750)+band(o,hz,1,750)
 # Each partial against points 3% either side (1.25 Hz bins).
 rows=[(k,amp(R4*k),max(amp(R4*k*.97),amp(R4*k*1.03))) for k in ratios]
 check(all(at>2*off for k,at,off in rows),'resonator type %d rings every partial at C4 x ratio on executed ARM %s'%(mode,[(round(k,2),round(at/max(off,1e-9),1)) for k,at,off in rows]))
# F6: CHRD's chord (0x1de) picks the partials; MIN9 (0) is the row above.
CHORD=0x1de
CHORDS={1:[0,4,7,11,12,14,19,24],2:[0,3,7,10,12,15,19,22],3:[0,4,7,11,12,16,19,23],4:[0,2,5,7,12,14,17,19],
        5:[0,7,12,19,24,31,36,43],6:[0,5,10,15,20,25,30,35],7:[0,3,6,9,12,15,18,21]}
for c,semis in CHORDS.items():
 R4=440*2**((60-69)/12)
 _,o=run([(CHORD,c),(MODE,CHRD),(DRIVE,note_knob(60)),(TONE,800),(MIX,1000)],900,imp)
 def amp(hz):return band(o,hz,0,750)+band(o,hz,1,750)
 rows=[(n,amp(R4*2**(n/12)),max(amp(R4*2**(n/12)*.97),amp(R4*2**(n/12)*1.03))) for n in semis]
 check(all(at>2*off for n,at,off in rows),'CHRD chord %d rings its semitones %s over C4 on executed ARM %s'%(c,semis,[(n,round(at/max(off,1e-9),1)) for n,at,off in rows]))
_,h0=run([(MODE,HARM),(DRIVE,note_knob(48)),(TONE,600),(MIX,1000)],120,imp)
_,h5=run([(CHORD,5),(MODE,HARM),(DRIVE,note_knob(48)),(TONE,600),(MIX,1000)],120,imp)
check(h0==h5,'the chord only affects CHRD: HARM is bit-identical with any chord')
_,o=run([(MODE,HARM),(DRIVE,note_knob(36)),(TONE,700),(MIX,1000)],200,imp)
check(any(abs(a-b)>1e-4 for x in o[-60:] for a,b in zip(x[:32],x[32:])),'resonator output is stereo (alternate partials panned)')
_,mono=run([(MODE,HARM),(DRIVE,note_knob(48)),(TONE,600),(MIX,1000)],200,imp,stereo=False)
check(any(abs(x)>1e-4 for o in mono[-100:] for x in o[:32]),'resonator runs on a mono track')
_,nob=run([(MODE,CHRD),(DRIVE,note_knob(36)),(TONE,600),(MIX,1000)],80,src,bank=0)
check(all(exact(o[:32])==exact(src(b)) for b,o in enumerate(nob)),'without the bank the resonator plays exact dry')
for mode in (HARM,CHRD,BELL):
 u,_,_=gs();peak=0.;finite=True;ph=0.
 for b in range(400):
  l=[]
  for i in range(32):ph+=ROOT/48000.;l.append(1. if ph%1<.5 else -1.)
  out,_=gblock(u,l,l,[(MODE,mode),(DRIVE,note_knob(36)),(TONE,1000),(MIX,1000),(OUT,6000)] if b==0 else [])
  x=struct.unpack('<64f',out);finite&=all(math.isfinite(v) for v in x);peak=max(peak,max(abs(v) for v in x))
 check(finite and peak<=2.,'full-scale square on the root, 12 s decay, +6 dB: finite, peak %.3f (%d)'%(peak,mode))
# F5 crackle regression (F4 crackled on hardware at full wet on sustained /
# distorted low input: its instant-attack limiter stepped its gain 30-46 dB
# deep). Distorted saw bass and a boomy kick, 12 s decay, 100% wet: the
# limiter gain (bank slot +176) moves smoothly and the output has no
# second-difference spikes beyond what smooth resonances give.
def crackle(mode,material,pitch):
 u,_,_=gs();prev=[0.,0.];d2=[];gains=[];ph=[0.,0.]
 for b in range(600):
  l=[]
  for i in range(32):
   t=(b*32+i)/48000.
   if material=='bass':ph[0]=(ph[0]+55/48000.)%1;l.append(.6*math.tanh(4*(2*ph[0]-1)))
   else:
    tb=t%.5;ph[1]+=(45+120*math.exp(-tb*35))/48000.;l.append(.95*math.tanh(1.5*math.exp(-tb*6)*math.sin(2*math.pi*ph[1])))
  out,_=gblock(u,l,l,[(MODE,mode),(DRIVE,pitch),(TONE,1000),(MIX,1000)] if b==0 else [])
  o=struct.unpack('<32f',out[:128])
  for x in o:
   if b>=150:d2.append(abs(x-2*prev[0]+prev[1]))
   prev=[x,prev[0]]
  if b>=150:gains.append(struct.unpack('<f',u.mem_read(BANK+176,4))[0])
 rms=math.sqrt(sum(x*x for x in d2)/len(d2));steps=max(abs(gains[i]-gains[i-1]) for i in range(1,len(gains)))
 return max(d2)/rms,steps,min(gains)
for mode in (HARM,CHRD,BELL):
 for material,pitch in (('bass',note_knob(33)),('kick',note_knob(24))):
  spike,steps,gmin=crackle(mode,material,pitch)
  check(spike<15 and steps<.2,'no limiter crackle at full wet: %s, type %d, spike %.1f x RMS, largest gain move per block %.3f, min gain %.3f'%(material,mode,spike,steps,gmin))
# Shifter <-> resonator in the same slot: fade through dry, re-initialised.
u,_,_=gs();running=[];prev=None;jumps=0.;finite=True
plan={0:[(MODE,SHFT),(DRIVE,K),(TONE,300),(MIX,700)],150:[(MODE,HARM),(DRIVE,note_knob(45)),(TONE,500),(MIX,600)],300:[(MODE,SPRD),(DRIVE,K)],450:[(MODE,BELL),(DRIVE,note_knob(57))],600:[(ON,0)]}
for b in range(750):
 l=sine(220,.3,b);out,_=gblock(u,l,l,plan.get(b,[]))
 o=struct.unpack('<64f',out);finite&=all(math.isfinite(x) and abs(x)<2 for x in o)
 for x in o[:32]:
  if prev is not None:jumps=max(jumps,abs(x-prev))
  prev=x
 if b in (149,299,449,599):running.append(u32(u,EQ+0x1d0+64))
check(finite and running==[SHFT,HARM,SPRD,BELL],'SHFT -> HARM -> SPRD -> BELL run in turn '+str(running))
check(jumps<.15,'no step larger than the signal slope across shifter / resonator changes (%.3f)'%jumps)
check(exact(struct.unpack('<32f',out[:128]))==exact(sine(220,.3,749)),'INSRT Off after the resonator is exact dry')
for stereo in (False,True):
 u,_,_=gs()
 for b in range(60):gblock(u,sine(440,.3,b),sine(660,.3,b) if stereo else None,[(MODE,CHRD),(DRIVE,note_knob(40)),(TONE,600),(MIX,700)] if b==0 else [])
 count=[0];h=u.hook_add(UC_HOOK_CODE,lambda q,a,n,_:count.__setitem__(0,count[0]+1))
 gblock(u,sine(440,.3,60),sine(660,.3,60) if stereo else None,[])
 u.hook_del(h)
 print('INFO native EQ+insert block, %s CHRD: %d instructions'%('stereo' if stereo else 'mono',count[0]))
 check(count[0]<(5500 if stereo else 5000),'resonator block cost stays near the bench (%d)'%count[0])
# SD4: on silence, once the tail has decayed, SPEC sleeps (silence passes,
# the insert costs about a drive at rest); sound wakes it.
z=[0.]*32
for mode,tone in ((CHRD,200),(SHFT,300)):
 u,_,_=gs()
 for b in range(60):gblock(u,sine(440,.3,b),sine(660,.3,b),[(MODE,mode),(DRIVE,note_knob(40) if mode==CHRD else K),(TONE,tone),(MIX,700)] if b==0 else [])
 ring=0.
 for b in range(600):
  out,_=gblock(u,z,z,[])
  if b==2:ring=max(abs(x) for x in struct.unpack('<64f',out))
 count=[0];h=u.hook_add(UC_HOOK_CODE,lambda q,a,n,_:count.__setitem__(0,count[0]+1))
 out,_=gblock(u,z,z,[])
 u.hook_del(h);silent=struct.unpack('<64f',out)
 name={CHRD:'CHRD',SHFT:'SHFT'}[mode]
 print('INFO native EQ+insert block, stereo %s asleep on silence: %d instructions'%(name,count[0]))
 check(ring>1e-4 and all(x==0. for x in silent) and count[0]<1500,'%s rings on after the input stops, then sleeps on silence (%d instructions)'%(name,count[0]))
 out,_=gblock(u,sine(440,.3,0),sine(660,.3,0),[])
 check(any(abs(a-b)>1e-3 for a,b in zip(struct.unpack('<32f',out[:128]),sine(440,.3,0))),'%s wakes on sound (wet on the first block)'%name)
del u;gc.collect()
print(H.checks,'frequency shifter and resonator ARM checks passed; device sound and CPU remain hardware checks.')

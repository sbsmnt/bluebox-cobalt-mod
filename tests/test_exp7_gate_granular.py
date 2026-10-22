"""exp.7 against the pinned exp.6 release: Gate encoder acceleration, MIXER
Gate meters on the Gate fader's scale, and Granular power normalization for
decorrelated grains. Emulator checks; feel, display and sound need hardware.
"""
import sys
from gate_ui_fixture import *
import gate_fixture as GF
import delay_fixture as D
import firmware_harness as H
import importlib.util,json
folder=ROOT/'tests/references/tape-exp6'
check(hashlib.sha256((folder/'patch_bluebox.py').read_bytes()).hexdigest()=='7dcdb3489a286167e8000493c3c7202386aca5bb63f596a37ba03d4c5a31acaf','comparison patcher is the pinned exp.6 release')
spec=importlib.util.spec_from_file_location('exp6',folder/'patch_bluebox.py');p6=importlib.util.module_from_spec(spec);spec.loader.exec_module(p6)
ref=p6.transform(stock)
check(hashlib.sha256(ref).hexdigest()=='4a8980a19158acb3473cb08f5a6b8bda1e3641f3ab5dc46cc2c070bfc1586acb','exp.6 reference image rebuilt from stock')
report={}

# ---- 1. Gate encoder acceleration (real native knob, TRACK and MIXER widgets).
def turn_at(w,delta,ms):
 clock[0]+=ms;w32(u,0x3001eb3c,clock[0]);return call(u,S['gate_turn'],w,delta&0xffffffff)
setup(1,0,-60000);k=P+0xbe0+0x8f0;t=T+0xde48
# Slow travel (one event per second) keeps exp.6's fine behavior exactly.
# Native fine travel: 80 units per 0.1 dB above -48 dB, 160 below.
for v,delta,expect in ((-60000,80,-60000),(-60000,80,-59900),(-20000,80,-19900)):
 if v!=val(k):setup(1,0,v);w32(u,0x38800a40+8,0)
 turn_at(k,delta,1000);flush()
 check(val(k)==expect,'slow turn keeps fine travel: %d +%d units -> %d'%(v,delta,expect))
# Slow but continuous turning (1 unit/ms, below the native 2 units/ms) stays fine.
setup(1,0,-30000);w32(u,0x38800a40+8,0)
for i in range(20):turn_at(k,16,16)
flush();slow=val(k)
check(slow==-29600,'slow continuous turning (1 unit/ms, 320 units) keeps unaccelerated fine travel: %d'%slow)
# Fast turning sweeps the range quickly and lands exactly on the endpoints.
def sweep(start,delta,ms,limit=200):
 setup(1,0,start);w32(u,0x38800a40+8,0);n=0
 target=-12000 if delta>0 else -96000
 while val(k)!=target and n<limit:turn_at(k,delta,ms);flush();n+=1
 return n,val(k)
n_up,v_up=sweep(-96000,160,10)
n_down,v_down=sweep(-12000,-160,10)
print('INFO fast sweep events (16 units/ms): up',n_up,'down',n_down)
check(v_up==-12000 and n_up<=40,'fast turn sweeps Off to -12 dB in at most 40 events (%d)'%n_up)
check(v_down==-96000 and n_down<=40,'fast turn sweeps -12 dB to a single Off endpoint in at most 40 events (%d)'%n_down)
turn_at(k,-160,10);turn_at(k,-160,10);flush()
check(val(k)==-96000,'fast turning past Off stays at the single Off endpoint')
n_mid,_=sweep(-96000,48,16,limit=1000)
print('INFO medium sweep events (3 units/ms):',n_mid)
check(n_up<n_mid<1000,'medium speed accelerates less than fast speed')
# Direction reversal restarts slow, so a fast spin can be corrected finely.
setup(1,0,-50000);w32(u,0x38800a40+8,0)
for i in range(8):turn_at(k,160,10)
flush();top=val(k);turn_at(k,-160,10);flush()
check(top-val(k)<=400,'first detent after reversing drops our multiplier; only the native 2x history remains (moved %d mdB)'%(top-val(k)))
# MIXER and TRACK share the wrapper; the TRACK knob follows the same value.
check(val(t)==val(k),'TRACK knob mirrors the accelerated MIXER value')
report['encoder']=dict(fast_up_events=n_up,fast_down_events=n_down,medium_up_events=n_mid)

# ---- 2. MIXER Gate meters on the Gate fader's scale (real stock dB lookup).
g,_,_=GF.gsetup()
pub=[]
for addr,n in ((0x804bab0,1),(0x804bacc,2)):
 GF.stub(g,addr,lambda q,n=n:pub.append([struct.unpack('<f',struct.pack('<I',q.reg_read(r)))[0] for r in (UC_ARM_REG_S0,UC_ARM_REG_S1)[:n]]))
mix=0x24048000;GS7=M['symbols']['gate7']
w32(g,0x38800400,0x4e473937)
def publish(pid,ch,peak_l,peak_r,stereo):
 g.mem_write(mix+0xe6c,struct.pack('<H',pid))
 g.mem_write(0x38800400+4+12*40+ch*8,struct.pack('<2f',peak_l,peak_r))
 for r,v in zip((UC_ARM_REG_S0,UC_ARM_REG_S1),(-33.,-34.)):g.reg_write(r,struct.unpack('<I',struct.pack('<f',v))[0])
 if stereo:call(g,GS7['gate_meter_stereo'],0x2404a000,ch,mix)
 else:call(g,GS7['gate_meter_mono'],0x2404a000,ch,mix)
 return pub[-1]
exact=True;checked=0
for p_db in (-95.,-90.,-72.,-60.,-48.,-36.,-24.,-13.,-12.5):
 amp=10**(p_db/20)
 out=publish(0x1ae,3,amp,amp*.5,True)
 fader=(p_db+96.)/84.
 meter=[(x+60.)/60. for x in out]
 r_db=p_db+20*math.log10(.5);rf=(r_db+96.)/84.
 exact&=abs(meter[0]-fader)<2e-3 and abs(meter[1]-max(0.,rf))<2e-3;checked+=1
check(exact,'Gate-selected MIXER meter height equals the Gate fader position for the same dB (%d levels, stereo)'%checked)
top=publish(0x1ae,3,1.,1.,True)
check(all(-1e-5<x<0. for x in top),'input at or above -12 dB fills the meter without the native >=0 dB clip branch')
low=publish(0x1ae,3,0.,0.,True)
check(all(x==-60. for x in low),'silence publishes exactly -60 dB (empty meter)')
mono=publish(0x1ae,2,10**(-48/20),0.,False)
check(abs((mono[0]+60)/60-(48./84.))<2e-3,'mono Gate meter uses the same mapping')
check(publish(4,3,.5,.5,True)==[-33.,-34.],'Vol selection publishes native values unchanged')
check(publish(0x1ae,12,.5,.5,True)==[-33.,-34.],'MAIN (channel 12) publishes native values unchanged')
for at in (0x804f8be,0x804f9c6,0x804f9e0):
 check(any(p['name']=='Gate input meter '+hex(at) for p in M['patches']),'MIXER meter publication routed through the Gate wrapper '+hex(at))

# ---- 3. Granular: coherent settings unchanged; decorrelated louder; loop gain unchanged.
def granular_pair():
 a=D.dsetup(ref)[0];b=D.dsetup()[0]
 for q in (a,b):D.dblock(q,D.ZERO,evs=D.INITIAL);D.steady(q)
 return a,b
def run(a,b,blocks,evs0,amp=.3):
 ea=eb=0.;same_pcm=True;same_state=True
 for n in range(blocks):
  ev=evs0 if n==0 else []
  oa,_=D.dblock(a,signal(amp,n,480),signal(amp*.7,n,733),ev);ob,_=D.dblock(b,signal(amp,n,480),signal(amp*.7,n,733),ev)
  same_pcm&=oa==ob;same_state&=a.mem_read(0x38800000,168)==b.mem_read(0x38800000,168)
  if n>=blocks//2:
   ea+=sum(x*x for x in struct.unpack('<64f',oa));eb+=sum(x*x for x in struct.unpack('<64f',ob))
 return same_pcm,same_state,10*math.log10(eb/ea)
a,b=granular_pair()
same_pcm,same_state,_=run(a,b,900,[(0x1a0,1),(0x1a3,4),(0x1a2,80),(0x39,700),(0x1a4,0),(0x4a,0)])
check(same_pcm and same_state,'jitter 0 / pitch 0 Granular is bit-identical to exp.6 (PCM and grain state)')
levels={}
for jit in (100,500,750):
 a,b=granular_pair()
 _,same_state,gain=run(a,b,1200,[(0x1a0,1),(0x1a3,4),(0x1a2,80),(0x39,0),(0x1a4,jit)])
 levels[jit]=gain
 print('INFO density 4 jitter %d%%: exp.7 wet level vs exp.6 %+.2f dB'%(jit//10,gain))
 check(same_state and 2.<gain<7.,'jitter %d%%: grain/feedback state identical, wet louder by 2..7 dB (%.2f)'%(jit//10,gain))
a,b=granular_pair()
_,same_state,gain=run(a,b,1200,[(0x1a0,1),(0x1a3,4),(0x1a2,80),(0x39,700),(0x1a4,500),(0x4a,7)])
print('INFO density 4 jitter 50%% pitch +7 feedback 70%%: %+.2f dB'%gain)
check(same_state,'with feedback 70% the grain, capture and feedback state stay bit-identical (loop gain unchanged)')
levels['j50_p7_fb70']=gain
# The blend glides: 20 ms from 0 to 1 after jitter is enabled.
b=D.dsetup()[0];D.dblock(b,D.ZERO,evs=D.INITIAL);D.steady(b)
for n in range(200):D.dblock(b,signal(.3,n,480),evs=[(0x1a0,1),(0x1a3,4)] if n==0 else [])
D.dblock(b,signal(.3,201,480),evs=[(0x1a4,500)])
d1=struct.unpack('<f',bytes(b.mem_read(0x38800000+168,4)))[0]
for n in range(40):D.dblock(b,signal(.3,202+n,480))
d2=struct.unpack('<f',bytes(b.mem_read(0x38800000+168,4)))[0]
check(0.<d1<.05 and d2==1.,'normalization blend ramps (%.4f after one block) and settles at 1 within 30 blocks'%d1)
report['granular_wet_gain_db_vs_exp6']=levels
(ROOT/'tests/exp7-gate-granular.json').write_text(json.dumps(dict(version=M['version'],baseline='1.0.0-tape.exp.6',**report,hardware_tested=False),indent=2)+'\n')
print(H.checks,'exp.7 checks passed; encoder feel, meter display and sound need hardware.')

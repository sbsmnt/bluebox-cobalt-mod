"""DJ EQ on the M7: the compiled module runs under Unicorn on real 32-frame
blocks. Only the delay constructor it chains after and the gate/native EQ
body (which ends by tail-calling obj+8, as the stock EQ does) are stubbed.
Checks: exact bypass, flat allpass when active, kills, boosts, the EQ curve,
the filter, click-free transitions, settings events and node chaining."""
from firmware_harness import *
import firmware_harness as H
D=M['symbols']['dj7'];LINK=0x38800110;OBJ=0xc0000000;VT=OBJ+0x100;NEXTN=0xc0000200;NVT=NEXTN+0x40
BUS=CTX+0x150e4+5*20;L=0x24020000;R=0x24020100;ARENA=0xc0100000

u=machine(patched,double=True)
w32(u,0x24000000,0xc3000000);w32(u,0x24000004,ARENA)          # bump allocator cursor/limit
def alloc(u):
 a=u32(u,0x24000004);w32(u,0x24000004,a+u.reg_read(UC_ARM_REG_R0));u.reg_write(UC_ARM_REG_R0,a)
stub(u,0x80412a4,alloc);stub(u,D['dly_construct']&~1,lambda u:u.reg_write(UC_ARM_REG_R0,7))
check(call(u,D['dj_construct'],0x24060000)==7,'construct chains to the delay constructor and returns its result')
check(u32(u,LINK)==0x444a4531 and u32(u,LINK+4)==ARENA,'DJ state taken from the SDRAM bump allocator, linked in backup SRAM')

# Channel 5 EQ node; its port returns bus 5. The stubbed gate/EQ body tail-calls obj+8.
w32(u,OBJ,VT);w32(u,VT+0x54,0x24050001);u.mem_write(0x24050000,bytes.fromhex('0520'+'7047'))  # movs r0,#5; bx lr
w32(u,OBJ+0x18,5)
nexts=[];w32(u,NEXTN,NVT);w32(u,NVT+12,0x24050011);u.mem_write(0x24050010,bytes.fromhex('0020'+'7047'))
def next_node(u,a,s,d):
 nexts.append((u.reg_read(UC_ARM_REG_R0),bytes(u.mem_read(L,128)),bytes(u.mem_read(R,128))))
u.hook_add(UC_HOOK_CODE,next_node,begin=0x24050010,end=0x24050010)
w32(u,OBJ+8,NEXTN)
def gate_body(u):
 nxt=u32(u,OBJ+8);u.reg_write(UC_ARM_REG_R0,nxt);u.reg_write(UC_ARM_REG_PC,u32(u,u32(u,nxt)+12))
gh=u.hook_add(UC_HOOK_CODE,lambda u,a,s,d:gate_body(u),begin=D['gate_process']&~1,end=D['gate_process']&~1)

def events(ch,evs,target=None):
 q=CTX+ch*0x604+4
 for i,(pid,v) in enumerate(evs):u.mem_write(q+i*24,struct.pack('<B7xIHHiI',0x39,ch if target is None else target,pid,0,v,0))
 w32(u,q+0x600,len(evs))
def block(l,r=None,evs=(),silent=False):
 events(5,evs)
 u.mem_write(BUS,struct.pack('<IIIIBBxx',32,32,L,R,int(silent),int(r is not None)))
 u.mem_write(L,struct.pack('<32f',*l));u.mem_write(R,struct.pack('<32f',*(r if r is not None else [0.]*32)))
 nexts.clear();call(u,D['dj_process_node'],OBJ,CTX)
 check_chain=len(nexts)==1 and nexts[0][0]==NEXTN and u32(u,OBJ+8)==NEXTN
 if not check_chain:raise AssertionError('chain broken')
 return list(struct.unpack('<32f',nexts[0][1])),list(struct.unpack('<32f',nexts[0][2]))
IDS=dict(high=0x1ba,himid=0x1bb,lomid=0x1bc,low=0x1bd,filt=0x1be,res=0x1bf)
def setp(**kw):return [(IDS[k],v) for k,v in kw.items()]
def tone(hz,blocks,amp=.5,start=0):
 return [[amp*math.sin(2*math.pi*hz*(start+b*32+i)/48000) for i in range(32)] for b in range(blocks)]
state={'t':0}
def run(hz,blocks,evs=()):
 out=[];first=True
 for b in tone(hz,blocks,start=state['t']):
  l,r=block(b,b,evs if first else ());first=False;out+=l
 state['t']+=blocks*32;return out
def level_db(hz,evs=(),settle=60,measure=40):
 run(hz,settle,evs);y=run(hz,measure)
 period=48000/hz;y=y[:max(1,int(int(len(y)/period)*period))]  # whole periods only
 rms=math.sqrt(sum(v*v for v in y)/len(y));return 20*math.log10(max(rms,1e-12)/(.5/math.sqrt(2)))

# Exact bypass at neutral; buffers are passed through untouched.
x=tone(1000,1)[0];l,r=block(x,x)
check(l==list(struct.unpack('<32f',struct.pack('<32f',*x))),'neutral: exact bypass, next node called once, obj+8 restored')
l,r=block(x,None);check(l==list(struct.unpack('<32f',struct.pack('<32f',*x))),'neutral mono bus: exact bypass')

# Active but flat: four bands at 0 dB sum to an allpass (ISO, LR4).
flat=[level_db(f,setp(high=1)) for f in (60,300,1000,3000,12000)]
check(all(abs(v)<.08 for v in flat),'ISO, bands at 0 dB except High +0.06 dB: flat within 0.08 dB from 60 Hz to 12 kHz '+str([round(v,3) for v in flat]))
lows=level_db(50,setp(high=0,low=-100))
mids=level_db(3000)
check(lows<-40 and abs(mids)<.5,'ISO: Low kill removes 50 Hz (%.1f dB), 3 kHz untouched (%.2f dB)'%(lows,mids))
highs=level_db(12000,setp(low=0,high=-100))
check(highs<-35,'ISO: High kill at 12 kHz, 1.3 octaves above the 5 kHz crossover: %.1f dB'%highs)
lm=level_db(500,setp(high=0,lomid=-100));hm=level_db(2500,setp(lomid=0,himid=-100))
check(lm<-20 and hm<-20,'ISO (2.3-octave mid bands): Lo Mid kill at 500 Hz (%.1f dB), Hi Mid kill at 2.5 kHz (%.1f dB)'%(lm,hm))
boost=level_db(60,setp(himid=0,low=100))
check(abs(boost-6)<.3,'ISO: Low +6 dB boosts 60 Hz by %.2f dB'%boost)

# EQ curve (global, from the Main node): LR2, no kill at the minimum.
events(12,[(0x1c0,1)]);w32(u,OBJ+0x18,12);call(u,D['dj_process_node'],OBJ,CTX);w32(u,OBJ+0x18,5);events(12,[])
eq_low=level_db(40,setp(low=-100))
check(-30<eq_low<-15,'EQ curve: Low at minimum attenuates 40 Hz by %.1f dB, no kill'%eq_low)
events(12,[(0x1c0,0)]);w32(u,OBJ+0x18,12);call(u,D['dj_process_node'],OBJ,CTX);w32(u,OBJ+0x18,5);events(12,[])
run(1000,200,setp(low=0))

# Filter: left low-pass, right high-pass.
lp=level_db(8000,setp(filt=-100),settle=120);lp_pass=level_db(25)
check(lp<-30 and lp_pass>-3,'Filter full left (40 Hz low-pass): 8 kHz %.1f dB, 25 Hz %.1f dB'%(lp,lp_pass))
hp=level_db(60,setp(filt=100),settle=120);hp_pass=level_db(15000)
check(hp<-30 and hp_pass>-3,'Filter full right (10 kHz high-pass): 60 Hz %.1f dB, 15 kHz %.1f dB'%(hp,hp_pass))
# Filter -37 (x10 in the M7) puts the low-pass corner at 2 kHz.
res=level_db(2000,setp(filt=-37,res=1000),settle=120);nores=level_db(2000,setp(res=0),settle=120)
check(res>nores+6,'Res raises the 2 kHz corner: %.1f dB vs %.1f dB without'%(res,nores))
run(1000,250,setp(filt=0,res=0))

# Click-free: steps in a sine output stay near the sine's own slope.
def max_step(y):return max(abs(y[i+1]-y[i]) for i in range(len(y)-1))
slope=.5*2*math.pi*200/48000
y=run(200,3)+run(200,20,setp(low=-100))+run(200,30)
check(max_step(y)<4*slope,'entering a Low kill from bypass: no click (max step %.4f, sine slope %.4f)'%(max_step(y),slope))
y=[]
for v in range(0,-101,-5):y+=run(200,2,setp(filt=v))
y+=run(200,40)
check(max_step(y)<4*slope,'filter sweep in MIDI-like steps: no click (max step %.4f)'%max_step(y))
y=run(200,10,setp(low=0,filt=0))+run(200,400)
check(max_step(y)<4*slope,'returning to neutral: crossfade back, no click')
x=tone(1000,1)[0];l,r=block(x,x)
check(l==list(struct.unpack('<32f',struct.pack('<32f',*x))),'after the hold and fade, neutral is an exact bypass again')
y=run(200,30,setp(filt=-50))+run(200,40,setp(filt=0))
check(max_step(y)<4*slope,'turning the filter back to Off: no click (max step %.4f)'%max_step(y))
run(1000,300)

# Kill buttons jump a band in one event (-6 dB -> kill -> -6 dB): the gain
# ramps over about 10 ms, so no click, also while the EQ path is running.
for name,hz,band in (('Low',100,'low'),('Hi Mid',1500,'himid')):
 s=.5*2*math.pi*hz/48000
 run(hz,300)
 y=run(hz,30,setp(**{band:-23}))+run(hz,30,setp(**{band:-100}))+run(hz,30,setp(**{band:-23}))+run(hz,30,setp(**{band:0}))+run(hz,30)
 check(max_step(y)<4*s,'%s kill button toggles (-6 dB <-> kill <-> 0 dB): no click (max step %.4f, sine slope %.4f)'%(name,max_step(y),s))
# The centre detent stops a turn at 0: filter from LP over 0 (held there) to HP.
run(200,300)   # settle on the new tone first
y=run(200,10,setp(filt=-10))+run(200,10,setp(filt=0))+run(200,30)+run(200,10,setp(filt=10))+run(200,30,setp(filt=0))+run(200,300)
check(max_step(y)<4*slope,'filter turned through the detent (LP -> 0 -> HP -> 0): no click (max step %.4f)'%max_step(y))
y=run(200,30,setp(filt=-30))+run(200,60,setp(filt=30))+run(200,30,setp(filt=0))+run(200,300)
check(max_step(y)<4*slope,'filter jumping LP -> HP in one step (MIDI): faded through dry, no click (max step %.4f)'%max_step(y))

# Global settings arrive on the Main node (channel 12).
def main_events(evs):
 events(12,evs);w32(u,OBJ+0x18,12);call(u,D['dj_process_node'],OBJ,CTX);w32(u,OBJ+0x18,5);events(12,[])
# 3 bands (Low | Mid | High at 200 Hz / 5 kHz): Lo Mid is ignored.
main_events([(0x1c4,1)])
x=tone(1000,1)[0];run(1000,300,setp(lomid=-100));l,r=block(x,x)
check(l==list(struct.unpack('<32f',struct.pack('<32f',*x))),'3 bands: the Lo Mid knob is ignored, neutral stays an exact bypass')
lo3=level_db(50,setp(low=-100));mid3=level_db(1000,setp(low=0,himid=-100));hi3=level_db(16000,setp(himid=0,high=-100))
check(lo3<-40 and mid3<-30 and hi3<-30,'3 bands ISO kills: Low 50 Hz %.1f dB, Mid 1 kHz %.1f dB, High 16 kHz %.1f dB'%(lo3,mid3,hi3))
flat3=[level_db(f,setp(high=1)) for f in (60,1000,12000)]
check(all(abs(v)<.08 for v in flat3),'3 bands at 0 dB: flat (allpass) '+str([round(v,3) for v in flat3]))
main_events([(0x1c4,0)]);run(1000,300,setp(high=0))
# Filter slope 24 dB and Res Max.
s12=level_db(4000,setp(filt=-37),settle=120)
main_events([(0x1c7,1)]);s24=level_db(4000,settle=120)
check(s24<s12-8,'24 dB slope: an octave above a 2 kHz corner %.1f dB vs %.1f dB at 12 dB'%(s24,s12))
main_events([(0x1c7,0)])
full=level_db(2000,setp(res=1000),settle=120)
main_events([(0x1c8,0)]);capped=level_db(2000,settle=120)
check(full>10 and capped<0,'Res Max 0 removes the resonance peak: %.1f dB vs %.1f dB at 100 %%'%(capped,full))
main_events([(0x1c8,1000)]);run(1000,300,setp(filt=0,res=0))

# MIDI-learned CCs: the M7 routes them to Main's queue (12), addressed to the track.
run(1000,300);events(12,[(0x1bd,-100)],target=5);w32(u,OBJ+0x18,12);call(u,D['dj_process_node'],OBJ,CTX);w32(u,OBJ+0x18,5);events(12,[])
midi=level_db(50,settle=60)
check(midi<-40,'a Low kill sent by a MIDI CC through the Main queue reaches track 6: 50 Hz %.1f dB'%midi)
events(12,[(0x1bd,0)],target=5);w32(u,OBJ+0x18,12);call(u,D['dj_process_node'],OBJ,CTX);w32(u,OBJ+0x18,5);events(12,[])
run(1000,300)

# MIDI learn scales CCs by the M7's parameter table: Cobalt's ids need ranges there.
def s32(v):return v-(1<<32) if v>=1<<31 else v
rng=[(s32(call(u,0x80555c1,i)),s32(call(u,0x80555e9,i))) for i in (0x1ba,0x1bd,0x1be,0x1bf,0x1ae)]
check(u32(u,0x24008e50)!=0 and rng==[(-100,100),(-100,100),(-100,100),(0,1000),(-96000,-12000)],
      'M7 parameter table: bands/Filter -100..100, Res 0..1000, Gate -96000..-12000 %s'%rng)
check(s32(call(u,0x80555e9,0x4))!=0,'native records are kept (VOL has a range)')

# Silent bus untouched; foreign targets and the Main node never touch tracks.
l,r=block([.25]*32,[.25]*32,setp(low=-100),silent=True)
check(l==[.25]*32,'silent bus is left alone')
events(5,[(0x1bd,0)],target=6);u.mem_write(BUS,struct.pack('<IIIIBBxx',32,32,L,R,0,1))
nexts.clear();call(u,D['dj_process_node'],OBJ,CTX)
run(1000,300);x=tone(1000,1)[0];l,r=block(x,x)
check(l!=x,'events for another track do not reach this one (its Low kill stays)')
print('DJ EQ checks:',H.checks)

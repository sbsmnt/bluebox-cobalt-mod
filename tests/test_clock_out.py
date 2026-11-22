"""Out 2 clock (c1): the Cobalt output wrapper draws clock pulses on OUT2
(bus 16) before the native output node runs. The real stock tick-to-sample
helper (0x08058910) and bus accessors run; only the native output node body
is stubbed, to capture OUT2 as the node would see it. The transport is a
plain object with the fields the stock metronome reads: +0 block start
sample, +0x18 tempo in BPM, +0x30 tick position (960 per quarter), +0x38
playing. As on hardware, the PPQ events go to the stock clock node's queue
(index 24) and the OUT2 buffers sit in DTCM, where the stock audio
allocator puts them. Static checks cover the settings rows and the
transport pins."""
from firmware_harness import *
import firmware_harness as H
CS=M['symbols']['cue7'];OUT=0xc0000000;NATIVE=0x804e21c;STATE=0x388000e8
CH=12;Q=CTX+CH*0x604+4;CLK=24;QC=CTX+CLK*0x604+4;TR=0x24010000;BUS=CTX+0x150e4+16*20
L,R=0x20010000,0x20011000   # DTCM, as from the stock allocator 0x080412a4
PULSE=240

def setup(n=32,bpm=120.):
 u=machine(patched,double=True);got=[]
 def native(u):
  b=u.mem_read(BUS,20);fr,used,lp,rp,silent,stereo=struct.unpack('<IIIIBBxx',b)
  k=used or fr
  got.append((list(struct.unpack('<%df'%k,u.mem_read(lp,4*k))),
              list(struct.unpack('<%df'%k,u.mem_read(rp,4*k))) if stereo else None,silent))
  u.reg_write(UC_ARM_REG_R0,1)
 stub(u,NATIVE,native)
 w32(u,OUT+0x18,CH);w32(u,CTX,TR)
 u.mem_write(TR,bytes(0x40));u.mem_write(TR+0x18,struct.pack('<f',bpm))
 return u,got
def events(u,evs,target=None,queue=None):
 """Phones Src (0x178) goes to the output node's queue 12, everything else
 to the clock node's queue 24, each with its node's target, as the stock
 dispatcher addresses them; target/queue override that."""
 per={Q:[],QC:[]}
 for pid,v in evs:
  q=queue if queue is not None else (Q if pid==0x178 else QC)
  t=target if target is not None else (CH if q==Q else CLK)
  per[q].append(struct.pack('<B7xIHHiI',0x39,t,pid,0,v,0))
 for q,items in per.items():
  for k,b in enumerate(items):u.mem_write(q+k*24,b)
  w32(u,q+0x600,len(items))
def block(u,got,start,pos,n=32,playing=1,evs=(),audio=(0.25,-0.25),silent=0,stereo=1,target=None,queue=None):
 events(u,evs,target,queue)
 w32(u,TR,start);u.mem_write(TR+0x30,struct.pack('<q',pos));u.mem_write(TR+0x38,bytes([playing]))
 u.mem_write(L,struct.pack('<%df'%n,*[audio[0]]*n));u.mem_write(R,struct.pack('<%df'%n,*[audio[1]]*n))
 u.mem_write(BUS,struct.pack('<IIIIBBxx',n,n,L,R,silent,stereo))
 ret=call(u,CS['cue_out_process'],OUT,CTX)
 return ret,got[-1]
def run(u,got,samples,n=32,bpm=120.,first=None,**kw):
 """Blocks of n frames with the tick position at each block start, as
 the transport reports it (whole ticks). Returns both channels joined."""
 spt=3000./bpm;l=[];r=[]
 for b in range(samples//n):
  s=b*n;evs=first if (b==0 and first) else ()
  _,(bl,br,_)=block(u,got,s,int(s/spt),n=n,evs=evs,**kw);l+=bl;r+=br
 return l,r
def edges(x):return [i for i,v in enumerate(x) if v==1. and (i==0 or x[i-1]!=1.)]
def widths(x):
 out=[];i=0
 while i<len(x):
  if x[i]==1.:
   j=i
   while j<len(x) and x[j]==1.:j+=1
   out.append(j-i);i=j
  else:i+=1
 return out

# Static patch layout: rows, lists, keys, defaults, pinned stock handlers.
m=machine(patched)
def cstr(a):return bytes(m.mem_read(a,16)).split(b'\0')[0]
names=[cstr(u32(m,CS['clk_rate_names']+4*i)) for i in range(7)]
check(names==[b'Off',b'1 PPQ',b'2 PPQ',b'4 PPQ',b'8 PPQ',b'12 PPQ',b'24 PPQ'],'Clk Out list reads Off, 1, 2, 4, 8, 12, 24 PPQ')
check(u32(m,0x8135404)==CS['clk_rate_names'],'both rows use the new list')
check(bytes(m.mem_read(0x813521e,2))==bytes.fromhex('0723') and bytes(m.mem_read(0x8135234,2))==bytes.fromhex('0723'),'both rows register seven choices')
check([cstr(u32(m,a)) for a in (0x8135408,0x8135410)]==[b'Clk Out L:',b'Clk Out R:'],'rows read Clk Out L (0x110) and Clk Out R (0x111)')
check([cstr(u32(m,a)) for a in (0x8135400,0x813540c)]==[b'o2clkl',b'o2clkr'],'new project keys, so old clkinppq/clkoutppq values are ignored')
check(bytes(m.mem_read(0x81209ba,2))==bytes.fromhex('0022') and bytes(m.mem_read(0x81209c6,2))==bytes.fromhex('0022'),'project defaults are Off')
check(stock[0x81209ba-0x8040000:][:2]==bytes.fromhex('0522'),'stock default was index 5 (24 PPQ)')
check(bytes(m.mem_read(0x805802c,2))==bytes.fromhex('0522') and bytes(m.mem_read(0x8058018,2))==bytes.fromhex('0522'),'stock transport reads 24 PPQ for both ids')
check(bytes(m.mem_read(0x8058030,4))==stock[0x8058030-0x8040000:][:4] and struct.unpack('<6i',m.mem_read(0x806b0bc,24))==(1,2,4,8,12,24),'stock PPQ table index 5 is 24')

# Off: OUT2 is never touched, playing or not.
u,got=setup()
_,(l,r,silent)=block(u,got,0,0)
check(l==[0.25]*32 and r==[-0.25]*32,'both Off: OUT2 audio passes unchanged')
_,(l,r,silent)=block(u,got,32,1,silent=1,stereo=0)
check(silent==1 and r is None,'both Off: a silent mono OUT2 bus stays silent and mono')
check(block(u,got,64,2)[0]==1,'wrapper returns the native result')

# Left at 24 PPQ, 120 BPM: 40 ticks = 1000 samples, 5 ms pulses, right keeps audio.
u,got=setup()
l,r=run(u,got,4096,first=[(0x110,6)])
check(edges(l)==[0,1000,2000,3000,4000],'24 PPQ at 120 BPM: a pulse every 1000 samples from the downbeat')
check(widths(l)[:4]==[PULSE]*4 and set(l)=={0.,1.},'pulses are 240 samples (5 ms) at full scale, 0 between')
check(r==[-0.25]*4096,'right channel keeps its OUT2 audio while left is the clock')
check(struct.unpack('<I2B',u.mem_read(STATE,6))==(0x434c4b31,6,0),'clock rates kept in backup SRAM')

# A pulse tick that ends one block and starts the next fires once: block
# 1024 starts at tick 40 (40.96), the pulse at sample 1000 already ran.
u,got=setup()
l,r=run(u,got,2048,first=[(0x110,6)])
check(edges(l)==[0,1000,2000] and widths(l)[1]==PULSE,'a tick on a block boundary fires once, full width')

# Every rate, right channel, 64-frame blocks at 100 BPM (30 samples per tick).
for value,ppq in [(1,1),(2,2),(3,4),(4,8),(5,12),(6,24)]:
 u,got=setup(bpm=100.)
 l,r=run(u,got,57600+64,n=64,bpm=100.,first=[(0x111,value)])
 period=28800//ppq
 check(edges(r)==list(range(0,57600+1,period)) and l==[0.25]*len(l),'%d PPQ at 100 BPM: right pulses every %d samples, left audio kept'%(ppq,period))

# Both channels at once, different rates.
u,got=setup()
l,r=run(u,got,24064,n=64,first=[(0x110,1),(0x111,4)])
check(edges(l)==[0,24000] and edges(r)==list(range(0,24064,3000)),'left 1 PPQ and right 8 PPQ run together')

# Stopped: a channel that is on is silent; a pulse in progress finishes.
u,got=setup()
block(u,got,0,0,evs=[(0x110,6)])
_,(l,r,_)=block(u,got,32,1,playing=0)
check(l==[1.]*32,'stop during a pulse: the pulse finishes')
for b in range(2,9):_,(l,r,_)=block(u,got,b*32,b,playing=0)
check(l==[0.]*32 and r==[-0.25]*32,'stopped: no pulses, the clock channel stays at 0, the other keeps audio')
_,(l,r,_)=block(u,got,0,0)
check(edges(l)==[0],'play again from the same tick: the first pulse fires')

# Loop back: the position jumps back to the loop start and its pulse fires.
u,got=setup()
run(u,got,2048,first=[(0x110,6)])
_,(l,r,_)=block(u,got,2048,0)
check(edges(l)==[0],'a jump back to the loop start fires its pulse')

# Count-in or pre-roll: negative positions round up to the next pulse.
u,got=setup()
_,(l,r,_)=block(u,got,-2050,-82,evs=[(0x110,6)])
check(l==[0.]*32,'tick -82: the pulse at tick -80 is 50 samples ahead, past this block')
_,(l,r,_)=block(u,got,-2018,-81)
# The stock helper rounds negative ticks up a sample (tick -80 = sample -1999).
check(edges(l)==[19],'tick -81: the pulse at tick -80 starts on the sample the stock helper gives')

# Mono and silent OUT2 buses become stereo; Off channels keep or zero audio.
u,got=setup()
_,(l,r,_)=block(u,got,0,0,stereo=0,evs=[(0x111,6)])
check(l==[0.25]*32 and r==[1.]*32,'mono OUT2: left keeps the audio, right carries the clock')
u,got=setup()
_,(l,r,silent)=block(u,got,0,0,silent=1,evs=[(0x111,6)])
check(silent==0 and l==[0.]*32 and r==[1.]*32,'silent OUT2: the stale left buffer is zeroed, right carries the clock')

# Out-of-range values and other ids leave the clock off; Off stops at once.
u,got=setup()
_,(l,r,_)=block(u,got,0,0,evs=[(0x110,7),(0x111,-1),(0x112,6)])
check(l==[0.25]*32 and r==[-0.25]*32,'values outside 0..6 and neighbouring ids are Off')
block(u,got,0,0,evs=[(0x110,6)])
_,(l,r,_)=block(u,got,32,1,evs=[(0x110,0)])
check(l==[1.]*32,'Off during a pulse: the pulse finishes')
for b in range(2,10):_,(l,r,_)=block(u,got,b*32,b)
check(l==[0.25]*32,'after the pulse, Off hands left back to OUT2 audio')

# Only the clock node's own events count: c1 read queue 12 and missed them.
u,got=setup()
_,(l,r,_)=block(u,got,0,0,evs=[(0x110,6)],queue=Q,target=CH)
check(l==[0.25]*32,'PPQ events in the output node queue 12 are not the clock rows')
_,(l,r,_)=block(u,got,0,0,evs=[(0x110,6)],target=25)
check(l==[0.25]*32,'events in queue 24 addressed to another node are ignored')
_,(l,r,_)=block(u,got,0,0,evs=[(0x110,6)])
check(edges(l)==[0],'PPQ events to the clock node (queue and target 24) set the rate')
check(L>>28==2 and u32(u,BUS+8)==L,'OUT2 buffers in DTCM are written (c1 skipped them)')

# Phones AUTO still works alongside: the cue source event is read as before.
u,got=setup()
block(u,got,0,0,evs=[(0x178,2),(0x110,6)])
check(struct.unpack('<Ii',u.mem_read(0x38800660,8))==(0x43554534,2),'Phones Src AUTO event read in the same pass')
print('Clock Out checks:',H.checks)

"""Execute the patched native track source and MIDI dispatcher under ARM.
Only allocation, SD/transport-free final sinks and IPC delivery are boundaries.
The native source, events, EQ/insert chain and MIDI mapping code execute.
"""
from gate_fixture import *
import firmware_harness as H
S=M['symbols']['synth7']; SRC=0xc0100000; SIDE=SRC+0x5a8
ON=0x1bf; ENGINE=0x1c0; CHANNEL=0x1c1
u,seen,_=gsetup()
call(u,S['synth_source_construct'],SRC)
w32(u,SRC+0x18,0);u.mem_write(SRC+0x1e,struct.pack('<H',0));w32(u,SRC+8,EQ)
u.mem_write(SRC+0xa20,b'END!')
BUS=CTX+0x150e4
captured=[];formats=[]
def capture(q):
 bus=CTX+0x150e4
 left=u32(q,bus+8);right=u32(q,bus+12)
 formats.append(q.mem_read(bus+17,1)[0])
 captured.append(bytes(q.mem_read(left,128))+bytes(q.mem_read(right,128)))
 q.reg_write(UC_ARM_REG_R0,7)
stub(u,NEXT_FN,capture)
def source(evs=(),l=.125,r=-.25,n=32,silent=False,stereo=True):
 q=CTX+4
 for i,(pid,value) in enumerate(evs):u.mem_write(q+i*24,struct.pack('<B7xIHHiI',0x39,0,pid,0,value,0))
 w32(u,q+0x600,len(evs))
 desc=struct.pack('<IIIIBBxx',32,n,BUF,BUF+0x100,int(silent),int(stereo))
 u.mem_write(BUS,desc)
 u.mem_write(BUF,struct.pack('<32f',*([l]*32)));u.mem_write(BUF+0x100,struct.pack('<32f',*([r]*32)))
 raw=bytes(u.mem_read(BUF,384))
 ret=call(u,u32(u,u32(u,SRC)+12),SRC,CTX)
 return captured[-1],ret,raw,desc
out,ret,raw,desc=source()
check(out==struct.pack('<64f',*([.125]*32+[-.25]*32)) and ret==7,'Synth Off preserves native live audio and chain return')
out,_,raw,desc=source([(ON,1)])
check(out==bytes(256),'enabled synth with no notes suppresses both live input channels')
check(bytes(u.mem_read(BUF,384))==raw and bytes(u.mem_read(BUS,20))==desc,'private synth buffers leave original input samples and descriptor untouched')
check(bytes(u.mem_read(SRC+0xa20,4))==b'END!','source sidecar constructor/render remain inside the 0xa20 allocation (S10)')

# Run the real native M7 MIDI node which unpacks IPC data and calls the patch.
MAP=0xc0200000;MSG=0x24070000;MQ=CTX+25*0x604+4
w32(u,MAP+0x18,25)
def midi(status,a,b):
 packed=3<<24|status<<16|a<<8|b
 u.mem_write(MQ,struct.pack('<B7xIIII',0x10,25,packed,0,0));w32(u,MQ+0x600,1)
 return call(u,0x804f4dc,MAP,CTX)
midi(0x91,69,127)
check(source()[0]==bytes(256),'notes on a different MIDI channel cannot sound this synth')
midi(0x90,69,127)
outputs=[]
for i in range(20):outputs.extend(struct.unpack('<32f',source()[0][:128]))
check(max(map(abs,outputs))>.01 and all(math.isfinite(x) for x in outputs),'native packed MIDI Note On produces finite Plaits FM audio')
out,_,raw,desc=source()
check(out[:128]==out[128:],'mono synth feeds identical left/right samples for native panning')
midi(0x90,72,70);midi(0x80,72,0)
midi(0xb0,64,127);midi(0x80,69,0)
check(any(source()[0]),'sustain pedal holds audio after key release')
midi(0xb0,64,0)
for i in range(240):out=source()[0]
check(out==bytes(256),'sustain release eventually reaches exact silence without live input leakage')
midi(0x90,60,127);source();midi(0x90,60,0)
for i in range(240):out=source()[0]
check(out==bytes(256),'velocity-zero Note On acts as Note Off')
midi(0x90,48,127)
for i in range(5):source()
midi(0xb0,120,0)
check(source()[0]==bytes(256),'All Sound Off immediately silences voice')
source([(CHANNEL,16)]);midi(0x90,60,127)
check(source()[0]==bytes(256),'channel edit clears stale notes and rejects former channel')
midi(0x9f,60,127)
check(any(source()[0]),'MIDI channel 16 is selectable and playable')
source([(ENGINE,1)])
check(source()[0]==bytes(256),'engine switch clears held notes')
midi(0x9f,60,110)
wave=[]
for i in range(20):wave.extend(struct.unpack('<32f',source()[0][:128]))
check(max(map(abs,wave))>.01 and all(math.isfinite(x) for x in wave),'second engine executes original Plaits Waveshaping DSP')
source([(ENGINE,2)])
midi(0x9f,60,110)
va=[]
for i in range(20):va.extend(struct.unpack('<32f',source()[0][:128]))
check(max(map(abs,va))>.01 and all(math.isfinite(x) for x in va),'third engine executes original Plaits Virtual Analog DSP with owned scratch buffer')
for engine,name in [(3,'Cluster'),(4,'Cluster (a project saved with Swarm, removed in S14)')]:
 source([(ENGINE,engine)]);midi(0x9f,60,110);sound=[]
 for block in range(40):sound.extend(struct.unpack('<32f',source()[0][:128]))
 check(max(map(abs,sound))>.005 and all(math.isfinite(x) for x in sound),'new '+name+' engine produces finite audio through native packed MIDI/source chain')
 check(bytes(u.mem_read(SRC+0xa20,4))==b'END!','new '+name+' scratch remains inside source allocation')
 source([(0x1c8,1)])
 peak=0.
 for block in range(200):peak=max(peak,max(map(abs,struct.unpack('<32f',source(silent=True)[0][:128]))))
 check(peak>.005,'new '+name+' engine sustains Drone without live input')
 source([(0x1c8,0)]);midi(0xbf,120,0)
 check(source()[0]==bytes(256),'new '+name+' engine obeys All Sound Off')
source([(ON,0)])
check(source()[0]==struct.pack('<64f',*([.125]*32+[-.25]*32)),'turning Synth Off restores live input')
source([(ON,1)]);check(source()[0]==bytes(256),'re-enabling synth cannot revive a stale held note')
out,_,raw,desc=source([(0x1c8,1)],silent=True)
check(any(out) and bytes(u.mem_read(BUF,384))==raw,'Drone plays with silent physical input and never mutates shared samples')

out,_,_,_=source(stereo=False)
check(any(out) and formats[-1]==0,'Synth preserves native mono channel format')
out,_,_,_=source(stereo=True)
check(any(out) and formats[-1]==1,'Synth preserves native stereo channel format')

midi(0xbf,120,0)
check(source()[0]==bytes(256),'All Sound Off silences Drone until a new note or Drone toggle')

# Exercise new routes and the appended attack key through real source events
# and packed MIDI. Compare rendered PCM rather than reading private offsets.
def rendered_settings(extra,blocks=160):
 call(u,S['synth_source_construct'],SRC)
 w32(u,SRC+0x18,0);u.mem_write(SRC+0x1e,struct.pack('<H',0));w32(u,SRC+8,EQ)
 source([(ON,1),(CHANNEL,16),(ENGINE,0),(0x1c9,20000)]+extra)
 midi(0x9f,84,127)
 sound=[]
 for frame in range(blocks):sound.append(struct.unpack('<32f',source()[0][:128]))
 check(all(math.isfinite(x) and abs(x)<=.25 for frame in sound for x in frame),'new modulation settings produce finite native output')
 return sound
instant=rendered_settings([(0x1cd,4),(0x1ce,-100),(0x1d0,0),(0x1cf,500)])
slow=rendered_settings([(0x1cd,4),(0x1ce,-100),(0x1d0,100),(0x1cf,500)])
e0=sum(x*x for frame in instant[30:80] for x in frame)
e1=sum(x*x for frame in slow[30:80] for x in frame)
check(e1>e0*4 and e1>1e-6,'native Cutoff envelope with nonzero attack delays the negative filter sweep audibly')
plain=rendered_settings([(0x1c9,1000)])
lfo=rendered_settings([(0x1c9,1000),(0x1cb,4),(0x1cc,1000),(0x1ca,20000)])
check(sum((a-b)**2 for f,g in zip(plain,lfo) for a,b in zip(f,g))>1e-4,'native LFO Cutoff destination changes rendered audio')
check(bytes(u.mem_read(SRC+0xa20,4))==b'END!','cutoff modulation and modulation attack remain within source storage (0xa20, S10)')
shape_audio=[]
for shape in range(4):
 shape_audio.append(rendered_settings([(0x1c9,1000),(0x1cb,4),(0x1cc,1000),(0x1ca,20000),(0x1d1,shape)]))
check(len({struct.pack('<5120f',*[x for frame in audio for x in frame]) for audio in shape_audio})==4,'all four LFO shapes produce distinct native Cutoff modulation')
# Validate actual encoded allocation instructions and lifecycle removal.
for at in (0x8050cd8,0x8056e22):
 u.emu_start(at|1,at+4,count=1)
 check(u.reg_read(UC_ARM_REG_R0)==0xa20,'native source allocation reserves the synth sidecar '+hex(at))
stub(u,0x8056d3c,lambda q:None)
freed=[];stub(u,0x806899c,lambda q:freed.append((q.reg_read(UC_ARM_REG_R0),q.reg_read(UC_ARM_REG_R1))))
call(u,u32(u,u32(u,SRC)+8),SRC)
check(freed==[(SRC,0xa20)] and u32(u,0x38800184)==0,'deleting destructor unregisters MIDI pointer and frees the exact 0xa20 size')
print(H.checks,'native synth audio/MIDI checks passed; hardware timing and audio remain unverified.')

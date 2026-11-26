"""FX1 mode 6, Freeze (spectral freeze), on the real delay node: constructor,
SDRAM arena allocation, queue events, the send bus in place, the hold on a
silent bus, fades, mode changes and the no-memory fallback. The DSP itself is
checked on the host by the prototype (Bluebox-CFW/cobalt-spectral-freeze)."""
from delay_fixture import *
import math
GMODE=0x1a0;F_FREEZE=0x19c;F_FADE=0x19d;F_DRIFT=0x19e;F_TONE=0x19f;F_LEVEL=0x1e6;F_TIME=0x1e7;F_TMODE=0x1eb
LINK=0x38800e80
CJOB=0x1c8e0   # struct spectral_freeze: cjob (capture job)
def capturing(u):return 0<u32(u,u32(u,LINK+4)+CJOB)<7
def sine(b,hz=440.,amp=.3):return [amp*math.sin(2*math.pi*hz*(b*32+i)/48000) for i in range(32)]
def silent_block(u,evs=()):
 """A block whose send bus is flagged silent (nothing sent to FX1)."""
 devents(u,evs)
 u.mem_write(DBUS,struct.pack('<IIIIBBxx',32,32,BUF,BUF+0x100,1,1))
 u.mem_write(BUF,struct.pack('<32f',*ZERO));u.mem_write(BUF+0x100,struct.pack('<32f',*ZERO))
 call(u,u32(u,u32(u,DOBJ)+12),DOBJ,CTX,count=20000000)
 return struct.unpack('<64f',bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128)))
def block(u,l,evs=()):
 out,_=dblock(u,l,evs=evs);return struct.unpack('<64f',out)
def tone_amp(x,hz):
 re=sum(v*math.cos(2*math.pi*hz*i/48000) for i,v in enumerate(x));im=sum(v*math.sin(2*math.pi*hz*i/48000) for i,v in enumerate(x))
 return 2*math.hypot(re,im)/len(x)

# Constructor: one instance from the boot-time arena, linked from 0xe80.
u,_,cursor=dsetup()
state=u32(u,LINK+4)
check(u32(u,LINK)==0x46524c31 and 0xc0000000<=state<cursor and u32(u,state)==0x46525a32,'Freeze state taken from the SDRAM arena at boot and linked at backup SRAM 0xe80')
check(all(u32(u,LINK+8+4*i)==0 for i in range(1,6)) and all(u32(u,0x38800ea0+4*i)==0 for i in (0,1,2,6)),'G14: the G2-G9 diagnostics are gone (nothing published but the link and the bookkeeping)')

# Freeze mode, not frozen: the send returns exact silence.
out=[]
for b in range(40):out+=block(u,sine(b),evs=INITIAL+[(GMODE,6),(F_FADE,0),(F_DRIFT,0),(F_TONE,500)] if b==0 else [])
check(all(v==0.0 for v in out[-640:]),'Freeze mode, not frozen: FX1 returns exact silence (no stock delay)')
# Press Freeze after 0.2 s of a sine; then the send goes silent.
for b in range(40,340):out=block(u,sine(b),evs=[(F_FREEZE,1)] if b==340-100 else [])
held=[];count=[0]
h=u.hook_add(UC_HOOK_CODE,lambda q,a,s,d:count.__setitem__(0,count[0]+1),begin=0x08000000,end=0x081fffff)
for b in range(340,440):held+=silent_block(u)[:32]
u.hook_del(h)
rms=[math.sqrt(sum(v*v for v in held[i*800:(i+1)*800])/800) for i in range(4)]
pitch=freq(held[-1600:])
print('INFO held on a silent send: RMS per 17 ms %s (sine 0.212; G35: Level 500 = about the input level), pitch %.1f Hz; %d instructions per block'%([round(x,3) for x in rms],pitch,count[0]//100))
check(all(abs(20*math.log10(x/.2121))<1.5 for x in rms[1:]) and abs(pitch-440)<1.,'Freeze holds the sine at its pitch and about its input level (G35: Level 500), steadily, while the send is silent (after the first frames build up)')
check(count[0]//100<40000,'holding costs %d emulator instructions per block (whole FX1 node)'%(count[0]//100))
# Freeze off fades out to exact silence.
for b in range(440,480):tail=silent_block(u,evs=[(F_FADE,0),(F_FREEZE,0)] if b==440 else [])
check(all(v==0.0 for v in tail),'Freeze off: the hold stops and FX1 returns exact silence')
# Leaving the mode with Freeze left On, then coming back: no stale hold.
for b in range(480,520):block(u,sine(b),evs=[(F_FREEZE,1)] if b==480 else [])
for b in range(520,530):block(u,sine(b),evs=[(GMODE,5)] if b==520 else [])
back=[]
for b in range(530,600):back+=block(u,sine(b),evs=[(GMODE,6)] if b==530 else [])
check(all(v==0.0 for v in back[-640:]),'leaving Freeze mode stops the hold; back in Freeze mode a new press is needed')
# Other modes are untouched: Delay (5) repeats as before.
u2,_,_=dsetup();block(u2,ZERO,evs=INITIAL+[(GMODE,5)]);steady(u2)
rep=[]
for b in range(200):rep+=block(u2,sine(b) if b<5 else ZERO)
check(max(abs(v) for v in rep[32*10:])>.01,'Delay mode still repeats (Freeze only acts in mode 6)')
# No memory: Freeze mode is silent instead of playing the stock delay.
u3=machine();stub(u3,0x8052478,lambda u:u.reg_write(UC_ARM_REG_R0,42))
w32(u3,0x24000004,0xc0010000);w32(u3,0x24000000,0xc0010000+1000)
call(u3,SYMS['dly_construct'],DOBJ)
check(u32(u3,LINK)!=0x46524c31,'no room in the arena: no Freeze state is linked')
u4,_,_=dsetup();w32(u4,LINK,0)
quiet=[]
for b in range(40):quiet+=block(u4,sine(b),evs=INITIAL+[(GMODE,6),(F_FREEZE,1)] if b==0 else [])
check(all(v==0.0 for v in quiet),'without its state, Freeze mode returns silence rather than the stock delay')
u5,_,_=dsetup();st=u32(u5,LINK+4)
# G8: the whole state cleared after boot (as found on the device): set up again on use.
u5.mem_write(st,bytes(139616))
out=[]
for b in range(340):out=block(u5,sine(b),evs=INITIAL+[(GMODE,6),(F_FADE,0),(F_DRIFT,0),(F_TIME,3)] if b==0 else [(F_FREEZE,1)] if b==240 else [])   # G26: Time 1/8, the default (no Off)
held=[]
for b in range(340,440):held+=silent_block(u5)[:32]
check(u32(u5,0x38800eac)==1 and u32(u5,st)==0x46525a32 and abs(freq(held[-1600:])-440)<1. and max(abs(v) for v in held)>.1,'G8: a cleared state is set up again once on use, and then holds the sine (%d)'%u32(u5,0x38800eac))
# G9: the hold's FFT buffer and tables in DTCM when the stock pool has room.
u6,_,_=dsetup();st=u32(u6,LINK+4)
fast=u32(u6,0x38800ea0+20)
check(0x20000000<=fast<0x2001f800 and u32(u6,st+16+16*0)!=0,'G9: the hot FFT buffer and tables are placed in DTCM (%#x)'%fast)
u7=machine();heap7=[0xc0010000]
def alloc7(u):
 a=heap7[0];heap7[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;w32(u,0x24000004,heap7[0]);u.reg_write(UC_ARM_REG_R0,a)
stub(u7,0x80412a4,alloc7);stub(u7,0x8052478,lambda u:u.reg_write(UC_ARM_REG_R0,42))
w32(u7,0x24000000,0xc2000000);w32(u7,0x24000004,0xc0010000);w32(u7,0x240006c0,0x1f800-0x9000-0x3000)
call(u7,SYMS['dly_construct'],DOBJ,count=200000000)
check(u32(u7,0x38800ea0+20)==0 and u32(u7,0x240006c0)==0x1f800-0x9000-0x3000,'G9: without 16 KB to spare after it in the DTCM pool, the hot part stays in SDRAM and the pool is untouched (G30: the diagnostic timers are gone)')
# G9: controls survive the state being set up again.
for b in range(4):block(u6,sine(b),evs=INITIAL+[(GMODE,6),(F_FADE,0),(F_DRIFT,0),(F_TONE,800),(F_LEVEL,700),(F_TIME,3),(F_TMODE,1)] if b==0 else [])
u6.mem_write(st,bytes(139616));w32(u6,0x38800ea0+16,0)
block(u6,sine(5))
fs=struct.unpack('<8i',u6.mem_read(st+4,32))
check(fs[:7]==(0,0,0,800,700,4,1) and u32(u6,0x38800eac)==1,'G9: after a clear, the state is set up again with the controls last received %s'%(fs[:7],))
# G9: timed Freeze from the transport. One block = one tick here (32 samples per tick).
TR=0x24040000
def offset(u):
 tr=u.reg_read(UC_ARM_REG_R0);t=struct.unpack('<q',u.mem_read(u.reg_read(UC_ARM_REG_R1),8))[0]
 u.reg_write(UC_ARM_REG_R0,((t-struct.unpack('<q',u.mem_read(tr+0x30,8))[0])*32)&0xffffffff)
def timed(mode,time=2,stop=None):   # G26: Time values without Off (2 = 1/16)
 u,_,_=dsetup();stub(u,0x8058910,offset);u.mem_write(TR+0x38,b'\1')
 out=[];caps=[]
 for b in range(1400):
  u.mem_write(TR+0x30,struct.pack('<q',b))
  if b==stop:u.mem_write(TR+0x38,b'\0')
  hz=440. if (b//240)%2==0 else 660.
  evs=INITIAL+[(GMODE,6),(F_FADE,0),(F_DRIFT,0),(F_TONE,500),(F_TIME,time),(F_TMODE,mode)] if b==0 else []
  out+=block(u,sine(b,hz),evs=evs)[:32]
  caps.append(capturing(u))
 return out,caps
out,caps=timed(0)
starts=[b for b in range(1,1400) if caps[b] and not caps[b-1]]
step_out=[freq(out[(k*240+120)*32:(k*240+200)*32]) for k in (2,3,4)]
print('INFO Step: captures start at blocks %s; hold pitch in steps 2..4: %s'%(starts,[round(x) for x in step_out]))
check(len(starts)>=4 and all(abs(b%240-(240-(60*32+1024+128)//32))<=2 for b in starts[1:]) and max(abs(v) for v in out[400*32:])>.1,'Step runs with the transport, no Freeze press needed; a new capture starts %d blocks before every 1/16 boundary'%((60*32+1024+128)//32))
check(abs(step_out[0]-660)<2 and abs(step_out[1]-440)<2 and abs(step_out[2]-660)<2,'Step: each step holds the snapshot taken just before its boundary, i.e. the end of the step before (the material changes every step here)')
out,caps=timed(1)
rms=[math.sqrt(sum(v*v for v in out[(k*240+60)*32:(k*240+200)*32])/(140*32)) for k in range(1,6)]
print('INFO Gate: RMS in steps 1..5: %s'%[round(x,3) for x in rms])
check(rms[0]<.001 and rms[1]>.03 and rms[2]<.001 and rms[3]>.03 and rms[4]<.001,'Gate (no Freeze press): the hold sounds on even steps and is silent on odd ones')
out,caps=timed(0,time=0)
check(sum(1 for b in range(1,1400) if caps[b] and not caps[b-1])>=15,'G26: no Off: Time 0 is the first step length (1/64), captures run on their own')
out,_=timed(0,stop=600)
check(min(max(abs(v) for v in out[k*32:(k+100)*32]) for k in range(500,1300,100))>.1,'Step keeps running when the transport stops (own clock at the project tempo)')
out,caps=timed(1,stop=0)
rms=[math.sqrt(sum(v*v for v in out[(k*240+100)*32:(k*240+220)*32])/(120*32)) for k in range(0,5)]
print('INFO Gate, transport stopped: RMS in steps 0..4: %s'%[round(x,3) for x in rms])
check(rms[1]<.001 and rms[2]>.1 and rms[3]<.001 and rms[4]>.1,'Gate with the transport stopped: same steps at the project tempo, starting on an open step (step 0 holds the empty history here: the test signal starts with it)')
# G17: the Freeze toggle with Time set works as on the delays: a press holds
# what is playing now, Step / Gate pause while it is on, letting go resumes.
def pressed(mode,time=2,on=700,off=1100):
 u,_,_=dsetup();stub(u,0x8058910,offset);u.mem_write(TR+0x38,b'\1')
 out=[];caps=[]
 for b in range(1500):
  u.mem_write(TR+0x30,struct.pack('<q',b))
  hz=440. if (b//240)%2==0 else 660.
  evs=INITIAL+[(GMODE,6),(F_FADE,0),(F_DRIFT,0),(F_TONE,500),(F_TIME,time),(F_TMODE,mode)] if b==0 else [(F_FREEZE,1)] if b==on else [(F_FREEZE,0)] if b==off else []
  out+=block(u,sine(b,hz),evs=evs)[:32];caps.append(capturing(u))
 return out,caps
out,caps=pressed(0)
starts=[b for b in range(1,1500) if caps[b] and not caps[b-1]]
held=[freq(out[k*32:(k+80)*32]) for k in (760,860,960,1040)]
print('INFO Step + press at 700..1100: captures start at %s; pitch while held %s'%(starts,[round(x,2) for x in held]))
check(any(700<=b<=702 for b in starts) and not any(703<b<1100 for b in starts) and all(abs(x-440)<2 for x in held),'G17: with Time set, a Freeze press captures at once (the 440 Hz then playing) and holds it: no timed captures while it is on')
check(any(1100<b<1400 for b in starts),'G17: letting go of Freeze resumes the timed Step captures')
out,caps=pressed(0,time=0)
starts=[b for b in range(1,1500) if caps[b] and not caps[b-1]]
held=[freq(out[k*32:(k+80)*32]) for k in (800,880,960,1020)]
print('INFO 1/64 + press at 700: pitch while held %s'%[round(x,1) for x in held])
check(all(abs(x-440)<2 for x in held),'G27: at 1/64 a Freeze press captures at once (restarting the running timed capture) and holds a clean 440 Hz (the history no longer pauses for each load)')
out,_=pressed(1)
rms=[math.sqrt(sum(v*v for v in out[k*32:(k+100)*32])/3200) for k in (760,880,1000)]
print('INFO Gate + press: RMS %s'%[round(x,3) for x in rms])
check(min(rms)>.1,'G17: Gate with Freeze pressed: the hold sounds through the closed steps too')
# G17: 1/64. Shorter than a capture plus a hop: captures run back to back, the hold keeps sounding.
out,caps=timed(0,time=0)
starts=[b for b in range(1,1400) if caps[b] and not caps[b-1]]
print('INFO Step 1/64: %d captures in 1400 blocks'%len(starts))
check(len(starts)>=15 and min(max(abs(v) for v in out[k*32:(k+60)*32]) for k in range(300,1300,60))>.1,'G17: Step 1/64 keeps capturing back to back and the hold keeps sounding')
out,_=timed(1,time=0)
env=[max(abs(v) for v in out[k*32:(k+8)*32]) for k in range(400,1300,8)]
check(max(env)>.1 and min(env)<.02,'G17: Gate 1/64 opens and closes (peaks %.2f, dips %.3f)'%(max(env),min(env)))
print(H.checks,'FX1 Freeze checks passed')

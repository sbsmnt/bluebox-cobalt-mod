"""Native insert hook, allocation, per-track model/XML and executed ARM DSP.
Only allocation, storage/IPC boundaries and final audio sink are stubs.
Instruction counts are not physical CPU or SD/DMA timing measurements.
"""
from gate_fixture import *
import firmware_harness as H
from capstone import Cs,CS_ARCH_ARM,CS_MODE_THUMB,CS_MODE_MCLASS
import json,random,gc,base64
I0=0x1ba;IDS=list(range(I0,I0+5));SIDE=EQ+0x1d0
ref=json.loads((ROOT/'tests/references/cobalt-1.0.0-manifest.json').read_text())
release=bytearray(stock)
for p in ref['patches']:
 data=base64.b64decode(p['new']);release[p['offset']:p['offset']+len(data)]=data
release=bytes(release)
check(hashlib.sha256(release).hexdigest()=='062ce9e3343247c048a4494afa9a385fb2cdf9b08965e1bd5291679c088821ba','comparison is exact uploaded Cobalt 1.0.0 release')
check(M['version']=='1.3.0-dev.4' and len(M['patches'])==284,'distinct identity and 284 guarded regions (277 + the input-picker header name fix + the XY pad target stubs and label + the boot screen version)')
for name in ('M7 additional code','M7 compressor telemetry code','in-place bank2 PCM24 burst recording loop','in-place bank2 PCM24 burst mono playback loop','in-place bank2 PCM24 burst stereo playback loop'):
 check(next(p for p in ref['patches'] if p['name']==name)==next(p for p in M['patches'] if p['name']==name),'release payload retained exactly: '+name)

# Execute all three allocation instructions, not an arithmetic size mirror.
for at in (0x804303e,0x8050c88,0x8051056):
 u=machine();u.emu_start(at|1,at+4,count=1)
 check(u.reg_read(UC_ARM_REG_R0)==0x240,'factory/input/Main allocation reserves full sidecar '+hex(at))
u,seen,_=gsetup();check(u32(u,SIDE)==0x494e5331 and bytes(u.mem_read(SIDE+4,20))==struct.pack('<5i',0,350,650,1000,0) and bytes(u.mem_read(SIDE+88,8))==struct.pack('<2i',0,1),'native EQ constructor initializes insert Off, switched On, and independent defaults')
u.mem_write(EQ+0x240,b'GUARD123')
call(u,GT['gate_construct'],EQ,4)
check(u32(u,EQ+0x1cc)==0xffffffff and bytes(u.mem_read(EQ+0x240,8))==b'GUARD123','constructor initializes channel tag and sidecar within the enlarged allocation')
# Native virtual factory clone uses enlarged allocation and patched constructor.
requests=[];cursor=[0xc0100000]
def allocate(q):
 n=q.reg_read(UC_ARM_REG_R0);requests.append(n);a=cursor[0];cursor[0]=(a+n+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8068980,allocate);clone=call(u,0x804303c,EQ)
check(requests[0]==0x240 and u32(u,clone+0x1d0)==0x494e5331,'native EQ factory allocates and resets a complete sidecar')
freed=[];stub(u,0x8041428,lambda q:None)
stub(u,0x806899c,lambda q:freed.append((q.reg_read(UC_ARM_REG_R0),q.reg_read(UC_ARM_REG_R1))))
call(u,0x8042e30,clone)
check(freed==[(clone,0x240)],'native deleting destructor passes correct enlarged allocation size')

# Drive Off must preserve release PCM and downstream return, including Gate.
pair=[gsetup(image=release)[0],gsetup()[0]];rng=random.Random(81022)
same=True
for b in range(90):
 l=[rng.uniform(-1,1) for _ in range(32)];r=[rng.uniform(-1,1) for _ in range(32)]
 ev=[(0x1ae,-40000)] if b==20 else [(0x1ae,-96000)] if b==60 else []
 a,ar=gblock(pair[0],l,r,ev);c,cr=gblock(pair[1],l,r,ev)
 same &= a==c and ar==cr==7
check(same,'default Off matches release PCM and native downstream return across gate changes')
del pair;gc.collect()

# Twelve separate states; wrong-target and Main commands never activate Drive.
for ch in range(12):
 u,seen,_=gsetup(ch=ch)
 before=bytes(u.mem_read(SIDE,64));gblock(u,[.12]*32,[.03]*32,[(I0,1)],ch=ch,foreign=(ch+1)%12)
 check(bytes(u.mem_read(SIDE,64))==before,'foreign audio command leaves insert state unchanged '+str(ch))
 ev=[(I0,1),(I0+1,1000),(I0+2,700),(I0+3,1000),(I0+4,-3000)]
 for b in range(90):out,ret=gblock(u,signal(.3,b,431),signal(.08,b,719),ev if b==0 else [],ch=ch)
 vals=struct.unpack('<64f',out)
 check(u32(u,SIDE+4)==1 and ret==7 and all(math.isfinite(x) for x in vals),'independent mono/stereo track accepts Drive and keeps chain '+str(ch))
 check(seen[-1]==out,'insert runs before native EQ/downstream consumer '+str(ch))
 # Complete fade-out then compare output bits, not a tolerance.
 for b in range(110):out,_=gblock(u,[.125]*32,[-.0625]*32,[(I0,0)] if b==0 else [],ch=ch)
 check(out==struct.pack('<64f',*([.125]*32+[-.0625]*32)),'Off settles to exact dry audio '+str(ch))
 del u;gc.collect()
u,_,_=gsetup(ch=12)
for b in range(3):out,_=gblock(u,[.125]*32,[-.0625]*32,[(I0,1),(I0+1,1000)],ch=12)
check(out==struct.pack('<64f',*([.125]*32+[-.0625]*32)) and u32(u,SIDE+4)==0,'Main EQ node bypasses insert regardless of commands')

# Exercise production O2 sample loop with aliased channels and exact guards.
hot=GT['insert_drive_audio'];SBUF=0x24040000;LBUF=SBUF+0x100;RBUF=SBUF+0x240
def state(q,mode=1,drive=800,tone=650,mix=1000,output=0):
 q.mem_write(SBUF,struct.pack('<I5i10fiI4f',0x494e5331,mode,drive,tone,mix,output,1,1,0,1,1,1,0,0,0,0,0,1,1,1,1,1))
for n in (0,1,2,15,31,32,33):
 u=machine();state(u);u.mem_write(LBUF-4,b'LEFT'+struct.pack('<32f',*([.2]*32))+b'END!');u.mem_write(RBUF-4,b'RGHT'+struct.pack('<32f',*([-.1]*32))+b'END!')
 call(u,hot,SBUF,LBUF,RBUF,n)
 limit=min(n,32)
 check(bytes(u.mem_read(LBUF-4,4))==b'LEFT' and bytes(u.mem_read(RBUF-4,4))==b'RGHT' and bytes(u.mem_read(LBUF+limit*4,132-limit*4))==struct.pack('<f',.2)*(32-limit)+b'END!' and bytes(u.mem_read(RBUF+limit*4,132-limit*4))==struct.pack('<f',-.1)*(32-limit)+b'END!','DSP frame count and exact output boundaries '+str(n))
u=machine();v=machine();state(u);state(v)
for q in (u,v):q.mem_write(LBUF,struct.pack('<32f',*signal(.3,0)))
call(u,hot,SBUF,LBUF,LBUF,32);call(v,hot,SBUF,LBUF,0,32)
check(bytes(u.mem_read(LBUF,128))==bytes(v.mem_read(LBUF,128)),'aliased stereo buffer processes exactly once as mono')
u=machine();state(u);u.mem_write(LBUF,struct.pack('<32I',*([0x7fc12345,0x7f800000,0xff800000,0x7f7fffff]*8)))
call(u,hot,SBUF,LBUF,0,32)
check(all(math.isfinite(x) for x in struct.unpack('<32f',u.mem_read(LBUF,128))),'active insert handles nonfinite and extreme samples without persistent poison')
u=machine();state(u,mode=0);bits=struct.pack('<32I',*([0x80000000,0,0x7fc12345,0x7f800000]*8));u.mem_write(LBUF,bits);call(u,hot,SBUF,LBUF,0,32)
check(bytes(u.mem_read(LBUF,128))==bits,'settled Off preserves signed zero and nonfinite dry bits')

# Real native settings registration, generic channel list and model setter.
APP=0x240173d0;TMP=0x24008000;LIST=0x24050000
u=machine();heap=[0xc0010000]
def allocate4(q):
 a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,allocate4);call(u,0x8134cb8,0);call(u,0x8120abc)
for pid,key in zip(IDS,['insmode','insdrive','instone','insmix','insoutput']):
 meta=0xc0010000+28*pid
 check(u32(u,meta+24)!=0 and bytes(u.mem_read(u32(u,meta+24),32)).split(b'\0')[0]==key.encode(),'new native definition/project key '+key)
for ch in range(12):
 u.mem_write(TMP,b'\1');setting=APP+0xc08+ch*0x30;call(u,0x81202b8,setting,TMP)
 check([call(u,0x8120234,setting,pid) for pid in IDS]==[0,350,650,1000,0] and struct.unpack('<HH',u.mem_read(setting+10,4))==(94,112) and call(u,0x8120234,setting,0x1d5)==1 and call(u,0x8120234,setting,0x1de)==0,'all five defaults, Insert On and Chord (F6) fit the native channel capacity '+str(ch))
notifications=[];stub(u,0x813c384,lambda q:notifications.append(bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24))))
commands=[];stub(u,0x8108784,lambda q:commands.append(tuple(q.reg_read(r) for r in (UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3))))
stub(u,0x81247c8,lambda q:None)
u.mem_write(TMP,struct.pack('<H',3));call(u,0x8124b44,APP,TMP,I0+1,850)
check(call(u,0x8120234,APP+0xc08+3*0x30,I0+1)==850 and commands==[(3,I0+1,850)],'native model commit saves one track and sends its exact audio command')
check(call(u,0x8120234,APP+0xc08+2*0x30,I0+1)==350,'neighboring track retains its independent Drive value')

# Native XML writer/reader; only XML storage primitives are boundary stubs.
XML=0x24058000;VTY=XML+0x100;VXML=XML+0x200;values={};last=[''];current=[0]
w32(u,VTY,0x80dfe01);stub(u,0x80dfe00,lambda q:q.reg_write(UC_ARM_REG_R0,1));w32(u,XML,VXML);w32(u,VXML,0x80dfe11);stub(u,0x80dfe10,lambda q:q.reg_write(UC_ARM_REG_R0,XML))
for addr in (0x8107b16,0x8106c92):stub(u,addr,lambda q:q.reg_write(UC_ARM_REG_R0,XML))
def key(q):last[0]=bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),64)).split(b'\0')[0].decode();q.reg_write(UC_ARM_REG_R0,XML)
stub(u,0x81074dc,key);stub(u,0x8106dc6,lambda q:values.setdefault(current[0],{}).update({last[0]:q.reg_read(UC_ARM_REG_R1)}));stub(u,0x8106dba,lambda q:None)
expected={}
for ch in range(12):
 current[0]=ch;setting=APP+0xc08+ch*0x30;w32(u,setting,VTY);expected[ch]=[ch%5,200+ch*30,300+ch*40,500+ch*25,-ch*500]
 for pid,val in zip(IDS,expected[ch]):call(u,0x8120264,setting,pid,val&0xffffffff)
 call(u,0x8120b84,XML,XML,setting)
 check([values[ch].get(k) for k in ['insmode','insdrive','instone','insmix','insoutput']]==[v&0xffffffff for v in expected[ch]],'native project writer saves independent insert settings '+str(ch))
 u.mem_write(TMP,b'\1');call(u,0x81202b8,setting,TMP);w32(u,setting,VTY)
stub(u,0x81074a6,lambda q:q.reg_write(UC_ARM_REG_R0,0));stub(u,0x8106ae8,lambda q:q.reg_write(UC_ARM_REG_R0,XML))
def find(q):last[0]=bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),64)).split(b'\0')[0].decode();q.reg_write(UC_ARM_REG_R0,XML if last[0] in values[current[0]] else 0)
def read(q):w32(q,q.reg_read(UC_ARM_REG_R1),values[current[0]][last[0]]);q.reg_write(UC_ARM_REG_R0,0)
stub(u,0x810747c,find);stub(u,0x8106d9e,read)
for ch in range(12):
 current[0]=ch;setting=APP+0xc08+ch*0x30;call(u,0x8120c3c,XML,setting)
 check([call(u,0x8120234,setting,pid) for pid in IDS]==[v&0xffffffff for v in expected[ch]],'native project reader restores insert settings '+str(ch))
 for k in ['insmode','insdrive','instone','insmix','insoutput']:values[ch].pop(k)
 u.mem_write(TMP,b'\1');call(u,0x81202b8,setting,TMP);w32(u,setting,VTY);call(u,0x8120c3c,XML,setting)
 check(call(u,0x8120234,setting,I0)==0,'older project keeps insert Off '+str(ch))

# Instruction footprint through the complete native gate/EQ hook.
records=[]
for stereo in (False,True):
 for mode in range(5):
  u,_,_=gsetup(stereo=stereo)
  for b in range(100):gblock(u,signal(.2,b),signal(.1,b,719) if stereo else None,[(I0,mode),(I0+1,750)] if b==0 else [])
  count=[0]
  def instruction(q,a,size,data):count[0]+=1
  hook=u.hook_add(UC_HOOK_CODE,instruction);u.ctl_remove_cache(0x8000000,0x8200000)
  gblock(u,signal(.2,100),signal(.1,100,719) if stereo else None)
  u.hook_del(hook);records.append(dict(stereo=stereo,mode=mode,instructions=count[0]))
  check(count[0]<16000,'complete native 32-frame insert/gate/EQ callback bounded '+str((stereo,mode,count[0])))
(ROOT/'tests/insert-instructions.json').write_text(json.dumps(dict(version=M['version'],records=records,hardware_cpu_measured=False,all_13_objects_extra_heap_bytes=13*(0x240-0x1cc)),indent=2)+'\n')
print(H.checks,'native insert integration checks passed; hardware sound, routing taps and CPU capacity need device tests.')

"""Actual ARM master callback; pinned alpha.5 golden PCM and preserved effects.
No CPU-cycle timing, peripheral emulation or physical-device claims.
"""
from firmware_harness import *
import firmware_harness as H
import json,base64,random
TAPE=0x38800980
# Reserved slots are actually unused, not inferred from their ordinals.
u=machine(stock);stub(u,0x8103128,lambda q:q.reg_write(UC_ARM_REG_R0,0xc0010000));call(u,0x8134cb8,0)
for pid in (0x1b6,0x1b7,0x1b8):check(bytes(u.mem_read(0xc0010000+pid*28,28))==bytes(28),'stock parameter slot unused '+hex(pid))
fixture=json.loads((ROOT/'tests/references/alpha5-soft-golden.json').read_text())
check(fixture['alpha5_patched_sha256']=='9a2fcd84134963d136ed56f51bac327c90c8965892de0147fbcbda3fd9351ef2','alpha.5 reference is hash identified')
u,nodes=setup();rng=random.Random(517)
for i,item in enumerate(fixture['blocks']):
 l=[rng.uniform(-1,1) for _ in range(32)];r=[rng.uniform(-1,1) for _ in range(32)]
 out,ret=block(u,l,r,item['events'])
 check(out==base64.b64decode(item['pcm']) and ret==item['return'],'Soft bit-exact alpha.5 PCM and return block '+str(i))
check(len(nodes)==len(fixture['blocks']),'Soft native chain executes once per block')
# Native M4 master commit: model + one audio command + refresh. Only IPC,
# allocation, dirty-file storage and observer fan-out are substituted.
m4=machine();heap=[0xc0010000];APP=0x300173d0;TMP=0x24008000
def allocate(q):
 a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(m4,0x8103128,allocate);m4.mem_write(TMP,b'\x02');call(m4,0x81202b8,APP+0xe78,TMP)
m4.mem_write(TMP,struct.pack('<H',12));commands=[];notices=[]
stub(m4,0x8108784,lambda q:commands.append(tuple(q.reg_read(r) for r in (UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3))))
stub(m4,0x81247c8,lambda q:None);stub(m4,0x813c384,lambda q:notices.append(bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24))))
for pid,value in ((0x43,600),(0x1b6,1),(0x1b7,275),(0x1b8,-3000)):
 commands.clear();notices.clear();call(m4,0x8124b44,APP,TMP,pid,value&0xffffffff)
 check(commands==[(12,pid,value&0xffffffff)],'native master edit forwards exactly one IPC command '+hex(pid))
 check(call(m4,0x8120234,APP+0xe78,pid)==value&0xffffffff,'native master edit updates saved settings '+hex(pid))
 check(len(notices)==1 and struct.unpack_from('<H',notices[0])[0]==0x66,'native master edit issues refresh '+hex(pid))
 a,_=setup();block(a,signal(.2,0),signal(.1,0),[(pid,value),(0xb5,0)])
 if pid!=0x43:check(struct.unpack_from('<i',a.mem_read(TAPE,100),{0x1b6:4,0x1b7:8,0x1b8:12}[pid])[0]==value,'native master command consumed by M7 '+hex(pid))
# Exercise real native queue and preserve adjacent shared state with canaries.
u,nodes=setup();u.mem_write(TAPE-16,b'L'*16);u.mem_write(TAPE+128,b'R'*16)
out,_=block(u,[.2]*32,[0.]*32,[(0xb5,0),(0x43,650),(0x1b6,1),(0x1b7,320),(0x1b8,-3000)])
check(struct.unpack('<Iiii',u.mem_read(TAPE,16))==(0x54505832,1,320,-3000),'all experimental controls reach M7 through native master queue')
for i in range(90):out,ret=block(u,signal(.7,i),[0.]*32)
v=struct.unpack('<64f',out)
check(all(math.isfinite(x) for x in v) and max(map(abs,v))<1,'Tape produces bounded finite audio')
check(v[32:]==(0.,)*32,'Tape filter states do not leak left audio into right')
check(len(nodes)==91 and ret==7 and u32(u,OBJ+8)==NEXT,'Tape restores linkage and calls downstream once')
check(u.mem_read(TAPE-16,16)==b'L'*16 and u.mem_read(TAPE+128,16)==b'R'*16,'Tape state remains inside isolated reserved memory')
# Off must ignore mode, Tone and non-unity trim after the short wet fade.
block(u,[.7]*32,[-.2]*32,[(0x43,0),(0x1b8,6000)])
out,ret=block(u,[.7]*32,[-.2]*32)
check(out==struct.pack('<64f',*([.7]*32+[-.2]*32)),'Drive zero is bit-exact dry unity even with Tape and +6dB trim')
check(f32(u,TAPE+16)==0 and f32(u,TAPE+24)==1 and f32(u,TAPE+28)==0,'Off resets crossfade, trim and drive runtime state')
# Parameter clamps, invalid enum fallback.
block(u,[0.]*32,[0.]*32,[(0x1b6,8),(0x1b7,-99),(0x1b8,99999)])
check(struct.unpack('<iii',u.mem_read(TAPE+4,12))==(0,0,6000),'bad values clamp and unknown mode falls back to Soft')
block(u,[0.]*32,[0.]*32,[(0x1b7,2000),(0x1b8,-99999)])
check(struct.unpack('<ii',u.mem_read(TAPE+8,8))==(1000,-12000),'Tone and Output lower/upper bounds enforced')
# Relative output trim applied smoothly to settled Soft/Tape, not immediately.
for mode in (0,1):
 a,_=setup();b,_=setup()
 for i in range(100):
  ev=[(0xb5,0),(0x43,500),(0x1b6,mode)] if i==0 else []
  oa,_=block(a,signal(.15,i),signal(.15,i),ev)
  ob,_=block(b,signal(.15,i),signal(.15,i),ev+([(0x1b8,-6000)] if i==0 else []))
 va=struct.unpack('<64f',oa);vb=struct.unpack('<64f',ob)
 ratio=(sum(x*x for x in vb)/sum(x*x for x in va))**.5
 check(abs(ratio-10**(-6/20))<.021,'mode '+str(mode)+' output trim approaches -6dB smoothly')
# Tape-to-Soft change must fade, not jump to the old path.
u,_=setup()
for i in range(60):block(u,signal(.5,i),signal(.2,i),[(0xb5,0),(0x43,700),(0x1b6,1)] if i==0 else [])
blend=f32(u,TAPE+16);block(u,signal(.5,60),signal(.2,60),[(0x1b6,0)])
check(0<f32(u,TAPE+16)<blend,'mode switch fades toward Soft over samples')
# Short, silent and empty native buses; do not overwrite guard samples.
for frames,cap,silent in ((4,32,0),(32,32,1),(0,0,0)):
 u,_=setup();block(u,[.8]*32,[-.5]*32,[(0xb5,0),(0x43,700),(0x1b6,1)])
 events(u,[]);u.mem_write(CTX+0x150e4+12*20,struct.pack('<IIIIBBxx',cap,frames,BUF,BUF+0x100,silent,1))
 before=bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128));call(u,u32(u,u32(u,OBJ)+12),OBJ,CTX)
 after=bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128))
 check(after==before if silent or not cap else after[16:128]==before[16:128] and after[144:]==before[144:],'Tape respects native buffer bounds '+str((frames,cap,silent)))
# Tape must leave the stock compressor and its GR report unchanged.
a,_=setup();b,_=setup()
for i in range(96):
 ev=[(0xb5,1),(0x132,-40000),(0x135,0)] if i==0 else []
 block(a,signal(.4,i),signal(.2,i),ev)
 block(b,signal(.4,i),signal(.2,i),ev+([(0x43,800),(0x1b6,1)] if i==0 else []))
check(report(a)==report(b) and a.mem_read(OBJ,0x130)==b.mem_read(OBJ,0x130),'Tape leaves native compressor state and shared GR reading unchanged')
# Native helper expands mono; identical Tape states give identical stereo PCM.
u,_=setup();block(u,[.3]*32,[.0]*32,[(0xb5,0),(0x43,600),(0x1b6,1)])
# Fresh independent channel state for this mono test.
u.mem_write(TAPE+36,bytes(64));events(u,[]);u.mem_write(CTX+0x150e4+12*20,struct.pack('<IIIIBBxx',32,32,BUF,BUF+0x100,0,0))
u.mem_write(BUF,struct.pack('<32f',*([.3]*32)));call(u,u32(u,u32(u,OBJ)+12),OBJ,CTX)
check(u.mem_read(BUF,128)==u.mem_read(BUF+0x100,128),'native mono expansion renders identical Tape channels')
# Golden hashes from the exact alpha.5 archive: Shoegaze/delay, Gate, profiler.
# exp.4 deliberately optimizes Granular/Shoegaze in delay7; parity with exp.3
# is covered by test_exp4_performance.py. Profiler and Gate stay exact.
for name,digest in fixture['preserved_payloads'].items():
 if name in ('delay7.bin','gate7.bin'):continue # exp.4/exp.6 optimizations
 check(hashlib.sha256((ROOT/'build'/name).read_bytes()).hexdigest()==digest,'unchanged alpha.5 audio payload '+name)
steps=[0];u,_=setup()
for i in range(4):block(u,signal(.6,i),signal(.2,i),[(0xb5,0),(0x43,700),(0x1b6,1)] if i==0 else [])
h=u.hook_add(UC_HOOK_CODE,lambda *a:steps.__setitem__(0,steps[0]+1));block(u,signal(.6,5),signal(.2,5));u.hook_del(h)
check(steps[0]<30000,'Tape callback completes within bounded emulator instructions')
print('INFO Tape callback instructions (not cycles/CPU %):',steps[0])
print(H.checks,'experimental master integration checks passed; hardware listening and CPU required.')

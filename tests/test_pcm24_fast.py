"""Execute native and candidate ARM PCM24 converters, then the native writer.

No Python replacement of floating arithmetic or PCM packing is used. Whole-writer
fixtures execute the native cache lookup/packing/file wrapper; only FatFS f_write
and an unexpected-error logger are stubs. Instruction counts are not cycles.
"""
import base64,gc,json,random
import firmware_harness as H
from firmware_harness import *
from cache_fixture import init,CACHE,SID

NATIVE=0x08108df4
FAST=M['symbols']['4']['record_pcm24_fast']
SRC=0x24020000;DST=0x24040000
ref=json.loads((ROOT/'tests/references/c10-reference.json').read_text())
c10=bytearray(stock)
for p0 in ref['patches']:
 o=p0['offset'];b=base64.b64decode(p0['new']);c10[o:o+len(b)]=b
c10=bytes(c10)
check(hashlib.sha256(c10).hexdigest()==ref['patched_sha256'],'reconstructed exact preserved C10 reference')
hot=(ROOT/'build/pcmhot.bin').read_bytes()
check(patched[NATIVE-0x8040000:NATIVE-0x8040000+len(hot)]==hot and M['symbols']['4']['record_pcm24_burst']==NATIVE,
      'burst recording loop is installed at the original native converter address')
sites={p0['address'] for p0 in M['patches'] if p0['name'].startswith('record PCM24 conversion ')}
check(sites=={0x0810aa28,0x0810aa50,0x0810aa6c},'exactly three recording PCM24 call sites redirected')

gprs=[UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11]
fprs=[getattr(__import__('unicorn.arm_const',fromlist=['']),f'UC_ARM_REG_S{i}') for i in range(16,32)]
regs=gprs+fprs
seeds=[0x12340000+i for i in range(len(regs))]
controls=0x07c00000
def convert(u,fn,bits,stride=1,fpscr=0,count=None,measure=False):
 count=len(bits) if count is None else count
 raw=struct.pack('<%dI'%len(bits),*bits)
 u.mem_write(SRC,raw)
 step=stride*3
 low=min(0,(max(count,1)-1)*step);high=max(0,(max(count,1)-1)*step)+3
 start=DST+low-16;size=high-low+32
 u.mem_write(start,b'\xa5'*size)
 for r,v in zip(regs,seeds):u.reg_write(r,v)
 u.reg_write(UC_ARM_REG_FPSCR,fpscr)
 n=[0];hook=None
 if measure:
  hook=u.hook_add(UC_HOOK_CODE,lambda q,a,s,d:n.__setitem__(0,n[0]+1))
  # Unicorn does not instrument every already-translated block retroactively.
  u.ctl_flush_tb()
 call(u,fn,SRC,count&0xffffffff,DST,stride&0xffffffff)
 if hook:u.hook_del(hook)
 output=bytes(u.mem_read(start,size))
 assert output[:16]==b'\xa5'*16 and output[-16:]==b'\xa5'*16,'output overrun'
 if count>0:
  touched={16-low+i*step+j for i in range(count) for j in range(3)}
  assert all(v==0xa5 or i in touched for i,v in enumerate(output)),'stride padding modified'
 assert bytes(u.mem_read(SRC,len(raw)))==raw,'source modified'
 assert [u.reg_read(r) for r in regs]==seeds,'callee-saved GPR/FPU registers changed'
 assert u.reg_read(UC_ARM_REG_SP)==0x2407f000,'stack not restored'
 assert u.reg_read(UC_ARM_REG_FPSCR)&controls==fpscr&controls,'FP control changed'
 return output,n[0],u.reg_read(UC_ARM_REG_FPSCR)

edge=[]
for sign in (0,0x80000000):
 for exponent in range(256):
  for mantissa in (0,1,0x3fffff,0x400000,0x7ffffe,0x7fffff):
   edge.append(sign|(exponent<<23)|mantissa)
# Dense neighborhoods of PCM24 quantization boundaries and both clipping edges.
for base in (0x2f800000,0x30000000,0x30800000,0x3effffff,0x3f000000,0x3f7fffff,0x3f800000):
 for delta in range(-16,17):
  for sign in (0,0x80000000):edge.append(sign|(base+delta))
u0=machine(c10,cpu_model=UC_CPU_ARM_CORTEX_M4);u1=machine(cpu_model=UC_CPU_ARM_CORTEX_M4)
for rounding in range(4):
 for fz in range(2):
  for dn in range(2):
   mode=(rounding<<22)|(fz<<24)|(dn<<25)
   a,_,_=convert(u0,NATIVE,edge,fpscr=mode)
   b,_,_=convert(u1,FAST,edge,fpscr=mode)
   check(a==b,f'edge/boundary PCM bytes and ABI match: rounding={rounding}, FZ={fz}, DN={dn}')

rng=random.Random(0xc1124)
for chunk in range(32):
 bits=[rng.getrandbits(32) for _ in range(8192)]
 check(convert(u0,NATIVE,bits)[0]==convert(u1,FAST,bits)[0],f'random float-bit corpus chunk {chunk+1}/32 matches')

audio=[struct.unpack('<I',struct.pack('<f',.95*math.sin(i*.077)))[0] for i in range(1024)]
for stride in (-3,-1,0,1,2,4):
 check(convert(u0,NATIVE,audio,stride)[0]==convert(u1,FAST,audio,stride)[0],
       f'audio/stride {stride}: bytes, gaps, guards, source and ABI match')
for count in (0,-1,-0x80000000):
 for u,fn in ((u0,NATIVE),(u1,FAST)):
  # Both pointers deliberately unmapped; signed nonpositive count must return.
  for r,v in zip(regs,seeds):u.reg_write(r,v)
  call(u,fn,0xdead0000,count&0xffffffff,0xdead0000,2)
  assert [u.reg_read(r) for r in regs]==seeds
 check(True,f'nonpositive count {count}: neither function touches invalid buffers')

perf=[]
for stride in (1,2):
 a,old,_=convert(u0,NATIVE,audio[:512],stride,measure=True)
 b,new,_=convert(u1,FAST,audio[:512],stride,measure=True)
 check(a==b and new<old,f'512-sample conversion stride {stride}: {old} -> {new} instructions, same bytes')
 perf.append(dict(fixture='converter',samples=512,stride=stride,c10_instructions=old,c16_instructions=new))
del u0,u1;gc.collect()

GLOBAL=0x3000ab38;TEMP=0xc3300000;FILE=GLOBAL+0x10
def writer(image,channels):
 u=init(image,'hit',0,cpu_model=UC_CPU_ARM_CORTEX_M4)
 w32(u,GLOBAL,CACHE);w32(u,GLOBAL+8,TEMP)
 u.mem_write(GLOBAL+0x1610+SID*2,struct.pack('<H',0))
 w32(u,GLOBAL+0x18,44);w32(u,GLOBAL+0x1c,44)
 u.mem_write(GLOBAL+0x54,struct.pack('<H',channels));u.mem_write(FILE+0x28,b'\1')
 pattern=edge+audio
 pool=[]
 for idx,page in ((0,0),(1,0 if channels==2 else 1),(2,1)):
  e=CACHE+idx*24;w32(u,e+8,page);w32(u,e+0x10,0x9800)
  u.mem_write(e+0x14,struct.pack('<HHBB',SID,1 if idx==0 and channels==2 else 0xffff,2,int(idx==1 and channels==2)))
  bits=[pattern[(i+idx*71)%len(pattern)] for i in range(0x9800)]
  data=struct.pack('<%dI'%len(bits),*bits);ptr=u32(u,e+4)
  u.mem_write(ptr,data);pool.append((ptr,data))
 table=u32(u,CACHE+0x352c)
 u.mem_write(table+0x400*2,struct.pack('<H',0));u.mem_write(table+0x401*2,struct.pack('<H',2))
 writes=[];logs=[];n=[0]
 def fw(q):
  p=q.reg_read(UC_ARM_REG_R1);size=q.reg_read(UC_ARM_REG_R2)
  writes.append(bytes(q.mem_read(p,size)))
  w32(q,q.reg_read(UC_ARM_REG_R3),size);q.reg_write(UC_ARM_REG_R0,0)
 stub(u,0x0811a64c,fw)
 stub(u,0x0810311c,lambda q:logs.append((q.reg_read(UC_ARM_REG_R0),q.reg_read(UC_ARM_REG_R1))))
 for r,v in zip(regs,seeds):u.reg_write(r,v)
 u.hook_add(UC_HOOK_CODE,lambda q,a,s,d:n.__setitem__(0,n[0]+1))
 u.ctl_flush_tb()
 ret=call(u,0x0810a8d0,GLOBAL,SID,0)
 assert [u.reg_read(r) for r in regs]==seeds,'whole writer ABI changed'
 assert not logs,'native writer logged an error'
 assert len(writes)==1 and len(writes[0])==131028,'unexpected native first-block size'
 assert all(bytes(u.mem_read(ptr,len(data)))==data for ptr,data in pool),'input samples changed'
 snapshot=(writes,bytes(u.mem_read(CACHE,0x3700)),bytes(u.mem_read(GLOBAL,0x3000)),ret)
 del u;gc.collect()
 return snapshot,n[0]

for channels in (1,2):
 a,old=writer(c10,channels);b,new=writer(patched,channels)
 check(a==b,f'native {channels}-channel full writer: PCM, cache and file state match C10')
 check(new<old,f'native {channels}-channel writer: {old} -> {new} instructions with FatFS stub')
 perf.append(dict(fixture='native first WAV block writer',channels=channels,pcm_bytes=131028,
                  c10_instructions=old,c16_instructions=new,
                  reduction_percent=round(100*(old-new)/old,2)))

result=dict(assertions=H.checks,edge_patterns=len(edge),random_patterns=32*8192,
            fp_modes=16,performance=perf,
            limits=['Unicorn instruction counts are not CPU cycles or whole-device CPU percentages.',
                    'FatFS write is stubbed; no physical SD, DMA, RTOS scheduling or overdub timing is tested.',
                    'PCM bytes and FP control/ABI match fixtures; FP exception sticky flags may differ.',
                    'Queue producer policy/capacity remain native, including unchecked overrun.'])
(ROOT/'tests/pcm24-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(f'{H.checks} PCM24 assertions passed; {len(edge)} edge patterns x16 FP modes; 262144 random patterns.')

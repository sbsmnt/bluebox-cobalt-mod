"""Execute native PCM24 decoders and the native whole-page file reader.
Only FatFS and logging are stubs. No SD/DMA/RTOS timing is modeled.
"""
from firmware_harness import *
import firmware_harness as H
from cache_fixture import init as cache_init,CACHE,SID
import base64,json,random,gc
S=M['symbols']['4'];IO=0x38800d80;PT=0x38800ec0;TICK=0x3000da08
ref=json.loads((ROOT/'tests/references/c13-reference.json').read_text());c13=bytearray(stock)
for p0 in ref['patches']:
 b=base64.b64decode(p0['new']);c13[p0['offset']:p0['offset']+len(b)]=b
c13=bytes(c13)
check(hashlib.sha256(c13).hexdigest()==ref['patched_sha256'],'exact failed hardware C13 baseline reconstructed')
# (C16's 'M7 payload is C13-exact' scope check does not apply: alpha.6 carries the full M7 effects code.)
check({p0['address'] for p0 in M['patches'] if p0['name'].startswith('playback PCM24 conversion ')}=={0x0810aece,0x0810af54},'only two native PCM24 playback call sites redirected')
dm=(ROOT/'build/pcmdm.bin').read_bytes();ds=(ROOT/'build/pcmds.bin').read_bytes()
check(patched[0x08108c8c-0x8040000:0x08108cd4-0x8040000]==dm+b'\0\xbf'*((0x48-len(dm))//2) and patched[0x08108cd4-0x8040000:0x08108d44-0x8040000]==ds+b'\0\xbf'*((0x70-len(ds))//2),
      'C16 burst mono/stereo decode loops fill the stock PCM24 decoder bodies in place')
check(patched[0x08108d44-0x8040000:0x08108df4-0x8040000]==stock[0x08108d44-0x8040000:0x08108df4-0x8040000],'native 8/16-bit decoders between the slots remain byte-identical')
SRC=0x24010000;LEFT=0x24030000;RIGHT=0x24040000;TXT=0x24060000
regs=[UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11]
regs += [getattr(__import__('unicorn.arm_const',fromlist=['']),f'UC_ARM_REG_S{i}') for i in range(16,32)]
seed=[0x12340000+i for i in range(len(regs))]
rng=random.Random(0xc1424)
values=[0,1,0x7fffff,0x800000,0xffffff,0xfffffe,0x7ffffe,0x800001]
values += [((1<<k)+d)&0xffffff for k in range(24) for d in (-2,-1,0,1,2)]
values += [rng.getrandbits(24) for _ in range(17000)]
raw=b''.join(v.to_bytes(3,'little') for v in values)
def decode(u,new,n,ch,align=0,mode=0,alias=None,count=False):
 src=SRC+align;dest=src+4 if alias=='input' else LEFT
 right=dest if alias=='outputs' else RIGHT
 u.mem_write(SRC-16,b'\xa5'*(len(raw)+64));u.mem_write(src,raw)
 if alias!='input':u.mem_write(LEFT-16,b'\xa5'*(n*4+32))
 if right!=dest:u.mem_write(RIGHT-16,b'\xa5'*(n*4+32))
 before=bytes(u.mem_read(src,n*ch*3));reads=[];steps=[0]
 def read(q,access,a,size,v,d):
  if src<=a<src+n*ch*3:
   assert a+size<=src+n*ch*3,'source overread'
   assert size==1 or size==4 and a%4==0,'unaligned/wide input load'
   reads.append((a,size))
 h=u.hook_add(UC_HOOK_MEM_READ,read)
 for r,v in zip(regs,seed):u.reg_write(r,v)
 u.reg_write(UC_ARM_REG_FPSCR,mode)
 hc=None
 if count:
  hc=u.hook_add(UC_HOOK_CODE,lambda q,a,s,d:steps.__setitem__(0,steps[0]+1));u.ctl_flush_tb()
 fn=S['record_pcm24_read_mono' if ch==1 else 'record_pcm24_read_stereo'] if new else (0x08108c8c if ch==1 else 0x08108cd4)
 call(u,fn,src,n,dest,right)
 if hc:u.hook_del(hc)
 u.hook_del(h)
 assert [u.reg_read(r) for r in regs]==seed and u.reg_read(UC_ARM_REG_SP)==0x2407f000,'ABI changed'
 assert u.reg_read(UC_ARM_REG_FPSCR)==mode,'exact PCM integer conversion changed FP flags/control'
 if alias!='input':
  assert bytes(u.mem_read(src,n*ch*3))==before,'source changed'
  assert bytes(u.mem_read(dest-16,16))==b'\xa5'*16 and bytes(u.mem_read(dest+n*4,16))==b'\xa5'*16,'left guards changed'
 if ch==2 and alias!='outputs':assert bytes(u.mem_read(right-16,16))==b'\xa5'*16 and bytes(u.mem_read(right+n*4,16))==b'\xa5'*16,'right guards changed'
 return bytes(u.mem_read(dest,n*4)),bytes(u.mem_read(right,n*4)) if ch==2 else b'',reads,steps[0]
u0=machine(c13,cpu_model=UC_CPU_ARM_CORTEX_M4);u1=machine(cpu_model=UC_CPU_ARM_CORTEX_M4)
perf=[]
for ch in (1,2):
 for align in range(4):
  for n in (0,1,2,3,4,5,6,7,8,9,31,32,513,4096):
   assert decode(u0,False,n,ch,align)[:2]==decode(u1,True,n,ch,align)[:2],(ch,align,n)
  check(True,f'decoder ch{ch}/alignment{align}: zero/small/bulk, signs, boundaries, guards and ABI match native')
 for rounding in range(4):
  for fz in range(2):
   for dn in range(2):
    mode=(rounding<<22)|(fz<<24)|(dn<<25)
    assert decode(u0,False,8192,ch,0,mode)[:2]==decode(u1,True,8192,ch,0,mode)[:2]
 check(True,f'decoder ch{ch}: exact floats and FP flags in all16 FP control modes')
 for alias in (('input','outputs') if ch==2 else ('input',)):
  for align in range(4):
   assert decode(u0,False,64,ch,align,alias=alias)[:2]==decode(u1,True,64,ch,align,alias=alias)[:2]
  check(True,f'decoder ch{ch}: {alias} overlap retains native ordering for all source alignments')
 a=decode(u0,False,4096,ch,count=True);b=decode(u1,True,4096,ch,count=True)
 covered=sorted(x for a0,n in b[2] for x in range(a0,a0+n))
 check(a[:2]==b[:2] and covered==list(range(SRC,SRC+4096*ch*3)) and all(n==4 for _,n in b[2]),f'decoder ch{ch}: aligned bulk reads every packed byte once with {len(b[2])} word loads (native {len(a[2])} byte loads)')
 for align in (1,2,3):
  x=decode(u0,False,4096,ch,align);y=decode(u1,True,4096,ch,align)
  covered=sorted(z for a0,n in y[2] for z in range(a0,a0+n))
  fallback=ch==2 and align&1
  check(x[:2]==y[:2] and covered==list(range(SRC+align,SRC+align+4096*ch*3)) and (all(n==1 for _,n in y[2]) if fallback else sum(n==1 for _,n in y[2])<=(18 if ch==1 else 12)),
        f'decoder ch{ch}/source offset{align}: exact floats, each byte read once, '+('odd stereo source uses scalar fallback' if fallback else 'byte loads only at edges'))
 check(b[3]<a[3],f'decoder ch{ch}: {a[3]} ->{b[3]} instructions with exact native floats')
 perf.append(dict(fixture='decoder',channels=ch,frames=4096,c13_instructions=a[3],c16_instructions=b[3]))
# Independent random corpus across262144 packed PCM samples.
for k in range(32):
 raw=rng.randbytes(8192*3)
 assert decode(u0,False,8192,1)[:2]==decode(u1,True,8192,1)[:2]
check(True,'262144 independent random signed PCM24 samples decode to byte-identical floats')
raw=b''.join(v.to_bytes(3,'little') for v in values)
del u0,u1;gc.collect()
u=machine(cpu_model=UC_CPU_ARM_CORTEX_M4)
for ch in (1,2):
 for n in range(1,10):
  src=0x30048000-n*ch*3;l=0x24080000-n*4;r=0xc2000000-n*4
  u.mem_write(src,(bytes.fromhex('000040') if ch==1 else bytes.fromhex('0000400000c0'))*n)
  call(u,S['record_pcm24_read_mono' if ch==1 else 'record_pcm24_read_stereo'],src,n,l,r)
  assert bytes(u.mem_read(l,n*4))==struct.pack('<f',.5)*n
  if ch==2:assert bytes(u.mem_read(r,n*4))==struct.pack('<f',-.5)*n
 check(True,f'decoder ch{ch}: mapped-memory end reads never overread or overrun')
 for n in (0,-1,-0x80000000):call(u,S['record_pcm24_read_mono' if ch==1 else 'record_pcm24_read_stereo'],0xdead0000,n&0xffffffff,0xdead0000,0xdead0000)
 check(True,f'decoder ch{ch}: signed nonpositive count touches no input/output')
del u;gc.collect()

GLOBAL=0x3000ab38;TEMP=0xc3300000;FILE=GLOBAL+0x10;FAT=0x24060000
def reader(image,ch,header,page=3,bits=24,floating=False,short=False,measure=False):
 u=cache_init(image,'missing',cpu_model=UC_CPU_ARM_CORTEX_M4)
 w32(u,GLOBAL,CACHE);w32(u,GLOBAL+8,TEMP);w32(u,FILE+4,FAT)
 w32(u,FILE+8,header);w32(u,FILE+0xc,header);w32(u,FILE+0x10,0x1000000)
 u.mem_write(FILE+0x1a,struct.pack('<H',ch));u.mem_write(FILE+0x24,struct.pack('<H',ch*bits//8))
 u.mem_write(FILE+0x28,b'\1');u.mem_write(FILE+0x2b,bytes([int(floating)]))
 e=CACHE;u.mem_write(e+0x16,struct.pack('<H',1 if ch==2 else 0xffff))
 outputs=[0xc3100010+i*0x28000 for i in range(ch)]
 for i,p in enumerate(outputs):w32(u,CACHE+i*24+4,p)
 for p in outputs:u.mem_write(p-16,b'\xa5'*(0x9800*4+32))
 # page-specific arbitrary bytes also exercise all signed formats and float data.
 data=random.Random(0xc14+page).randbytes(0x9800*ch*bits//8)
 if short:w32(u,FILE+0x10,page*0x9800*ch*bits//8+len(data)-17)
 seen=[];logs=[];cursor=[header];steps=[0]
 def seek(q):
  cursor[0]=q.reg_read(UC_ARM_REG_R2);seen.append(('seek',cursor[0]));q.reg_write(UC_ARM_REG_R0,0)
 def read(q):
  a=q.reg_read(UC_ARM_REG_R1);n=q.reg_read(UC_ARM_REG_R2);off=cursor[0]-header-page*0x9800*ch*bits//8
  payload=data[off:off+n]
  if short:payload=payload[:max(0,len(data)-17-off)]
  q.mem_write(a,payload);w32(q,q.reg_read(UC_ARM_REG_R3),len(payload));cursor[0]+=len(payload)
  seen.append(('read',n,len(payload)));q.reg_write(UC_ARM_REG_R0,0)
 stub(u,0x0811a9dc,seek);stub(u,0x0811a4ac,read);stub(u,0x0810311c,lambda q:logs.append(q.reg_read(UC_ARM_REG_R0)))
 for r,v in zip(regs,seed):u.reg_write(r,v)
 if measure:u.hook_add(UC_HOOK_CODE,lambda q,a,s,d:steps.__setitem__(0,steps[0]+1))
 call(u,0x0810adc8,GLOBAL,0,page,0)
 assert [u.reg_read(r) for r in regs]==seed and u.reg_read(UC_ARM_REG_SP)==0x2407f000
 assert not logs
 samples=[]
 for p in outputs:
  assert bytes(u.mem_read(p-16,16))==b'\xa5'*16 and bytes(u.mem_read(p+0x9800*4,16))==b'\xa5'*16,'page output guards changed'
  samples.append(bytes(u.mem_read(p,0x9800*4)))
 result=(samples,bytes(u.mem_read(CACHE,0x3700)),bytes(u.mem_read(GLOBAL,0x3000)),seen)
 del u;gc.collect();return result,steps[0]
for ch in (1,2):
 for header in (44,45,46,47):
  for short in (False,True):
   a,old=reader(c13,ch,header,short=short,measure=header==44 and not short)
   b,new=reader(patched,ch,header,short=short,measure=header==44 and not short)
   check(a==b,f'native whole-page PCM24 reader ch{ch}/header{header}/short{short}: exact floats/cache/file state and FatFS calls')
   if header==44 and not short:
    check(new<old,f'native reader ch{ch}: {old} ->{new} instructions with exact floats')
    perf.append(dict(fixture='native page reader',channels=ch,frames=0x9800,c13_instructions=old,c16_instructions=new))
 for bits,floating in ((8,False),(16,False),(32,False),(32,True)):
  assert reader(c13,ch,44,bits=bits,floating=floating)[0]==reader(patched,ch,44,bits=bits,floating=floating)[0]
  check(True,f'native ch{ch}/{bits}-bit/float{floating} playback path remains byte/state equivalent')

# alpha.6 removes the C16 decode-time diagnostics (no U report).
result=dict(assertions=H.checks,random_pcm_samples=262144,fp_modes=16,performance=perf,
            limits=['Instruction counts are not cycles, measured hardware CPU or SD throughput.','Native page-reader fixtures stub FatFS and logging; physical DMA/concurrency are unmodeled.','Queue/cache policy and SD configuration remain native; C16 is not a confirmed recording fix.'])
(ROOT/'tests/playback-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(H.checks,'PCM24 playback/native reader checks passed; hardware timing remains unmeasured.')

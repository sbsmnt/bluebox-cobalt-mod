"""C16 in-place scalar packing, native two-pass stereo and packing timing.
Native C12 is the byte/state baseline. Physical SDRAM/SD timing is not modeled.
"""
from firmware_harness import *
import firmware_harness as H
from cache_fixture import init as cache_init,CACHE,SID
import base64,json,random,gc
S=M['symbols']['4'];IO=0x38800d80;PT=0x38800ec0;TICK=0x3000da08
ref=json.loads((ROOT/'tests/references/c12-reference.json').read_text());c12=bytearray(stock)
for p0 in ref['patches']:
 b=base64.b64decode(p0['new']);c12[p0['offset']:p0['offset']+len(b)]=b
c12=bytes(c12);OLD=ref['symbols']['4']['record_pcm24_fast']
check(hashlib.sha256(c12).hexdigest()==ref['patched_sha256'],'preserved C12 hardware-test baseline reconstructed exactly')
check(hashlib.sha256((ROOT/'src/pcm24_hot_m4.h').read_text().replace('__attribute__((noinline)) ','').replace('record_pcm24_scalar','record_pcm24_fast').encode()).hexdigest()=='fdf7900e9e3cf75bb6c15a27b8e851367b542e816e492598b2b2f4bceb6fcef2','cold edge/NaN path retains exact pinned tested C12 scalar conversion source')
SRC=0x24020000;RIGHT=0x24030000;DST=0x24040000;TXT=0x24060000
u0=machine(c12,cpu_model=UC_CPU_ARM_CORTEX_M4);u1=machine(cpu_model=UC_CPU_ARM_CORTEX_M4)
rng=random.Random(0xc1324)
bits=[0,0x80000000,0x3f800000,0xbf800000,0x7f800000,0xff800000,0x7fc00001,0xff800001,1,0x80000001,0x3f7fffff,0xbf7fffff]+[rng.getrandbits(32) for _ in range(8192)]
left=struct.pack('<%dI'%len(bits),*bits);right=struct.pack('<%dI'%len(bits),*reversed(bits))
registers=[UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11]
registers += [getattr(__import__('unicorn.arm_const',fromlist=['']),f'UC_ARM_REG_S{i}') for i in range(16,32)]
seed=[0x12340000+i for i in range(len(registers))]
def pack(u,new,n,channels,align=0,mode=0,alias=False):
 u.mem_write(SRC,left);u.mem_write(RIGHT,right)
 dst=(SRC+4 if alias else DST)+align;size=n*channels*3
 if not alias:u.mem_write(dst-16,b'\xa5'*(size+32))
 writes=[]
 def store(q,access,a,count,value,data):
  if dst<=a<dst+size:
   assert a+count<=dst+size,'output store overrun'
   assert count==1 or count in (2,4) and a%count==0,'unaligned/wide packed store'
   writes.append((a,count))
 h=u.hook_add(UC_HOOK_MEM_WRITE,store)
 for reg,value in zip(registers,seed):u.reg_write(reg,value)
 u.reg_write(UC_ARM_REG_FPSCR,mode)
 if channels==1:call(u,S['record_pcm24_fast'] if new else OLD,SRC,n,dst,1)
 elif new:call(u,S['record_pcm24_stereo'],SRC,RIGHT,n,dst)
 else:
  call(u,OLD,SRC,n,dst,2);call(u,OLD,RIGHT,n,dst+3,2)
 u.hook_del(h)
 assert [u.reg_read(r) for r in registers]==seed,'packing ABI not preserved'
 assert u.reg_read(UC_ARM_REG_SP)==0x2407f000 and u.reg_read(UC_ARM_REG_FPSCR)&0x07c00000==mode&0x07c00000
 output=bytes(u.mem_read(dst,size))
 if not alias:
  assert bytes(u.mem_read(SRC,len(left)))==left and bytes(u.mem_read(RIGHT,len(right)))==right,'source changed'
  assert bytes(u.mem_read(dst-16,16))==b'\xa5'*16 and bytes(u.mem_read(dst+size,16))==b'\xa5'*16,'guard changed'
 return output,bytes(u.mem_read(SRC,len(left))),writes

for ch in (1,2):
 for align in range(4):
  for n in (0,1,2,3,4,5,6,7,8,9,31,32,513,4096):
   a,_,_=pack(u0,False,n,ch,align);b,_,_=pack(u1,True,n,ch,align)
   assert a==b,(ch,align,n)
  check(True,f'{ch}-channel alignment{align}: zero/small counts, small/bulk counts, clipping/NaNs, padding and ABI match C12')
 for rounding in range(4):
  for fz in range(2):
   for dn in range(2):
    mode=(rounding<<22)|(fz<<24)|(dn<<25)
    assert pack(u0,False,8192,ch,0,mode)[0]==pack(u1,True,8192,ch,0,mode)[0]
 check(True,f'{ch}-channel scalar8192-pattern bytes match in all16 FP modes')
 for align in range(4):
  a,old_src,_=pack(u0,False,64,ch,align,alias=True);b,new_src,_=pack(u1,True,64,ch,align,alias=True)
  check(a==b and old_src==new_src,f'{ch}-channel overlap alignment{align} retains sequential C12 byte behavior')
 a,_,old_w=pack(u0,False,4096,ch);b,_,new_w=pack(u1,True,4096,ch)
 check(a==b and len(new_w)<len(old_w),f'{ch}-channel random bit corpus: NaN/infinity groups fall back to exact scalar bytes')
 # Audio range (|x|<=1.2, includes clipping) never needs the scalar fallback.
 saved=(left,right);audio=[struct.unpack('<I',struct.pack('<f',1.2*math.sin(i*.0131+ch)))[0] for i in range(len(bits))]
 left=struct.pack('<%dI'%len(audio),*audio);right=struct.pack('<%dI'%len(audio),*reversed(audio))
 a,_,old_w=pack(u0,False,4096,ch);b,_,new_w=pack(u1,True,4096,ch)
 words=[w for w in new_w if w[1]==4];single=[w for w in new_w if w[1]==1]
 check(a==b and len(words)==4096*ch*3//4 and not single and all(w[0]%4==0 for w in words) and sorted(w[0] for w in words)==[DST+4*i for i in range(4096*ch*3//4)],
       f'{ch}-channel aligned bulk writes every PCM byte once with aligned word stores only ({len(words)} words vs {len(old_w)} C12 byte stores)')
 for align in (1,2,3):
  a,_,old_w=pack(u0,False,4096,ch,align);b,_,new_w=pack(u1,True,4096,ch,align)
  covered=sorted(x for a0,n in new_w for x in range(a0,a0+n))
  check(a==b and covered==list(range(DST+align,DST+align+4096*ch*3)) and (sum(n==1 for _,n in new_w)==len(new_w)==4096*6 if ch==2 and align&1 else sum(n==1 for _,n in new_w)<=(18 if ch==1 else 12)),
        f'{ch}-channel destination offset {align}: same bytes, each written once, '+('odd stereo destination uses exact scalar fallback' if ch==2 and align&1 else 'byte stores only at edges'))
 left,right=saved
del u0,u1;gc.collect()
u=machine(cpu_model=UC_CPU_ARM_CORTEX_M4)
for ch in (1,2):
 for n in range(1,10):
  l=0x30048000-n*4;r=0xc2000000-n*4;d=0x24080000-n*ch*3
  u.mem_write(l,struct.pack('<%df'%n,*([.5]*n)));u.mem_write(r,struct.pack('<%df'%n,*([-.5]*n)))
  if ch==1:call(u,S['record_pcm24_fast'],l,n,d,1)
  else:call(u,S['record_pcm24_stereo'],l,r,n,d)
  expected=(bytes.fromhex('000040') if ch==1 else bytes.fromhex('0000400000c0'))*n
  assert bytes(u.mem_read(d,len(expected)))==expected
 check(True,f'{ch}-channel counts1..9 at mapped-memory ends neither overread input nor overrun output')
for n in (0,-1,-0x80000000):call(u,S['record_pcm24_stereo'],0xdead0000,0xdead0000,n&0xffffffff,0xdead0000)
check(True,'two-pass stereo nonpositive counts return without touching unmapped input/output')
del u;gc.collect()

GLOBAL=0x3000ab38;TEMP=0xc3300000;FILE=GLOBAL+0x10
def writer(image,ch,block,header):
 u=cache_init(image,'missing',cpu_model=UC_CPU_ARM_CORTEX_M4)
 w32(u,GLOBAL,CACHE);w32(u,GLOBAL+8,TEMP)
 u.mem_write(GLOBAL+0x1610+SID*2,struct.pack('<H',0));w32(u,GLOBAL+0x18,header)
 w32(u,GLOBAL+0x1c,header if not block else block*0x20000);w32(u,GLOBAL+0x60,block)
 u.mem_write(GLOBAL+0x54,struct.pack('<H',ch));u.mem_write(FILE+0x28,b'\1')
 start=max(0,(block*0x20000-header)//(3*ch));first=start//0x9800
 table=u32(u,CACHE+0x352c);pool=[]
 for rel in range(3):
  page=first+rel;idx=rel*ch
  for channel in range(ch):
   e=CACHE+(idx+channel)*24;w32(u,e+8,page);w32(u,e+0x10,0x9800)
   u.mem_write(e+0x14,struct.pack('<HHBB',SID,idx+1 if ch==2 and not channel else 0xffff,2,channel))
   raw=struct.pack('<%dI'%0x9800,*[bits[(i+page*29+channel*31)%len(bits)] for i in range(0x9800)])
   ptr=u32(u,e+4);u.mem_write(ptr,raw);pool.append((ptr,raw))
  u.mem_write(table+(page+0x400 if page<=31 else page&1023)*2,struct.pack('<H',idx))
 writes=[];logs=[];hits=[0,0]
 def fat(q):
  p=q.reg_read(UC_ARM_REG_R1);n=q.reg_read(UC_ARM_REG_R2);writes.append(bytes(q.mem_read(p,n)))
  w32(q,q.reg_read(UC_ARM_REG_R3),n);q.reg_write(UC_ARM_REG_R0,0)
 stub(u,0x0811a64c,fat);stub(u,0x0810311c,lambda q:logs.append(q.reg_read(UC_ARM_REG_R0)))
 for k,at in enumerate((0x0810aa50,0x0810aa6c)):
  u.hook_add(UC_HOOK_CODE,lambda q,a,n,d,k=k:hits.__setitem__(k,hits[k]+1),begin=at,end=at)
 ret=call(u,0x0810a8d0,GLOBAL,SID,block)
 assert not logs and len(writes)==1 and len(writes[0])==(0x20000-header if not block else 0x20000)
 assert all(bytes(u.mem_read(p,len(data)))==data for p,data in pool),'source changed'
 result=(writes,bytes(u.mem_read(CACHE,0x3700)),bytes(u.mem_read(GLOBAL,0x3000)),ret)
 del u;gc.collect();return result,hits
for ch in (1,2):
 for header in (44,46):
  for block in (0,1,2,3,7):
   a,old=writer(c12,ch,block,header);b,new=writer(patched,ch,block,header)
   check(a==b,f'real native writer ch{ch}/header{header}/block{block}: full bytes, partial-frame boundaries, page crossings, cache and file state match C12')
   if ch==2:assert old[0]==old[1] and new[0]==old[0] and new[1]==0,'C16 bridge must replace every native stereo R pass'
check(True,'native C16 writer enters the fused stereo bridge once per native L call and never reaches the replaced R pass')
# Same whole-writer cases with audio-range samples so the burst loops (not the
# NaN fallback) produce nearly every byte inside the real native writer.
bits=[struct.unpack('<I',struct.pack('<f',1.1*math.sin(i*.0173)*math.cos(i*.0011)))[0] for i in range(8192)]
for ch in (1,2):
 for header in (44,46):
  for block in (0,1,3,7):
   a,_=writer(c12,ch,block,header);b,_=writer(patched,ch,block,header)
   check(a==b,f'audio-range native writer ch{ch}/header{header}/block{block}: burst path bytes, cache and file state match C12')

# alpha.6 removes the C16 packing-time diagnostics (no P report).
print(H.checks,'in-place PCM24 recording checks passed; hardware bus/CPU speed remains unmeasured.')

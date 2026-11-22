"""New modes on executed ARM, exact I1 CLIP audio compatibility and enum ABI."""
from gate_fixture import *
import firmware_harness as H
import base64,json,gc
ref=json.loads((ROOT/'tests/references/cobalt-inserts-I1-manifest.json').read_text())
i1=bytearray(stock)
for p in ref['patches']:
 data=base64.b64decode(p['new']);i1[p['offset']:p['offset']+len(data)]=data
i1=bytes(i1)
check(hashlib.sha256(i1).hexdigest()=='fde8a338ab7cfdc9397807edb247db1eb46111bd0c51b0f05fbba9de4ee5b4e0','I1 audio reference is exactly the hardware-tested Drive experiment')
u,_,_=gsetup(image=i1);call(u,ref['symbols']['gate7']['gate_construct'],EQ,4)
w32(u,EQ+0x18,0);u.mem_write(EQ+0x1e,b'\0\0');w32(u,EQ+8,NEXT)
v,_,_=gsetup();equal=True
for b in range(150):
 ev=[(0x1ba,1),(0x1bb,700),(0x1bc,300),(0x1bd,720),(0x1be,-3000)] if b==0 else [(0x1bb,425)] if b==45 else [(0x1ba,0)] if b==85 else []
 l=signal(.31,b,719);r=signal(.19,b,523)
 a,ar=gblock(u,l,r,ev);c,cr=gblock(v,l,r,ev);equal &= a==c and ar==cr==7
check(equal,'CLIP up to 70% Drive retains bit-identical I1 Drive PCM through enable, parameter changes and Off')
# S8d: above 70% CLIP gets up to 3x more gain: at full Drive a moderate sine
# loses more of its fundamental to harmonics than in I1.
def thd(q):
 outs=[]
 for b in range(120):
  out,_=gblock(q,signal(.12,b,220),None,[(0x1ba,1),(0x1bb,1000),(0x1bc,1000),(0x1bd,1000),(0x1be,0)] if b==0 else [])
  outs+=struct.unpack('<32f',out[:128])
 xs=outs[-2400:];n=len(xs)
 c=sum(x*math.cos(2*math.pi*220*i/48000) for i,x in enumerate(xs));s_=sum(x*math.sin(2*math.pi*220*i/48000) for i,x in enumerate(xs))
 fund=2*(c*c+s_*s_)**.5/n;total=(sum(x*x for x in xs)/n)**.5*2**.5
 return (max(total*total-fund*fund,0))**.5/fund
u,_,_=gsetup(image=i1);call(u,ref['symbols']['gate7']['gate_construct'],EQ,4);w32(u,EQ+0x18,0);u.mem_write(EQ+0x1e,b'\0\0');w32(u,EQ+8,NEXT)
old_thd=thd(u);v,_,_=gsetup();new_thd=thd(v)
check(new_thd>old_thd*1.15,'CLIP at full Drive distorts more than I1 (THD %.3f -> %.3f)'%(old_thd,new_thd))
del u,v;gc.collect()
signatures=[]
NEXT_MODE={1:3,3:4,4:1}
for mode in (1,3,4):
 u,seen,_=gsetup();safe=True
 for b in range(140):
  ev=[(0x1ba,mode),(0x1bb,850),(0x1bc,850)] if b==0 else []
  out,ret=gblock(u,signal(.31,b,719),signal(.13,b,523),ev)
  safe &= all(math.isfinite(x) and abs(x)<1 for x in struct.unpack('<64f',out)) and ret==7
 check(safe and u32(u,EQ+0x1d0+4)==mode and u32(u,EQ+0x1d0+64)==mode,'native queue activates distinct running algorithm '+str(mode))
 signatures.append(struct.unpack('<64f',out))
 # Same reduction guard on both channels and the new mode transitions.
 for b in range(160):out,_=gblock(u,[.1]*32,[-.2]*32,[(0x1ba,NEXT_MODE[mode])] if b==0 else [])
 check(u32(u,EQ+0x1d0+64)==NEXT_MODE[mode] and all(math.isfinite(x) for x in struct.unpack('<64f',out)),'native mode transition finishes at requested algorithm '+str(NEXT_MODE[mode]))
 for b in range(120):out,_=gblock(u,[.125]*32,[-.0625]*32,[(0x1ba,0)] if b==0 else [])
 check(out==struct.pack('<64f',*([.125]*32+[-.0625]*32)),'every new algorithm settles to exact dry Off '+str(mode))
 del u;gc.collect()
check(all(sum(abs(a-b) for a,b in zip(signatures[i],signatures[j]))>.1 for i in range(3) for j in range(i+1,3)),'ARM output distinguishes all three pairs of distortion algorithms')
# Registration must preserve I1's mode=1 and expose all five native labels.
u=machine();heap=[0xc0010000]
def alloc(q):
 a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,alloc);call(u,0x8134cb8,0)
meta=0xc0010000+28*0x1ba;choices=u32(u,meta+8)
names=[bytes(u.mem_read(u32(u,choices+4*i),20)).split(b'\0')[0].decode() for i in range(u32(u,meta+12))]
check(names==['CLIP','CLIP','CLIP','FOLD','WRAP','LP','BP','HP','DJ','SHFT','SPRD','RING','HARM','CHRD','BELL'],'native enum labels keep ordinals 0-4 (fresh 0 and legacy SIN show CLIP) and add the filter types')
print(H.checks,'new-mode ARM/compatibility checks passed; device sound and CPU remain hardware checks.')

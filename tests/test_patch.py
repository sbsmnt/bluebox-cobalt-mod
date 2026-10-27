"""Integration tests: real desktop stock profiler/getter/flip/presenter under Unicorn.
Only display blitting and its final hardware call are stubbed; no device emulation.
Usage: python3 tests/test_patch.py /path/to/original/BLUEBOX.BIN
"""
import base64,hashlib,importlib.util,json,pathlib,struct,sys,tempfile,subprocess
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import *
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('patcher',ROOT/'patch_bluebox.py');patcher=importlib.util.module_from_spec(spec);spec.loader.exec_module(patcher)
M=patcher.MANIFEST
stock=pathlib.Path(sys.argv[1]).read_bytes();patched=patcher.transform(stock)
s7=M['symbols']['7'];s4=M['symbols']['4'];RET=0x080dfff0
COUNT=0

def check(cond,message):
 global COUNT
 if not cond:raise AssertionError(message)
 COUNT+=1;print('PASS',message)
def u32(uc,a):return struct.unpack('<I',uc.mem_read(a,4))[0]
def w32(uc,a,v):uc.mem_write(a,struct.pack('<I',v&0xffffffff))
def machine(image=patched):
 uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x200000),(0x24000000,0x80000),(0x30000000,0x48000),(0x38800000,0x1000),(0x58024000,0x1000),(0xe0001000,0x1000),(0xe000e000,0x1000),(0xc0000000,0x180000)]:uc.mem_map(a,n)
 uc.mem_write(0x08040000,image);uc.reg_write(UC_ARM_REG_SP,0x2407f000)
 # Allow stock floating-point profiling summary on Cortex-M.
 uc.reg_write(UC_ARM_REG_C1_C0_2,0xf00000)
 return uc
def call(uc,a,*args,end=RET):
 for r,v in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):uc.reg_write(r,v)
 uc.reg_write(UC_ARM_REG_LR,RET|1)
 uc.emu_start(a|1,end,count=500000)
 if uc.reg_read(UC_ARM_REG_PC)!=end:raise AssertionError('Function did not return: '+hex(a))
def stub(uc,a,fn):
 def h(uc,addr,size,data):fn(uc);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
 uc.hook_add(UC_HOOK_CODE,h,begin=a,end=a)
def bl_target(image,a):
 h1,h2=struct.unpack_from('<HH',image,a-0x08040000);s=(h1>>10)&1;i1=1-(((h2>>13)&1)^s);i2=1-(((h2>>11)&1)^s)
 value=(s<<24)|(i1<<23)|(i2<<22)|((h1&1023)<<12)|((h2&2047)<<1)
 return a+4+(value-(1<<25) if s else value)
check(bl_target(patched,0x0804c50e)==s7['cpu_prof_end'],'desktop audio call redirects to compiled M7 hook')
# Scenes: the display call goes to pc_present (on-screen messages), which ends
# by calling Cobalt's cpu_present.
pcm4=M['symbols']['pcm4']
check(bl_target(patched,0x08137496)==pcm4['pc_present'],'desktop display call redirects to the scenes overlay hook')
from capstone import *
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB|CS_MODE_MCLASS)
pp=pcm4['pc_present']&~1
calls=[int(i.op_str[1:],16) for i in md.disasm(bytes(patched[pp-0x08040000:pp-0x08040000+0x200]),pp) if i.mnemonic in ('bl','b.w') and i.op_str.startswith('#')]
check(s4['cpu_present']&~1 in calls,'which chains to Cobalt\'s compiled M4 display hook (cpu_present)')
check(len(stock)==len(patched),'firmware length unchanged')
check(stock[0xa0000:0xc0000]==patched[0xa0000:0xc0000],'storage application unchanged')
allowed=set()
for p in M['patches']:allowed.update(range(p['offset'],p['offset']+len(base64.b64decode(p['new']))))
check(all(i in allowed for i,(a,b) in enumerate(zip(stock,patched)) if a!=b),'all differences confined to declared patch regions')
check(patcher.transform(patched,True)==stock,'restoring reconstructs original byte-for-byte')
for label,bad in [('short input',stock[:-1]),('corrupted input',bytes([stock[0]^1])+stock[1:]),('already patched',patched)]:
 try:patcher.transform(bad)
 except ValueError:check(True,'refuses '+label)
 else:raise AssertionError('accepted '+label)
with tempfile.TemporaryDirectory() as tmp:
 inp=pathlib.Path(tmp)/'stock.bin';inp.write_bytes(stock);out=pathlib.Path(tmp)/'output.bin'
 def cli(*args):return subprocess.run([sys.executable,str(ROOT/'patch_bluebox.py'),str(inp),*map(str,args)],capture_output=True,text=True)
 check(cli('--check').returncode==0 and not out.exists(),'CLI check writes no output')
 check(cli('--output',out).returncode==0 and out.read_bytes()==patched,'standalone CLI writes expected patched file')
 check(cli('--output',out).returncode!=0 and out.read_bytes()==patched,'CLI refuses to overwrite existing output')
 check(cli('--output',inp).returncode!=0 and inp.read_bytes()==stock,'CLI refuses to overwrite stock input')
# Actual desktop profiler: run its entry function, inject elapsed DWT cycles,
# execute the patched BL instruction itself and let the real exit write its ring.
ENG=0x24010000;RING=0x24020000;DWT=0xe0001004
uc=machine();w32(uc,ENG+0x3e58,RING)
cycles=1000000
for n in range(500):
 cycles=(cycles+320000)&0xffffffff;w32(uc,DWT,cycles)
 call(uc,0x0804bbd8,ENG)
 busy=224000 if n%10==0 else 96000
 w32(uc,DWT,cycles+busy)
 call(uc,0x0804c50e,ENG,end=0x0804c512)
magic,avg,peak,seq=struct.unpack('<IHHI',uc.mem_read(0x38800f00,12))
print('report:',magic,avg,peak,seq)
check(magic==0x44504355 and seq>=1,'real desktop profiler publishes a CPU report')
check(330<=avg<=350 and 695<=peak<=700,'real profiler reports expected average (~34%) and peak (70%)')
check(u32(uc,RING)==320000 and u32(uc,RING+4)==96000,'original profiler period/busy entries preserved')
check(u32(uc,0x580244e0)&(1<<28) and u32(uc,0x58024800)&256,'backup SRAM clock and write access enabled')
check(u32(uc,0xe000ef68)==0x38800f00,'M7 cleans published report cache line')
# Compare stock and patched exit: all ring bytes and count remain identical.
for n in [0,1,2047,2048,0xfffffffe]:
 a=machine(stock);b=machine();
 for u in [a,b]:
  w32(u,ENG+0x3e58,RING);w32(u,ENG+0x3e5c,n);w32(u,ENG+0x3e58+0x410,0xffff0000);w32(u,ENG+0x3e58+0x40c,0xffff8000);w32(u,DWT,0x00020000)
 call(a,0x0804bbf8,ENG);call(b,s7['cpu_prof_end'],ENG)
 check(a.mem_read(RING,0x4000)==b.mem_read(RING,0x4000) and u32(a,ENG+0x3e5c)==u32(b,ENG+0x3e5c),'stock profiler preserved with ring count '+str(n)+' and cycle-counter wrap')
# Actual desktop presenter -> patched BL -> actual stock backbuffer getter/flip.
# Stub only blit (would require DMA2D) and final LTDC programming call.
DISP=0x30001d80;SURF=0x30010000;FB=0xc0000000
CALLEE=[UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11]
def display_test(fmt,bpp,avg=500,peak=800,layer=1,parity=0,magic=0x44504355,invalid=None):
 uc=machine();w32(uc,DISP+0x118,parity);w32(uc,DISP+0x11c,parity)
 for index,off in enumerate([0x120,0x12c,0x138,0x144]):
  desc=struct.pack('<IHHBBH',FB+index*0x50000,320,240,fmt,0,bpp)
  uc.mem_write(DISP+off,desc)
 if invalid:
  off=(0x120 if layer else 0x138)+(12 if parity else 0)
  if invalid=='width':uc.mem_write(DISP+off+4,struct.pack('<H',319))
  elif invalid=='end':w32(uc,DISP+off,0x2407fff0)
 uc.mem_write(SURF,struct.pack('<IHHBBH',FB,320,240,fmt,0,bpp))
 uc.mem_write(0x38800f00,struct.pack('<IHHI',magic,avg,peak,1))
 calls=[]
 stub(uc,0x08101660,lambda u:calls.append(('blit',u.reg_read(UC_ARM_REG_R0))))
 stub(uc,0x08112b9a,lambda u:calls.append(('flip',u.reg_read(UC_ARM_REG_R1),u.reg_read(UC_ARM_REG_R2))))
 vals=[0x12340000+i for i in range(8)]
 for r,v in zip(CALLEE,vals):uc.reg_write(r,v)
 sp=uc.reg_read(UC_ARM_REG_SP);call(uc,0x08137458,SURF,layer)
 check([uc.reg_read(r) for r in CALLEE]==vals and uc.reg_read(UC_ARM_REG_SP)==sp,'presenter preserves callee-saved registers and stack')
 index=(0 if layer else 2)+parity;base=FB+index*0x50000
 check(calls==[('blit',DISP),('flip',u32(uc,DISP+(0x120 if layer else 0x138)+(12 if parity else 0)),layer)],'stock blit and hardware flip called in order')
 check(u32(uc,DISP+(0x118 if layer else 0x11c))==parity+1,'stock buffer parity advances once')
 return uc,base
for fmt,bpp in [(1,4),(2,3),(3,2),(4,2)]:
 uc,fb=display_test(fmt,bpp)
 def px(x,y):return bytes(uc.mem_read(fb+(y*320+x)*bpp,bpp))
 def encoded(c):
  r=c>>16&255;g=c>>8&255;b=c&255
  return struct.pack('<I',0xff000000|c) if fmt==1 else bytes([b,g,r]) if fmt==2 else struct.pack('<H',0x8000|((r>>3)<<10)|((g>>3)<<5)|(b>>3)) if fmt==3 else struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(b>>3))
 check(all(px(x,y)==encoded(0x00c850) for x in range(314,317) for y in range(10,17)),'format '+str(fmt)+': correct green 50% fill')
 check(px(315,6)==encoded(0xffffff) and px(315,3)==encoded(0x303030),'format '+str(fmt)+': correct peak and background')
 changed=[]
 data=uc.mem_read(fb,320*240*bpp)
 for i,v in enumerate(data):
  if v:changed.append(i//bpp)
 check(all(((i%320 in range(314,317)) or (i%320 in range(126,166))) and (i//320 in range(3,17)) for i in changed),'format '+str(fmt)+': framebuffer writes confined to meter and numeric overlay')
for avg,col in [(0,0x3186),(800,0xfd80),(950,0xf945),(65535,0xf945)]:
 uc,fb=display_test(4,2,avg=avg,peak=0)
 actual=struct.unpack('<H',uc.mem_read(fb+(16*320+315)*2,2))[0]
 check(actual==col,'load color/clamping at '+str(avg))
for kwargs in [dict(magic=0),dict(invalid='width'),dict(invalid='end'),dict(fmt=4,bpp=4),dict(fmt=9,bpp=2)]:
 opts=dict(fmt=4,bpp=2);opts.update(kwargs);uc,fb=display_test(**opts)
 check(bytes(uc.mem_read(FB,0x140000))==bytes(0x140000),'no pixel writes for unsupported descriptor or absent report: '+str(kwargs))
for layer,parity in [(0,0),(0,1),(1,1)]:
 uc,fb=display_test(4,2,layer=layer,parity=parity)
 check(uc.mem_read(fb+(16*320+315)*2,2)!=b'\0\0','correct backbuffer selected for layer/parity '+str((layer,parity)))
print('\n'+str(COUNT)+' checks passed. Emulator tests do not establish physical hardware reliability.')

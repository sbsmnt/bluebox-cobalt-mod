"""Cold application context allocation/defaults and application-only patch bounds.
This executes the M4 context initializer, not device boot or a bootloader.
Only arena allocation, filename storage setup and IPC transport are boundaries.
"""
from firmware_harness import *
import firmware_harness as H
import json,base64
APP=0x300173d0;TMP=0x24008000
IDS=list(range(0x1bf,0x1d5))+list(range(0x1d7,0x1de));DEFAULTS=[0,0,1,500,350,500,5,150,500,0,20000,1000,0,0,0,0,500,0,0,0,0,0]+[1000,0,0,0,0,0,0]
reference=json.loads((ROOT/'tests/references/cobalt-synth-S2-manifest.json').read_text())
prior=bytearray(stock)
for patch in reference['patches']:
 offset=patch['offset'];new=base64.b64decode(patch['new']);prior[offset:offset+len(new)]=new
check(hashlib.sha256(prior).hexdigest()==reference['patched_sha256'],'failure reference is the exact previously shipped S2 application')

def context(image):
 u=machine(image,cpu_model=UC_CPU_ARM_CORTEX_M4);heap=[0xc0010000];allocations=[]
 def allocate(q):
  size=q.reg_read(UC_ARM_REG_R0);a=heap[0];heap[0]=(a+size+15)&~15;allocations.append(size);q.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,allocate)
 for address in (0x81089c2,0x811fe5a,0x8108784):stub(u,address,lambda q:None)
 call(u,0x8126cbc,APP)
 return u,heap,allocations
old,_,_=context(bytes(prior));u,heap,allocations=context(patched)
for ch in range(12):
 setting=APP+0xc08+ch*0x30
 check(struct.unpack('<HH',old.mem_read(setting+10,4))==(68,68) and call(old,0x8120234,setting,0x1cf)==0,'S2 reproduces undersized cold array and missing envelope decay on track '+str(ch))
 check(struct.unpack('<HH',u.mem_read(setting+10,4))==(94,112) and [call(u,0x8120234,setting,pid) for pid in IDS]==DEFAULTS,'complete cold M4 application initializer retains all synth defaults on track '+str(ch))
check(allocations.count(896)==23 and allocations.count(48)==1024,'cold allocation: the twelve track arrays and the 11 other parameter sets at 112 entries (spectral, as combo); 1024 small native arrays stay small')
# Repeated resets reuse the 88-entry allocations; source parameter buffers do
# not grow on every project load. Existing smaller arrays upgrade once.
setting=APP+0xc08;ptr=u32(u,setting+4);before=heap[0]
u.mem_write(TMP,b'\1')
for repeat in range(5):call(u,0x81202b8,setting,TMP)
check(u32(u,setting+4)==ptr and heap[0]==before,'repeated project resets reuse the correctly sized track allocation')
small=TMP+0x100;call(u,0x8120202,small,68);smallptr=u32(u,small+4)
call(u,0x81202b8,small,TMP);upgrade=u32(u,small+4);before=heap[0]
call(u,0x81202b8,small,TMP)
check(upgrade!=smallptr and u32(u,small+4)==upgrade and heap[0]==before and struct.unpack('<HH',u.mem_read(small+10,4))==(94,112),'legacy preallocated arrays upgrade only once before defaults')
for kind in range(2,9):
 setting=TMP+0x200+kind*0x20;u.mem_write(TMP,bytes([kind]));call(u,0x81202b8,setting,TMP)
 check(struct.unpack('<H',u.mem_read(setting+12,2))[0]==112,'lazy non-track settings use the parameter-set capacity 112 for kind '+str(kind))
# The generated patch set cannot contain bytes outside this application image.
check(all(0x08041000<=p['address']<0x08040000+len(stock) and p['offset']==p['address']-0x08040000 for p in M['patches']),'all patches are inside application firmware, beyond its initial startup block')
check(patched[:0x1000]==stock[:0x1000] and patched[0xc0000:0xc1000]==stock[0xc0000:0xc1000],'both application vector and early startup blocks remain byte-identical')
check(M['synth_experiment']['source_allocation']==0xa20,'source allocation is 0xa20: the 0xa00 boot-tested through S9 plus 0x20 for LFO2 (S10, boot untested)')
# Backup SRAM survives firmware updates. Before a new graph is constructed,
# the retained S1/S2 registry must be rejected, without touching its pointers.
REG=0x38800180;MAP=0xc0200000;MSG=TMP+0x600
for tag,image,fn in [('S2',bytes(prior),reference['symbols']['synth7']['synth_midi_dispatch']),('S3',patched,M['symbols']['synth7']['synth_midi_dispatch'])]:
 q=machine(image);w32(q,REG,0x53594e31);w32(q,REG+4,0xdead0000)
 q.mem_write(MSG,bytes([0x90,60,127,0,3]));forwarded=[]
 stub(q,0x804f0c4,lambda v:forwarded.append(True))
 failed=False
 try:call(q,fn,MAP,MSG,0x30000000)
 except UcError:failed=True
 if tag=='S2':check(failed and not forwarded,'previous S2 dereferences a retained old-layout graph pointer before construction')
 else:check(not failed and forwarded and all(u32(q,REG+4+i*4)==0 for i in range(12)),'S3 invalidates retained S1/S2 pointers before MIDI and preserves native mapping')
q=machine();w32(q,REG,0x53594e32);w32(q,REG+4,0xdead0000)
q.mem_write(MSG,bytes([0x90,60,127,0,3]));forwarded=[]
stub(q,0x804f0c4,lambda v:forwarded.append(True))
call(q,M['symbols']['synth7']['synth_midi_dispatch'],MAP,MSG,0x30000000)
check(forwarded and all(u32(q,REG+4+i*4)==0 for i in range(12)),'S3 also rejects retained S2a filter-state semantics before MIDI')
q=machine();w32(q,REG,0x53594e33);w32(q,REG+4,0xdead0000)
q.mem_write(MSG,bytes([0x90,60,127,0,3]));forwarded=[]
stub(q,0x804f0c4,lambda v:forwarded.append(True));call(q,M['symbols']['synth7']['synth_midi_dispatch'],MAP,MSG,0x30000000)
check(forwarded and all(u32(q,REG+4+i*4)==0 for i in range(12)),'S3 rejects the smaller S2b voice layout before MIDI')
print(H.checks,'application startup allocation and patch-boundary checks passed; hardware boot is unverified.')

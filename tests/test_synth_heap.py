"""Actual M7 malloc/source factory under a controlled bounded-heap fixture.
32 KiB is a synthetic remaining budget, not measured free memory on hardware.
Unlike the audio harness, this does not replace malloc or source construction.
"""
from firmware_harness import *
import firmware_harness as H
import base64,json
BASE=0x2400a628;END=0x240476b8;BUDGET=32768
reference=json.loads((ROOT/'tests/references/cobalt-synth-S3-manifest.json').read_text())
prior=bytearray(stock)
for patch in reference['patches']:
 b=base64.b64decode(patch['new']);prior[patch['offset']:patch['offset']+len(b)]=b
check(hashlib.sha256(prior).hexdigest()==reference['patched_sha256'],'failure reference reconstructs the exact shipped S3 application')

def sources(image):
 q=machine(image)
 # Seed the native malloc free list within its literal SRAM bounds.
 w32(q,0x240001e4,BASE);w32(q,BASE,0);w32(q,BASE+4,END-BASE)
 call(q,0x806868c,END-BASE-BUDGET-16)
 check(u32(q,BASE+4)==BUDGET,'native allocator leaves the controlled 32 KiB source budget')
 def fatal(q,address,size,data):raise MemoryError('native fatal allocation loop')
 q.hook_add(UC_HOOK_CODE,fatal,begin=0x8064a38,end=0x8064a38)
 pointers=[];failed=False
 try:
  for ch in range(12):pointers.append(call(q,0x8056e20))
 except MemoryError:failed=True
 return q,pointers,failed
q,pointers,failed=sources(bytes(prior))
check(failed and len(pointers)==9,'S3 exhausts the bounded native heap before twelve tracks are constructed')
q,pointers,failed=sources(patched)
check(not failed and len(pointers)==12 and len(set(pointers))==12,'S4 constructs all twelve distinct sources through the real native allocator/factory')
for p in pointers:
 check(BASE<p<END and u32(q,p+0x5a8)==0x53594e3a,'new compact voice is initialized in its owned native heap block')
check(all(abs(pointers[i]-pointers[i-1])==0xa30 for i in range(1,12)),'all source allocation strides match the 0xa20 request (S10: 0xa00 + 0x20 for LFO2) including native overhead')
check(reference['synth_experiment']['source_allocation']-M['synth_experiment']['source_allocation']==1024-0x20,'S10 still reserves 992 bytes less per source than S3 (S4 removed 1024, LFO2 adds 32)')
# Reject retained pointers from the immediately preceding S3 layout too.
r=machine();w32(r,0x38800180,0x53594e34);w32(r,0x38800184,0xdead0000)
MSG=0x24008000;r.mem_write(MSG,bytes([0x90,60,127,0,3]));forwarded=[]
stub(r,0x804f0c4,lambda q:forwarded.append(True))
call(r,M['symbols']['synth7']['synth_midi_dispatch'],0xc0200000,MSG,0x30000000)
check(forwarded and all(u32(r,0x38800184+ch*4)==0 for ch in range(12)),'S4 rejects S3 retained graph pointers before any MIDI dereference')
r=machine();w32(r,0x38800180,0x53594e35);w32(r,0x38800184,0xdead0000)
r.mem_write(MSG,bytes([0x90,60,127,0,3]));forwarded=[]
stub(r,0x804f0c4,lambda q:forwarded.append(True))
call(r,M['symbols']['synth7']['synth_midi_dispatch'],0xc0200000,MSG,0x30000000)
check(forwarded and all(u32(r,0x38800184+ch*4)==0 for ch in range(12)),'S4 rejects the boot-confirmed S3a layout before any MIDI dereference')
r=machine();w32(r,0x38800180,0x53594e36);w32(r,0x38800184,0xdead0000)
r.mem_write(MSG,bytes([0x90,60,127,0,3]));forwarded=[]
stub(r,0x804f0c4,lambda q:forwarded.append(True))
call(r,M['symbols']['synth7']['synth_midi_dispatch'],0xc0200000,MSG,0x30000000)
check(forwarded and all(u32(r,0x38800184+ch*4)==0 for ch in range(12)),'S5 rejects the hardware-tested S4 layout before MIDI')
print(H.checks,'native bounded-heap checks passed; full device startup memory remains unmeasured.')

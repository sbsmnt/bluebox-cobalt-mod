"""S7b/S8a: every native preallocation of the twelve track settings lists holds
the 80 Cobalt entries (S8a adds Insert On) with headroom. At 68 slots a full list overran into the following array
(observed on hardware: track 12 carried the FX delay's settings, and loading
zeroed FX1/FX2 sends). Real native allocator/reset/defaults; allocation only stubbed."""
from firmware_harness import *
import firmware_harness as H
import capstone
md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_THUMB|capstone.CS_MODE_MCLASS)
def imm(image,at):
 i=next(md.disasm(image[at-0x08040000:at-0x08040000+2],at));return i.mnemonic,i.op_str
SITES={0x0811e164:'r6',0x0811e26e:'r6',0x08126cce:'r7'}
for at,reg in SITES.items():
 check(imm(stock,at)==('movs',reg+', #0x44'),'stock track-list loop preallocates 68 at '+hex(at))
 check(imm(patched,at)==('movs',reg+', #0x70'),'patched track-list loop preallocates 112 at '+hex(at)) # spectral, as combo: parameter sets 112
# Fill an 88-slot preallocation with the real track defaults and append past
# them: the neighbouring array must be untouched and capacity must hold.
u=machine();heap=[0xc0010000]
def allocate(q):
 a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,allocate);call(u,0x8134cb8,0)
SET=0x24060000;NEXT=0x24060040;TMP=0x24008000
for s in (SET,NEXT):u.mem_write(s,bytes(16));call(u,0x81201c8,s)
call(u,0x8120202,SET,112);call(u,0x8120202,NEXT,14)
neighbour=bytes(u.mem_read(u32(u,NEXT+4),14*8))
u.mem_write(TMP,b'\1');call(u,0x81202b8,SET,TMP)
n,cap=struct.unpack('<HH',u.mem_read(SET+10,4))
check(n==94 and cap==112,'all 94 track defaults (synth 87 + chord + DJ EQ 6) fit a 112-slot preallocation, 18 free')
for extra in range(30):call(u,0x812028e,SET,0x500+extra,1)
check(struct.unpack('<HH',u.mem_read(SET+10,4))==(112,112),'native add stops at capacity')
check(bytes(u.mem_read(u32(u,NEXT+4),14*8))==neighbour,'the following settings array is never overwritten')
check(u32(u,NEXT+4)>=u32(u,SET+4)+112*8,'112 entries are really allocated before the next array')
print(H.checks,'track settings capacity checks passed; device load/save still needs hardware.')

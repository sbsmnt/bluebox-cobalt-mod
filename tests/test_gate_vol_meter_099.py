"""Gate selects exactly the native Vol reference and stock publication code."""
from gate_fixture import *
u,_,_=gsetup();mix=0x24048000;channel=0x24049000
for offset in (-60.,-20.,0.,6.):
 results=[]
 for pid in (4,0x1ae):
  u.mem_write(mix+0xe6c,struct.pack('<H',pid));u.mem_write(channel+0x30,struct.pack('<f',offset))
  u.reg_write(UC_ARM_REG_R8,mix);u.reg_write(UC_ARM_REG_R4,channel)
  call(u,0x804f872,end=0x804f8fe)
  results.append(u.reg_read(UC_ARM_REG_S1))
 check(results[0]==results[1]==struct.unpack('<I',struct.pack('<f',offset))[0],'native Gate/Vol selection loads identical reference '+str(offset))
# exp.7 reinstates the publication overrides: Gate-selected MIXER meters show
# the detector input on the Gate fader's scale (test_exp7_gate_granular.py).
for at in (0x804f8be,0x804f9c6,0x804f9e0):
 check(any(p['name']=='Gate input meter '+hex(at) for p in M['patches']),'Gate meter publication override installed '+hex(at))
# Native reporting calls retain actual values regardless of selected parameter.
seen=[]
for address,num in ((0x804bab0,1),(0x804bacc,2)):
 def take(q,num=num):seen.append([q.reg_read(r) for r in (UC_ARM_REG_S0,UC_ARM_REG_S1)[:num]])
 stub(u,address,take)
for pid in (4,):  # Gate (0x1ae) is mapped by design since exp.7
 u.mem_write(mix+0xe6c,struct.pack('<H',pid));u.reg_write(UC_ARM_REG_R8,mix)
 for at in (0x804f8be,0x804f9c6,0x804f9e0):
  expected=[struct.unpack('<I',struct.pack('<f',x))[0] for x in (-56.,-61.)]
  for r,v in zip((UC_ARM_REG_S0,UC_ARM_REG_S1),expected):u.reg_write(r,v)
  call(u,at,0x2404a000,0,end=at+4)
  check(seen[-1]==expected[:len(seen[-1])],'native publication retains Vol/Gate dB values '+str((pid,hex(at))))
# exp.6 rewrites the gate scan/gain loop with identical arithmetic
# (bit-identity: test_exp6_gate_granular.py); native Vol metering retained.
print(str(H.checks)+' v1.0.0-alpha.4 Vol/Gate meter checks passed; physical display remains a hardware check.')

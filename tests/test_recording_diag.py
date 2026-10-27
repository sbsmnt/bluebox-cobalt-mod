"""Native header status text and Tools identification on the full mod.
The CPU % number was removed in v1.0.0; only the CPU bar remains.
"""
from firmware_harness import *
import firmware_harness as H
import json,base64
S=M['symbols']['4'];APP=0x24040000;PAGES=APP+0x280;HEADER=PAGES+0x3c;TEXT=HEADER+0xe4+0x31
check(not any(k in S for k in ('cpu_numeric_draw','cpu_numeric_tick','cpu_header_has_error')),'no CPU % number code in the M4 payload')
marker=b'Cb 1.1a3'
u=machine(patched);w32(u,0x3000d9fc,APP);call(u,0x813961c,HEADER)
raw=[0]
def status(q):
 data=bytearray(40);struct.pack_into('<Q',data,16,128*1024*1024);struct.pack_into('<I',data,24,raw[0]);struct.pack_into('<I',data,28,64);q.mem_write(q.reg_read(UC_ARM_REG_R1),bytes(data));q.reg_write(UC_ARM_REG_R0,1)
stub(u,0x8121fc4,status);stub(u,0x813a244,lambda q:None);stub(u,0x81403b2,lambda q:q.reg_write(UC_ARM_REG_R0,48))
for count,label in ((0,b'128MB'),(2432,b'ERROR 2.4K'),(4000,b'ERROR 4.0K'),(10000,b'ERROR 10K')):
 raw[0]=count;call(u,0x81397b8,HEADER,0x24038000)
 check(bytes(u.mem_read(TEXT,32)).split(b'\0')[0]==label,'native header status text intact '+label.decode())
# Real Tools constructor, marker and official version.
heap=[0xc0010000]
def allocate(q):a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,allocate);call(u,0x8134cb8,0);TOOLS=0xc0140000;call(u,0x813140c,TOOLS)
check(bytes(u.mem_read(TOOLS+0xd4+0x65,32)).split(b'\0')[0]==marker,'native Tools identifies '+marker.decode())
check(bytes(u.mem_read(TOOLS+0x34+0x65,32)).split(b'\0')[0]==b'1.5.2','official version remains 1.5.2')
# Profiler and Gate M7 remain exp2. exp.4 deliberately optimizes the master
# (gr7) and Granular/Shoegaze (delay7); test_exp4_performance.py covers them.
ref=json.loads((ROOT/'tests/references/recording-baseline.json').read_text())
for name,digest in ref['m7_payloads'].items():
 if name not in ('gr7.bin','delay7.bin','gate7.bin'):check(hashlib.sha256((ROOT/'build'/name).read_bytes()).hexdigest()==digest,'full-mod retains exp2 '+name)
print(H.checks,'header status and Tools identification checks passed; SD timing requires hardware.')

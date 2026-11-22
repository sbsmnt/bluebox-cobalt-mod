"""Isolated steady native source callback: S6a/S7 PCM and ARM instructions; Wavetable slot is now Cluster.
Instructions are not hardware cycles, nor a prediction of full-rig CPU percent.
"""
from firmware_harness import *
import firmware_harness as H
import base64,json
ref=json.loads((ROOT/'tests/references/cobalt-synth-S6a-manifest.json').read_text());old=bytearray(stock)
for p in ref['patches']:
 b=base64.b64decode(p['new']);old[p['offset']:p['offset']+len(b)]=b
check(hashlib.sha256(old).hexdigest()==ref['patched_sha256'],'performance reference reconstructs exact shipped S6a')
SRC=0xc0100000;NODE=0x24040000;VT=NODE+0x100

def source(image,symbols,engine):
 u=machine(image);call(u,symbols['synth_source_construct'],SRC)
 w32(u,SRC+0x18,0);u.mem_write(SRC+0x1e,struct.pack('<H',0));w32(u,SRC+8,NODE)
 w32(u,NODE,VT);w32(u,VT+12,NEXT_FN|1);pcm=[]
 def sink(q):
  bus=CTX+0x150e4
  pcm.append(bytes(q.mem_read(u32(q,bus+8),128))+bytes(q.mem_read(u32(q,bus+12),128)))
  q.reg_write(UC_ARM_REG_R0,7)
 stub(u,NEXT_FN,sink)
 u.mem_write(CTX+0x150e4,struct.pack('<IIIIBBxx',32,32,BUF,BUF+0x100,1,1))
 for i,(pid,value) in enumerate([(0x1bf,1),(0x1c8,1),(0x1c0,engine)]):u.mem_write(CTX+4+i*24,struct.pack('<B7xIHHiI',0x39,0,pid,0,value,0))
 w32(u,CTX+4+0x600,3);call(u,symbols['synth_source_process'],SRC,CTX);w32(u,CTX+4+0x600,0)
 for i in range(256):call(u,symbols['synth_source_process'],SRC,CTX)
 pcm.clear();count=[0]
 def instruction(q,a,n,data):count[0]+=1
 hook=u.hook_add(UC_HOOK_CODE,instruction,begin=0x8040000,end=0x8150000)
 for i in range(32):call(u,symbols['synth_source_process'],SRC,CTX)
 u.hook_del(hook)
 return b''.join(pcm),count[0]
records=[]
# F2: two engines. VA is now ordinal 0 (S6a's slot 2), Cluster ordinal 1
# (S6a's slot 3, then Wavetable); S6a's FM / Shape are gone.
for engine,name,s6a_slot in [(0,'Virtual Analog',2),(1,'Cluster (was Wavetable)',3)]:
 expected,before=source(bytes(old),ref['symbols']['synth7'],s6a_slot)
 actual,after=source(patched,M['symbols']['synth7'],engine)
 if engine==0:check(actual==expected,'primary stereo-duplicated output stays PCM bit-identical to S6a '+name)
 else:check(actual!=expected and any(actual),'Cluster replaces Wavetable output')
 print('INFO',name,before,after)
 # S10: the second LFO adds ~37 instructions per block while Off (~0.7% here).
 # S14 (Swarm removed) measured FM 1.011 / Waveshaping 1.018 against S13's
 # 1.008 / 1.012: compiler layout, about 0.03% CPU per voice. S15 adds the
 # output gain stage (~18 instructions per block at 0 dB): 1.013 / 1.021.
 # Limit 2.5%.
 check(after<=before*1.025 if engine==0 else after<before,name+(' source stays within 2.5% of S6a instructions' if engine==0 else ' costs fewer ARM instructions than Wavetable'))
 record=dict(engine=name,blocks=32,s6a_instructions=before,s7_instructions=after,reduction_percent=100*(before-after)/before)
 records.append(record);print('INFO',json.dumps(record))
check(sum(r['s7_instructions'] for r in records)<sum(r['s6a_instructions'] for r in records),'combined two-engine source instruction cost improves')
(ROOT/'tests/synth-performance.json').write_text(json.dumps(dict(version=M['version'],hardware_cycles_measured=False,records=records),indent=2)+'\n')
print(H.checks,'native synth performance checks passed; retest actual CPU during recording.')

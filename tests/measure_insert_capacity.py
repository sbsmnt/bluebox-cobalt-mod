"""Reproduce the exact-release vs I5 native-callback instruction footprint.
Usage: PYTHONPATH=/path/to/deps python3 tests/measure_insert_capacity.py BLUEBOX.BIN
No device CPU percentage, physical cache or SD/RTOS timing is inferred.
"""
from gate_fixture import *
import base64,json,gc
reference=json.loads((ROOT/'tests/references/cobalt-1.0.0-manifest.json').read_text())
release=bytearray(stock)
for p in reference['patches']:
 data=base64.b64decode(p['new']);release[p['offset']:p['offset']+len(data)]=data
release=bytes(release)
assert hashlib.sha256(release).hexdigest()=='062ce9e3343247c048a4494afa9a385fb2cdf9b08965e1bd5291679c088821ba'
records=[]
for stereo in (False,True):
 for name,image,mode in [('Cobalt 1.0.0 Off',release,0),('I5 Off',patched,0),('I5 CLIP',patched,1),('I5 FOLD',patched,3),('I5 WRAP',patched,4)]:
  u,_,_=gsetup(image=image,stereo=stereo)
  for b in range(300):
   ev=[(0x1ba,mode),(0x1bb,750)] if b==0 and image==patched else []
   gblock(u,signal(.2,b),signal(.1,b,719) if stereo else None,ev)
  counter=[0]
  def count(q,a,n,data):counter[0]+=1
  hook=u.hook_add(UC_HOOK_CODE,count);u.ctl_remove_cache(0x8000000,0x8200000)
  gblock(u,signal(.2,300),signal(.1,300,719) if stereo else None)
  u.hook_del(hook);records.append(dict(build=name,stereo=stereo,instructions=counter[0]))
  del u;gc.collect()
report=dict(version=M['version'],firmware_sha256=M['patched_sha256'],frames=32,gate='Off',eq='native flat/bypass',drive_percent=75,tone_percent=65,mix_percent=100,output_db=0,
            native_callbacks=records,additional_heap_bytes=1196,additional_ui_bytes=28,
            hardware_cpu_measured=False,limitations=['Executed instructions are not cycles or whole-device CPU percentages.','The final audio sink is stubbed.','Physical cache latency, other FX, interrupts, RTOS, recording and SD/DMA are not represented.'])
(ROOT/'tests/insert-capacity.json').write_text(json.dumps(report,indent=2)+'\n')
for row in records:print(row)

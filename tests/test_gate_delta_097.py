"""Real ARM PCM regression against the preserved v0.9.6 gate release."""
from gate_fixture import *
import importlib.util,os
baseline=(ROOT/'tests'/'references'/'v0.9.6-gate')
spec=importlib.util.spec_from_file_location('gate096',baseline/'patch_bluebox.py');old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
check(old.MANIFEST['patched_sha256']=='87ce7b8b99acc98b3f34347914c8dd1736986c80f332bf2300efb94c50e8aeda','exact v0.9.6 baseline retained')
for ch in range(12):
 a,_,_=gsetup(old.transform(stock),ch=ch);b,_,_=gsetup(ch=ch);same=True
 for i in range(700):
  ev=[(0x1ae,{-1:-96000,0:-80000,1:-40000,2:-96000,3:-70000}[i//175])] if i%175==0 else []
  if i%83==0:ev+=[(0x12e,(i//83)&1)]
  amp=.1 if i%150<30 else .0001 if i%150<100 else .000001
  kwargs=dict(ch=ch,frames=[32,17,31,1][(i//100)%4],silent=(i%71==0))
  l=signal(amp,i,731);r=signal(amp*.2,i,411)
  oa,ra=gblock(a,l,r,ev,**kwargs);ob,rb=gblock(b,l,r,ev,**kwargs)
  same &= oa==ob and ra==rb
 check(same,'Gate PCM/return remains bit-identical to v0.9.6 through edits, EQ bypass, silence and short frames, track'+str(ch+1))
print(str(H.checks)+' gate v0.9.6/v1.0.0-alpha.4 ARM audio regression checks passed.')

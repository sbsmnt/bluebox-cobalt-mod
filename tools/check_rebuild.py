"""Verify all 25 generated artifacts reproduce across two fresh rebuilds."""
import hashlib,json,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
files=[*(f'build/{name}.{suffix}' for name in ('cm7','cm4','gr7','delay7','gate7','synth7') for suffix in ('elf','bin','symbols')),
       'build/pcmhot.bin','build/pcmdm.bin','build/pcmds.bin','patch_manifest.json','patch_bluebox.py','patch_bluebox.html','docs/index.html']
def snapshot():return {p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in files}
initial=snapshot();runs=[]
for n in (1,2):
    p=subprocess.run(['sh',str(root/'build.sh'),str(pathlib.Path(sys.argv[1]).resolve())],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (root/'tests'/f'rebuild-{n}.log').write_bytes(p.stdout)
    if p.returncode:raise SystemExit(p.stdout.decode(errors='replace'))
    current=snapshot()
    if current!=initial:raise SystemExit('Rebuild mismatch: '+str([name for name in files if initial[name]!=current[name]]))
    runs.append(dict(run=n,identical=True,files=len(files)))
(root/'tests/build-reproducibility.json').write_text(json.dumps(dict(artifacts=initial,runs=runs),indent=2)+'\n')
print('PASS',len(files),'generated artifacts identical across two rebuilds')

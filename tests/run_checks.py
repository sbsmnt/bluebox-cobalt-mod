"""Run the emulator, host-sanitizer and patcher suites; keep each log.
Usage: python3 tests/run_checks.py /path/to/original/BLUEBOX.BIN [--browser]
Requires the packages in tests/requirements.txt, Node.js and a host C compiler.
--browser also runs test_browser.cjs (needs the playwright Node package).
"""
import concurrent.futures,os,pathlib,subprocess,sys,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
stock=str(pathlib.Path(sys.argv[1]).resolve())
commands={p.name:[sys.executable,str(p),stock] for p in sorted((root/'tests').glob('test_*.py'))}
commands['test_web_logic.cjs']=['node',str(root/'tests/test_web_logic.cjs'),stock]
commands['test_theme_preview.cjs']=['node',str(root/'tests/test_theme_preview.cjs')]
if '--browser' in sys.argv[2:]:commands['test_browser.cjs']=['node',str(root/'tests/test_browser.cjs'),stock]
env=os.environ.copy()
failed=[]
def run(item):
 name,cmd=item
 p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env)
 log=root/'tests'/(pathlib.Path(name).stem+'.log');log.write_bytes(p.stdout)
 return name,p.returncode,p.stdout.decode(errors='replace').splitlines()[-3:]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for name,rc,tail in pool.map(run,commands.items()):
  print(('FAIL' if rc else 'OK'),name,flush=True)
  print('\n'.join(tail),flush=True)
  if rc:failed.append(name)
for source,logname in [('granular_host.c','granular-host.log'),('delay_types_host.c','delay-types-host.log'),('gate_host.c','gate-host.log'),('shoegaze_host.c','shoegaze-host.log'),('tape_host.c','tape-host.log')]:
 with tempfile.TemporaryDirectory(prefix='bluebox-host-check-') as td:
  binary=str(pathlib.Path(td)/'host-check')
  subprocess.run(['cc','-std=c11','-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined',str(root/'tests'/source),'-lm','-o',binary],check=True)
  hostenv=env|{'ASAN_OPTIONS':'detect_leaks=0'}
  p=subprocess.run([binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=hostenv)
  (root/'tests'/logname).write_bytes(p.stdout)
  print(p.stdout.decode(errors='replace'),end='')
  if p.returncode:failed.append(source+' ASan/UBSan')
if failed:raise SystemExit('FAILED: '+', '.join(failed))
print('All emulator, patcher and sanitized host suites passed. No physical hardware or CPU-cycle measurement.')

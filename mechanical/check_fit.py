from pathlib import Path
import subprocess,concurrent.futures,json,os,shutil
source=Path(__file__).resolve().parent
r=source.parent/'local'/'mechanical'/'export';v=r/'validation'
v.mkdir(parents=True,exist_ok=True)
scad=os.environ.get('OPENSCAD') or shutil.which('openscad')
if not scad:raise SystemExit('Set OPENSCAD to the executable, or add openscad to PATH.')
def check(name):
 p=subprocess.run([scad,'--hardwarnings','--export-format','asciistl','-D',f'part="{name}"','-o',str(v/(name+'.stl')),str(source/'enclosure.scad')],capture_output=True,text=True)
 (v/(name+'.log')).write_text(p.stdout+p.stderr)
 empty='Current top level object is empty' in p.stderr
 assert 'WARNING:' not in p.stderr and 'ERROR:' not in p.stderr,(name,p.stderr)
 assert p.returncode==0 or (p.returncode==1 and empty),(name,p.stderr)
 vs=[]
 if not empty and (v/(name+'.stl')).exists():
  vs=[tuple(map(float,l.split()[1:])) for l in (v/(name+'.stl')).read_text().splitlines() if l.strip().startswith('vertex')]
 result={'name':name,'returncode':p.returncode,'empty':empty,'bounds':[(min(t[i] for t in vs),max(t[i] for t in vs)) for i in range(3)] if vs else None}
 print(result,flush=True);return result
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:res=list(pool.map(check,['interference','device_check','glue_interference','routing_check','slide_check']))
(v/'fit-check.json').write_text(json.dumps(res,indent=2))

for q in res:
 if q['name']=='interference':
  assert q['empty'] or (q['bounds'] is not None and all(abs(z-17.2)<1e-5 for z in q['bounds'][2])),q
 else:assert q['empty'],q
print('PASS: nominal fits and insertion path; physical fit still requires trial assembly.')

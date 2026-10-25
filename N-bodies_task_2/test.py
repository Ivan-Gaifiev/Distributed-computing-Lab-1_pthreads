"""python test.py ./nbody[.exe] -- solar generator and integration smoke tests."""
import csv,math,subprocess,sys,tempfile
from pathlib import Path
from generate_bodies import generate
exe=str(Path(sys.argv[1]).resolve());root=Path(__file__).resolve().parent
# Explicit local parent avoids platform-specific restricted temporary directories.
with tempfile.TemporaryDirectory(dir=root) as tmp:
 d=Path(tmp)
 def run(inp,t,p,ok=True):
  r=subprocess.run([exe,str(inp),str(t),str(p)],cwd=d,capture_output=True,text=True,timeout=20)
  assert (r.returncode==0)==ok,(r.returncode,r.stderr)
  if ok:
   with (d/'output_coordinates.csv').open() as f:return [[float(x) for x in row] for row in csv.reader(f,delimiter=';')]
 inp=d/'bodies.txt';other=d/'same.txt';generate(37,inp,42);generate(37,other,42)
 assert inp.read_bytes()==other.read_bytes()
 base=run(inp,5400,1)
 assert [row[0] for row in base]==[0,3600,5400]
 for p in (2,4):
  a=run(inp,5400,p)
  assert len(a)==len(base)
  assert all(math.isclose(x,y,abs_tol=1e-3,rel_tol=1e-12) for u,v in zip(a,base) for x,y in zip(u,v))
 assert len(run(inp,0,4))==1
 pair=d/'pair.txt';pair.write_text('2\n2 0 0 0 1 0 0\n3 2 0 0 0 0 0\n')
 assert run(pair,3600,4)[1]==[3600,3600,0,2,0]
 pair.write_text('2\n1 0 0 0 0 0 0\n1 0 0 0 0 0 0\n');run(pair,3600,4,False)
print('PASS: reproducible generator, 1/2/4-thread agreement, partial step, zero duration, explicit Euler, collision termination.')

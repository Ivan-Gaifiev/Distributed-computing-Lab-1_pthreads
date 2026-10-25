import argparse,csv,json,math,os,platform,random,statistics,subprocess,time
from pathlib import Path
from generate_bodies import generate

def read_csv(path):
    with path.open() as f:return [[float(x) for x in r] for r in csv.reader(f,delimiter=';')]
def main():
    p=argparse.ArgumentParser()
    p.add_argument('--exe',required=True);p.add_argument('--serial',required=True)
    p.add_argument('--sizes',type=int,nargs='+',default=[256,1024,4096])
    p.add_argument('--threads',type=int,nargs='+',default=[1,2,4,8])
    p.add_argument('--steps',type=int,default=24);p.add_argument('--repeats',type=int,default=3)
    p.add_argument('--out',default='results');a=p.parse_args()
    if min(a.sizes+a.threads+[a.steps,a.repeats])<1:p.error('positive values required')
    exes={'serial':str(Path(a.serial).resolve()),'pthread':str(Path(a.exe).resolve())}
    out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
    runs=out/'runs';runs.mkdir(exist_ok=True)
    raw=[];summary=[];rng=random.Random(42)
    for n in a.sizes:
        inp=out/f'bodies_{n}.txt';generate(n,inp)
        cases=[('serial',0)]+[('pthread',p) for p in a.threads]
        samples={case:[] for case in cases};trajectories={}
        # One unmeasured warm-up per case. Each run has its own directory.
        for rep in range(-1,a.repeats):
            order=cases.copy();rng.shuffle(order)
            for kind,threads in order:
                d=runs/f'n{n}_{kind}_{threads}_{rep}';d.mkdir(exist_ok=True)
                cmd=[exes[kind],str(inp),str(a.steps*3600)]
                if kind=='pthread':cmd.append(str(threads))
                start=time.perf_counter();r=subprocess.run(cmd,cwd=d,capture_output=True,text=True,timeout=300);elapsed=time.perf_counter()-start
                if r.returncode:raise RuntimeError(f'{cmd}: {r.stderr}')
                data=read_csv(d/'output_coordinates.csv')
                if len(data)!=a.steps+1 or any(len(row)!=1+2*n for row in data):raise AssertionError('wrong CSV shape')
                if any(not math.isfinite(x) for row in data for x in row):raise AssertionError('nonfinite output')
                if data[-1][0]!=a.steps*3600:raise AssertionError('wrong final time')
                if rep>=0:
                    raw.append(dict(n=n,kind=kind,threads=threads,repeat=rep+1,seconds=elapsed));samples[(kind,threads)].append(elapsed)
                trajectories[(kind,threads)]=data
                # Keep only input + raw timings, avoid retaining large intermediate CSVs.
                (d/'output_coordinates.csv').unlink();d.rmdir()
        base=trajectories[('serial',0)];serial=statistics.median(samples[('serial',0)])
        for kind,threads in cases:
            diff=0.
            for rr,bb in zip(trajectories[(kind,threads)],base):
                for x,y in zip(rr,bb):
                    diff=max(diff,abs(x-y))
                    if abs(x-y)>1e-3+1e-12*max(abs(x),abs(y)):raise AssertionError('trajectory mismatch')
            median=statistics.median(samples[(kind,threads)])
            mean=statistics.mean(samples[(kind,threads)])
            one_mean=statistics.mean(samples[('pthread',1)]) if ('pthread',1) in samples else None
            summary.append(dict(n=n,kind=kind,threads=threads,median_seconds=median,mean_seconds=mean,stdev_seconds=statistics.stdev(samples[(kind,threads)]) if len(samples[(kind,threads)])>1 else 0,speedup_vs_one_worker=(one_mean/mean if kind=='pthread' and one_mean else ''),efficiency_vs_one_worker=(one_mean/(threads*mean) if kind=='pthread' and one_mean else ''),min_seconds=min(samples[(kind,threads)]),max_seconds=max(samples[(kind,threads)]),speedup=serial/median,max_abs_difference_m=diff))
        print(f'N={n} done',flush=True)
    for name,data in [('timings_raw.csv',raw),('timings_summary.csv',summary)]:
        with (out/name).open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
    meta=dict(platform=platform.platform(),processor=platform.processor(),logical_cpus=os.cpu_count(),steps=a.steps,repeats=a.repeats,threads=a.threads,sizes=a.sizes,timer='perf_counter, whole subprocess wall time including startup, input, allocation, barriers, Euler, CSV and cleanup',baseline='independent serial_baseline.c; same flags, dt, input and CSV precision',date='2026-10-25',tolerance='abs=1e-3 m + rel=1e-12',warmup='1 per case; randomized case order; runs sequential')
    (out/'environment.json').write_text(json.dumps(meta,indent=2))
    print(json.dumps(summary,indent=2))
if __name__=='__main__':main()

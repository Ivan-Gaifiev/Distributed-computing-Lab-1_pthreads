import argparse, math, random
from pathlib import Path

def generate(n, path, seed=42):
    if n<1: raise ValueError('N must be positive')
    rng=random.Random(seed)
    # Grid prevents near-zero initial separations; jitter removes exact symmetry.
    side=math.ceil(math.sqrt(n)); gap=1e10
    with Path(path).open('w') as f:
        f.write(f'{n}\n')
        for i in range(n):
            x=((i%side)-(side-1)/2+rng.uniform(-.1,.1))*gap
            y=((i//side)-(side-1)/2+rng.uniform(-.1,.1))*gap
            m=rng.uniform(1e20,1e22)
            vx,vy=rng.uniform(-100,100),rng.uniform(-100,100)
            f.write(f'{m:.17g} {x:.17g} {y:.17g} 0 {vx:.17g} {vy:.17g} 0\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description='Reproducible synthetic 2D benchmark input, SI units.')
    p.add_argument('n',type=int);p.add_argument('output');p.add_argument('--seed',type=int,default=42)
    a=p.parse_args();generate(a.n,a.output,a.seed)

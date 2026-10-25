"""Read the solver CSV (; or ,; optional header), export orbit PNG and animated GIF."""
import argparse,csv,math
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation,PillowWriter
AU=149597870700.
NAMES=['Sun','Mercury','Venus','Earth','Mars','Jupiter','Saturn','Uranus','Neptune']
COLORS=['#d99500','#7d8794','#d17c44','#2676cf','#c84536','#ad754f','#b7a45b','#329da3','#685bb8']
def load(path):
    with Path(path).open(encoding='utf-8-sig') as f:
        text=f.read();sep=';' if ';' in text.splitlines()[0] else ','
        rows=list(csv.reader(text.splitlines(),delimiter=sep))
    try:float(rows[0][0])
    except ValueError:rows=rows[1:]
    a=np.array(rows,dtype=float)
    if a.ndim!=2 or len(a)<1 or a.shape[1]<3 or a.shape[1]%2!=1 or not np.isfinite(a).all():raise ValueError('Expected finite rows t,x1,y1,...')
    if np.any(np.diff(a[:,0])<0):raise ValueError('Time must be nondecreasing')
    return a[:,0],a[:,1:].reshape(len(a),-1,2)/AU

def main():
    p=argparse.ArgumentParser();p.add_argument('csv');p.add_argument('--out',default='visualization')
    p.add_argument('--gif',action='store_true');p.add_argument('--frames',type=int,default=180)
    p.add_argument('--names',nargs='+');a=p.parse_args()
    if a.frames<1:p.error('frames must be positive')
    t,xy=load(a.csv);n=xy.shape[1];out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
    names=a.names or [f'Body {i+1}' for i in range(n)]
    if len(names)!=n:p.error('names must match body count')
    # Two equal-scale views: full system plus central region. Coordinates remain barycentric.
    fig,axes=plt.subplots(1,2,figsize=(12,6),layout='constrained')
    limits=[max(.1,float(abs(xy).max())*1.08),2.0]
    for ax,lim,title in zip(axes,limits,['All bodies','Central region']):
        ax.set(xlim=(-lim,lim),ylim=(-lim,lim),xlabel='X [AU]',ylabel='Y [AU]',title=title)
        ax.set_aspect('equal');ax.grid(alpha=.2);ax.plot(0,0,'+',color='#777777',markersize=5)
        for i in range(n):
            color=COLORS[i%len(COLORS)]
            ax.plot(xy[:,i,0],xy[:,i,1],color=color,lw=1,label=names[i])
            ax.scatter(*xy[-1,i],color=color,s=36 if i==0 else 14)
    axes[0].legend(loc='upper left',fontsize=8,ncol=2)
    fig.suptitle(f'Simulated trajectories | {t[-1]/86400:.1f} days | barycentric XY projection\nDistances in AU; marker sizes are symbolic')
    fig.savefig(out/'orbits.png',dpi=160)
    # Save full paths in PNG; animate actual sampled CSV rows with growing trails.
    if a.gif:
        for ax in axes:
            for line in list(ax.lines)[1:]:line.remove()
            for col in list(ax.collections):col.remove()
        trails=[];dots=[]
        for ax in axes:
            for i in range(n):
                color=COLORS[i%len(COLORS)]
                trails.append(ax.plot([],[],color=color,lw=1,alpha=.65)[0])
                dots.append(ax.plot([],[],'o',color=color,ms=7 if i==0 else 4)[0])
        indices=np.unique(np.linspace(0,len(t)-1,min(a.frames,len(t)),dtype=int))
        def update(k):
            for j,(line,dot) in enumerate(zip(trails,dots)):
                i=j%n;line.set_data(xy[:k+1,i,0],xy[:k+1,i,1]);dot.set_data([xy[k,i,0]],[xy[k,i,1]])
            fig.suptitle(f'Simulated motion | day {t[k]/86400:.1f} | barycentric XY projection\nDistances in AU; marker sizes are symbolic')
            return trails+dots
        animation=FuncAnimation(fig,update,frames=indices,interval=50,blit=False)
        animation.save(out/'motion.gif',writer=PillowWriter(fps=20),dpi=90)
    plt.close(fig)
    print(f'{n} bodies, {len(t)} rows -> {out}')
if __name__=='__main__':main()

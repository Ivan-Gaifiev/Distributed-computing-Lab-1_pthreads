"""Average repeated raw timings; plot time, speedup and parallel efficiency.
Run: python report_performance.py --results results
"""
import argparse,csv,statistics
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def report(folder):
    folder=Path(folder)
    with (folder/'timings_raw.csv').open() as f:raw=list(csv.DictReader(f))
    groups={}
    for row in raw:
        if row['kind']=='pthread':groups.setdefault((int(row['n']),int(row['threads'])),[]).append(float(row['seconds']))
    sizes=sorted({n for n,p in groups});summary=[]
    for n in sizes:
        t1=statistics.mean(groups[n,1])
        for nn,p in sorted(groups):
            if nn!=n:continue
            times=groups[n,p];mean=statistics.mean(times);speed=t1/mean
            summary.append(dict(n=n,threads=p,repeats=len(times),mean_seconds=mean,stdev_seconds=statistics.stdev(times) if len(times)>1 else 0,min_seconds=min(times),max_seconds=max(times),speedup=speed,efficiency=speed/p))
    with (folder/'performance_mean.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(summary[0]));w.writeheader();w.writerows(summary)
    for key,ylabel,name in [('mean_seconds','Mean wall time [s]','time_mean'),('speedup','Speedup T1 / Tp','speedup_mean'),('efficiency','Efficiency T1 / (p * Tp) [%]','efficiency_mean')]:
        fig,ax=plt.subplots(figsize=(7.5,4.8),layout='constrained')
        for n in sizes:
            rows=[r for r in summary if r['n']==n];x=[r['threads'] for r in rows]
            y=[r[key]*(100 if key=='efficiency' else 1) for r in rows]
            if key=='mean_seconds':ax.errorbar(x,y,yerr=[r['stdev_seconds'] for r in rows],fmt='o-',capsize=4,label=f'N={n}')
            else:ax.plot(x,y,'o-',label=f'N={n}')
        ax.set(xlabel='Worker threads p',ylabel=ylabel)
        ax.set_xticks(sorted({p for n,p in groups}));ax.grid(alpha=.25);ax.legend()
        if key=='mean_seconds':ax.set_yscale('log');ax.set_title('Arithmetic mean; error bars: sample standard deviation')
        if key=='efficiency':ax.axhline(100,color='gray',ls='--',lw=1)
        fig.savefig(folder/f'{name}.png',dpi=160);fig.savefig(folder/f'{name}.svg');plt.close(fig)
    text='''# Производительность: средние времена и эффективность

Использованы фактические сырые замеры из timings_raw.csv, по три запуска каждого варианта после одного прогрева. Это пересчёт ранее выполненных измерений, а не новые запуски. Условия: Windows 11, AMD64 Family 25 Model 80, 12 логических процессоров, GCC 16.2.0, -O2, 24 шага по 3600 с, CSV записывается на каждом шаге. Время включает весь процесс: запуск, чтение, память, вычисления, синхронизацию и вывод.

Для каждого N базой служит **та же pthreads-программа с одним рабочим**, а не отдельная последовательная программа. Число p — рабочие потоки; дополнительно существует главный поток, суммирующий силы и записывающий CSV.

Среднее время: T_p = (t_1 + ... + t_k)/k, k=3.
Ускорение: S_p = T_1/T_p.
Эффективность: E_p = S_p/p = T_1/(p*T_p); в таблице — проценты.
На графике времени показано выборочное стандартное отклонение, не доверительный интервал.

| N | Рабочие p | Среднее, с | Станд. откл., с | Ускорение | Эффективность |
|---|---:|---:|---:|---:|---:|
'''
    for r in summary:text+=f"| {r['n']} | {r['threads']} | {r['mean_seconds']:.6f} | {r['stdev_seconds']:.6f} | {r['speedup']:.3f} | {r['efficiency']*100:.1f}% |\n"
    text+='''
## Выводы

Для 256 тел вычислений мало: результаты близки по времени, влияние запуска, барьеров, CSV и фоновой нагрузки существенно. Небольшие ускорения нельзя считать устойчивым эффектом по трём коротким замерам.

При увеличении N доля попарных вычислений O(N²) растёт, поэтому распараллеливание становится полезнее. Для 4096 тел восемь рабочих сокращают время относительно одного рабочего, но ускорение ниже идеального восьмикратного. Эффективность снижается с числом потоков: суммирование O(PN) остаётся в главном потоке, запись CSV последовательна, есть ожидания на барьерах и затраты памяти/кэша. Это объяснение согласуется со структурой кода; отдельное профилирование долей времени не выполнялось.

Главный результат: увеличение числа потоков полезно на достаточно больших задачах, но не даёт линейного ускорения. Для точных выводов на другой машине следует повторить замеры; три повтора — небольшой объём выборки. Проверка совпадения траекторий была выполнена при исходных запусках.

Графики: time_mean.png, speedup_mean.png, efficiency_mean.png; также сохранены редактируемые в векторном редакторе SVG и таблица performance_mean.csv. Повторное построение: python report_performance.py --results results.
'''
    (folder/'PERFORMANCE.md').write_text(text,encoding='utf-8')
    return summary
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--results',default='results');a=p.parse_args();rows=report(a.results)
    for r in rows:
        if r['n']==max(v['n'] for v in rows):print(r)

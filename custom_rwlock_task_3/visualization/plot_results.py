"""Plot saved benchmark measurements and replace their section in the report."""

import argparse
from collections import defaultdict
import csv
from dataclasses import dataclass
import math
import os
from pathlib import Path
import statistics

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.ticker import MaxNLocator, PercentFormatter


HERE = Path(__file__).resolve().parent
LAB = HERE.parent
START = "<!-- benchmark-plots:start -->"
END = "<!-- benchmark-plots:end -->"
SIZES = (100, 1000)
MIXES = ("100/0/0", "90/5/5", "50/25/25")
STYLES = {"custom": ("#2563AB", "o"), "pthread": ("#D76A27", "s")}
FILES = {"time": "execution_time.png", "speedup": "speedup.png",
         "efficiency": "efficiency.png"}


@dataclass(frozen=True)
class Measurement:
    median: float
    minimum: float
    maximum: float
    speedup: float
    efficiency: float
    repeats: int


def read_csv(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError(f"Empty CSV: {path}")
    return rows


def key(row):
    return int(row["initial_keys"]), row["mix"], int(row["threads"]), row["lock"]


def positive(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise ValueError(f"Expected a finite positive number, got {value!r}")
    return number


def same(actual, expected, label):
    if not math.isclose(actual, expected, rel_tol=1e-9, abs_tol=1e-12):
        raise ValueError(f"{label}: CSV contains {actual}, expected {expected}")


def load_results(directory):
    """Cross-check summary values against raw runs before plotting any points."""
    summary = read_csv(directory / "summary.csv")
    raw = read_csv(directory / "raw.csv")
    groups = defaultdict(list)
    repeats = set()
    operations = set()
    for row in raw:
        group = key(row)
        repeat = int(row["repeat"])
        if repeat <= 0 or (group, repeat) in repeats:
            raise ValueError(f"Invalid or duplicate measured repeat: {group}, {repeat}")
        repeats.add((group, repeat))
        groups[group].append(positive(row["seconds"]))
        operations.add(int(row["ops"]))

    index = {}
    for row in summary:
        group = key(row)
        if group in index:
            raise ValueError(f"Duplicate summary configuration: {group}")
        if group not in groups:
            raise ValueError(f"Missing raw measurements: {group}")
        values = groups[group]
        count = int(row["repeats"])
        if count != len(values):
            raise ValueError(f"Repeat count differs for {group}")
        median = positive(row["median_seconds"])
        minimum = positive(row["min_seconds"])
        same(median, statistics.median(values), f"Median {group}")
        same(minimum, min(values), f"Minimum {group}")
        index[group] = Measurement(median, minimum, max(values),
                                   positive(row["speedup"]),
                                   positive(row["efficiency"]), count)
    if set(index) != set(groups):
        raise ValueError("Summary and raw configurations differ")

    threads = sorted({group[2] for group in index})
    expected = {(size, mix, p, lock) for size in SIZES for mix in MIXES
                for p in threads for lock in STYLES}
    if not threads or threads[0] != 1 or set(index) != expected:
        raise ValueError("Expected both locks, sizes 100/1000, all three mixes, "
                         "and the same positive thread counts including 1")
    if len(operations) != 1 or min(operations) <= 0:
        raise ValueError("All measurements must use the same positive operation count")
    for (size, mix, p, lock), value in index.items():
        baseline = index[size, mix, 1, lock].median
        same(value.speedup, baseline / value.median, f"Speedup {(size, mix, p, lock)}")
        same(value.efficiency, value.speedup / p, f"Efficiency {(size, mix, p, lock)}")
    return index, threads, operations.pop()


def configure_style():
    plt.rcParams.update({
        "font.family": "DejaVu Sans", "font.size": 11,
        "text.color": "#1E293B", "axes.labelcolor": "#475569",
        "xtick.color": "#64748B", "ytick.color": "#64748B",
        "axes.edgecolor": "#CBD5E1", "axes.spines.top": False,
        "axes.spines.right": False, "axes.titleweight": "semibold",
        "axes.titlepad": 13, "axes.labelpad": 8,
        "figure.facecolor": "#F8FAFC", "axes.facecolor": "white",
        "savefig.facecolor": "#F8FAFC",
    })


def draw_chart(index, threads, operations, metric, output):
    titles = {"time": "Время выполнения", "speedup": "Speedup",
              "efficiency": "Эффективность"}
    subtitles = {
        "time": "Медиана и разброс измерений · меньше — лучше",
        "speedup": "S = T₁ / Tₚ · baseline: один поток того же режима",
        "efficiency": "E = S / p · 100% соответствует линейному ускорению",
    }
    labels = {"time": "Время, мс", "speedup": "Speedup, ×",
              "efficiency": "Эффективность, %"}
    fig, axes = plt.subplots(2, 3, figsize=(15, 8.8), squeeze=False)
    fig.subplots_adjust(left=0.075, right=0.975, bottom=0.12, top=0.76,
                        wspace=0.27, hspace=0.52)
    fig.text(0.075, 0.94, titles[metric], fontsize=26, weight="bold")
    fig.text(0.075, 0.893, subtitles[metric], fontsize=12, color="#64748B")
    legend = [Line2D([], [], color=color, marker=marker, linewidth=2.3,
                     markersize=7, label=lock)
              for lock, (color, marker) in STYLES.items()]
    fig.legend(handles=legend, loc="upper left", bbox_to_anchor=(0.067, 0.868),
               ncol=2, frameon=False, columnspacing=2)
    shared_top = None
    if metric == "speedup":
        shared_top = max(1.8, max(v.speedup for v in index.values()) * 1.15)
    elif metric == "efficiency":
        shared_top = max(110, max(v.efficiency * 100 for v in index.values()) * 1.12)

    for row, size in enumerate(SIZES):
        for col, mix in enumerate(MIXES):
            ax = axes[row, col]
            ax.set_title(f"{size} ключей  ·  {mix}", loc="left", fontsize=12)
            ax.set_axisbelow(True)
            ax.grid(axis="y", color="#E2E8F0", linewidth=0.8)
            ax.set_xticks(threads)
            ax.set_xlabel("Потоки")
            if col == 0:
                ax.set_ylabel(labels[metric])
            panel_top = 0
            for lock, (color, marker) in STYLES.items():
                values = [index[size, mix, p, lock] for p in threads]
                if metric == "time":
                    ys = [v.median * 1000 for v in values]
                    errors = [[(v.median - v.minimum) * 1000 for v in values],
                              [(v.maximum - v.median) * 1000 for v in values]]
                    ax.errorbar(threads, ys, yerr=errors, color=color, marker=marker,
                                linewidth=2.1, markersize=6, capsize=3,
                                elinewidth=1.1, markeredgecolor="white",
                                markeredgewidth=0.7)
                    panel_top = max(panel_top, max(v.maximum * 1000 for v in values))
                else:
                    ys = [v.speedup if metric == "speedup" else v.efficiency * 100
                          for v in values]
                    ax.plot(threads, ys, color=color, marker=marker, linewidth=2.1,
                            markersize=6, markeredgecolor="white", markeredgewidth=0.7)
            if metric == "time":
                ax.set_ylim(0, panel_top * 1.16)
                ax.yaxis.set_major_locator(MaxNLocator(nbins=5))
            else:
                reference = 1 if metric == "speedup" else 100
                ax.axhline(reference, color="#94A3B8", linestyle=(0, (4, 4)),
                           linewidth=1, zorder=1)
                ax.set_ylim(0, shared_top)
                if metric == "efficiency":
                    ax.yaxis.set_major_formatter(PercentFormatter(xmax=100, decimals=0))
            ax.margins(x=0.07)

    counts = sorted({v.repeats for v in index.values()})
    count_label = "/".join(map(str, counts))
    note = ("Усы: min–max, не доверительный интервал. Шкала времени своя у каждой панели."
            if metric == "time" else "Одинаковая вертикальная шкала во всех панелях.")
    operation_label = f"{operations:,}".replace(",", " ")
    fig.text(0.075, 0.045,
             f"{operation_label} операций · {count_label} повторов · "
             "смеси: поиск / вставка / удаление, %",
             fontsize=10, color="#64748B")
    fig.text(0.075, 0.019, note, fontsize=9, color="#64748B")
    try:
        fig.savefig(output / FILES[metric], dpi=200)
    finally:
        plt.close(fig)


def report_body(report, output):
    def link(metric):
        return Path(os.path.relpath(output / FILES[metric], report.parent)).as_posix()

    return "\n".join([
        START, "### Графики", "",
        "Строки — начальная длина списка, столбцы — доли поиска/вставки/удаления. "
        "Синий — `custom`, оранжевый — `pthread`.", "",
        f"![Время выполнения: медиана и диапазон min–max](<{link('time')}>)", "",
        "Время — медиана, усы — минимум и максимум измерений, а не доверительный "
        "интервал. Каждая панель имеет свою шкалу времени, начинающуюся с нуля.", "",
        f"![Speedup относительно одного потока того же режима](<{link('speedup')}>)", "",
        "Speedup считается относительно одного потока той же блокировки и нагрузки. "
        "Пунктир `S = 1` отделяет ускорение от замедления.", "",
        f"![Эффективность параллельного выполнения в процентах](<{link('efficiency')}>)", "",
        "Эффективность — `100 × S/p` процентов. Пунктир соответствует 100%; "
        "у speedup и эффективности шкала одинакова во всех панелях.", END,
    ])


def replace_section(body, section):
    """Replace only the marked section; insert it before tables on first use."""
    if START in body or END in body:
        if body.count(START) != 1 or body.count(END) != 1:
            raise ValueError("Report contains incomplete or duplicate plot markers")
        start, end = body.index(START), body.index(END)
        if end < start:
            raise ValueError("Plot markers are reversed")
        return body[:start] + section + body[end + len(END):]
    results = body.find("## Результаты")
    table = body.find("\n### ", results) if results >= 0 else -1
    if table < 0:
        raise ValueError("Report has no results table section to insert plots before")
    return body[:table] + "\n" + section + "\n" + body[table:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=LAB / "results",
                        help="Directory containing raw.csv and summary.csv")
    parser.add_argument("--output", type=Path, help="PNG directory")
    parser.add_argument("--report", type=Path, help="Existing Markdown report to update")
    args = parser.parse_args()
    results = args.results.resolve()
    default = results == (LAB / "results").resolve()
    output = (args.output or (HERE / "figures" if default else results / "figures")).resolve()
    report = (args.report or (LAB / "REPORT.md" if default else results / "REPORT.md")).resolve()
    try:
        index, threads, operations = load_results(results)
        body = report.read_text(encoding="utf-8")
        updated = replace_section(body, report_body(report, output))
        output.mkdir(parents=True, exist_ok=True)
        configure_style()
        for metric in FILES:
            draw_chart(index, threads, operations, metric, output)
        report.write_text(updated, encoding="utf-8")
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Cannot plot benchmark results: {error}\n")
    print(f"Checked {len(index)} configurations; saved 3 PNG files to {output}")
    print(f"Updated {report}")


if __name__ == "__main__":
    main()

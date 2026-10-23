"""Run the fixed matrix serially; save raw measurements and regenerate REPORT.md.

Only Python's standard library is needed. No Python code creates test threads:
the measured list operations are implemented in C using pthreads.
"""
import csv
import ctypes
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import struct
import subprocess
import sys
import time

from test_cli import FIELDS

ROOT = Path(__file__).resolve().parent
RESULTS = ROOT / "results"
EXE = ROOT / ("rwlock_bench.exe" if os.name == "nt" else "rwlock_bench")
MIXES = [("100/0/0", "1", "0"), ("90/5/5", "0.9", "0.05"),
         ("50/25/25", "0.5", "0.25")]
REPEATS = 10
OPS = 100_000


def command(args, timeout=30):
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True,
                          errors="replace", timeout=timeout, check=True).stdout.strip()


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def hardware():
    info = {"os": platform.platform(), "machine": platform.machine(),
            "logical_processors": os.cpu_count(), "physical_cores": None,
            "process_cpu_count": getattr(os, "process_cpu_count", os.cpu_count)(),
            "cpu": platform.processor()}
    if os.name == "nt":
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                           r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
            info["cpu"] = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        get_info = kernel.GetLogicalProcessorInformationEx
        get_info.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
        get_info.restype = ctypes.c_int
        length = ctypes.c_ulong()
        get_info(0, None, ctypes.byref(length))  # RelationProcessorCore
        if not length.value:
            raise ctypes.WinError(ctypes.get_last_error())
        data = ctypes.create_string_buffer(length.value)
        if not get_info(0, data, ctypes.byref(length)):
            raise ctypes.WinError(ctypes.get_last_error())
        offset = cores = 0
        raw = data.raw
        while offset < length.value:
            relationship, size = struct.unpack_from("II", raw, offset)
            if size < 8 or offset + size > length.value:
                raise RuntimeError("Invalid processor topology response")
            cores += relationship == 0
            offset += size
        info["physical_cores"] = cores
    elif Path("/proc/cpuinfo").exists():
        blocks = Path("/proc/cpuinfo").read_text().strip().split("\n\n")
        records = [dict(line.split(":", 1) for line in block.splitlines() if ":" in line)
                   for block in blocks]
        records = [{k.strip(): v.strip() for k, v in r.items()} for r in records]
        pairs = {(r["physical id"], r["core id"]) for r in records
                 if "physical id" in r and "core id" in r}
        info["physical_cores"] = len(pairs) or None
        info["cpu"] = records[0].get("model name", info["cpu"])
    return info


def metadata():
    make = shutil.which("mingw32-make" if os.name == "nt" else "make")
    if not make:
        raise RuntimeError("GNU Make not found in PATH")
    # Use the interpreter running this script, including a virtualenv or a
    # python3-only Linux installation. Suppress recursive Make directory banners
    # so stdout contains only the JSON configuration.
    build = json.loads(command([make, "--no-print-directory", "-s", "print-config",
                                f"PYTHON={sys.executable}"]))
    compiler = shutil.which(build["cc"])
    if not compiler:
        raise RuntimeError(f"Compiler not found: {build['cc']}")
    runtime = {"implementation": "winpthreads" if os.name == "nt" else "system pthreads",
               "libc": platform.libc_ver()}
    if os.name == "nt":
        dll = Path(compiler).parent / "libwinpthread-1.dll"
        runtime.update(path=str(dll), sha256=digest(dll))
        # Read the package database directly: neither pacman nor WMI is needed.
        msys_root = Path(compiler).parents[2]
        packages = sorted((msys_root / "var/lib/pacman/local").glob(
            "mingw-w64-x86_64-winpthreads-*/desc"))
        if packages:
            desc = packages[-1].read_text()
            runtime["package_version"] = desc.split("%VERSION%\n", 1)[1].splitlines()[0]
    return {"started_utc": datetime.now(timezone.utc).isoformat(),
            "hardware": hardware(), "build": build,
            "compiler": command([compiler, "--version"]),
            "compiler_target": command([compiler, "-dumpmachine"]),
            "pthread_runtime": runtime, "python": sys.version,
            "executable_sha256": digest(EXE),
            "source_sha256": {p.name: digest(p) for p in sorted(ROOT.iterdir())
                              if p.suffix in (".c", ".h", ".py") or p.name == "Makefile"},
            "seed": 1, "operations": OPS, "measured_repeats": REPEATS,
            "warmups_per_configuration_and_backend": 1,
            "clock": "clock_gettime(CLOCK_MONOTONIC)",
            "timing": "before opening start gate to last worker timestamp after last operation",
            "speedup_baseline": "median time with one worker, same lock, list size and mix",
            "efficiency_formula": "efficiency = speedup / threads (fraction, not percent)",
            "cpu_affinity": "not pinned", "processes": "one benchmark process at a time"}


def measurement(mode, threads, initial, read, insert):
    args = [str(EXE), "--lock", mode, "--threads", str(threads),
            "--initial", str(initial), "--ops", str(OPS), "--read", read,
            "--insert", insert, "--seed", "1", "--key-range", str(initial * 2)]
    p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True,
                       errors="replace", timeout=180, check=True)
    if p.stderr:
        raise RuntimeError(p.stderr)
    rows = list(csv.reader(io.StringIO(p.stdout)))
    if len(rows) != 1 or len(rows[0]) != len(FIELDS):
        raise RuntimeError(f"Invalid CSV: {p.stdout!r}")
    row = dict(zip(FIELDS, rows[0]))
    if sum(int(row[k]) for k in ("read_ops", "insert_ops", "delete_ops")) != OPS:
        raise RuntimeError("Operation count mismatch")
    if float(row["seconds"]) <= 0:
        raise RuntimeError("Invalid elapsed time")
    return row


def summarize(rows):
    groups = {}
    for r in rows:
        key = (int(r["initial_keys"]), r["mix"], int(r["threads"]), r["lock"])
        groups.setdefault(key, []).append(float(r["seconds"]))
    summary = []
    for (initial, mix, threads, mode), times in sorted(groups.items()):
        if len(times) != REPEATS:
            raise RuntimeError("Incomplete measurement group")
        median = statistics.median(times)
        baseline = statistics.median(groups[(initial, mix, 1, mode)])
        speedup = baseline / median
        summary.append(dict(initial_keys=initial, mix=mix, threads=threads,
                            lock=mode, repeats=len(times), median_seconds=median,
                            min_seconds=min(times), ops_per_second=OPS / median,
                            speedup=speedup, efficiency=speedup / threads))
    return summary


def write_report(meta, summary):
    # Publish only general software names. Hardware, timestamps, installation
    # paths and full build metadata stay in the ignored local metadata.json.
    os_description = meta["hardware"]["os"]
    system = next((name for name in ("Windows", "Linux", "Darwin")
                   if os_description.startswith(name)), "POSIX")
    runtime = ("winpthreads" if meta["pthread_runtime"]["implementation"] == "winpthreads"
               else "pthreads")
    lines = [
        "# Лабораторная работа 1, часть 3: блокировка чтения-записи", "",
        "## Алгоритм и связь с семинарами", "",
        "Реализована собственная `my_rwlock_t`: мьютекс, две условные переменные, "
        "счётчики активных и ожидающих читателей, ожидающих писателей и флаг активного писателя. "
        "Это схема раздела 2.3 ТЗ; создание и завершение потоков взято из семинара 1, "
        "работа с мьютексами — из семинара 2, условные переменные — из семинара 3.", "",
        "Все изменения состояния происходят под внутренним мьютексом. Читатель ждёт, "
        "пока активен писатель или есть ожидающий писатель. Писатель ждёт отсутствия "
        "активных владельцев. Ожидания находятся в циклах `while`: сигнал означает "
        "не передачу владения, а необходимость повторно проверить условие допуска. "
        "После допуска внутренний мьютекс освобождается, поэтому читатели работают параллельно.", "",
        "При освобождении последним владельцем сначала пробуждается один ожидающий писатель. "
        "Если писателей нет, пробуждаются все ожидающие читатели. Если читатели ещё активны, "
        "писатель остаётся ждать последнего. Инварианты: не более одного активного писателя; "
        "при активном писателе активных читателей нет.", "",
        "Политика даёт приоритет писателям, но не FIFO и не строгую справедливость: "
        "непрерывная очередь писателей может задерживать читателей, порядок писателей "
        "не гарантирован. Рекурсивный захват, upgrade, timed/try-вызовы, отмена ожидающих "
        "потоков и отслеживание владельцев не реализованы. Вызовы возвращают коды ошибок "
        "в стиле pthreads. Уничтожение разрешено после завершения всех пользователей; "
        "при наличии владельцев или ожидающих возвращается `EBUSY`.", "",
        "## Односвязный список и интерфейс", "",
        "Упорядоченный список без повторов адаптирован из "
        "`../data/seminars/pth_ll_rwl/pth_ll_rwl.c`. `Member` защищён чтением; "
        "`Insert` и `Delete` — записью на всё время операции. В обоих режимах "
        "исполняется один код списка, выбор блокировки использует одинаковые "
        "указатели на функции. Аргументы потоков передаются структурами. "
        "Остаток операций распределяется между первыми потоками; счётчики локальны "
        "и объединяются после завершения измерений. При частичном отказе создания "
        "потоков старт отменяется, созданные потоки пробуждаются и присоединяются.", "",
        "Команды сборки, запуска и описание CSV приведены в [README.md](README.md). "
        "Из Python запускаются только проверки и процессы программы; параллельные "
        "операции со списком реализованы на C с pthreads.", "",
        "## Методика измерений", "",
        f"Начальные размеры: 100 и 1000; операций: {OPS:,}; смеси поиска/вставки/удаления: "
        "100/0/0, 90/5/5 и 50/25/25 процентов. Число потоков: "
        + ", ".join(map(str, meta["thread_counts"])) + ". "
        f"Для каждой конфигурации и режима сделан один прогрев и {REPEATS} измерений. "
        "Прогревочные результаты сохранены отдельно и в статистику не включаются.", "",
        "Перед каждым запуском создаётся новый список с равномерно расположенными "
        "уникальными ключами `floor(i*key_range/initial)`. Диапазон `[0,key_range)` "
        "равен удвоенной начальной длине, вместо 100000000 в исходном примере. "
        "Это повышает вероятность успешного поиска и удаления. Нагрузка содержит "
        "заданные количества операций каждого типа (для произвольных долей используются "
        "целые части произведений, остаток — удаления), затем перемешивается. "
        "Ключи и порядок генерируются SplitMix64 с seed=1 заранее. "
        "Контрольная сумма `workload_id` проверяет совпадение нагрузки между режимами "
        "и числами потоков. Планировщик может менять взаимный порядок операций, "
        "число успешных операций и итоговый список.", "",
        "Таймер `clock_gettime(CLOCK_MONOTONIC)` запускается главным потоком после "
        "готовности всех рабочих потоков, непосредственно перед открытием стартового "
        "шлюза. Конец — максимальная отметка рабочего потока после его последней операции. "
        "В интервал входят пробуждение из шлюза, блокировки, обход списка, выделение и "
        "освобождение узлов и локальные счётчики; создание потоков, подготовка, join, "
        "объединение результатов, проверка списка и CSV исключены. Для потоков без "
        "операций учитывается время выхода из шлюза.", "",
        "Все процессы запускаются последовательно; порядок режимов чередуется между "
        "повторами. Привязка к ядрам не используется. Медиана — среднее двух центральных "
        "значений из 10; минимум сохранён отдельно. Пропускная способность = "
        "100000 / медиана времени; ускорение = медиана одного потока / медиана "
        "данного числа потоков для той же смеси, длины и блокировки.", "",
        "Эффективность рассчитывается по формуле `E = S/p = T1/(p*Tp)`, "
        "где `S` — ускорение, `p` — число рабочих потоков, `T1` и `Tp` — "
        "медианы времени с одним и с `p` потоками. В качестве последовательного "
        "базового времени используется однопоточный запуск того же режима. "
        "В `summary.csv` поле `efficiency` хранит долю, а в таблицах отчёта "
        "эффективность указана в процентах (`100*E`). Например, при ускорении 2 "
        "на четырёх потоках эффективность равна 0.5, или 50%.", "",
        "## Среда", "",
        f"Измерения выполнены в {system} с GCC и {runtime}. "
        "Команды сборки приведены в README, флаги — в Makefile. Результаты зависят от среды; "
        "для другой системы нужны отдельные измерения.", "",
        "Подробные сведения о компьютере и локальные пути сохраняются только "
        "в `metadata.json`, исключённом из Git.", "",
        "## Проверка корректности", "",
        "Пройдены проверки одновременных читателей, ожидания последнего читателя, "
        "приоритета ожидающего писателя, пробуждения после писателя, исключительности "
        "записи и устойчивости к пробуждению без изменения условия. Сценарии используют "
        "условные переменные; регистрация ожидающих наблюдается под внутренним "
        "мьютексом с `sched_yield`, без `sleep`. Тайм-ауты служат только для обнаружения зависания.", "",
        "Для обоих режимов пройдена смешанная нагрузка из 8 потоков по 2000 захватов "
        "с проверкой исключительности и вставками/поиском/удалением. Проверены "
        "операции с пустым списком, началом, серединой, концом и повторным ключом; "
        "проверяющий код обнаруживает нарушенный порядок и циклы. CLI-проверки "
        "охватывают остаток деления, число потоков больше числа операций, пустой "
        "начальный список, чистые смеси, одинаковый seed, 19 некорректных вводов и "
        "искусственный отказ третьего `pthread_create` после двух успешных созданий. "
        "Каждый измеряемый запуск также проверяет порядок, отсутствие повторов/циклов, "
        "точное количество операций и баланс длины списка.", "",
        "Сводный журнал: [tests.txt](results/tests.txt). Проверки подтверждают "
        "пройденные сценарии, но не являются формальным доказательством отсутствия всех гонок.", "",
        "## Результаты", "",
        "Сырые измерения: [raw.csv](results/raw.csv); прогрев: "
        "[warmup.csv](results/warmup.csv); сводка: [summary.csv](results/summary.csv). "
        "Время ниже указано в миллисекундах. Ускорение каждой блокировки считается "
        "относительно её собственного однопоточного запуска.", "",
        "<!-- benchmark-plots:start -->",
        "<!-- Regenerate plots with visualization/plot_results.py after measurements. -->",
        "<!-- benchmark-plots:end -->", "",
    ]
    for initial in (100, 1000):
        lines += [f"### Начальная длина {initial}", "",
                  "| Поиск/вставка/удаление, % | Потоки | Блокировка | Медиана, мс | Минимум, мс | Оп/с | Ускорение | Эффективность, % |",
                  "|---|---:|---|---:|---:|---:|---:|---:|"]
        for r in summary:
            if r["initial_keys"] == initial:
                lines.append(f"| {r['mix']} | {r['threads']} | {r['lock']} | "
                             f"{r['median_seconds'] * 1000:.3f} | {r['min_seconds'] * 1000:.3f} | "
                             f"{r['ops_per_second']:.0f} | {r['speedup']:.3f} | "
                             f"{r['efficiency'] * 100:.2f} |")
        lines.append("")
    index = {(r["initial_keys"], r["mix"], r["threads"], r["lock"]): r for r in summary}
    ratios = []
    for key, r in index.items():
        if key[-1] == "custom":
            other = index[(*key[:-1], "pthread")]
            ratios.append((other["median_seconds"] / r["median_seconds"], key))
    wins = sum(ratio > 1 for ratio, _ in ratios)
    lines += ["## Выводы", "",
              f"По медиане собственная блокировка быстрее библиотечной в {wins} из "
              f"{len(ratios)} сопоставленных конфигураций. "
              "Отношение времени pthread/custom больше 1 означает преимущество custom.", ""]
    for label, (ratio, key) in (("Наибольшее", max(ratios)), ("Наименьшее", min(ratios))):
        initial, mix, threads, _ = key
        lines.append(f"{label} отношение pthread/custom: {ratio:.3f}, "
                     f"длина {initial}, смесь {mix}, потоков {threads}.")
        lines.append("")
    best = max(summary, key=lambda r: r["speedup"])
    lines += [f"Наибольшее ускорение относительно одного потока своей блокировки: "
              f"{best['speedup']:.3f} ({best['lock']}, длина {best['initial_keys']}, "
              f"смесь {best['mix']}, потоков {best['threads']}).", "",
              "Разница отражает не только стоимость блокировок, но и их политику "
              "допуска, планирование потоков и накладные расходы. Вставки и удаления "
              "сериализуют весь список; каждый вход и выход собственной блокировки "
              "также использует общий внутренний мьютекс. Увеличение числа потоков "
              "поэтому не обязано ускорять программу. Политика библиотечной блокировки "
              "используется по умолчанию и не приравнивается к политике собственной.", "",
              "Без закрепления потоков, контроля частоты CPU и изоляции фоновой нагрузки "
              "результаты имеют шум. Небольшие различия медиан не следует считать "
              "доказанным преимуществом; для проверки доступны все исходные измерения.", ""]
    report = ROOT / "REPORT.md" if RESULTS == ROOT / "results" else RESULTS / "REPORT.md"
    result_link = os.path.relpath(RESULTS, report.parent).replace(os.sep, "/")
    readme_link = os.path.relpath(ROOT / "README.md", report.parent).replace(os.sep, "/")
    body = "\n".join(lines).replace("(results/", f"({result_link}/")
    body = body.replace("(README.md)", f"({readme_link})")
    report.write_text(body, encoding="utf-8")


def main():
    RESULTS.mkdir(exist_ok=True)
    if any((RESULTS / name).exists() for name in ("raw.csv", "metadata.json")):
        raise SystemExit("Results already exist. Preserve them and choose a new directory "
                         "with --output DIRECTORY, or move them before repeating.")
    meta = metadata()
    thread_counts = [n for n in (1, 2, 4, 8) if n <= (os.cpu_count() or 1)]
    meta["thread_counts"] = thread_counts
    tests = command([sys.executable, str(ROOT / "test_cli.py")], timeout=120)
    (RESULTS / "tests.txt").write_text(tests + "\n", encoding="utf-8")
    (RESULTS / "metadata.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")
    measured = []
    extra = ["mix", "repeat", "order"]
    started = time.monotonic()
    with (RESULTS / "raw.csv").open("w", newline="", encoding="utf-8") as raw_file, \
         (RESULTS / "warmup.csv").open("w", newline="", encoding="utf-8") as warm_file:
        raw_writer = csv.DictWriter(raw_file, fieldnames=extra + FIELDS)
        warm_writer = csv.DictWriter(warm_file, fieldnames=extra + FIELDS)
        raw_writer.writeheader()
        warm_writer.writeheader()
        for initial in (100, 1000):
            for mix, read, insert in MIXES:
                hashes = set()
                for threads in thread_counts:
                    for repeat in range(REPEATS + 1):
                        modes = ("custom", "pthread") if repeat % 2 == 0 else ("pthread", "custom")
                        for order, mode in enumerate(modes, 1):
                            row = measurement(mode, threads, initial, read, insert)
                            hashes.add(row["workload_id"])
                            if len(hashes) != 1:
                                raise RuntimeError("Workload differs across configurations")
                            row.update(mix=mix, repeat=repeat, order=order)
                            if repeat:
                                raw_writer.writerow(row)
                                measured.append(row)
                                raw_file.flush()
                            else:
                                warm_writer.writerow(row)
                                warm_file.flush()
                    print(f"Completed initial={initial}, mix={mix}, threads={threads}; "
                          f"elapsed={time.monotonic() - started:.1f}s", flush=True)
    summary = summarize(measured)
    with (RESULTS / "summary.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    meta["finished_utc"] = datetime.now(timezone.utc).isoformat()
    meta["measured_rows"] = len(measured)
    meta["completed"] = True
    (RESULTS / "metadata.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")
    write_report(meta, summary)
    print(f"Saved {len(measured)} measurements, summary and REPORT.md", flush=True)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        if len(sys.argv) == 3 and sys.argv[1] == "--output":
            RESULTS = Path(sys.argv[2]).resolve()
        else:
            raise SystemExit("Usage: python run_benchmarks.py [--output DIRECTORY]")
    main()

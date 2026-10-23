"""Integration checks, with process timeouts to turn deadlocks into failures."""
import csv
import io
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
EXE = ROOT / ("rwlock_bench.exe" if os.name == "nt" else "rwlock_bench")
FIELDS = "lock,threads,initial_keys,ops,read_fraction,insert_fraction,seed,key_range,read_ops,insert_ops,delete_ops,read_hits,insert_success,delete_success,final_size,workload_id,seconds,ops_per_second".split(",")


def run(args, success=True, executable=EXE):
    p = subprocess.run([str(executable), *args], capture_output=True, text=True, timeout=60)
    assert (p.returncode == 0) == success, (args, p.returncode, p.stderr)
    if not success:
        assert not p.stdout and p.stderr, (args, p.stdout, p.stderr)
        return None
    assert not p.stderr, p.stderr
    return p.stdout


def row(mode, threads, ops, initial=20, read="0.5", insert="0.25", seed="7"):
    output = run(["--lock", mode, "--threads", str(threads), "--initial", str(initial),
                  "--ops", str(ops), "--read", read, "--insert", insert, "--seed", seed])
    lines = list(csv.reader(io.StringIO(output)))
    assert len(lines) == 1 and len(lines[0]) == len(FIELDS), output
    r = dict(zip(FIELDS, lines[0]))
    assert sum(int(r[x]) for x in ("read_ops", "insert_ops", "delete_ops")) == ops, r
    assert int(r["final_size"]) == initial + int(r["insert_success"]) - int(r["delete_success"]), r
    assert float(r["seconds"]) > 0 and float(r["ops_per_second"]) > 0, r
    return r


def main():
    test_exe = ROOT / ("test_rwlock.exe" if os.name == "nt" else "test_rwlock")
    print(run([], executable=test_exe), end="")
    for mode in ("custom", "pthread"):
        row(mode, 3, 1001)
        row(mode, 8, 3)
        row(mode, 1, 1, initial=0)
        r = row(mode, 4, 1000, read="1", insert="0")
        assert r["read_ops"] == "1000" and r["final_size"] == "20"
        row(mode, 4, 1000, read="0", insert="1")
        row(mode, 4, 1000, read="0", insert="0")
        row(mode, 2, 100, seed="18446744073709551615")
    # Sequential results and fixed workloads must agree across backends.
    a, b = row("custom", 1, 2000), row("pthread", 1, 2000)
    for field in FIELDS:
        if field not in ("lock", "seconds", "ops_per_second"):
            assert a[field] == b[field], (field, a, b)
    assert row("custom", 4, 2000)["workload_id"] == a["workload_id"]
    invalid = [
        ["--threads", "0"], ["--threads", "-1"], ["--threads", "x"],
        ["--ops", "0"], ["--ops", "18446744073709551615"],
        ["--initial", "4", "--key-range", "3"], ["--initial", "-1"],
        ["--key-range", "0"], ["--key-range", "2147483648"],
        ["--read", "nan"], ["--read", "inf"], ["--read", "0.5junk"],
        ["--read", "1", "--insert", "0.1"], ["--insert", "-0.1"],
        ["--seed", "18446744073709551616"], ["--lock", "bad"],
        ["--threads"], ["--unknown", "1"], ["--threads", "1", "--threads", "2"],
    ]
    for args in invalid:
        run(args, success=False)
    failure_exe = ROOT / ("test_create_failure.exe" if os.name == "nt" else "test_create_failure")
    failure = subprocess.run([str(failure_exe), "--threads", "4", "--ops", "100"],
                             capture_output=True, text=True, timeout=30)
    assert failure.returncode != 0 and not failure.stdout, failure
    assert "pthread_create after 2 threads" in failure.stderr, failure.stderr
    assert "Usage" in run(["--help"])
    print(f"PASS CLI: both backends, remainder, empty ranges, workloads, {len(invalid)} invalid inputs")
    print("PASS partial pthread_create failure: waiting workers aborted and joined")


if __name__ == "__main__":
    main()

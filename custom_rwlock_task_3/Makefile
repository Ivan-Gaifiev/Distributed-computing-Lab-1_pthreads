CC = gcc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -pthread
LDLIBS = -pthread
ifeq ($(OS),Windows_NT)
EXE = .exe
PYTHON ?= python
else
EXE =
PYTHON ?= python3
endif

COMMON = my_rwlock.c list.c lock_backend.c
HEADERS = my_rwlock.h list.h lock_backend.h common.h

.PHONY: all test benchmark clean print-config
all: rwlock_bench$(EXE) test_rwlock$(EXE)

rwlock_bench$(EXE): bench.c $(COMMON) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) bench.c $(COMMON) -o $@ $(LDLIBS)

test_rwlock$(EXE): test_rwlock.c $(COMMON) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_rwlock.c $(COMMON) -o $@ $(LDLIBS)

test_create_failure$(EXE): bench.c $(COMMON) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DLAB3_TEST_CREATE_FAIL_AFTER=2 bench.c $(COMMON) -o $@ $(LDLIBS)

test: all test_create_failure$(EXE)
	"$(PYTHON)" test_cli.py

benchmark: all test_create_failure$(EXE)
	"$(PYTHON)" run_benchmarks.py

clean:
	"$(PYTHON)" -c "from pathlib import Path; [p.unlink() for n in ('rwlock_bench', 'rwlock_bench.exe', 'test_rwlock', 'test_rwlock.exe', 'test_create_failure', 'test_create_failure.exe') if (p := Path(n)).is_file()]"

print-config:
	@"$(PYTHON)" -c "import json,sys; print(json.dumps(dict(zip(('cc','cppflags','cflags','ldlibs'),sys.argv[1:]))))" "$(CC)" "$(CPPFLAGS)" "$(CFLAGS)" "$(LDLIBS)"

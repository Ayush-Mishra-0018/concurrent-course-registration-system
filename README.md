# Academia — Concurrent Course Registration System

A multithreaded TCP client-server course registration system written in C using POSIX sockets, pthreads, file I/O, and `fcntl`-based file locking — extended with production-style benchmarking, observability, and performance instrumentation.

---

## Architecture

```
                ┌─────────────────────────────────────┐
                │             TCP Server               │
                │  (accept loop + thread-per-client)   │
                │                                      │
                │   server.c                           │
                │   ├── metrics/metrics.c   ← perf     │
                │   ├── metrics/lock_stats.c ← locks   │
                │   └── logging/logger.c    ← logs     │
                └──────────────┬──────────────────────┘
                               │ pthread_t (one per client)
               ┌───────────────┼───────────────┐
               ▼               ▼               ▼
         admin_controller  faculty_controller  student_controller
               │               │               │
               └───────────────┴───────────────┘
                               │
                    utils/file_ops_*.c
                    (binary .dat files + fcntl + pthread mutexes)
```

### Concurrency Model

- **Thread-per-client**: Each TCP connection gets its own detached `pthread_t`
- **File locking**: `fcntl(F_SETLKW)` for inter-process safety
- **Pthread mutexes**: `g_course_mutex`, `g_enroll_mutex`, `g_student_mutex` for intra-process thread safety (Linux `fcntl` locks don't protect threads within the same process)
- **Lock order**: `g_course_mutex` → `g_enroll_mutex` (consistent to prevent deadlock)

### Synchronization Strategy

The enrollment critical section uses a **two-layer locking approach**:
1. `pthread_mutex_lock(&g_course_mutex)` — intra-process serialization
2. `fcntl(F_SETLKW, F_WRLCK)` on `courses.dat` — inter-process serialization
3. Re-read available seats *under* the lock (eliminates TOCTOU race)
4. `pthread_mutex_lock(&g_enroll_mutex)` + `fcntl` on `enrollments.dat`
5. Write enrollment record + decrement seats atomically

---

## Building

```bash
# Server + client
make clean && make all

# Benchmark tools (stress_client, race_test, data_seeder)
make benchmark-tools

# Everything at once
make all benchmark-tools
```

---

## Running

```bash
# Start server (prints metrics every 10s, full summary on Ctrl+C)
./server

# Connect interactive client
./client
```

**Default credentials:**
- Admin: `admin` / `admin123`
- Faculty: ID `1` / `faculty123`
- Student: ID `1001` / `student123`

---

## Benchmarking

### Quick Start

```bash
# 1. Seed 250 test students (IDs 9001-9250) + 5 courses (IDs 101-105)
./data_seeder --seed

# 2. Start server
./server &

# 3. Run stress test
./stress_client --clients 50 --duration 30

# 4. Run race condition test
./race_test --threads 50 --course-id 101
```

### Full Benchmark Suite

```bash
# Runs 10, 50, 100, 250 client levels — saves results to benchmark_results/
bash benchmarks/benchmark.sh

# Quick version (10, 50 clients, 10 seconds each)
bash benchmarks/benchmark.sh --quick

# Race condition validator only
bash benchmarks/run_race_test.sh
```

### Stress Client Options

```
./stress_client [options]
  -c, --clients  N     Concurrent client threads  (default: 50)
  -d, --duration N     Duration in seconds         (default: 30)
  -H, --host ADDR      Server host                 (default: 127.0.0.1)
  -p, --port PORT      Server port                 (default: 8080)
  -s, --start-id SID   First student ID            (default: 9001)
  -C, --course-id CID  Course to enroll/drop        (default: 101)
```

Each simulated session performs: `login → view courses → enroll → drop → logout`

---

## Observability

### Live Metrics (printed every 10s)

```
[METRICS] uptime=250s | reqs=12991 | rps=52.0 | clients=250(peak=251) | ok=12991 err=0
```

### Shutdown Summary (on Ctrl+C)

```
╔══════════════════════════════════════════════════════════╗
║         SERVER PERFORMANCE METRICS SUMMARY               ║
╠══════════════════════════════════════════════════════════╣
║  Uptime              :    283.8 sec                      ║
║  Total Requests      :    12,991                         ║
║  Successful          :    12,991   (100.0%)              ║
║  Throughput          :    45.78 req/sec                  ║
║  Peak Concurrent     :      251 clients                  ║
╠══════════════════════════════════════════════════════════╣
║  OPERATION LATENCIES                                     ║
║  auth           12,991      92.3 µs avg    2,597 µs max ║
║  enroll         12,991  265,511 µs avg  421,781 µs max  ║
║  drop           12,991   88,364 µs avg  167,305 µs max  ║
║  course_query   12,991  160,604 µs avg  162,097 µs max  ║
║  file_op       162,117       1.6 µs avg      617 µs max ║
╠══════════════════════════════════════════════════════════╣
║  SESSION LATENCY  (N=10,000 samples)                     ║
║  Avg  1312.59 ms   P50  1303.39 ms   P95  1411.60 ms   ║
╚══════════════════════════════════════════════════════════╝
```

### Lock Contention Report

```
╔══════════════════════════════════════════════════════════╗
║          LOCK CONTENTION ANALYSIS SUMMARY                ║
╠══════════════════════════════════════════════════════════╣
║  Read  Locks Acquired :   126,527                        ║
║  Write Locks Acquired :    35,590                        ║
║  Avg Read  Wait (µs)  :      1.69                        ║
║  Avg Write Wait (µs)  :      1.23                        ║
║  Max Read  Wait (µs)  :    617.93                        ║
║  Contention Events    :         0   (wait > 1 ms)        ║
╚══════════════════════════════════════════════════════════╝
```

### Structured Log (`logs/server_structured.log`)

JSON-line format — 64,956 entries generated across a full benchmark run:

```json
{"ts":"2026-05-09T00:07:02.803","tid":"128216534587072","cid":10,"op":"LOGIN","status":"OK","latency_ms":0.039}
{"ts":"2026-05-09T00:07:03.144","tid":"128216534587072","cid":10,"op":"ENROLL","status":"OK","latency_ms":265.4}
{"ts":"2026-05-09T00:07:03.232","tid":"128216534587072","cid":10,"op":"DROP","status":"OK","latency_ms":88.2}
{"ts":"2026-05-09T00:07:03.312","tid":"128216534587072","cid":10,"op":"LOGOUT","status":"OK","latency_ms":1341.2}
```

Fields: `ts` (ISO-8601), `tid` (thread ID), `cid` (socket), `op`, `status`, `latency_ms`, optional `detail`. Auto-rotates at 10 MB.

---

## Race Condition Validation

`race_test` uses a `pthread_barrier_t` to fire N threads simultaneously at the same course, then validates 4 consistency invariants:

| Check | Description |
|-------|-------------|
| `available_seats >= 0` | Seat count never goes negative |
| `available_seats <= max_seats` | Seat limit never exceeded |
| `available = max - actual_enrollments` | File-level internal consistency |
| No duplicate enrollments | Each student enrolled at most once |

**Results — 50 threads on a 30-seat course:**
```
Threads run         : 50
Successful enrolls  : 30    ← exactly max_seats
Rejected (full)     : 20    ← correctly blocked
Duplicate-blocked   : 0
available = max - actual  (0 == 30 - 30)   PASS
OVERALL: ALL CHECKS PASSED ✓
```

**Results — 100 threads on the same course:**
```
Successful enrolls  : 30    ← still exactly max_seats
Rejected (full)     : 70    ← 70 correctly turned away
OVERALL: ALL CHECKS PASSED ✓
```

---

## Benchmark Results

> Measured on localhost (server + clients on same machine). Each session = login → view → enroll → drop → logout (4 ops). Duration: 20 seconds per run.

### Throughput

| Clients | Sessions | Sess/sec | Ops/sec | Errors |
|---------|----------|----------|---------|--------|
| 10      | 150      | 7.44     | 29.78   | 0 (0%) |
| 50      | 750      | 37.19    | 148.78  | 0 (0%) |
| 100     | 1,598    | 74.98    | 299.91  | 0 (0%) |
| 250     | 4,000    | 190.65   | 762.58  | 0 (0%) |

Throughput scales **near-linearly** with client count. Error rate holds at **0% across all load levels**.

### Session Latency

| Clients | Avg (ms) | Min (ms) | Max (ms) |
|---------|----------|----------|----------|
| 10      | 1341.40  | 1331.59  | 1435.71  |
| 50      | 1339.20  | 1245.55  | 1500.55  |
| 100     | 1328.26  | 1245.33  | 1496.24  |
| 250     | 1307.70  | 1244.98  | 1497.52  |

Latency stays flat (~1.3 s) regardless of client count — the bottleneck is the protocol's network round-trips, not server computation or lock contention.

### Resource Usage (client-side)

| Clients | Max RSS (KB) | Voluntary ctxsw | Involuntary ctxsw |
|---------|-------------|----------------|-------------------|
| 10      | 1,900       | 9,074          | 44                |
| 50      | 2,044       | 44,467         | 142               |
| 100     | 2,188       | 92,924         | 450               |
| 250     | 4,060       | 230,339        | 1,867             |

Server peak RSS across the full run: **9.28 MB** at 251 concurrent clients (~37 KB/client).

---

## Performance Interpretation

| Metric | What it means |
|--------|--------------|
| **Sessions/sec** | Complete client workflows per second |
| **Ops/sec** | Individual operations per second (4× sessions/sec) |
| **Avg session latency** | End-to-end time per full workflow including all TCP round-trips |
| **P95 latency** | 95th-percentile session time — tail latency indicator |
| **Lock contention events** | Waits exceeding 1 ms — 0 observed across all runs |
| **Involuntary ctxsw** | OS-preempted thread switches — low ratio vs voluntary = I/O-bound (healthy) |
| **Sys vs User CPU** | 5:1 ratio (31 s sys / 5.7 s user) — expected for syscall-heavy file I/O server |

---

## Bug Found & Fixed During Testing

The race test caught a real **TOCTOU (Time-Of-Check-Time-Of-Use)** bug in the original enrollment logic.

**Before fix** — 50 threads on a 30-seat course produced:
```
Successful enrolls : 47   ← should be ≤ 30
available_seats    : -17  ← DATA CORRUPTION
```

**Root cause:** Seat count was checked *before* acquiring the write lock. Any two threads could both see `seats > 0` simultaneously and both enroll.

**Fix:** Re-read seats *inside* the critical section under both `pthread_mutex` and `fcntl` write lock — check and decrement are now atomic.

**After fix:**
```
Successful enrolls : 30   ← exactly max_seats
Rejected (full)    : 20   ← correctly blocked
ALL CHECKS PASSED ✓
```

---

## Project Structure

```
.
├── server.c                        # TCP server, thread dispatch, metrics init
├── client.c                        # Interactive TCP client
├── common_utils.c/h                # send_message / receive_input helpers
├── include/
│   ├── constants.h                 # PORT, MAX_CLIENTS, status codes, file paths
│   └── structures.h                # User, Student, Faculty, Course, Enrollment
├── controllers/
│   ├── admin_controller_{core,ops}.c
│   ├── faculty_controller_{core,ops}.c
│   └── student_controller_{core,ops}.c   ← instrumented with op latencies
├── utils/
│   ├── file_ops.h
│   ├── file_ops_core.c             ← lock_file() wired to instrumented wrappers
│   ├── file_ops_course.c
│   ├── file_ops_enrollment.c       ← mutex+fcntl dual-layer locking (TOCTOU fix)
│   └── file_ops_student.c
├── metrics/
│   ├── metrics.h/c                 # ServerMetrics, p95 ring buffer, resource usage
│   └── lock_stats.h/c              # LockStats, per-file pthread mutexes
├── logging/
│   └── logger.h/c                  # JSON-line structured logger, auto-rotate 10 MB
├── benchmarks/
│   ├── stress_client.c             # Multi-threaded TCP stress tester
│   ├── race_test.c                 # Concurrent enrollment consistency validator
│   ├── data_seeder.c               # Test fixture seeder (--seed/--cleanup/--status)
│   ├── benchmark.sh                # Full benchmark orchestration script
│   └── run_race_test.sh            # Isolated race condition test runner
├── data/                           # Binary .dat files (runtime, git-ignored)
├── logs/                           # server_structured.log (runtime, git-ignored)
├── benchmark_results/              # Saved benchmark outputs (runtime)
└── Makefile
```
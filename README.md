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
# Start server
./server

# Connect interactive client
./client
```

---

## Benchmarking

### Quick Start

```bash
# 1. Seed 250 test students + 5 courses
./data_seeder --seed

# 2. Start server
./server &

# 3. Run stress test (50 clients, 30 seconds)
./stress_client --clients 50 --duration 30

# 4. Run race condition test (50 concurrent enrollments)
./race_test --threads 50 --course-id 101
```

### Full Benchmark Suite

```bash
# Runs 10, 50, 100, 250 client levels and saves results
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

### Server Metrics (printed every 10s and on Ctrl+C)

```
[METRICS] uptime=30s | reqs=250 | rps=8.3 | clients=10(peak=10) | ok=250 err=0
```

Full summary on shutdown:
- Total / successful / failed requests
- Throughput (req/sec)
- Peak concurrent clients
- Per-operation latencies (auth, enroll, drop, course_query, file_op): avg / min / max (µs)
- P50 / P95 session latencies (ms)
- Resource usage: max RSS, user/sys CPU time, context switches

### Lock Contention Report (printed on Ctrl+C)

```
Read  Locks Acquired :      132
Write Locks Acquired :      110
Avg Read  Wait (µs)  :     1.29
Avg Write Wait (µs)  :     1.70
Contention Events    :        0   (wait > 1 ms)
```

### Structured Log (`logs/server_structured.log`)

JSON-line format, one entry per operation:

```json
{"ts":"2026-05-08T23:18:00.123","tid":"140234","cid":4,"op":"LOGIN","status":"OK","latency_ms":0.321}
{"ts":"2026-05-08T23:18:00.456","tid":"140234","cid":4,"op":"ENROLL","status":"OK","latency_ms":12.5}
{"ts":"2026-05-08T23:18:00.789","tid":"140234","cid":4,"op":"LOGOUT","status":"OK","latency_ms":1286.0}
```

Fields: `ts` (ISO-8601), `tid` (thread ID), `cid` (socket/client ID), `op`, `status`, `latency_ms`, optional `detail`

---

## Race Condition Validation

`race_test` spawns N threads simultaneously calling `enroll_student()` on the same course using a `pthread_barrier_t`, then validates:

| Check | Description |
|-------|-------------|
| `available_seats >= 0` | Seat count never goes negative |
| `available_seats <= max_seats` | Seat limit never exceeded |
| `available = max - actual_enrollments` | File state is internally consistent |
| No duplicate enrollments | Each student enrolled at most once |

**Sample output with 50 threads on a 30-seat course:**
```
Successful enrolls  : 30
Rejected (full)     : 20
OVERALL: ALL CHECKS PASSED ✓
```

---

## Performance Interpretation

| Metric | What it means |
|--------|--------------|
| **Sessions/sec** | Complete client workflows per second (login→view→enroll→drop→logout) |
| **Ops/sec** | Individual operations per second (4× sessions) |
| **Avg session latency** | End-to-end time per full workflow including network RTT |
| **P95 latency** | 95th percentile session time — tail latency indicator |
| **Lock contention events** | Times a lock wait exceeded 1ms — high values indicate file I/O bottleneck |
| **Involuntary ctxsw** | Thread preemptions — high values indicate CPU contention |

---

## Sample Benchmark Results

*(Run on Linux with server and clients on localhost)*

| Clients | Sessions | Sess/sec | Ops/sec | Avg Lat(ms) | Errors |
|---------|----------|----------|---------|-------------|--------|
| 10      | ~80      | ~7.7     | ~31     | ~1286       | 0      |
| 50      | TBD      | TBD      | TBD     | TBD         | TBD    |
| 100     | TBD      | TBD      | TBD     | TBD         | TBD    |
| 250     | TBD      | TBD      | TBD     | TBD         | TBD    |

*Run `bash benchmarks/benchmark.sh` to populate this table with your machine's actual numbers.*

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
│   ├── file_ops_enrollment.c       ← mutex+fcntl dual-layer locking
│   └── file_ops_student.c
├── metrics/
│   ├── metrics.h/c                 # ServerMetrics, p95 ring buffer, resource usage
│   └── lock_stats.h/c              # LockStats, per-file pthread mutexes
├── logging/
│   └── logger.h/c                  # JSON-line structured logger, auto-rotate 10MB
├── benchmarks/
│   ├── stress_client.c             # Multi-threaded TCP stress tester
│   ├── race_test.c                 # Concurrent enrollment consistency validator
│   ├── data_seeder.c               # Test fixture seeder (--seed/--cleanup/--status)
│   ├── benchmark.sh                # Full benchmark orchestration script
│   └── run_race_test.sh            # Isolated race condition test runner
├── data/                           # Binary .dat files (runtime)
├── logs/                           # server_structured.log (runtime)
├── benchmark_results/              # Saved benchmark outputs (runtime)
└── Makefile
```
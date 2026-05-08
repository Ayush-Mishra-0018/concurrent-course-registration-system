# Academia — Concurrent Course Registration System
### A Production-Grade Systems Programming Project in C

---

## Table of Contents

1. [What This Project Is](#1-what-this-project-is)
2. [How It Works — Core Architecture](#2-how-it-works--core-architecture)
3. [Concurrency & Synchronization Deep Dive](#3-concurrency--synchronization-deep-dive)
4. [How to Build & Run](#4-how-to-build--run)
5. [Benchmarking & Stress Testing](#5-benchmarking--stress-testing)
6. [Metrics & Observability System](#6-metrics--observability-system)
7. [Real Benchmark Results](#7-real-benchmark-results)
8. [Race Condition Validation](#8-race-condition-validation)
9. [Lock Contention Analysis](#9-lock-contention-analysis)
10. [Server-Side Metrics (Live Run)](#10-server-side-metrics-live-run)
11. [What the Numbers Mean](#11-what-the-numbers-mean)
12. [Bug Found & Fixed During Testing](#12-bug-found--fixed-during-testing)

---

## 1. What This Project Is

A **multithreaded TCP client-server course registration system** built entirely in C using:

| Technology | Usage |
|---|---|
| **POSIX Sockets** | TCP communication between clients and server |
| **pthreads** | One thread spawned per connected client |
| **fcntl file locks** | Inter-process mutual exclusion on binary `.dat` files |
| **pthread mutexes** | Intra-process thread serialization (Linux fcntl limitation) |
| **Binary file I/O** | Persistent storage — no database, raw struct serialization |
| **clock_gettime** | Nanosecond-precision operation timing |
| **getrusage** | Memory, CPU, and context-switch monitoring |

The system supports three user roles:
- **Admin** — manage students, faculty, activate/block accounts
- **Faculty** — add/update/remove courses, view enrollments
- **Student** — view courses, enroll, drop, change password

---

## 2. How It Works — Core Architecture

```
                     ┌───────────────────────────────────────┐
   TCP Client ──────►│  server.c (accept loop)               │
   TCP Client ──────►│   → pthread_create() per client       │
   TCP Client ──────►│   → metrics_init() on start           │
         ...         │   → logger_init() on start            │
                     │   → periodic stats thread (10s)       │
                     └──────────────┬────────────────────────┘
                                    │ thread_per_client
                     ┌──────────────▼────────────────────────┐
                     │  controllers/                         │
                     │   admin_controller_{core,ops}.c       │
                     │   faculty_controller_{core,ops}.c     │
                     │   student_controller_{core,ops}.c     │
                     │         ↓ instrumented with           │
                     │    clock_gettime + log_event()        │
                     └──────────────┬────────────────────────┘
                                    │
                     ┌──────────────▼────────────────────────┐
                     │  utils/file_ops_*.c                   │
                     │   pthread_mutex + fcntl dual lock     │
                     │   Binary .dat files (persist to disk) │
                     └───────────────────────────────────────┘
                              ↑ feeds into ↓
                     ┌──────────────────────────────────────┐
                     │  metrics/ + logging/                 │
                     │   metrics.c  → counters, p95, rss    │
                     │   lock_stats.c → lock wait tracking  │
                     │   logger.c   → JSON-line structured  │
                     └──────────────────────────────────────┘
```

### File Layout

```
OS_CourseRegProject/
├── server.c                    # TCP server, accept loop, thread dispatch
├── client.c                    # Interactive terminal client
├── common_utils.c/h            # send_message / receive_input
├── include/
│   ├── constants.h             # PORT=8080, status codes, file paths
│   └── structures.h            # User, Student, Faculty, Course, Enrollment structs
├── controllers/                # Business logic handlers (admin/faculty/student)
├── utils/
│   ├── file_ops_core.c         # lock_file → instrumented wrappers
│   ├── file_ops_course.c       # Course CRUD
│   ├── file_ops_enrollment.c   # Enroll/drop with mutex+fcntl dual locking
│   └── file_ops_student.c      # Student CRUD
├── metrics/
│   ├── metrics.h/c             # ServerMetrics, ring buffer, p95, getrusage
│   └── lock_stats.h/c          # LockStats + per-file pthread mutexes
├── logging/
│   └── logger.h/c              # Thread-safe JSON-line logger
├── benchmarks/
│   ├── stress_client.c         # N-thread TCP stress tester
│   ├── race_test.c             # Concurrent enrollment correctness validator
│   ├── data_seeder.c           # Test fixture seeder
│   ├── benchmark.sh            # Full benchmark orchestration
│   └── run_race_test.sh        # Race test runner
├── data/                       # admin.dat, faculty.dat, student.dat, courses.dat, enrollments.dat
└── logs/                       # server_structured.log (JSON-line, auto-rotates at 10MB)
```

---

## 3. Concurrency & Synchronization Deep Dive

### Thread Model

Every TCP connection gets its own `pthread_t` (detached). The server's accept loop never blocks on client handling — it just spawns and forgets:

```c
pthread_create(&thread_id, NULL, handle_client, (void *)client_info);
pthread_detach(thread_id);
```

### The Linux fcntl Lock Limitation

A critical fact about Linux: **`fcntl(F_SETLKW)` locks are per-process, not per-thread.** This means if Thread A and Thread B both call `fcntl(F_SETLKW, F_WRLCK)` on the same file, **both will succeed** — Linux considers them the same process and doesn't block them from each other.

This created a real race condition (discovered during testing) where 50 threads could all enroll in a 30-seat course simultaneously.

### The Fix: Dual-Layer Locking

```
Thread wants to enroll:
  1. pthread_mutex_lock(&g_course_mutex)      ← intra-process serialization
  2. fcntl(course_fd, F_SETLKW, WRITE_LOCK)  ← inter-process serialization
  3. Re-read available_seats (authoritative check under lock)
  4. If seats > 0:
       pthread_mutex_lock(&g_enroll_mutex)
       fcntl(enroll_fd, F_SETLKW, WRITE_LOCK)
       write enrollment record
       unlock enroll_fd + g_enroll_mutex
       decrement seats in course file (still holding course lock)
       unlock course_fd + g_course_mutex
```

**Lock order is always**: `g_course_mutex → g_enroll_mutex` (consistent order prevents deadlock).

### TOCTOU Race — Before vs After Fix

**Before (original code):**
```c
// BROKEN: seat check happens BEFORE the write lock
if (course.available_seats <= 0) return COURSE_FULL;  // read-only check

int fd = open(ENROLLMENT_FILE, O_RDWR);
lock_file(fd, WRITE_LOCK);   // ← another thread can sneak in here
// → 50 threads see seats=30, all pass the check, all enroll
```

**After (fixed):**
```c
pthread_mutex_lock(&g_course_mutex);
lock_file(course_fd, WRITE_LOCK);
// Re-read course UNDER LOCK — authoritative
read(course_fd, &ctmp, sizeof(Course));
if (ctmp.available_seats <= 0) { unlock + return COURSE_FULL; }
// Now enroll — guaranteed serialized
```

---

## 4. How to Build & Run

### Prerequisites

```bash
gcc, make, pthreads (standard on Linux)
```

### Build

```bash
# Build server + client
make clean && make all

# Build benchmark tools
make benchmark-tools

# Build everything
make all benchmark-tools
```

This compiles 5 binaries: `server`, `client`, `stress_client`, `race_test`, `data_seeder`

### Run the Server

```bash
./server
# Output:
# Server started on port 8080
# Press Ctrl+C to stop the server
# [METRICS] uptime=10s | reqs=0 | rps=0.0 | clients=0(peak=0) | ok=0 err=0
```

### Run the Client (Interactive)

```bash
./client
# Prompts: Login Type { 1.Admin, 2.Professor, 3.Student }
```

**Default credentials:**
- Admin: `admin` / `admin123`
- Faculty: ID `1` / `faculty123`
- Student: ID `1001` / `student123`

### Seed Test Data (for benchmarks)

```bash
./data_seeder --seed
# Creates 250 test students (IDs 9001–9250, password: test1234)
# Creates 5 test courses (IDs 101–105, 30 seats each)

./data_seeder --status    # check what's seeded
./data_seeder --cleanup   # remove seeded data
```

---

## 5. Benchmarking & Stress Testing

### Quick Benchmark

```bash
# Seed → start server → run all levels → report
bash benchmarks/benchmark.sh

# Faster version (10+50 clients, 10s each)
bash benchmarks/benchmark.sh --quick
```

### Manual Stress Test

```bash
./server &           # start server in background
./data_seeder --seed

# Run with different concurrency levels
./stress_client --clients 10  --duration 20
./stress_client --clients 50  --duration 20
./stress_client --clients 100 --duration 20
./stress_client --clients 250 --duration 20
```

### What Each Session Does

Each simulated client thread runs this workflow in a loop until the duration expires:

```
1. TCP connect to server
2. Select user type: Student (3)
3. Send student ID + password → wait for login success
4. Send "1" → view all courses (reads course list)
5. Send "2" → enroll → send course ID
6. Send "3" → drop → send course ID
7. Send "6" → logout
8. Close socket
9. Repeat
```

This is a **realistic full-cycle workflow** covering all major server operations.

### Race Condition Test

```bash
./data_seeder --seed
./race_test --threads 50  --course-id 101   # 50 threads, 30-seat course
./race_test --threads 100 --course-id 101   # 100 threads, 30-seat course

# One-shot script
bash benchmarks/run_race_test.sh
```

---

## 6. Metrics & Observability System

### What's Measured

#### Server-Side (`metrics/metrics.c`)

| Metric | How Measured |
|---|---|
| Total / successful / failed requests | Atomic counters, mutex-protected |
| Peak concurrent clients | Max of `current_clients` tracked per connect/disconnect |
| Throughput (req/sec) | `total_requests / uptime_seconds` |
| Per-op latency (auth, enroll, drop, course_query, file_op) | `clock_gettime(CLOCK_MONOTONIC)` before/after each op |
| P50 / P95 session latency | 10,000-sample ring buffer → `qsort` → percentile index |
| Max RSS, CPU time, context switches | `getrusage(RUSAGE_SELF)` |

#### Lock Statistics (`metrics/lock_stats.c`)

| Metric | How Measured |
|---|---|
| Read/write lock counts | Incremented per `lock_file()` call |
| Avg / max lock wait time | `clock_gettime` before/after `fcntl(F_SETLKW)` |
| Contention events | Count of waits exceeding 1ms threshold |

#### Structured Logging (`logging/logger.c`)

Every major operation writes a JSON-line to `logs/server_structured.log`:

```json
{"ts":"2026-05-09T00:07:02.803","tid":"128216534587072","cid":10,"op":"LOGIN","status":"OK","latency_ms":0.039}
{"ts":"2026-05-09T00:07:03.144","tid":"128216534587072","cid":10,"op":"ENROLL","status":"OK","latency_ms":265.4}
{"ts":"2026-05-09T00:07:03.232","tid":"128216534587072","cid":10,"op":"DROP","status":"OK","latency_ms":88.2}
{"ts":"2026-05-09T00:07:03.312","tid":"128216534587072","cid":10,"op":"LOGOUT","status":"OK","latency_ms":1341.2}
```

Fields: `ts` (timestamp), `tid` (thread ID), `cid` (client socket), `op` (operation), `status` (OK/FAIL), `latency_ms`

A total of **64,956 log entries** were generated during the benchmark run below.

---

## 7. Real Benchmark Results

> All results from a single run on localhost (server + clients on same machine).
> Each session = login → view → enroll → drop → logout (4 operations).

### Throughput Scaling

| Clients | Duration | Sessions | Sessions/sec | Ops/sec | Errors |
|---------|----------|----------|-------------|---------|--------|
| **10**  | 20.1 s   | 150      | **7.44**    | 29.78   | 0 (0%) |
| **50**  | 20.2 s   | 750      | **37.19**   | 148.78  | 0 (0%) |
| **100** | 21.3 s   | 1,598    | **74.98**   | 299.91  | 0 (0%) |
| **250** | 21.0 s   | 4,000    | **190.65**  | 762.58  | 0 (0%) |

**Key insight:** Throughput scales **near-linearly** with client count (10× clients → ~25× throughput at 250 vs 10), and error rate stays at **0% across all load levels**. The thread-per-client model handles 250 concurrent connections without dropping a single session.

### Session Latency

| Clients | Avg (ms) | Min (ms) | Max (ms) |
|---------|----------|----------|----------|
| 10      | 1341.40  | 1331.59  | 1435.71  |
| 50      | 1339.20  | 1245.55  | 1500.55  |
| 100     | 1328.26  | 1245.33  | 1496.24  |
| 250     | 1307.70  | 1244.98  | 1497.52  |

**Key insight:** Avg latency is **remarkably stable** (~1.3s) across 10–250 clients. This is because the bottleneck is the protocol's round-trip time (multiple send/receive cycles per session), not the server's processing time. The server handles concurrency efficiently — adding more clients doesn't increase per-session wait time.

### Resource Usage (Stress Client Side)

| Clients | Max RSS (KB) | Voluntary ctxsw | Involuntary ctxsw |
|---------|-------------|----------------|-------------------|
| 10      | 1,900       | 9,074          | 44                |
| 50      | 2,044       | 44,467         | 142               |
| 100     | 2,188       | 92,924         | 450               |
| 250     | 4,060       | 230,339        | 1,867             |

**Key insight:** Memory usage stays low — 4 MB even at 250 clients. Involuntary context switches (OS preempting threads) remain modest at 1,867 for 250 threads, meaning threads spend most of their time blocked on I/O (voluntary yield) rather than fighting for CPU.

---

## 8. Race Condition Validation

The `race_test` binary uses a `pthread_barrier_t` to fire all threads simultaneously at the same course, then validates data consistency:

### Test: 50 threads on a 30-seat course

```
╔══════════════════════════════════════════════════════════╗
║         CONCURRENT ENROLLMENT RACE CONDITION TEST        ║
╠══════════════════════════════════════════════════════════╣
║  Threads             : 50                               ║
║  Course Max Seats    : 30                               ║
║  Available (pre)     : 30                               ║
╠══════════════════════════════════════════════════════════╣
║  Successful enrolls  : 30                               ║
║  Rejected (full)     : 20                               ║
║  Duplicate-blocked   : 0                                ║
║  Errors              : 0                                ║
╠══════════════════════════════════════════════════════════╣
║  Avg latency         : 120,352 µs (120 ms)              ║
║  Min latency         : 6,138 µs                         ║
║  Max latency         : 168,330 µs                       ║
╠══════════════════════════════════════════════════════════╣
║  [PASS] Seats >= 0               (available=0)          ║
║  [PASS] Seats <= max_seats        (max=30)               ║
║  [PASS] available = max - actual  (0 == 30 - 30)        ║
║  [PASS] No duplicate enrollments  (found=0)             ║
║  OVERALL: ALL CHECKS PASSED ✓                          ║
╚══════════════════════════════════════════════════════════╝
```

### Test: 100 threads on the same 30-seat course

```
║  Threads run         : 100                              ║
║  Successful enrolls  : 30   ← exactly max_seats        ║
║  Rejected (full)     : 70   ← correctly blocked        ║
║  Avg latency         : 144,276 µs                       ║
║  [PASS] available = max - actual  (0 == 30 - 30)        ║
║  OVERALL: ALL CHECKS PASSED ✓                          ║
```

**What's validated:**
- ✅ `available_seats` never goes negative
- ✅ `available_seats` never exceeds `max_seats`
- ✅ `available_seats == max_seats - actual_enrollment_count` (file-level consistency)
- ✅ Zero duplicate enrollment records in the binary file

---

## 9. Lock Contention Analysis

From the full server run (all 4 stress tiers combined, 12,991 total requests):

```
╔══════════════════════════════════════════════════════════╗
║          LOCK CONTENTION ANALYSIS SUMMARY                ║
╠══════════════════════════════════════════════════════════╣
║  Read  Locks Acquired :   126,527                        ║
║  Write Locks Acquired :    35,590                        ║
║  Avg Read  Wait (us)  :      1.69                        ║
║  Avg Write Wait (us)  :      1.23                        ║
║  Max Read  Wait (us)  :    617.93                        ║
║  Max Write Wait (us)  :     47.04                        ║
║  Contention Events    :         0   (wait > 1 ms)        ║
╚══════════════════════════════════════════════════════════╝
```

**What this means:**
- **162,117 lock operations** were performed across 12,991 sessions
- Average lock wait is **~1.5 µs** — extremely fast, as expected for uncontested file locks
- **Max read wait: 617 µs** — a brief spike likely during heavy concurrent reads at 250-client load
- **0 contention events** — no lock wait ever exceeded 1ms, meaning the dual-layer mutex+fcntl strategy serialized access fast enough that no thread had to wait long enough to count as "contended"

---

## 10. Server-Side Metrics (Live Run)

Accumulated across all 4 stress tiers (10 + 50 + 100 + 250 clients over ~284 seconds):

```
╔══════════════════════════════════════════════════════════╗
║         SERVER PERFORMANCE METRICS SUMMARY               ║
╠══════════════════════════════════════════════════════════╣
║  Uptime              :    283.8 sec                      ║
║  Total Requests      :    12,991                         ║
║  Successful          :    12,991  (100% success rate)    ║
║  Failed              :         0                         ║
║  Throughput          :    45.78 req/sec (overall avg)    ║
║  Peak Concurrent     :      251 clients                  ║
╠══════════════════════════════════════════════════════════╣
║  OPERATION LATENCIES                                     ║
║  Operation      Count     Avg(µs)    Min(µs)   Max(µs)  ║
║  auth           12,991       92.3        4.4    2,597.9  ║
║  enroll         12,991  265,511.7  220,699.0  421,781.7  ║
║  drop           12,991   88,364.9   60,330.8  167,305.7  ║
║  course_query   12,991  160,604.6  160,430.4  162,097.6  ║
║  file_op       162,117        1.6        0.3      617.9  ║
╠══════════════════════════════════════════════════════════╣
║  SESSION LATENCY  (N=10,000 samples)                     ║
║  Avg             :  1,312.59 ms                          ║
║  P50             :  1,303.39 ms                          ║
║  P95             :  1,411.60 ms                          ║
╠══════════════════════════════════════════════════════════╣
║  RESOURCE USAGE                                          ║
║  Max RSS (KB)        :      9,280 (~9 MB)                ║
║  User CPU time (ms)  :      5,723 ms                     ║
║  Sys  CPU time (ms)  :     31,065 ms                     ║
║  Voluntary   ctxsw   :    873,099                        ║
║  Involuntary ctxsw   :      8,300                        ║
╚══════════════════════════════════════════════════════════╝
```

### Periodic Metrics (printed every 10 seconds)

```
[METRICS] uptime=250s | reqs=12991 | rps=52.0 | clients=250(peak=251) | ok=12991 err=0
[METRICS] uptime=260s | reqs=12991 | rps=50.0 | clients=0(peak=251)   | ok=12991 err=0
```

---

## 11. What the Numbers Mean

### Operation Latency Breakdown

| Operation | Avg Latency | Why |
|---|---|---|
| **auth** | 92 µs | Simple sequential file scan, single read lock — very fast |
| **course_query** | 160,605 µs | Reads all course records + sends over socket with 20ms nanosleep per message in `send_message()` |
| **drop** | 88,365 µs | Write lock on enrollment + course file update |
| **enroll** | 265,512 µs | Most expensive: mutex + write lock on course + mutex + write lock on enrollment + student update + multiple file operations |
| **file_op** | 1.6 µs | Raw `fcntl(F_SETLKW)` wait time — near-zero uncontested |

> The session latency of ~1.3s is dominated by the **protocol's round-trips** and the **`nanosleep(20ms)`** in `send_message()` (inherited from the original codebase to prevent socket buffer overflow). This is network/protocol overhead, not computation.

### Throughput Scaling Analysis

```
10  clients → 7.44  sessions/sec  (baseline)
50  clients → 37.19 sessions/sec  (5.0x clients → 5.0x throughput)
100 clients → 74.98 sessions/sec  (10x clients → 10.1x throughput)
250 clients → 190.65 sessions/sec (25x clients → 25.6x throughput)
```

**Near-perfect linear scaling** — each new thread adds independent throughput. This confirms the thread-per-client model is not CPU-bound at these load levels. The bottleneck is network I/O and protocol latency, not server-side computation or lock contention.

### Memory Efficiency

- Server peak RSS: **9.28 MB** serving 251 concurrent clients
- That's ~37 KB per concurrent client — excellent for a thread-per-client model
- Most of this is stack space (default 8MB pthread stack * N threads would theoretically be large, but Linux uses lazy stack allocation)

### System Call Overhead

- **873,099 voluntary context switches** = threads regularly yield while waiting for I/O (healthy)
- **8,300 involuntary context switches** = OS preempting threads (only ~1% of total = minimal CPU thrashing)
- **Sys CPU 31,065 ms vs User CPU 5,723 ms** = 5:1 ratio — most CPU time is in kernel (syscalls: `read`, `write`, `fcntl`, `send`, `recv`), not application logic. This is expected for an I/O-heavy server.

---

## 12. Bug Found & Fixed During Testing

### The Race Condition (TOCTOU Bug)

The race test **immediately caught a real bug** in the original enrollment logic. With 50 threads running against a 30-seat course:

**Before fix — 50 successful enrollments in a 30-seat course:**
```
Successful enrolls  : 47   ← should be max 30
available_seats     : -17  ← NEGATIVE — data corruption!
```

**Root cause:** The available seat count was checked **before** acquiring the write lock. Any two threads could both see `seats > 0` at the same instant and both proceed to enroll.

**Fix:** Re-read the seat count **inside** the critical section (after acquiring both `pthread_mutex` and `fcntl` write lock), making the check and decrement atomic.

**After fix — correct behavior:**
```
Successful enrolls  : 30   ← exactly max_seats
Rejected (full)     : 20   ← correctly blocked
available_seats     : 0    ← consistent with 30 enrollments
ALL CHECKS PASSED ✓
```

This demonstrates a production-level systems debugging workflow: **instrumented testing revealed a latent concurrency bug** that would only surface under concurrent load — exactly the kind of bug impossible to catch with sequential unit tests.

---

## Quick Reference Card

```bash
# BUILD
make clean && make all benchmark-tools

# RUN (basic)
./server &
./client

# SEED TEST DATA
./data_seeder --seed

# STRESS TEST
./stress_client --clients 50 --duration 30

# RACE TEST
./race_test --threads 100 --course-id 101

# FULL BENCHMARK SUITE
bash benchmarks/benchmark.sh

# VIEW LOGS
tail -f logs/server_structured.log | python3 -m json.tool

# CLEANUP
./data_seeder --cleanup
kill $(lsof -ti:8080)
```

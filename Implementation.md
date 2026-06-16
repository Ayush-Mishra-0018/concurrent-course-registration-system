# Implementation Details

A technical walkthrough of every extension added to the base course-registration system — what was built, how it works internally, and how every number in the results was actually produced.

---

## Table of Contents

1. [What the Base System Looked Like](#1-what-the-base-system-looked-like)
2. [Metrics Module — How Numbers Are Measured](#2-metrics-module--how-numbers-are-measured)
3. [Lock Contention Analysis](#3-lock-contention-analysis)
4. [Structured Logging](#4-structured-logging)
5. [Hooking Into Existing Code](#5-hooking-into-existing-code)
6. [Benchmark Tools](#6-benchmark-tools)
7. [The TOCTOU Bug — Discovery, Proof, and Fix](#7-the-toctou-bug--discovery-proof-and-fix)
8. [How Each Benchmark Number Was Produced](#8-how-each-benchmark-number-was-produced)

---

## 1. What the Base System Looked Like

The original codebase had:
- `server.c` — accept loop, `pthread_create` + `pthread_detach` per client
- `controllers/` — three handlers (admin, faculty, student) wired to the socket
- `utils/file_ops_*.c` — raw `fcntl(F_SETLKW)` locking on five binary `.dat` files
- `include/structures.h` — `Student`, `Faculty`, `Course`, `Enrollment` packed structs

There was no timing, no counters, no logging. The goal was to add all of that **without changing any existing logic** — only wrap and instrument.

---

## 2. Metrics Module — How Numbers Are Measured

### Files: `metrics/metrics.h`, `metrics/metrics.c`

#### The Central Struct

```c
typedef struct {
    uint64_t total_requests;
    uint64_t successful_requests;
    uint64_t failed_requests;
    int      current_clients;
    int      peak_concurrent_clients;
    OpStats  op_stats[OP_COUNT];          // per-op latency buckets
    uint64_t latency_samples[10000];      // ring buffer for p95
    int      latency_write_idx;
    int      latency_count;
    struct timespec server_start_time;
    pthread_mutex_t lock;
} ServerMetrics;
```

A single global `g_metrics` instance. Every field is updated under `g_metrics.lock` (a `pthread_mutex_t`), so any thread can call any metrics function without data races.

#### How `time_now_ns()` Works

Defined as an inline in `metrics.h`:

```c
static inline uint64_t time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
```

`CLOCK_MONOTONIC` is used (not `CLOCK_REALTIME`) because it never jumps backward and is unaffected by NTP adjustments. It gives nanosecond-precision wall time from an arbitrary epoch. Two calls bracketing an operation give exact elapsed nanoseconds:

```c
uint64_t t0  = time_now_ns();
/* ... operation ... */
uint64_t ns  = time_now_ns() - t0;
```

#### Per-Operation Latency Tracking

Five operation buckets: `OP_AUTH`, `OP_ENROLL`, `OP_DROP`, `OP_COURSE_QUERY`, `OP_FILE_OP`.

Each bucket stores `count`, `total_ns`, `min_ns`, `max_ns`. On every call to `metrics_record_latency(op, ns)`:

```c
s->count++;
s->total_ns += latency_ns;
if (latency_ns < s->min_ns) s->min_ns = latency_ns;
if (latency_ns > s->max_ns) s->max_ns = latency_ns;
```

At print time: `avg_us = (total_ns / count) / 1000.0`.

#### P50 / P95 Session Latency — Ring Buffer + qsort

Session latency (total time from TCP connect to socket close) is stored in a 10,000-slot ring buffer:

```c
int idx = g_metrics.latency_write_idx % LATENCY_SAMPLES_MAX;
g_metrics.latency_samples[idx] = latency_ns;
g_metrics.latency_write_idx++;
```

When the buffer fills, old samples are overwritten (ring behaviour). At summary time, the live samples are copied out and sorted with `qsort`:

```c
qsort(sorted, count, sizeof(uint64_t), cmp_u64);
uint64_t p50 = sorted[count / 2];
uint64_t p95 = sorted[(int)(count * 0.95)];
```

This gives true percentile values over the last 10,000 sessions — accurate without needing a histogram or streaming algorithm.

#### Resource Usage — `getrusage`

At shutdown, `metrics_print_summary()` calls:

```c
struct rusage ru;
getrusage(RUSAGE_SELF, &ru);
```

This populates:
- `ru.ru_maxrss` — peak RSS in KB (Linux)
- `ru.ru_utime` — user-space CPU time consumed
- `ru.ru_stime` — kernel-space CPU time consumed
- `ru.ru_nvcsw` — voluntary context switches (thread blocked on I/O)
- `ru.ru_nivcsw` — involuntary context switches (OS preempted the thread)

No sampling or polling needed — the OS accumulates these figures automatically for the process lifetime.

#### Periodic Stats Thread

In `server.c`, a dedicated detached thread wakes every 10 seconds and calls `metrics_print_periodic()`:

```c
static void *periodic_stats_thread(void *arg) {
    for (;;) {
        sleep(10);
        metrics_print_periodic();
    }
}
// spawned once in main():
pthread_create(&stats_tid, NULL, periodic_stats_thread, NULL);
pthread_detach(stats_tid);
```

`metrics_print_periodic()` prints a one-liner with uptime, request count, rolling RPS, and active/peak clients. The full `metrics_print_summary()` only runs on `SIGINT` (Ctrl+C) via the signal handler.

---

## 3. Lock Contention Analysis

### Files: `metrics/lock_stats.h`, `metrics/lock_stats.c`

#### The Instrumentation Strategy

The original code called `lock_file(fd, type)` which internally called `fcntl(F_SETLKW, ...)`. The entire instrumentation was inserted by changing just **two lines** in `utils/file_ops_core.c`:

```c
// Before (original):
int lock_file(int fd, int lock_type) {
    struct flock fl = { ... };
    return fcntl(fd, F_SETLKW, &fl);
}

// After (instrumented):
int lock_file(int fd, int lock_type) {
    return instrumented_lock_file(fd, lock_type);   // delegate
}
int unlock_file(int fd) {
    return instrumented_unlock_file(fd);
}
```

Every `lock_file()` call across all five `file_ops_*.c` files now automatically flows through the instrumented wrapper — zero changes needed in the business logic files.

#### Inside `instrumented_lock_file`

```c
int instrumented_lock_file(int fd, int lock_type) {
    uint64_t t0      = time_now_ns();
    int      ret     = raw_lock_file(fd, lock_type);   // actual fcntl
    uint64_t wait_ns = time_now_ns() - t0;

    // update LockStats counters
    if (lock_type == READ_LOCK) {
        g_lock_stats.read_lock_count++;
        g_lock_stats.read_total_wait_ns  += wait_ns;
        if (wait_ns > g_lock_stats.read_max_wait_ns)
            g_lock_stats.read_max_wait_ns = wait_ns;
    } else { /* WRITE_LOCK */
        g_lock_stats.write_lock_count++;
        g_lock_stats.write_total_wait_ns += wait_ns;
        if (wait_ns > g_lock_stats.write_max_wait_ns)
            g_lock_stats.write_max_wait_ns = wait_ns;
    }
    if (wait_ns > LOCK_CONTENTION_THRESHOLD_NS)   // 1,000,000 ns = 1 ms
        g_lock_stats.contention_count++;

    metrics_record_latency(OP_FILE_OP, wait_ns);   // feeds main metrics too
    return ret;
}
```

The wall time between calling `fcntl(F_SETLKW)` and it returning is exactly the time the thread spent **blocked waiting** for the lock. If no other process holds the lock, `fcntl` returns almost instantly (~1 µs). If there is contention, the thread sleeps in the kernel until the lock is released, and the measured wait time grows accordingly.

#### Per-File Pthread Mutexes

During implementation a critical Linux limitation was discovered (explained in detail in Section 7). The fix required three global mutexes, declared in `lock_stats.c` and initialised in `lock_stats_init()`:

```c
pthread_mutex_t g_course_mutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_enroll_mutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_student_mutex = PTHREAD_MUTEX_INITIALIZER;
```

These are exported in `lock_stats.h` so `file_ops_enrollment.c` can use them directly.

---

## 4. Structured Logging

### Files: `logging/logger.h`, `logging/logger.c`

#### Design Goals

- Thread-safe: multiple threads write concurrently
- Zero-allocation hot path: all formatting on the stack
- Auto-rotation: log file never grows unbounded
- Machine-readable: JSON-line format, one object per line

#### Thread Safety

A single `static pthread_mutex_t g_log_mutex` serialises all writes. `log_event()` locks, formats to the file pointer, calls `fflush()`, then unlocks. The lock is held for a single `fprintf` + `fflush` call, so contention is minimal.

#### Timestamp Generation

```c
static void iso_timestamp(char *buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);   // wall time for human readability
    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);
    int ms = (int)(ts.tv_nsec / 1000000);
    strftime(buf, ...);
    // appends ".NNN" milliseconds manually
}
```

`CLOCK_REALTIME` (not `CLOCK_MONOTONIC`) is used for log timestamps because they need to be human-readable wall-clock times. The nanoseconds are truncated to milliseconds for readability.

#### Auto-Rotation at 10 MB

Before every write, `maybe_rotate()` checks `ftell(g_log_fp)`. If the file exceeds `10 * 1024 * 1024` bytes, it closes the file, renames it to `server_structured.log.1`, and opens a fresh `server_structured.log`. This is a simple single-rotation scheme — enough for a benchmark run that generates ~65K lines.

#### Output Format

```json
{"ts":"2026-05-09T00:07:02.803","tid":"128216534587072","cid":10,"op":"LOGIN","status":"OK","latency_ms":0.039}
```

Fields: `ts` (ISO-8601 + ms), `tid` (raw `pthread_self()` cast to `unsigned long`), `cid` (socket fd used as client ID), `op` (operation name constant), `status` (`"OK"` or `"FAIL"`), `latency_ms` (formatted to 3 decimal places), optional `detail` string.

---

## 5. Hooking Into Existing Code

### `server.c` — Four Addition Points

**1. Init on startup (in `main`):**
```c
metrics_init();
lock_stats_init();
logger_init("logs/server_structured.log");
```

**2. Session timing (in `handle_client`):**
```c
uint64_t session_start = time_now_ns();
metrics_client_connect();
// ... all existing client handling logic unchanged ...
uint64_t session_ns = elapsed_ns(session_start);
metrics_record_session_latency(session_ns);
metrics_client_disconnect();
log_event(tid, cid, LOG_OP_LOGOUT, LOG_STATUS_OK, session_ns / 1e6, NULL);
```

**3. Periodic reporter (in `main`, after `listen`):**
```c
pthread_t stats_tid;
pthread_create(&stats_tid, NULL, periodic_stats_thread, NULL);
pthread_detach(stats_tid);
```

**4. Shutdown summary (in `handle_signal`):**
```c
void handle_signal(int sig) {
    if (sig == SIGINT) {
        metrics_print_summary();
        lock_stats_print();
        logger_close();
        close(server_socket);
        exit(EXIT_SUCCESS);
    }
}
```

### `controllers/student_controller_core.c` — Operation Wrapping

Each operation is wrapped with a `time_now_ns()` / `elapsed_ns()` pair and the result fed to the metrics and logger. Example for enroll:

```c
case 2: {
    uint64_t t0 = time_now_ns();
    int r = enroll_new_course(client_socket, student_id);
    uint64_t ns = elapsed_ns(t0);
    metrics_record_latency(OP_ENROLL, ns);
    log_event((unsigned long)pthread_self(), client_socket,
              LOG_OP_ENROLL,
              r == SUCCESS ? LOG_STATUS_OK : LOG_STATUS_FAIL,
              ns / 1e6, NULL);
    break;
}
```

The existing `enroll_new_course()` function body was not changed at all. The timing brackets wrap the call from the outside.

---

## 6. Benchmark Tools

### `data_seeder.c` — Deterministic Test Fixtures

Calls `file_ops` functions directly (no TCP, no server needed). On `--seed`:
- Iterates student IDs 9001–9250. If a record exists, reactivates it (`status = ACTIVE`) and resets `course_count = 0`. If not, creates it fresh.
- Does the same for courses 101–105 (resets `available_seats` to 30).
- **Truncates the enrollment file** with `open(ENROLLMENT_FILE, O_WRONLY | O_TRUNC)` to remove all stale records from previous runs.

This guarantees a perfectly clean, known state before every benchmark run.

### `stress_client.c` — Realistic Load Generation

Each of the N worker threads:
1. Allocates a `WorkerArg` struct (owns its own `ThreadStats` pointer and a unique `student_id = start_id + thread_index`)
2. Opens a TCP connection to the server
3. Runs the full protocol: choose user type → send ID + password → get menu → send choice 1 (view) → send choice 2 (enroll) → send course ID → send choice 3 (drop) → send course ID → send choice 6 (logout)
4. Measures wall time per complete session with `clock_gettime(CLOCK_MONOTONIC)`
5. Loops until the global `volatile int g_stop = 1` is set

The main thread calls `sleep(duration)` then sets `g_stop = 1`. All threads finish their current session and exit. Results are aggregated across all `ThreadStats` structs.

**Each client has its own student ID** (thread 0 uses 9001, thread 1 uses 9002, etc.), so there is no cross-thread contention at the student-record level — the stress test focuses contention on the course and enrollment files.

### `race_test.c` — Controlled Contention

Uses `pthread_barrier_t` to synchronise all threads to fire at the same instant:

```c
static pthread_barrier_t g_start_barrier;

static void *race_worker(void *arg) {
    RaceArg *ra = (RaceArg *)arg;
    pthread_barrier_wait(&g_start_barrier);   // all threads block here
    uint64_t t0 = time_now_ns();
    ra->result  = enroll_student(ra->student_id, ra->course_id);
    ra->latency_ns = elapsed_ns(t0);
    return NULL;
}
```

`pthread_barrier_wait` blocks each thread until all N threads have called it. The last thread to arrive releases all of them simultaneously. This maximises the chance of a real race by ensuring all threads hit `enroll_student()` in the same scheduler quantum.

After all threads join, four consistency invariants are validated:

```c
// 1. Seats never went negative
assert(after.available_seats >= 0);

// 2. Seats never exceeded max
assert(after.available_seats <= after.max_seats);

// 3. File state is internally consistent
int actual = count_course_enrollments(course_id);  // direct file scan
assert(after.available_seats == after.max_seats - actual);

// 4. No student enrolled more than once
// For each student: scan enrollment file, count records with that student_id + course_id
```

Check 3 uses a **direct binary file scan** (not `get_course_enrollments()`) to avoid the 100-record cap in the helper function.

---

## 7. The TOCTOU Bug — Discovery, Proof, and Fix

### What TOCTOU Means

**Time-Of-Check-Time-Of-Use**: a class of race condition where the state of a resource is checked at time T1, but by time T2 (when it's actually used), another thread has already changed it.

### How It Was Discovered

The first run of `race_test` with 50 threads on a 30-seat course produced:

```
Successful enrolls : 47    ← impossible: course has 30 seats
available_seats    : -17   ← negative: data corruption
```

The race test's consistency check 3 failed: `available_seats != max_seats - actual_enrollments`.

### Root Cause in the Original Code

The original `enroll_student()` in `file_ops_enrollment.c`:

```c
int enroll_student(int student_id, int course_id) {
    // Step 1: read course (read lock held briefly, then released)
    Course course;
    result = get_course_by_id(course_id, &course);

    // Step 2: CHECK seats HERE — no lock held
    if (course.available_seats <= 0) return COURSE_FULL;

    // <<< WINDOW: another thread can run here, also see seats > 0 >>>

    // Step 3: open enrollment file and acquire write lock
    int fd = open(ENROLLMENT_FILE, O_RDWR);
    lock_file(fd, WRITE_LOCK);

    // Step 4: write enrollment record ...
    // Step 5: unlock enrollment file

    // Step 6: decrement seats — but another thread already decremented too
    course.available_seats--;
    update_course(course);
}
```

`get_course_by_id` acquires a read lock, reads the course, then **releases the lock**. The seat check happens after the lock is gone. With 50 threads, many threads simultaneously see `available_seats = 30`, all pass the check, all acquire the enrollment write lock one-by-one, all write records, and all decrement the seat counter — from their own stale copy of the course struct.

### Why `fcntl` Alone Cannot Fix It

The natural instinct is "just hold the lock during the check". But there is a deeper problem: **Linux `fcntl(F_SETLKW)` locks are per-process, not per-thread**.

From the Linux man page (`fcntl(2)`):
> "If a process uses open(2) (or similar) to obtain more than one file descriptor for the same file, these file descriptors are treated independently by F_SETLK and F_SETLKW. An attempt to lock the file using one of these file descriptors may be denied by a lock that the calling process has already placed via another file descriptor."

More critically: **threads within the same process share the process's lock ownership**. Thread A and Thread B both calling `fcntl(F_SETLKW, F_WRLCK)` on the same file will both succeed — the kernel does not block Thread B if Thread A (same process) already holds the lock.

This was confirmed experimentally: even after adding `fcntl` write locks around the seat check, the race still occurred because all threads in the stress test are in the same process.

### The Fix — Dual-Layer Locking

The solution: use `pthread_mutex_t` for **intra-process** serialisation and keep `fcntl` for **inter-process** serialisation (if multiple server processes ever run). The mutex is acquired first, then `fcntl`, then the critical section executes:

```c
int enroll_student(int student_id, int course_id) {
    // 1. Acquire intra-process mutex
    pthread_mutex_lock(&g_course_mutex);

    // 2. Open course file and acquire fcntl write lock (inter-process)
    int course_fd = open(COURSE_FILE, O_RDWR);
    lock_file(course_fd, WRITE_LOCK);

    // 3. RE-READ course under the lock — authoritative value
    Course ctmp;
    off_t course_offset = lseek(course_fd, 0, SEEK_CUR);
    read(course_fd, &ctmp, sizeof(Course));
    // ...find matching course_id...

    // 4. Seat check is now authoritative — no thread can change seats concurrently
    if (course.available_seats <= 0) {
        unlock_file(course_fd); close(course_fd);
        pthread_mutex_unlock(&g_course_mutex);
        return COURSE_FULL;
    }

    // 5. Acquire enrollment mutex and fcntl lock (second in order)
    pthread_mutex_lock(&g_enroll_mutex);
    int enroll_fd = open(ENROLLMENT_FILE, O_RDWR | O_CREAT, 0644);
    lock_file(enroll_fd, WRITE_LOCK);

    // 6. Write enrollment record ...

    unlock_file(enroll_fd); close(enroll_fd);
    pthread_mutex_unlock(&g_enroll_mutex);

    // 7. Decrement seats while STILL holding course mutex + fcntl
    course.available_seats--;
    lseek(course_fd, course_offset, SEEK_SET);
    write(course_fd, &course, sizeof(Course));
    fsync(course_fd);

    unlock_file(course_fd); close(course_fd);
    pthread_mutex_unlock(&g_course_mutex);
}
```

**Lock order is always `g_course_mutex → g_enroll_mutex`** everywhere in the codebase. Consistent ordering prevents deadlock: no thread ever holds `g_enroll_mutex` and then tries to acquire `g_course_mutex`.

### Verification After Fix

```
race_test --threads 50  --course-id 101
  Successful enrolls : 30   (exactly max_seats)
  Rejected (full)    : 20   (correctly blocked)
  available_seats    : 0    (== 30 - 30)
  ALL CHECKS PASSED ✓

race_test --threads 100 --course-id 101
  Successful enrolls : 30
  Rejected (full)    : 70
  ALL CHECKS PASSED ✓
```

The fix is also tested implicitly by the stress_client runs: across 12,991 total enroll/drop cycles at up to 250 concurrent threads, **0 errors** were recorded.

---

## 8. How Each Benchmark Number Was Produced

### Stress Test Numbers

Each number in the results table comes from a single `stress_client` run:

| Number | Source |
|--------|--------|
| `Sessions Completed: 150` | `stats[i].sessions_completed` summed across all worker threads |
| `Sessions/sec: 7.44` | `total_sessions / elapsed_wall_time` (measured with `clock_gettime`) |
| `Ops/sec: 29.78` | `total_ops / elapsed_wall_time` where `ops = sessions * 4` (view+enroll+drop+logout) |
| `Avg Lat: 1341 ms` | `sum(all session latencies) / total_sessions` |
| `Min/Max Lat` | tracked per-thread in `ThreadStats.min_latency_ns / max_latency_ns` |
| `Max RSS: 1900 KB` | `getrusage(RUSAGE_SELF).ru_maxrss` on the stress_client process |
| `Voluntary ctxsw: 9074` | `getrusage(RUSAGE_SELF).ru_nvcsw` — threads blocking on TCP recv |

### Server-Side Operation Latencies

| Number | Source |
|--------|--------|
| `auth avg 92 µs` | `g_metrics.op_stats[OP_AUTH].total_ns / count / 1000` |
| `enroll avg 265,511 µs` | `g_metrics.op_stats[OP_ENROLL].total_ns / count / 1000` |
| Enroll is slowest because it: (1) acquires two mutexes, (2) scans course file, (3) scans enrollment file, (4) writes two records, (5) calls `fsync` | — |
| `course_query avg 160,604 µs` | Dominated by `send_message()` which has a `nanosleep(20ms)` per message to avoid socket buffer overflow — sending a full course list triggers multiple `send_message` calls |
| `file_op avg 1.6 µs` | Time `fcntl(F_SETLKW)` blocks — essentially zero when uncontested |
| `P95 session: 1411 ms` | From 10,000-sample ring buffer, sorted with `qsort`, index at `count * 0.95` |

### Lock Contention Numbers

| Number | Source |
|--------|--------|
| `Read Locks: 126,527` | `g_lock_stats.read_lock_count` — each `get_*` call acquires one |
| `Write Locks: 35,590` | `g_lock_stats.write_lock_count` — each `add/update` call acquires one |
| `Avg Read Wait: 1.69 µs` | `read_total_wait_ns / read_lock_count / 1000` |
| `Contention Events: 0` | Zero fcntl waits exceeded 1 ms — mutexes absorbed all real contention before fcntl was reached |

### Why Session Latency Is ~1.3 s

The ~1.3 s per session has nothing to do with file I/O or locking. Tracing the protocol:

1. Client sends user-type → server receives → server sends prompt: **~1 RTT**
2. Client sends ID → server receives → server sends prompt: **~1 RTT**
3. Client sends password → authenticate (~92 µs) → server sends menu: **~1 RTT**
4. Client sends "1" → view courses → multiple `send_message` calls each with `nanosleep(20ms)` → server sends menu: **~160 ms**
5. Client sends "2" → view courses again (another `nanosleep` batch) → server asks for course ID: **~160 ms**
6. Client sends course ID → enroll (~265 ms) → server sends menu: **~1 RTT**
7. Client sends "3" → drop handler (which also calls view_all_courses) → server asks for course ID: **~160 ms**
8. Client sends course ID → drop (~88 ms): **~1 RTT**
9. Client sends "6" → logout: **~1 RTT**

The dominant factor is the `nanosleep` inside `send_message()` repeated across course-listing steps. The locking and file I/O latency is small by comparison.

### Why Throughput Scales Linearly

```
10  clients →  7.44 sess/sec
50  clients → 37.19 sess/sec  (5.0x clients → 5.0x throughput)
100 clients → 74.98 sess/sec  (10x clients → 10.1x throughput)
250 clients → 190.65 sess/sec (25x clients → 25.6x throughput)
```

Each client thread spends most of its time **blocked on network I/O** (send/recv syscalls). While one thread is blocked, other threads run. Adding more threads adds more independent I/O parallelism without meaningfully increasing CPU or lock pressure. This confirms the server is I/O-bound, not CPU-bound — the correct behaviour for a file-backed networked system.

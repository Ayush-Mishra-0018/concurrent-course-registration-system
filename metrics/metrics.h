#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <sys/resource.h>

#define LATENCY_SAMPLES_MAX 10000

/* ── Operation types ── */
typedef enum {
    OP_AUTH         = 0,
    OP_ENROLL       = 1,
    OP_DROP         = 2,
    OP_COURSE_QUERY = 3,
    OP_FILE_OP      = 4,
    OP_COUNT        = 5
} OpType;

static const char * const OP_NAMES[OP_COUNT] = {
    "auth", "enroll", "drop", "course_query", "file_op"
};

/* ── Per-operation latency stats ── */
typedef struct {
    uint64_t count;
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
} OpStats;

/* ── Master metrics struct ── */
typedef struct {
    /* Request counters */
    uint64_t total_requests;
    uint64_t successful_requests;
    uint64_t failed_requests;

    /* Concurrency */
    int current_clients;
    int peak_concurrent_clients;

    /* Per-operation latencies */
    OpStats op_stats[OP_COUNT];

    /* Session latency ring buffer (for p95) */
    uint64_t latency_samples[LATENCY_SAMPLES_MAX];
    int      latency_write_idx;
    int      latency_count;

    /* Server start time */
    struct timespec server_start_time;

    /* Mutex protecting all fields */
    pthread_mutex_t lock;
} ServerMetrics;

extern ServerMetrics g_metrics;

/* ── API ── */
void metrics_init(void);
void metrics_client_connect(void);
void metrics_client_disconnect(void);
void metrics_record_request(int success);
void metrics_record_latency(OpType op, uint64_t latency_ns);
void metrics_record_session_latency(uint64_t latency_ns);
void metrics_print_summary(void);
void metrics_print_periodic(void);

/* ── Inline time helpers ── */
static inline uint64_t time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline uint64_t elapsed_ns(uint64_t start_ns) {
    return time_now_ns() - start_ns;
}

#endif /* METRICS_H */

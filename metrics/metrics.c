#include "metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

/* ── Global instance ── */
ServerMetrics g_metrics;

/* ── qsort comparator for uint64_t ── */
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

void metrics_init(void) {
    memset(&g_metrics, 0, sizeof(g_metrics));
    pthread_mutex_init(&g_metrics.lock, NULL);
    clock_gettime(CLOCK_MONOTONIC, &g_metrics.server_start_time);
    for (int i = 0; i < OP_COUNT; i++)
        g_metrics.op_stats[i].min_ns = UINT64_MAX;
}

void metrics_client_connect(void) {
    pthread_mutex_lock(&g_metrics.lock);
    g_metrics.current_clients++;
    if (g_metrics.current_clients > g_metrics.peak_concurrent_clients)
        g_metrics.peak_concurrent_clients = g_metrics.current_clients;
    pthread_mutex_unlock(&g_metrics.lock);
}

void metrics_client_disconnect(void) {
    pthread_mutex_lock(&g_metrics.lock);
    if (g_metrics.current_clients > 0)
        g_metrics.current_clients--;
    pthread_mutex_unlock(&g_metrics.lock);
}

void metrics_record_request(int success) {
    pthread_mutex_lock(&g_metrics.lock);
    g_metrics.total_requests++;
    if (success) g_metrics.successful_requests++;
    else         g_metrics.failed_requests++;
    pthread_mutex_unlock(&g_metrics.lock);
}

void metrics_record_latency(OpType op, uint64_t latency_ns) {
    pthread_mutex_lock(&g_metrics.lock);
    OpStats *s = &g_metrics.op_stats[op];
    s->count++;
    s->total_ns += latency_ns;
    if (latency_ns < s->min_ns) s->min_ns = latency_ns;
    if (latency_ns > s->max_ns) s->max_ns = latency_ns;
    pthread_mutex_unlock(&g_metrics.lock);
}

void metrics_record_session_latency(uint64_t latency_ns) {
    pthread_mutex_lock(&g_metrics.lock);
    int idx = g_metrics.latency_write_idx % LATENCY_SAMPLES_MAX;
    g_metrics.latency_samples[idx] = latency_ns;
    g_metrics.latency_write_idx++;
    if (g_metrics.latency_count < LATENCY_SAMPLES_MAX)
        g_metrics.latency_count++;
    pthread_mutex_unlock(&g_metrics.lock);
}

static double uptime_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec  - g_metrics.server_start_time.tv_sec) +
           (now.tv_nsec - g_metrics.server_start_time.tv_nsec) * 1e-9;
}

void metrics_print_summary(void) {
    pthread_mutex_lock(&g_metrics.lock);

    double uptime_s = uptime_seconds();
    double rps = uptime_s > 0 ? g_metrics.total_requests / uptime_s : 0.0;

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║         SERVER PERFORMANCE METRICS SUMMARY               ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  Uptime              : %8.1f sec                      ║\n", uptime_s);
    printf("║  Total Requests      : %8lu                           ║\n", (unsigned long)g_metrics.total_requests);
    printf("║  Successful          : %8lu                           ║\n", (unsigned long)g_metrics.successful_requests);
    printf("║  Failed              : %8lu                           ║\n", (unsigned long)g_metrics.failed_requests);
    printf("║  Throughput          : %8.2f req/sec                  ║\n", rps);
    printf("║  Peak Concurrent     : %8d clients                   ║\n", g_metrics.peak_concurrent_clients);
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  OPERATION LATENCIES                                     ║\n");
    printf("║  %-14s  %7s  %8s  %8s  %8s ║\n",
           "Operation", "Count", "Avg(us)", "Min(us)", "Max(us)");
    printf("║  ─────────────────────────────────────────────────────── ║\n");

    for (int i = 0; i < OP_COUNT; i++) {
        OpStats *s = &g_metrics.op_stats[i];
        if (s->count == 0) continue;
        double avg_us = (s->total_ns / (double)s->count) / 1000.0;
        double min_us = (s->min_ns == UINT64_MAX ? 0.0 : s->min_ns) / 1000.0;
        double max_us = s->max_ns / 1000.0;
        printf("║  %-14s  %7lu  %8.1f  %8.1f  %8.1f ║\n",
               OP_NAMES[i], (unsigned long)s->count, avg_us, min_us, max_us);
    }

    /* p95 from session latency ring buffer */
    if (g_metrics.latency_count > 0) {
        uint64_t *sorted = malloc(g_metrics.latency_count * sizeof(uint64_t));
        if (sorted) {
            memcpy(sorted, g_metrics.latency_samples,
                   g_metrics.latency_count * sizeof(uint64_t));
            qsort(sorted, g_metrics.latency_count, sizeof(uint64_t), cmp_u64);
            int    p95_idx = (int)(g_metrics.latency_count * 0.95);
            uint64_t p50   = sorted[g_metrics.latency_count / 2];
            uint64_t p95   = sorted[p95_idx];
            uint64_t sum   = 0;
            for (int i = 0; i < g_metrics.latency_count; i++) sum += sorted[i];
            uint64_t avg   = sum / (uint64_t)g_metrics.latency_count;
            free(sorted);
            printf("╠══════════════════════════════════════════════════════════╣\n");
            printf("║  SESSION LATENCY  (N=%d samples)\n", g_metrics.latency_count);
            printf("║  Avg             : %8.2f ms                          ║\n", avg / 1e6);
            printf("║  P50             : %8.2f ms                          ║\n", p50 / 1e6);
            printf("║  P95             : %8.2f ms                          ║\n", p95 / 1e6);
        }
    }

    /* Resource usage */
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  RESOURCE USAGE                                          ║\n");
    printf("║  Max RSS (KB)        : %8ld                           ║\n", ru.ru_maxrss);
    printf("║  User CPU time (ms)  : %8ld                           ║\n",
           ru.ru_utime.tv_sec * 1000 + ru.ru_utime.tv_usec / 1000);
    printf("║  Sys  CPU time (ms)  : %8ld                           ║\n",
           ru.ru_stime.tv_sec * 1000 + ru.ru_stime.tv_usec / 1000);
    printf("║  Voluntary   ctxsw   : %8ld                           ║\n", ru.ru_nvcsw);
    printf("║  Involuntary ctxsw   : %8ld                           ║\n", ru.ru_nivcsw);
    printf("╚══════════════════════════════════════════════════════════╝\n");

    pthread_mutex_unlock(&g_metrics.lock);
}

void metrics_print_periodic(void) {
    pthread_mutex_lock(&g_metrics.lock);
    double uptime_s = uptime_seconds();
    double rps = uptime_s > 0 ? g_metrics.total_requests / uptime_s : 0.0;
    printf("[METRICS] uptime=%.0fs | reqs=%lu | rps=%.1f | "
           "clients=%d(peak=%d) | ok=%lu err=%lu\n",
           uptime_s,
           (unsigned long)g_metrics.total_requests, rps,
           g_metrics.current_clients, g_metrics.peak_concurrent_clients,
           (unsigned long)g_metrics.successful_requests,
           (unsigned long)g_metrics.failed_requests);
    fflush(stdout);
    pthread_mutex_unlock(&g_metrics.lock);
}

#include "lock_stats.h"
#include "metrics.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* Global lock stats instance */
LockStats g_lock_stats;

/* Per-file mutexes — compensate for Linux fcntl not blocking same-process threads */
pthread_mutex_t g_course_mutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_enroll_mutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_student_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Raw lock/unlock (fcntl) — no instrumentation ── */
static int raw_lock_file(int fd, int lock_type) {
    struct flock fl;
    fl.l_type   = (lock_type == 0) ? F_RDLCK : F_WRLCK;  /* 0 = READ_LOCK */
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;
    return fcntl(fd, F_SETLKW, &fl);
}

static int raw_unlock_file(int fd) {
    struct flock fl;
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;
    return fcntl(fd, F_SETLK, &fl);
}

void lock_stats_init(void) {
    memset(&g_lock_stats, 0, sizeof(g_lock_stats));
    pthread_mutex_init(&g_lock_stats.lock, NULL);
    pthread_mutex_init(&g_course_mutex,  NULL);
    pthread_mutex_init(&g_enroll_mutex,  NULL);
    pthread_mutex_init(&g_student_mutex, NULL);
}

int instrumented_lock_file(int fd, int lock_type) {
    uint64_t t0  = time_now_ns();
    int      ret = raw_lock_file(fd, lock_type);
    uint64_t wait_ns = time_now_ns() - t0;

    pthread_mutex_lock(&g_lock_stats.lock);
    if (lock_type == 0) {                          /* READ_LOCK */
        g_lock_stats.read_lock_count++;
        g_lock_stats.read_total_wait_ns += wait_ns;
        if (wait_ns > g_lock_stats.read_max_wait_ns)
            g_lock_stats.read_max_wait_ns = wait_ns;
    } else {                                       /* WRITE_LOCK */
        g_lock_stats.write_lock_count++;
        g_lock_stats.write_total_wait_ns += wait_ns;
        if (wait_ns > g_lock_stats.write_max_wait_ns)
            g_lock_stats.write_max_wait_ns = wait_ns;
    }
    if (wait_ns > LOCK_CONTENTION_THRESHOLD_NS)
        g_lock_stats.contention_count++;
    pthread_mutex_unlock(&g_lock_stats.lock);

    /* Also record in main metrics as a file-op latency sample */
    metrics_record_latency(OP_FILE_OP, wait_ns);

    return ret;
}

int instrumented_unlock_file(int fd) {
    return raw_unlock_file(fd);
}

void lock_stats_print(void) {
    pthread_mutex_lock(&g_lock_stats.lock);

    uint64_t rc = g_lock_stats.read_lock_count;
    uint64_t wc = g_lock_stats.write_lock_count;
    double avg_r = rc ? (g_lock_stats.read_total_wait_ns  / (double)rc) / 1000.0 : 0.0;
    double avg_w = wc ? (g_lock_stats.write_total_wait_ns / (double)wc) / 1000.0 : 0.0;

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║          LOCK CONTENTION ANALYSIS SUMMARY                ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  Read  Locks Acquired : %8lu                          ║\n", (unsigned long)rc);
    printf("║  Write Locks Acquired : %8lu                          ║\n", (unsigned long)wc);
    printf("║  Avg Read  Wait (us)  : %8.2f                         ║\n", avg_r);
    printf("║  Avg Write Wait (us)  : %8.2f                         ║\n", avg_w);
    printf("║  Max Read  Wait (us)  : %8.2f                         ║\n",
           g_lock_stats.read_max_wait_ns  / 1000.0);
    printf("║  Max Write Wait (us)  : %8.2f                         ║\n",
           g_lock_stats.write_max_wait_ns / 1000.0);
    printf("║  Contention Events    : %8lu  (wait > 1 ms)          ║\n",
           (unsigned long)g_lock_stats.contention_count);
    printf("╚══════════════════════════════════════════════════════════╝\n");

    pthread_mutex_unlock(&g_lock_stats.lock);
}

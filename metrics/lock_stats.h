#ifndef LOCK_STATS_H
#define LOCK_STATS_H

#include <stdint.h>
#include <pthread.h>

/* Wait > 1ms counts as a "contention event" */
#define LOCK_CONTENTION_THRESHOLD_NS 1000000ULL

typedef struct {
    uint64_t read_lock_count;
    uint64_t write_lock_count;
    uint64_t read_total_wait_ns;
    uint64_t write_total_wait_ns;
    uint64_t read_max_wait_ns;
    uint64_t write_max_wait_ns;
    uint64_t contention_count;   /* waits exceeding threshold */
    pthread_mutex_t lock;
} LockStats;

extern LockStats g_lock_stats;

/*
 * Per-file mutexes for intra-process thread safety.
 * Linux fcntl locks are per-process and do NOT block threads within the
 * same process from each other. These mutexes fill that gap.
 */
extern pthread_mutex_t g_course_mutex;    /* protects COURSE_FILE   */
extern pthread_mutex_t g_enroll_mutex;    /* protects ENROLLMENT_FILE */
extern pthread_mutex_t g_student_mutex;   /* protects STUDENT_FILE  */

void lock_stats_init(void);

int  instrumented_lock_file(int fd, int lock_type);
int  instrumented_unlock_file(int fd);

void lock_stats_print(void);

#endif /* LOCK_STATS_H */

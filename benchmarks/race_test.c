/*
 * race_test.c — Concurrency correctness validator for course enrollment.
 *
 * Spawns N threads that ALL simultaneously call enroll_student() for the
 * same course.  After all threads finish, validates:
 *   1. available_seats >= 0
 *   2. available_seats == max_seats - actual_enrollment_count
 *   3. No duplicate enrollment records exist
 *
 * Usage:
 *   ./race_test [--threads N] [--course-id CID] [--student-id-start SID]
 *
 * Default: 50 threads, course 101, student IDs starting at 9001
 *
 * Run AFTER ./data_seeder --seed
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "../utils/file_ops.h"
#include "../metrics/metrics.h"

/* ── Thread argument ── */
typedef struct {
    int thread_idx;
    int student_id;
    int course_id;
    int result;        /* SUCCESS / error code */
    uint64_t latency_ns;
} RaceArg;

/* ── Barrier so all threads hammer at the same instant ── */
static pthread_barrier_t g_start_barrier;

static void *race_worker(void *arg) {
    RaceArg *ra = (RaceArg *)arg;

    /* Wait until all threads are ready */
    pthread_barrier_wait(&g_start_barrier);

    uint64_t t0 = time_now_ns();
    ra->result  = enroll_student(ra->student_id, ra->course_id);
    ra->latency_ns = elapsed_ns(t0);

    return NULL;
}

/* Count actual enrollment records for a course by scanning the file directly.
 * This avoids the 100-record cap in get_course_enrollments(). */
static int count_course_enrollments(int course_id) {
    int fd = open(ENROLLMENT_FILE, O_RDONLY);
    if (fd < 0) return 0;
    Enrollment e;
    int count = 0;
    while (read(fd, &e, sizeof(Enrollment)) == (ssize_t)sizeof(Enrollment)) {
        if (e.course_id == course_id && e.student_id > 0) count++;
    }
    close(fd);
    return count;
}

/* ── Check for duplicate (student_id, course_id) pairs ── */
static int count_duplicates(int course_id, int student_id_start, int n) {
    int dups = 0;
    for (int i = 0; i < n; i++) {
        int sid = student_id_start + i;
        Enrollment enrollments[50];
        int cnt = 0;
        get_student_enrollments(sid, enrollments, &cnt);
        int course_count = 0;
        for (int j = 0; j < cnt; j++) {
            if (enrollments[j].course_id == course_id) course_count++;
        }
        if (course_count > 1) {
            dups++;
            fprintf(stderr, "[race_test] DUPLICATE: student %d enrolled %d times in course %d\n",
                    sid, course_count, course_id);
        }
    }
    return dups;
}

int main(int argc, char *argv[]) {
    int n_threads         = 50;
    int course_id         = 101;
    int student_id_start  = 9001;

    /* Parse CLI args */
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--threads")          == 0) n_threads        = atoi(argv[i+1]);
        else if (strcmp(argv[i], "--course-id")   == 0) course_id        = atoi(argv[i+1]);
        else if (strcmp(argv[i], "--student-id-start") == 0) student_id_start = atoi(argv[i+1]);
    }

    metrics_init();

    /* Make sure lock_stats_init is callable even in standalone mode */
    extern void lock_stats_init(void);
    lock_stats_init();

    init_data_files();

    /* Verify course exists */
    Course course;
    if (get_course_by_id(course_id, &course) != SUCCESS) {
        fprintf(stderr,
            "[race_test] ERROR: Course ID %d not found. Run ./data_seeder --seed first.\n",
            course_id);
        return 1;
    }

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║         CONCURRENT ENROLLMENT RACE CONDITION TEST        ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  Threads             : %-32d ║\n", n_threads);
    printf("║  Course ID           : %-32d ║\n", course_id);
    printf("║  Course Name         : %-32s ║\n", course.name);
    printf("║  Max Seats           : %-32d ║\n", course.max_seats);
    printf("║  Available Seats (pre): %-31d ║\n", course.available_seats);
    printf("╚══════════════════════════════════════════════════════════╝\n");

    /* Cap threads to available seats + a little extra for "rejected" paths */
    if (n_threads > 250) n_threads = 250;

    pthread_barrier_init(&g_start_barrier, NULL, n_threads);

    RaceArg *args   = calloc(n_threads, sizeof(RaceArg));
    pthread_t *tids = calloc(n_threads, sizeof(pthread_t));

    for (int i = 0; i < n_threads; i++) {
        args[i].thread_idx = i;
        args[i].student_id = student_id_start + i;
        args[i].course_id  = course_id;
        pthread_create(&tids[i], NULL, race_worker, &args[i]);
    }

    /* Collect results */
    int success_count  = 0;
    int full_count     = 0;
    int error_count    = 0;
    int dup_count      = 0;
    uint64_t total_ns  = 0;
    uint64_t min_ns    = UINT64_MAX;
    uint64_t max_ns    = 0;

    for (int i = 0; i < n_threads; i++) {
        pthread_join(tids[i], NULL);
        total_ns += args[i].latency_ns;
        if (args[i].latency_ns < min_ns) min_ns = args[i].latency_ns;
        if (args[i].latency_ns > max_ns) max_ns = args[i].latency_ns;

        switch (args[i].result) {
            case SUCCESS:         success_count++; break;
            case COURSE_FULL:     full_count++;    break;
            case ALREADY_ENROLLED: dup_count++;    break;
            default:              error_count++;   break;
        }
    }

    pthread_barrier_destroy(&g_start_barrier);

    /* ── Validation ── */
    Course after;
    get_course_by_id(course_id, &after);
    int actual_enrollments = count_course_enrollments(course_id);
    int duplicates         = count_duplicates(course_id, student_id_start, n_threads);

    int pass = 1;
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║              RACE TEST RESULTS                           ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  Threads run         : %-32d ║\n", n_threads);
    printf("║  Successful enrolls  : %-32d ║\n", success_count);
    printf("║  Rejected (full)     : %-32d ║\n", full_count);
    printf("║  Duplicate-blocked   : %-32d ║\n", dup_count);
    printf("║  Errors              : %-32d ║\n", error_count);
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  LATENCY (microseconds)                                  ║\n");
    printf("║  Avg               : %-34.1f ║\n",
           (total_ns / (double)n_threads) / 1000.0);
    printf("║  Min               : %-34.1f ║\n", min_ns / 1000.0);
    printf("║  Max               : %-34.1f ║\n", max_ns / 1000.0);
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  CONSISTENCY CHECKS                                      ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");

    /* Check 1: seats not negative */
    int c1 = after.available_seats >= 0;
    printf("║  [%s] Seats >= 0               (available=%d)\n",
           c1 ? "PASS" : "FAIL", after.available_seats);
    if (!c1) pass = 0;

    /* Check 2: seats not exceed max */
    int c2 = after.available_seats <= after.max_seats;
    printf("║  [%s] Seats <= max_seats        (max=%d)\n",
           c2 ? "PASS" : "FAIL", after.max_seats);
    if (!c2) pass = 0;

    /* Check 3: actual enrollments match seat math */
    int expected_seats = after.max_seats - actual_enrollments;
    int c3 = (after.available_seats == expected_seats);
    printf("║  [%s] available = max - actual  (%d == %d - %d)\n",
           c3 ? "PASS" : "FAIL",
           after.available_seats, after.max_seats, actual_enrollments);
    if (!c3) pass = 0;

    /* Check 4: no duplicate enrollments in file */
    int c4 = (duplicates == 0);
    printf("║  [%s] No duplicate enrollments  (found=%d)\n",
           c4 ? "PASS" : "FAIL", duplicates);
    if (!c4) pass = 0;

    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  OVERALL: %-46s ║\n", pass ? "ALL CHECKS PASSED ✓" : "SOME CHECKS FAILED ✗");
    printf("╚══════════════════════════════════════════════════════════╝\n\n");

    /* Print lock contention stats */
    extern void lock_stats_print(void);
    lock_stats_print();

    free(args);
    free(tids);
    return pass ? 0 : 1;
}

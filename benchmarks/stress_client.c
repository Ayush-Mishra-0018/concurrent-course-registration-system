/*
 * stress_client.c — Concurrent TCP stress tester for the course-registration server.
 *
 * Each worker thread opens its own TCP connection and runs a realistic
 * student workflow: login → view courses → enroll → drop → logout.
 *
 * Usage:
 *   ./stress_client [options]
 *
 *   -c, --clients  N    Number of concurrent client threads  (default: 50)
 *   -d, --duration N    Test duration in seconds             (default: 30)
 *   -H, --host ADDR     Server host                          (default: 127.0.0.1)
 *   -p, --port PORT     Server port                          (default: 8080)
 *   -s, --start-id SID  First student ID in test pool        (default: 9001)
 *   -C, --course-id CID Course ID to enroll/drop             (default: 101)
 *
 * Run AFTER:  ./data_seeder --seed  &&  ./server &
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>

#define DEFAULT_CLIENTS    50
#define DEFAULT_DURATION   30
#define DEFAULT_HOST       "127.0.0.1"
#define DEFAULT_PORT       8080
#define DEFAULT_START_ID   9001
#define DEFAULT_COURSE_ID  101
#define STUDENT_PASSWORD   "test1234"
#define RECV_TIMEOUT_S     5
#define BUF_SIZE           4096

/* ── Configuration (set once from main, read-only in threads) ── */
typedef struct {
    char    host[64];
    int     port;
    int     n_clients;
    int     duration_sec;
    int     student_id_start;
    int     course_id;
} Config;

/* ── Per-thread stats ── */
typedef struct {
    unsigned long sessions_completed;
    unsigned long sessions_failed;
    unsigned long ops_total;
    uint64_t      total_latency_ns;
    uint64_t      min_latency_ns;
    uint64_t      max_latency_ns;
} ThreadStats;

/* ── Per-thread argument (wraps stats + config) ── */
typedef struct {
    ThreadStats *stats;
    int          student_id;
    int          course_id;
} WorkerArg;

/* ── Shared ── */
static Config        g_cfg;
static volatile int  g_stop = 0;   /* set by main after duration expires */

/* ── Helpers ── */
static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static int tcp_connect(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    /* Set receive timeout */
    struct timeval tv = { RECV_TIMEOUT_S, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(g_cfg.port);
    inet_pton(AF_INET, g_cfg.host, &srv.sin_addr);

    if (connect(fd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/*
 * recv_until_prompt — read from socket until one of the sentinel strings
 * appears in the buffer.  Returns bytes read, or -1 on timeout/error.
 */
static int recv_until_prompt(int fd, char *buf, int bufsz,
                              const char **sentinels, int n_sentinels) {
    int total = 0;
    buf[0] = '\0';
    for (;;) {
        int r = recv(fd, buf + total, bufsz - total - 1, 0);
        if (r <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return -1; /* timeout */
            return -1;
        }
        total += r;
        buf[total] = '\0';

        for (int i = 0; i < n_sentinels; i++) {
            if (strstr(buf, sentinels[i])) return total;
        }
        if (total >= bufsz - 1) return total;
    }
}

static int send_str(int fd, const char *s) {
    int len = (int)strlen(s);
    return send(fd, s, len, 0) == len ? 0 : -1;
}

/*
 * run_one_session — perform a complete student workflow:
 *   connect → login → view → enroll → drop → logout
 * Returns 0 on success.
 */
static int run_one_session(int student_id, int course_id, uint64_t *latency_ns) {
    uint64_t t0 = now_ns();
    char buf[BUF_SIZE];
    char id_str[32], cid_str[32];
    snprintf(id_str,  sizeof(id_str),  "%d\n", student_id);
    snprintf(cid_str, sizeof(cid_str), "%d\n", course_id);

    int fd = tcp_connect();
    if (fd < 0) return -1;

    const char *login_prompts[] = { "Enter Your Choice" };
    const char *enter_prompts[] = { "Enter " };
    const char *menu_prompts[]  = { "Enter Your Choice:" };
    const char *exit_prompts[]  = { "EXIT" };

    /* 1. Receive welcome + user-type prompt */
    if (recv_until_prompt(fd, buf, BUF_SIZE, login_prompts, 1) < 0) goto fail;

    /* 2. Choose Student (type 3) */
    if (send_str(fd, "3\n") < 0) goto fail;

    /* 3. Enter Student ID */
    if (recv_until_prompt(fd, buf, BUF_SIZE, enter_prompts, 1) < 0) goto fail;
    if (send_str(fd, id_str) < 0) goto fail;

    /* 4. Enter Password */
    if (recv_until_prompt(fd, buf, BUF_SIZE, enter_prompts, 1) < 0) goto fail;
    if (send_str(fd, STUDENT_PASSWORD "\n") < 0) goto fail;

    /* 5. Receive menu (check for login failure) */
    if (recv_until_prompt(fd, buf, BUF_SIZE, menu_prompts, 1) < 0) goto fail;
    if (strstr(buf, "failed") || strstr(buf, "Failed")) goto fail;

    /* 6. View courses (choice 1) */
    if (send_str(fd, "1\n") < 0) goto fail;
    if (recv_until_prompt(fd, buf, BUF_SIZE, menu_prompts, 1) < 0) goto fail;

    /* 7. Enroll (choice 2) */
    if (send_str(fd, "2\n") < 0) goto fail;
    if (recv_until_prompt(fd, buf, BUF_SIZE, enter_prompts, 1) < 0) goto fail;
    if (send_str(fd, cid_str) < 0) goto fail;
    if (recv_until_prompt(fd, buf, BUF_SIZE, menu_prompts, 1) < 0) goto fail;

    /* 8. Drop (choice 3) */
    if (send_str(fd, "3\n") < 0) goto fail;
    if (recv_until_prompt(fd, buf, BUF_SIZE, enter_prompts, 1) < 0) goto fail;
    if (send_str(fd, cid_str) < 0) goto fail;
    if (recv_until_prompt(fd, buf, BUF_SIZE, menu_prompts, 1) < 0) goto fail;

    /* 9. Logout (choice 6) */
    if (send_str(fd, "6\n") < 0) goto fail;
    recv_until_prompt(fd, buf, BUF_SIZE, exit_prompts, 1);

    close(fd);
    *latency_ns = now_ns() - t0;
    return 0;

fail:
    close(fd);
    *latency_ns = now_ns() - t0;
    return -1;
}

/* ── Worker thread ── */
static void *worker_thread(void *arg) {
    WorkerArg   *wa    = (WorkerArg *)arg;
    ThreadStats *stats = wa->stats;
    int student_id     = wa->student_id;
    int course_id      = wa->course_id;

    stats->min_latency_ns = UINT64_MAX;

    while (!g_stop) {
        uint64_t lat = 0;
        int ret = run_one_session(student_id, course_id, &lat);
        if (ret == 0) {
            stats->sessions_completed++;
            stats->ops_total += 4; /* view + enroll + drop + logout */
        } else {
            stats->sessions_failed++;
        }
        stats->total_latency_ns += lat;
        if (lat < stats->min_latency_ns) stats->min_latency_ns = lat;
        if (lat > stats->max_latency_ns) stats->max_latency_ns = lat;
    }
    return NULL;
}

/* ── Aggregate and print results ── */
static void print_results(ThreadStats *all, int n, double elapsed_s) {
    unsigned long total_sessions = 0, total_failed = 0, total_ops = 0;
    uint64_t grand_lat = 0, min_lat = UINT64_MAX, max_lat = 0;

    for (int i = 0; i < n; i++) {
        total_sessions += all[i].sessions_completed;
        total_failed   += all[i].sessions_failed;
        total_ops      += all[i].ops_total;
        grand_lat      += all[i].total_latency_ns;
        if (all[i].min_latency_ns < min_lat) min_lat = all[i].min_latency_ns;
        if (all[i].max_latency_ns > max_lat) max_lat = all[i].max_latency_ns;
    }

    unsigned long total_all = total_sessions + total_failed;
    double avg_ms   = total_all ? (grand_lat / (double)total_all) / 1e6 : 0;
    double sess_rps = elapsed_s > 0 ? total_sessions / elapsed_s : 0;
    double ops_rps  = elapsed_s > 0 ? total_ops       / elapsed_s : 0;
    double err_pct  = total_all ? (total_failed * 100.0) / total_all : 0;

    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║           STRESS TEST RESULTS                            ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  Concurrent Clients  : %-32d ║\n", n);
    printf("║  Test Duration       : %-28.1f sec ║\n", elapsed_s);
    printf("║  Sessions Completed  : %-32lu ║\n", total_sessions);
    printf("║  Sessions Failed     : %-32lu ║\n", total_failed);
    printf("║  Error Rate          : %-31.2f%% ║\n", err_pct);
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  THROUGHPUT                                              ║\n");
    printf("║  Sessions / sec      : %-32.2f ║\n", sess_rps);
    printf("║  Operations / sec    : %-32.2f ║\n", ops_rps);
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  SESSION LATENCY (ms)                                    ║\n");
    printf("║  Average             : %-32.2f ║\n", avg_ms);
    printf("║  Min                 : %-32.2f ║\n", min_lat / 1e6);
    printf("║  Max                 : %-32.2f ║\n", max_lat / 1e6);
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  RESOURCE USAGE                                          ║\n");
    printf("║  Max RSS (KB)        : %-32ld ║\n", ru.ru_maxrss);
    printf("║  Voluntary   ctxsw   : %-32ld ║\n", ru.ru_nvcsw);
    printf("║  Involuntary ctxsw   : %-32ld ║\n", ru.ru_nivcsw);
    printf("╚══════════════════════════════════════════════════════════╝\n\n");
}

int main(int argc, char *argv[]) {
    /* Defaults */
    g_cfg.n_clients       = DEFAULT_CLIENTS;
    g_cfg.duration_sec    = DEFAULT_DURATION;
    g_cfg.port            = DEFAULT_PORT;
    g_cfg.student_id_start = DEFAULT_START_ID;
    g_cfg.course_id       = DEFAULT_COURSE_ID;
    strncpy(g_cfg.host, DEFAULT_HOST, sizeof(g_cfg.host) - 1);

    /* Parse CLI */
    for (int i = 1; i < argc - 1; i++) {
        if      (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--clients"))        g_cfg.n_clients        = atoi(argv[i+1]);
        else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--duration"))       g_cfg.duration_sec     = atoi(argv[i+1]);
        else if (!strcmp(argv[i], "-H") || !strcmp(argv[i], "--host"))           strncpy(g_cfg.host, argv[i+1], sizeof(g_cfg.host)-1);
        else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--port"))           g_cfg.port             = atoi(argv[i+1]);
        else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--start-id"))       g_cfg.student_id_start = atoi(argv[i+1]);
        else if (!strcmp(argv[i], "-C") || !strcmp(argv[i], "--course-id"))      g_cfg.course_id        = atoi(argv[i+1]);
    }

    signal(SIGPIPE, SIG_IGN);  /* Ignore broken pipe from closed server connections */

    int n = g_cfg.n_clients;
    ThreadStats *stats = calloc(n, sizeof(ThreadStats));
    WorkerArg   *wargs = calloc(n, sizeof(WorkerArg));
    pthread_t   *tids  = calloc(n, sizeof(pthread_t));

    printf("\n[stress_client] Starting %d concurrent clients for %d seconds...\n",
           n, g_cfg.duration_sec);
    printf("[stress_client] Target: %s:%d | Student pool: %d–%d | Course: %d\n\n",
           g_cfg.host, g_cfg.port,
           g_cfg.student_id_start,
           g_cfg.student_id_start + n - 1,
           g_cfg.course_id);

    uint64_t t_start = now_ns();

    /* Launch threads — each thread i uses student_id_start + i */
    for (int i = 0; i < n; i++) {
        wargs[i].stats      = &stats[i];
        wargs[i].student_id = g_cfg.student_id_start + i;
        wargs[i].course_id  = g_cfg.course_id;
        pthread_create(&tids[i], NULL, worker_thread, &wargs[i]);
    }

    /* Run for duration, then signal stop */
    sleep(g_cfg.duration_sec);
    g_stop = 1;

    for (int i = 0; i < n; i++) pthread_join(tids[i], NULL);

    double elapsed = (now_ns() - t_start) / 1e9;
    print_results(stats, n, elapsed);

    free(stats);
    free(wargs);
    free(tids);
    return 0;
}

#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define LOG_ROTATE_BYTES  (10L * 1024L * 1024L)   /* rotate at 10 MB */
#define TS_BUF_SIZE       32

static FILE           *g_log_fp   = NULL;
static char            g_log_path[256];
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── ISO-8601 timestamp into buf (must be TS_BUF_SIZE bytes) ── */
static void iso_timestamp(char *buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);
    int ms = (int)(ts.tv_nsec / 1000000);
    strftime(buf, TS_BUF_SIZE - 6, "%Y-%m-%dT%H:%M:%S", &tm_info);
    /* append milliseconds */
    char ms_buf[8];
    snprintf(ms_buf, sizeof(ms_buf), ".%03d", ms);
    strncat(buf, ms_buf, TS_BUF_SIZE - strlen(buf) - 1);
}

/* ── Rotate log if it exceeds LOG_ROTATE_BYTES ── */
static void maybe_rotate(void) {
    if (!g_log_fp) return;
    long pos = ftell(g_log_fp);
    if (pos < LOG_ROTATE_BYTES) return;

    fclose(g_log_fp);

    char old_path[280];
    snprintf(old_path, sizeof(old_path), "%s.1", g_log_path);
    rename(g_log_path, old_path);

    g_log_fp = fopen(g_log_path, "a");
}

void logger_init(const char *log_path) {
    pthread_mutex_lock(&g_log_mutex);
    strncpy(g_log_path, log_path, sizeof(g_log_path) - 1);

    /* Ensure logs/ directory exists */
    char dir[270];
    strncpy(dir, log_path, sizeof(dir) - 1);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }

    g_log_fp = fopen(log_path, "a");
    if (!g_log_fp)
        fprintf(stderr, "[logger] WARNING: could not open %s\n", log_path);
    pthread_mutex_unlock(&g_log_mutex);
}

void logger_close(void) {
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_fp) { fclose(g_log_fp); g_log_fp = NULL; }
    pthread_mutex_unlock(&g_log_mutex);
}

void log_event(unsigned long thread_id,
               int           client_id,
               const char   *operation,
               const char   *status,
               double        latency_ms,
               const char   *detail) {
    char ts[TS_BUF_SIZE];
    iso_timestamp(ts);

    pthread_mutex_lock(&g_log_mutex);

    maybe_rotate();

    FILE *fp = g_log_fp ? g_log_fp : stderr;

    if (detail && detail[0]) {
        fprintf(fp,
            "{\"ts\":\"%s\",\"tid\":\"%lu\",\"cid\":%d,"
            "\"op\":\"%s\",\"status\":\"%s\","
            "\"latency_ms\":%.3f,\"detail\":\"%s\"}\n",
            ts, thread_id, client_id,
            operation, status, latency_ms, detail);
    } else {
        fprintf(fp,
            "{\"ts\":\"%s\",\"tid\":\"%lu\",\"cid\":%d,"
            "\"op\":\"%s\",\"status\":\"%s\","
            "\"latency_ms\":%.3f}\n",
            ts, thread_id, client_id,
            operation, status, latency_ms);
    }
    fflush(fp);

    pthread_mutex_unlock(&g_log_mutex);
}

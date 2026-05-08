#ifndef LOGGER_H
#define LOGGER_H

#include <pthread.h>
#include <stdint.h>

/* Operation name constants */
#define LOG_OP_LOGIN         "LOGIN"
#define LOG_OP_LOGOUT        "LOGOUT"
#define LOG_OP_ENROLL        "ENROLL"
#define LOG_OP_DROP          "DROP"
#define LOG_OP_VIEW_COURSES  "VIEW_COURSES"
#define LOG_OP_ADD_COURSE    "ADD_COURSE"
#define LOG_OP_AUTH_FAIL     "AUTH_FAIL"
#define LOG_OP_LOCK_CONTENTION "LOCK_CONTENTION"
#define LOG_OP_SERVER_STATS  "SERVER_STATS"

/* Status strings */
#define LOG_STATUS_OK        "OK"
#define LOG_STATUS_FAIL      "FAIL"
#define LOG_STATUS_DENY      "DENIED"

/*
 * log_event — write one JSON-line entry to logs/server_structured.log
 *
 *   thread_id  : pthread_self() cast to unsigned long
 *   client_id  : arbitrary client identifier (socket fd or sequential ID)
 *   operation  : one of LOG_OP_* constants
 *   status     : one of LOG_STATUS_* constants
 *   latency_ms : operation latency in milliseconds (0 if not applicable)
 *   detail     : optional free-form detail string (may be NULL)
 */
void log_event(unsigned long thread_id,
               int           client_id,
               const char   *operation,
               const char   *status,
               double        latency_ms,
               const char   *detail);

void logger_init(const char *log_path);
void logger_close(void);

#endif /* LOGGER_H */

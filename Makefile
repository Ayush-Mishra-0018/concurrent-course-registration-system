CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -O2
LDFLAGS = -pthread

# ── Source lists ─────────────────────────────────────────────────────────────

SERVER_SRCS = server.c \
              common_utils.c \
              utils/file_ops_core.c \
              utils/file_ops_course.c \
              utils/file_ops_enrollment.c \
              utils/file_ops_student.c \
              controllers/admin_controller_core.c \
              controllers/admin_controller_ops.c \
              controllers/faculty_controller_core.c \
              controllers/faculty_controller_ops.c \
              controllers/student_controller_core.c \
              controllers/student_controller_ops.c \
              metrics/metrics.c \
              metrics/lock_stats.c \
              logging/logger.c

CLIENT_SRCS = client.c \
              common_utils.c

# Shared file_ops + metrics + lock_stats for benchmark tools
FILEOPS_SRCS = utils/file_ops_core.c \
               utils/file_ops_course.c \
               utils/file_ops_enrollment.c \
               utils/file_ops_student.c \
               metrics/metrics.c \
               metrics/lock_stats.c

STRESS_SRCS  = benchmarks/stress_client.c
RACE_SRCS    = benchmarks/race_test.c   $(FILEOPS_SRCS)
SEEDER_SRCS  = benchmarks/data_seeder.c $(FILEOPS_SRCS)

# ── Object lists ──────────────────────────────────────────────────────────────

SERVER_OBJS  = $(SERVER_SRCS:.c=.o)
CLIENT_OBJS  = $(CLIENT_SRCS:.c=.o)
RACE_OBJS    = $(RACE_SRCS:.c=.o)
SEEDER_OBJS  = $(SEEDER_SRCS:.c=.o)

# ── Primary targets ───────────────────────────────────────────────────────────

all: dirs server client

benchmark-tools: dirs stress_client race_test data_seeder

dirs:
	mkdir -p data logs

# ── Server & client ───────────────────────────────────────────────────────────

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── Benchmark binaries ────────────────────────────────────────────────────────

# stress_client is standalone TCP — does NOT link file_ops
stress_client: $(STRESS_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# race_test links file_ops directly (no server needed)
race_test: $(RACE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# data_seeder links file_ops directly
data_seeder: $(SEEDER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── Convenience targets ───────────────────────────────────────────────────────

bench-run: benchmark-tools
	@echo "[bench] Seeding test data..."
	./data_seeder --seed
	@echo "[bench] Starting server in background..."
	./server &
	@sleep 1
	@echo "[bench] Running stress tests..."
	bash benchmarks/benchmark.sh
	@echo "[bench] Done. Check logs/ for structured logs."

# ── Generic rule ─────────────────────────────────────────────────────────────

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ── Housekeeping ──────────────────────────────────────────────────────────────

clean:
	rm -f server client stress_client race_test data_seeder
	rm -f $(SERVER_OBJS) $(CLIENT_OBJS) $(RACE_OBJS) $(SEEDER_OBJS)
	rm -rf data logs

debug: CFLAGS += -g -fsanitize=thread
debug: clean all

.PHONY: all benchmark-tools dirs server client \
        stress_client race_test data_seeder \
        bench-run clean debug
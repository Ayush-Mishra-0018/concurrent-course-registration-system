#!/usr/bin/env bash
# =============================================================================
# benchmark.sh — Full concurrent stress benchmark for the course-reg server
#
# Usage:
#   ./benchmarks/benchmark.sh [--quick]
#
# What it does:
#   1. Verifies the server is running (or starts it)
#   2. Seeds test data if not already seeded
#   3. Runs stress_client with 10, 50, 100, 250 concurrent clients
#   4. Saves each result to benchmark_results/
#   5. Prints a formatted summary table
#
# Prerequisites:
#   make all benchmark-tools
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
RESULTS_DIR="$ROOT_DIR/benchmark_results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

# Default concurrency levels; use --quick for a faster run
QUICK_MODE=0
if [[ "${1:-}" == "--quick" ]]; then QUICK_MODE=1; fi

if [[ "$QUICK_MODE" -eq 1 ]]; then
    CLIENT_LEVELS=(10 50)
    DURATION=10
else
    CLIENT_LEVELS=(10 50 100 250)
    DURATION=30
fi

HOST="127.0.0.1"
PORT=8080
STUDENT_START=9001
COURSE_ID=101

# ── Colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'; BLU='\033[1;34m'; NC='\033[0m'

# ── Helpers ───────────────────────────────────────────────────────────────────
check_binary() {
    local bin="$ROOT_DIR/$1"
    if [[ ! -x "$bin" ]]; then
        echo -e "${RED}[ERROR]${NC} $bin not found. Run: make all benchmark-tools"
        exit 1
    fi
}

server_running() {
    nc -z "$HOST" "$PORT" 2>/dev/null
}

# ── Pre-flight ────────────────────────────────────────────────────────────────
echo -e "${BLU}╔══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLU}║     Academia Course Registration — Benchmark Suite       ║${NC}"
echo -e "${BLU}╚══════════════════════════════════════════════════════════╝${NC}"
echo ""

check_binary "stress_client"
check_binary "data_seeder"
check_binary "server"

mkdir -p "$RESULTS_DIR"

# ── Seed data ─────────────────────────────────────────────────────────────────
echo -e "${YLW}[1/4] Seeding test data...${NC}"
cd "$ROOT_DIR"
./data_seeder --seed
echo ""

# ── Ensure server is running ──────────────────────────────────────────────────
STARTED_SERVER=0
if ! server_running; then
    echo -e "${YLW}[2/4] Starting server...${NC}"
    ./server > logs/server_bench.log 2>&1 &
    SERVER_PID=$!
    sleep 2
    STARTED_SERVER=1
    if ! server_running; then
        echo -e "${RED}[ERROR]${NC} Server failed to start. Check logs/server_bench.log"
        exit 1
    fi
    echo -e "${GRN}       Server started (PID $SERVER_PID)${NC}"
else
    echo -e "${GRN}[2/4] Server already running${NC}"
fi
echo ""

# ── Run stress tests ──────────────────────────────────────────────────────────
echo -e "${YLW}[3/4] Running stress tests...${NC}"
echo ""

SUMMARY_FILE="$RESULTS_DIR/summary_$TIMESTAMP.txt"
cat > "$SUMMARY_FILE" << EOF
Academia Course Registration System — Benchmark Summary
Generated: $(date)
Duration per run: ${DURATION}s
Host: $HOST:$PORT
EOF

echo "| Clients | Sessions | Sess/sec | Ops/sec | Avg Lat(ms) | Errors |" >> "$SUMMARY_FILE"
echo "|---------|----------|----------|---------|-------------|--------|" >> "$SUMMARY_FILE"

for N in "${CLIENT_LEVELS[@]}"; do
    echo -e "  ${BLU}→ Testing with ${N} concurrent clients (${DURATION}s)...${NC}"
    OUT_FILE="$RESULTS_DIR/run_${N}clients_$TIMESTAMP.txt"

    ./stress_client \
        --clients "$N" \
        --duration "$DURATION" \
        --host "$HOST" \
        --port "$PORT" \
        --start-id "$STUDENT_START" \
        --course-id "$COURSE_ID" \
        | tee "$OUT_FILE"

    # Extract key numbers for summary table
    SESSIONS=$(grep "Sessions Completed" "$OUT_FILE" | grep -oP '\d+' | tail -1 || echo "?")
    SESS_RPS=$(grep "Sessions / sec"    "$OUT_FILE" | grep -oP '[\d.]+' | head -1 || echo "?")
    OPS_RPS=$(grep "Operations / sec"  "$OUT_FILE" | grep -oP '[\d.]+' | head -1 || echo "?")
    AVG_LAT=$(grep "Average"           "$OUT_FILE" | grep -oP '[\d.]+' | head -1 || echo "?")
    ERRORS=$(grep "Sessions Failed"    "$OUT_FILE" | grep -oP '\d+' | tail -1 || echo "?")

    printf "| %-7d | %-8s | %-8s | %-7s | %-11s | %-6s |\n" \
        "$N" "$SESSIONS" "$SESS_RPS" "$OPS_RPS" "$AVG_LAT" "$ERRORS" \
        >> "$SUMMARY_FILE"

    echo ""
    sleep 2
done

# ── Print summary ─────────────────────────────────────────────────────────────
echo -e "${YLW}[4/4] Benchmark Summary${NC}"
echo ""
cat "$SUMMARY_FILE"
echo ""
echo -e "${GRN}Results saved to: $RESULTS_DIR/${NC}"
echo -e "${GRN}Structured logs:  logs/server_structured.log${NC}"

# ── Stop server if we started it ─────────────────────────────────────────────
if [[ "$STARTED_SERVER" -eq 1 ]]; then
    echo ""
    echo -e "${YLW}Stopping server (PID $SERVER_PID)...${NC}"
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
fi

echo -e "${GRN}Done!${NC}"

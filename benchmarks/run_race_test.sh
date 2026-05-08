#!/usr/bin/env bash
# =============================================================================
# run_race_test.sh — Isolated concurrency correctness validator
#
# Usage:
#   ./benchmarks/run_race_test.sh [--threads N] [--course-id CID]
#
# Verifies that simultaneous enrollments:
#   - Never produce negative seat counts
#   - Never exceed max_seats
#   - Never create duplicate enrollment records
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

THREADS=50
COURSE_ID=101
STUDENT_START=9001

for ((i=1; i<$#; i++)); do
    j=$((i+1))
    case "${!i}" in
        --threads)   THREADS=${!j}    ;;
        --course-id) COURSE_ID=${!j}  ;;
        --start-id)  STUDENT_START=${!j} ;;
    esac
done

RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'; BLU='\033[1;34m'; NC='\033[0m'

echo -e "${BLU}╔══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLU}║      Race Condition & Consistency Validation Test        ║${NC}"
echo -e "${BLU}╚══════════════════════════════════════════════════════════╝${NC}"
echo ""

cd "$ROOT_DIR"

# Check binary
if [[ ! -x "./race_test" ]]; then
    echo -e "${YLW}Building race_test...${NC}"
    make race_test
fi

if [[ ! -x "./data_seeder" ]]; then
    echo -e "${YLW}Building data_seeder...${NC}"
    make data_seeder
fi

# Seed
echo -e "${YLW}[1/3] Seeding test data...${NC}"
./data_seeder --seed
echo ""

# Run race test
echo -e "${YLW}[2/3] Running race test ($THREADS threads → course $COURSE_ID)...${NC}"
echo ""

if ./race_test \
       --threads "$THREADS" \
       --course-id "$COURSE_ID" \
       --student-id-start "$STUDENT_START"; then
    echo ""
    echo -e "${GRN}[3/3] PASSED: Concurrency correctness validated ✓${NC}"
    EXIT_CODE=0
else
    echo ""
    echo -e "${RED}[3/3] FAILED: Consistency violations detected ✗${NC}"
    EXIT_CODE=1
fi

echo ""
echo "To clean up seeded data: ./data_seeder --cleanup"
exit $EXIT_CODE

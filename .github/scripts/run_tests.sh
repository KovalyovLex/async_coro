#!/usr/bin/env bash
# Test runner - write PID to file for cleanup job to signal
# The cleanup job will monitor this PID and send signals when needed

set +e  # Don't exit on errors

BUILD_DIR="${1:-.}"
PID_FILE="$BUILD_DIR/test_process.pid"
LOG_FILE="$BUILD_DIR/test_output.log"

rm -f "$LOG_FILE"

echo "[$(date +'%Y-%m-%d %H:%M:%S')] Starting test runner (PID: $$)..."
echo "[$(date +'%Y-%m-%d %H:%M:%S')] Writing runner PID to $PID_FILE"
echo "[$(date +'%Y-%m-%d %H:%M:%S')] Logging output to $LOG_FILE"

# Write the runner PID so external cleanup can signal this script
echo "$$" > "$PID_FILE"

# Maintainable list of commands to run sequentially (each element is a full command string)
TEST_COMMANDS=(
    "./tests/tests_simple --gtest_repeat=30"
    "./tests/tests_long"
)

STOP=0
CHILD_PID=0

on_signal() {
    STOP=1
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Received signal, stopping..." | tee -a "$LOG_FILE" >&2
    if [ "$CHILD_PID" -ne 0 ]; then
        echo "[$(date +'%Y-%m-%d %H:%M:%S')] Killing child PID $CHILD_PID" | tee -a "$LOG_FILE" >&2
        kill -TERM -- "${CHILD_PID}" 2>/dev/null || true
    fi
}

trap on_signal SIGINT SIGTERM

FINAL_EXIT=0

for cmd in "${TEST_COMMANDS[@]}"; do
    if [ "$STOP" -ne 0 ]; then
        echo "[$(date +'%Y-%m-%d %H:%M:%S')] Stop requested, exiting loop." | tee -a "$LOG_FILE" >&2
        break
    fi

    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Starting: $cmd" | tee -a "$LOG_FILE" >&2

    # Run the command in background and capture its PID. Use bash -c to allow full command strings.
    bash -c "$cmd" >> "$LOG_FILE" 2>&1 &
    CHILD_PID=$!

    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Spawned PID: $CHILD_PID" | tee -a "$LOG_FILE" >&2

    wait "$CHILD_PID"
    EXIT_CODE=$?

    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Command '$cmd' exited with code: $EXIT_CODE" | tee -a "$LOG_FILE" >&2

    # If child was terminated by signal or non-zero exit, record and possibly stop
    if [ "$EXIT_CODE" -ne 0 ]; then
        FINAL_EXIT=$EXIT_CODE
    fi

    CHILD_PID=0
done

echo "" >&2
echo "[$(date +'%Y-%m-%d %H:%M:%S')] === TEST OUTPUT LOG ===" >&2
if [ -f "$LOG_FILE" ]; then
        cat "$LOG_FILE" >&2
else
        echo "[WARNING] Log file not found: $LOG_FILE" >&2
fi

rm -f "$PID_FILE"

exit "$FINAL_EXIT"

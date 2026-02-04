#!/usr/bin/env bash
# Test runner - write PID to file for cleanup job to signal
# The cleanup job will monitor this PID and send signals when needed

set +e  # Don't exit on errors

BUILD_DIR="${1:-.}"
PID_FILE="$BUILD_DIR/test_process.pid"
LOG_FILE="$BUILD_DIR/test_output.log"

echo "[$(date +'%Y-%m-%d %H:%M:%S')] Starting tests in foreground (PID: $$)..."
echo "[$(date +'%Y-%m-%d %H:%M:%S')] Writing PID to $PID_FILE"
echo "[$(date +'%Y-%m-%d %H:%M:%S')] Logging output to $LOG_FILE"

# Run async_coro_tests with output redirected to LOG_FILE
# Signals will be delivered to the process group
./async_coro_tests --gtest_repeat=30 >> "$LOG_FILE" 2>&1 &
TEST_PID=$!
echo "$TEST_PID" > "$PID_FILE"

echo "[$(date +'%Y-%m-%d %H:%M:%S')] Test process PID: $TEST_PID (written to $PID_FILE)"

# Wait for the test process
wait "$TEST_PID"
EXIT_CODE=$?

echo "[$(date +'%Y-%m-%d %H:%M:%S')] tests exited with code: $EXIT_CODE" | tee -a "$LOG_FILE" >&2

# Echo the log file at the end
echo "" >&2
echo "[$(date +'%Y-%m-%d %H:%M:%S')] === TEST OUTPUT LOG ===" >&2
if [ -f "$LOG_FILE" ]; then
    cat "$LOG_FILE" >&2
else
    echo "[WARNING] Log file not found: $LOG_FILE" >&2
fi

rm -f "$PID_FILE"
exit "$EXIT_CODE"

#!/usr/bin/env bash
# Cleanup script - sends SIGINT to test process when job is cancelled
# Monitors the PID file and sends signals to the running test process

BUILD_DIR="${1:-.}"
PID_FILE="$BUILD_DIR/test_process.pid"
LOG_FILE="$BUILD_DIR/test_output.log"
MAX_WAIT=5  # Maximum seconds to wait for PID file

echo "[$(date +'%Y-%m-%d %H:%M:%S')] Cleanup job started" >&2
echo "[$(date +'%Y-%m-%d %H:%M:%S')] Looking for PID file: $PID_FILE" >&2

# Wait for PID file to be created
WAIT_TIME=0
while [ ! -f "$PID_FILE" ] && [ $WAIT_TIME -lt $MAX_WAIT ]; do
    sleep 1
    WAIT_TIME=$((WAIT_TIME + 1))
done

if [ ! -f "$PID_FILE" ]; then
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] [ERROR] PID file not found after $MAX_WAIT seconds" >&2
    exit 1
fi

TEST_PID=$(cat "$PID_FILE")
echo "[$(date +'%Y-%m-%d %H:%M:%S')] Found test PID: $TEST_PID" >&2

# Verify process is running
if ! kill -0 "$TEST_PID" 2>/dev/null; then
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] [ERROR] Process $TEST_PID is not running" >&2
    
    # Echo logs anyway
    if [ -f "$LOG_FILE" ]; then
        echo "" >&2
        echo "[$(date +'%Y-%m-%d %H:%M:%S')] === TEST OUTPUT LOG ===" >&2
        cat "$LOG_FILE" >&2
        rm -f "$LOG_FILE"
    fi
    exit 1
fi

echo "[$(date +'%Y-%m-%d %H:%M:%S')] Process $TEST_PID is running" >&2

# Try SIGINT first
echo "[$(date +'%Y-%m-%d %H:%M:%S')] Sending SIGINT to process $TEST_PID..." >&2
kill -INT "$TEST_PID" 2>/dev/null
KILL_RESULT=$?
echo "[$(date +'%Y-%m-%d %H:%M:%S')] kill -INT result: $KILL_RESULT" >&2

# Wait a moment for graceful shutdown
sleep 2

# If still running, try SIGTERM
if kill -0 "$TEST_PID" 2>/dev/null; then
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Process still running, sending SIGTERM..." >&2
    kill -TERM "$TEST_PID" 2>/dev/null
    KILL_RESULT=$?
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] kill -TERM result: $KILL_RESULT" >&2
    sleep 2
fi

# Final check
if kill -0 "$TEST_PID" 2>/dev/null; then
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Process still running, force killing with SIGKILL..." >&2
    kill -9 "$TEST_PID" 2>/dev/null
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Sent SIGKILL" >&2
    sleep 1
fi

# Check final status
if kill -0 "$TEST_PID" 2>/dev/null; then
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] [ERROR] Process $TEST_PID still running after all attempts" >&2
else
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Process $TEST_PID successfully terminated" >&2
fi

# Print any remaining logs
echo "" >&2
echo "[$(date +'%Y-%m-%d %H:%M:%S')] === FINAL TEST OUTPUT LOG ===" >&2
if [ -f "$LOG_FILE" ]; then
    cat "$LOG_FILE" >&2
    rm -f "$LOG_FILE"
else
    echo "[WARNING] Log file not found: $LOG_FILE" >&2
fi

exit 0

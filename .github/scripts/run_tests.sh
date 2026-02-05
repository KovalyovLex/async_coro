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
    "./tests/tests_simple --gtest_repeat=500 --gtest_filter=*.multiple_workers*"
    "./tests/tests_simple --gtest_repeat=30"
    "./tests/tests_long"
)

STOP=0
CHILD_PID=0

on_signal() {
    STOP=1
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Received signal, stopping..."
    if [ "$CHILD_PID" -ne 0 ]; then
        echo "[$(date +'%Y-%m-%d %H:%M:%S')] Killing child PID $CHILD_PID"
        kill -TERM -- "${CHILD_PID}" 2>/dev/null || true
    fi
}

trap on_signal SIGINT SIGTERM

FINAL_EXIT=0

for cmd in "${TEST_COMMANDS[@]}"; do
    if [ "$STOP" -ne 0 ]; then
        echo "[$(date +'%Y-%m-%d %H:%M:%S')] Stop requested, exiting loop."
        break
    fi

    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Starting: $cmd" | tee -a "$LOG_FILE"

    # Run the command and capture output via a FIFO so that
    # - output is visible live on the console
    # - output is fully captured to the log (including messages produced while handling signals)
    FIFO="$BUILD_DIR/test_pipe.$$"
    rm -f "$FIFO"
    mkfifo "$FIFO"

    # Start tee to read from FIFO and append to the log while also writing to stdout
    tee -a "$LOG_FILE" < "$FIFO" &
    TEE_PID=$!

    # Start the command, redirecting both stdout and stderr into the FIFO
    bash -c "$cmd" > "$FIFO" 2>&1 &
    CHILD_PID=$!

    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Spawned PID: $CHILD_PID (tee PID: $TEE_PID)"

    wait "$CHILD_PID"
    EXIT_CODE=$?

    # Wait for tee to drain remaining output and exit
    wait "$TEE_PID" 2>/dev/null || true

    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Command '$cmd' exited with code: $EXIT_CODE" | tee -a "$LOG_FILE" >&2

    # If stop wasn't requested remove log file as it was printed to output
    if [ "$STOP" -eq 0 ]; then
        rm -f "$LOG_FILE"
    fi

    rm -f "$FIFO"

    # If child was terminated by signal or non-zero exit, record and possibly stop
    if [ "$EXIT_CODE" -ne 0 ]; then
        FINAL_EXIT=$EXIT_CODE
    fi

    CHILD_PID=0
done

rm -f "$PID_FILE"

exit "$FINAL_EXIT"

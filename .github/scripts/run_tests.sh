#!/usr/bin/env bash
# Signal-aware test runner script for GitHub Actions
# This script properly catches SIGTERM and forwards it to the test process

set +e  # Disable immediate exit on error so we can handle signals

CTEST_PID=""

# Set up signal handlers
handle_sigterm() {
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] SIGTERM received by runner script, forwarding to ctest (PID=$CTEST_PID)..." >&2
    if [ -n "$CTEST_PID" ]; then
        kill -TERM "$CTEST_PID" 2>/dev/null
        # Wait for graceful shutdown with timeout
        for i in {1..10}; do
            if ! kill -0 "$CTEST_PID" 2>/dev/null; then
                echo "[$(date +'%Y-%m-%d %H:%M:%S')] ctest process terminated gracefully" >&2
                break
            fi
            sleep 0.1
        done
        # Force kill if still running
        if kill -0 "$CTEST_PID" 2>/dev/null; then
            echo "[$(date +'%Y-%m-%d %H:%M:%S')] Force killing ctest process..." >&2
            kill -9 "$CTEST_PID" 2>/dev/null
        fi
    fi
    exit 143
}

trap handle_sigterm TERM

# Run ctest and capture its PID
echo "[$(date +'%Y-%m-%d %H:%M:%S')] Starting ctest..." >&2
ctest --output-on-failure &
CTEST_PID=$!

echo "[$(date +'%Y-%m-%d %H:%M:%S')] ctest PID: $CTEST_PID, waiting for completion..." >&2

# Wait for ctest to finish
wait "$CTEST_PID"
EXIT_CODE=$?

echo "[$(date +'%Y-%m-%d %H:%M:%S')] ctest exited with code: $EXIT_CODE" >&2
exit "$EXIT_CODE"

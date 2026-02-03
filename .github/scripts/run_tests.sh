#!/usr/bin/env bash
# Test runner with output redirection to files
# Output is written to files so it's preserved even if job is cancelled

set +e  # Don't exit on failures

LOG_DIR="${1:-.}"
STDOUT_LOG="$LOG_DIR/ctest-stdout.log"
STDERR_LOG="$LOG_DIR/ctest-stderr.log"

echo "[$(date +'%Y-%m-%d %H:%M:%S')] Starting ctest, redirecting output to files..." >&2
echo "STDOUT: $STDOUT_LOG" >&2
echo "STDERR: $STDERR_LOG" >&2

# Run ctest with output redirected to files
# stdout and stderr both go to files so they're preserved after cancellation
ctest --output-on-failure >"$STDOUT_LOG" 2>"$STDERR_LOG"
EXIT_CODE=$?

# Also print to console for live visibility during test runs
if [ -f "$STDOUT_LOG" ]; then
    tail -100 "$STDOUT_LOG"
fi

echo "[$(date +'%Y-%m-%d %H:%M:%S')] ctest exited with code: $EXIT_CODE" >&2
if [ -f "$STDERR_LOG" ]; then
    echo "=== STDERR output ===" >&2
    tail -50 "$STDERR_LOG" >&2
fi

exit "$EXIT_CODE"

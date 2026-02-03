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
# Both stdout and stderr are combined to stderr log to ensure we capture signal handler output
ctest --output-on-failure >"$STDOUT_LOG" 2>"$STDERR_LOG"
EXIT_CODE=$?

# Flush file buffers to disk
sync "$STDOUT_LOG" "$STDERR_LOG" 2>/dev/null || true

# Print to console for live visibility (last part only)
if [ -f "$STDOUT_LOG" ]; then
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] === Last 50 lines of STDOUT ===" >&2
    tail -50 "$STDOUT_LOG" >&2
fi

echo "[$(date +'%Y-%m-%d %H:%M:%S')] ctest exited with code: $EXIT_CODE" >&2

if [ -f "$STDERR_LOG" ]; then
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] === STDERR output ===" >&2
    tail -50 "$STDERR_LOG" >&2
fi

exit "$EXIT_CODE"

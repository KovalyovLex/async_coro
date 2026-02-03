#!/usr/bin/env bash
# Simple test runner - rely on GitHub Actions always() step for cleanup
# This script runs ctest in the foreground so output is visible in real-time

set +e  # Don't exit on failures

echo "[$(date +'%Y-%m-%d %H:%M:%S')] Starting ctest (PID: $$)..." >&2

# Run ctest directly in foreground (not background)
# This ensures all output is visible and process hierarchy is clear
ctest --output-on-failure
EXIT_CODE=$?

echo "[$(date +'%Y-%m-%d %H:%M:%S')] ctest exited with code: $EXIT_CODE" >&2
exit "$EXIT_CODE"

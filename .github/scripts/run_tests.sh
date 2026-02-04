#!/usr/bin/env bash
# Simple test runner - run tests directly in foreground
# Signals will propagate naturally to child processes (ctest or async_coro_tests)

set +e  # Don't exit on errors

echo "[$(date +'%Y-%m-%d %H:%M:%S')] Starting tests in foreground (PID: $$)..." >&2

# Run async_coro_tests directly - no backgrounding, no redirection tricks
# When GitHub sends SIGTERM, it will be delivered to the process group
./async_coro_tests --gtest_repeat=30

EXIT_CODE=$?
echo "[$(date +'%Y-%m-%d %H:%M:%S')] tests exited with code: $EXIT_CODE" >&2
exit "$EXIT_CODE"

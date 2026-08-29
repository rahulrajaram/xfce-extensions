#!/usr/bin/env bash
# run_all.sh — run every carousel behavior suite under Xvfb.
#
# Each suite spins up its own Xvfb + private dbus session, so suites
# must not run concurrently. Exit 0 iff every suite passes.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fail=0

for t in strip-smoke plasma-smoke; do
  echo "===== $t ====="
  if bash "$ROOT/tests/$t.sh" >/dev/null 2>&1; then
    echo "$t: PASS"
  else
    echo "$t: FAIL"
    fail=1
  fi
done

if [ "$fail" -eq 0 ]; then
  echo "ALL SUITES PASS"
else
  echo "SOME SUITES FAILED"
fi
exit "$fail"
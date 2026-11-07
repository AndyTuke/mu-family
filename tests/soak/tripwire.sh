#!/bin/bash
# Every 5 minutes: if any tracked file differs from the baseline taken at the start, record it and stop every night job.
cd /d/Dev/mu
git status --porcelain --untracked-files=no | sort > /tmp/night/baseline.txt
while [ ! -f /tmp/night/STOP ]; do
  sleep 300
  git status --porcelain --untracked-files=no | sort > /tmp/night/now.txt
  if ! diff -q /tmp/night/baseline.txt /tmp/night/now.txt >/dev/null; then
    { date; echo "TRACKED FILES CHANGED:"; diff /tmp/night/baseline.txt /tmp/night/now.txt; } > /tmp/night/ALERT.txt
    touch /tmp/night/STOP
    powershell -NoProfile -Command "Get-Process | Where-Object { \$_.Name -match '^mu-' } | Stop-Process -Force"
  fi
done

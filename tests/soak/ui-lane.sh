#!/bin/bash
# Lane 2: the in-process mu-Tronic UI soak (mu-tronic-uisoak), a fresh seed each run, until the deadline (new runs stop at 07:10).
cd /d/Dev/mu
END=$(date -d "07:10" +%s); [ $END -lt $(date +%s) ] && END=$(date -d "tomorrow 07:10" +%s)
SECS=${UI_SECS:-60}
EXE=${UI_EXE:-build/mu-tronic/mu-tronic-uisoak_artefacts/Debug/Standalone/mu-tronic-uisoak.exe}; TAG=${UI_TAG:-ui}
while [ $(date +%s) -lt $END ] && [ ! -f /tmp/night/STOP ]; do
  seed=$((RANDOM * 32768 + RANDOM))
  log=/tmp/night/$TAG-$seed.log; crash=/tmp/night/$TAG-$seed.crash.txt
  start=$(date +%s)
  MU_UISOAK_CRASH=$(cygpath -w $crash) timeout $((SECS + 120)) $EXE $SECS $seed > $log 2>&1
  code=$?
  asserts=$(grep -c "Assertion failure" $log)
  echo "$(date +%H:%M:%S) $TAG seed=$seed secs=$(( $(date +%s) - start )) exit=$code asserts=${asserts:-0} $(tail -1 $log | grep -o 'actions=[0-9]*')" >> /tmp/night/$TAG-results.txt
  if [ "$code" = "0" ] && [ "${asserts:-0}" = "0" ]; then rm -f $log $crash; else cp $log /tmp/night/KEEP-$TAG-$seed.log; [ -f $crash ] && cp $crash /tmp/night/KEEP-$TAG-$seed.crash.txt; fi
done
echo "$(date +%H:%M:%S) ui lane finished" >> /tmp/night/$TAG-results.txt

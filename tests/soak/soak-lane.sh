#!/bin/bash
# Lane 1: headless processor soaks, every engine, a fresh random seed each time, until the deadline (new jobs stop at 07:10).
cd /d/Dev/mu
END=$(date -d "07:10" +%s); [ $END -lt $(date +%s) ] && END=$(date -d "tomorrow 07:10" +%s)
SECS=${SOAK_SECS:-120}
while [ $(date +%s) -lt $END ] && [ ! -f /tmp/night/STOP ]; do
  for t in mu-clid mu-on mu-toni mu-tant mu-tilate mu-tronic; do
    [ $(date +%s) -ge $END ] && break; [ -f /tmp/night/STOP ] && break
    seed=$((RANDOM * 32768 + RANDOM))
    log=/tmp/night/soak-$t-$seed.log
    start=$(date +%s)
    MU_SOAK_SECONDS=$SECS MU_SOAK_SEED=$seed timeout $((SECS + 300)) build/$t/$t-tests_artefacts/Debug/$t-tests.exe $t-soak > $log 2>&1
    code=$?
    fails=$(grep -E "^=== .*failure" $log | sed 's/.*: \([0-9]*\) failure.*/\1/')
    asserts=$(grep -c "JUCE Assertion failure" $log)
    echo "$(date +%H:%M:%S) $t seed=$seed secs=$(( $(date +%s) - start )) exit=$code failures=${fails:-none} asserts=${asserts:-0}" >> /tmp/night/soak-results.txt
    if [ "$code" = "0" ] && [ "${asserts:-0}" = "0" ]; then rm -f $log; else cp $log /tmp/night/KEEP-$t-$seed.log; fi
  done
done
echo "$(date +%H:%M:%S) soak lane finished" >> /tmp/night/soak-results.txt

#!/bin/bash
# One-screen summary of the night's lanes (read-only).
N=/tmp/night
echo "time: $(date +%H:%M)   STOP=$([ -f $N/STOP ] && echo YES || echo no)   ALERT=$([ -f $N/ALERT.txt ] && echo YES || echo no)"
for f in soak asan-soak ui asan asanlong asan2 asan3; do
  r=$N/$f-results.txt; [ -f $r ] || continue
  t=$(grep -c "exit=" $r); bad=$(grep "exit=" $r | grep -vc "exit=0 failures=\(0\|none\) asserts=0\|exit=0 asserts=0")
  echo "$f: $t runs, $bad flagged"
done
ls $N | grep KEEP | head -20
tail -1 $N/audio-sweep.log

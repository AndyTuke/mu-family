#!/bin/bash
# Every product test suite under AddressSanitizer, one after another (a memory error aborts that suite with its report).
export PATH="$PATH:/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64"
export ASAN_OPTIONS="halt_on_error=1:detect_leaks=0:symbolize=1:print_stacktrace=1:windows_hook_rtl_allocators=true"
for t in mu-tilate mu-tronic mu-on mu-clid mu-toni mu-tant; do
  [ -f /tmp/night/STOP ] && break
  log=/tmp/night/asanfull-$t.log
  timeout 2400 /d/Dev/build-asan/$t/$t-tests_artefacts/Debug/$t-tests.exe > $log 2>&1
  echo "$(date +%H:%M) $t exit=$? $(grep -c 'ERROR: AddressSanitizer' $log) asan-errors; $(grep -E '^=== ' $log | tail -1)" >> /tmp/night/asanfull-results.txt
done
echo "$(date +%H:%M) asan full suites finished" >> /tmp/night/asanfull-results.txt

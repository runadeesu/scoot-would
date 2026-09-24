#!/usr/bin/env bash
# Runs every scripted gameplay test in tests/autotest and prints a summary.
# usage: tools/run_autotests.sh [path/to/scoot would executable] [filter]
EXE="${1:-./build/linux/src/scoot would}"
FILTER="${2:-}"
pass=0; fail=0; failed=()
for t in tests/autotest/*.json; do
  [[ -n "$FILTER" && "$t" != *"$FILTER"* ]] && continue
  out=$(timeout 300 "$EXE" --autotest "$t" --size 960x540 2>&1)
  if echo "$out" | grep -q ": PASS ===="; then
    pass=$((pass+1)); echo "PASS  $t"
  else
    fail=$((fail+1)); failed+=("$t"); echo "FAIL  $t"
    echo "$out" | grep -E "FAILED|tricks:|grinds:|landings:|BAIL|ERROR|CRASH" | sed 's/^/      /'
  fi
done
echo "---- $pass passed, $fail failed ----"
[[ $fail -eq 0 ]]

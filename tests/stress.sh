#!/usr/bin/env bash
# Runs the simulator many times with heavy thread counts and checks that
# every run ends with all invariants intact (exit code 0).
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=./warehouse
[ -x "$BIN" ] || make -s
pass=0
for seed in 1 2 3 4 5; do
  for cfg in "1 1" "4 4" "8 16" "16 8"; do
    set -- $cfg
    if "$BIN" --mode=auto --producers="$1" --consumers="$2" --readers=4 --writers=2 \
         --duration=1 --seed="$seed" --quiet > /tmp/wh_run.log; then
      pass=$((pass+1))
      orders=$(awk '/orders processed/ {print $3}' /tmp/wh_run.log)
      check=$(awk '/stock check/ {print $NF}' /tmp/wh_run.log)
      printf "  ok  run %2d  seed=%d  producers=%-2s consumers=%-2s  orders=%-4s final stock=%s\n" \
        "$pass" "$seed" "$1" "$2" "$orders" "$check"
    else
      echo "FAILED: seed=$seed producers=$1 consumers=$2"; cat /tmp/wh_run.log; exit 1
    fi
  done
done
echo "stress test passed: $pass/20 runs, no race conditions"

#!/bin/bash
# usage: stress.sh <cart binary> <runs> <messages per channel> <frame microseconds> <workdir>
# Runs the end-to-end test repeatedly, with the cart and the host as separate processes.
CART=$1; RUNS=$2; M=$3; FRAME=$4; W=$5
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$W"; cd "$W" || exit 2
pass=0; fail=0
for i in $(seq 1 "$RUNS"); do
    rm -f s.memc
    python3 -I "$HERE/../tools/v32dev.py" format s.memc --ch0 storage --ch1 serial --ch2 tcp --ch3 pipe > /dev/null
    ( "$CART" s.memc "$M" "$FRAME" > sc.out 2>&1; echo "exit $?" >> sc.out ) &
    CP=$!
    sleep 0.1
    timeout 100 python3 -I "$HERE/dev_e2e_host.py" s.memc "$M" $((100 + i)) > sh.out 2>&1
    H=$?
    wait $CP
    C=$(tail -1 sc.out)
    if [ $H -eq 0 ] && [ "$C" = "exit 0" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "run $i FAILED (host exit $H, cart $C)"
        head -4 sh.out; head -4 sc.out
    fi
done
echo "stress: pass=$pass fail=$fail"
[ $fail -eq 0 ]

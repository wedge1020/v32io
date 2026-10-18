#!/bin/sh
# The three adapter firmwares (RP2350-USB-A, -USB-C, -USB-CM) share most of
# their sources as plain copies. This lists the files that have drifted
# apart, so a fix made in one is not forgotten in the others.
#
# Differences that are meant to exist (see "DIFFERENCES WITH RP2350-USB-A" in
# the READMEs of -USB-C and -USB-CM) are listed in EXPECTED below.
cd "$(dirname "$0")/../firmware" || exit 1

EXPECTED="main.c tusb_config.h led.c"
status=0

for board in RP2350-USB-C RP2350-USB-CM; do
    for f in RP2350-USB-A/src/*; do
        name=$(basename "$f")
        case " $EXPECTED " in *" $name "*) continue ;; esac
        if ! cmp -s "$f" "$board/src/$name"; then
            echo "$board/src/$name differs from RP2350-USB-A/src/$name"
            status=1
        fi
    done
done

[ $status = 0 ] && echo "firmware sources in sync (not counting: $EXPECTED)"
exit $status

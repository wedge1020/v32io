#!/bin/sh
# Checks that the version stamps agree, for both components: the VERSION file,
# the v32io library header (V32IO_VERSION and V32IO_VERSION_NUMBER) and the
# manual pages of v32io and v32dev (the version in each .TH line, which says
# "v32io <version>" or "v32dev <version>" according to the manual the page is
# in). `make version` makes them agree.
#
#     tools/check-version.sh
cd "$(dirname "$0")/.." || exit 2
status=0

version=$(cat VERSION 2>/dev/null)
case "$version" in
    [0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-*) ;;
    *) echo "VERSION '$version' is not YYYYMMDD-dev or YYYYMMDD-release"; exit 1 ;;
esac
number=${version%%-*}

header=$(sed -n 's/^#define V32IO_VERSION[[:space:]]*"\(.*\)".*/\1/p' lib/v32io/v32io.h)
hnumber=$(sed -n 's/^#define V32IO_VERSION_NUMBER[[:space:]]*\([0-9]*\).*/\1/p' lib/v32io/v32io.h)
[ "$header" = "$version" ] || { echo "lib/v32io/v32io.h: V32IO_VERSION is '$header', VERSION says '$version'"; status=1; }
[ "$hnumber" = "$number" ] || { echo "lib/v32io/v32io.h: V32IO_VERSION_NUMBER is '$hnumber', VERSION says '$number'"; status=1; }

for component in v32io v32dev; do
    for f in man/$component/man3/*.3v32; do
        head -n 1 "$f" | grep -q '^\.so ' && continue
        stamp=$(sed -n "1s/^\.TH [^ ]* [^ ]* \"[^\"]*\" \"$component \([^\"]*\)\".*/\1/p" "$f")
        [ "$stamp" = "$version" ] || { echo "$f: .TH says '$stamp', VERSION says '$version'"; status=1; }
    done
done

[ $status -eq 0 ] && echo "version $version: VERSION, lib/v32io/v32io.h and the manual pages of v32io and v32dev agree"
exit $status

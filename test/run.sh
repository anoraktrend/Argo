#!/bin/sh
set -e
CC="${CC:-cc}"
PASS=0 FAIL=0

# --- Selftest: internal consistency check via -DSELFTEST ---
printf "[selftest] "
$CC -Wall -Wextra -pedantic -std=c99 -O2 -march=native \
   -D_POSIX_C_SOURCE=200809L -DSELFTEST -o /tmp/cc_selftest compress.c 2>/dev/null
if /tmp/cc_selftest test/input/compress.c -o /tmp/cc_selftest_out.c 2>/dev/null; then
    echo "OK"; PASS=$((PASS+1))
else
    echo "FAIL"; FAIL=$((FAIL+1))
fi

# --- Round-trip tests ---
mkdir -p test/work
for f in test/input/*; do
    name=$(basename "$f")
    printf "  %-25s " "$name"
    orig=$(wc -c < "$f")

    ./ccompress "$f" -o "test/work/$name.c" 2>/dev/null

    if ! $CC -o "test/work/${name}_extract" "test/work/$name.c" 2>/dev/null; then
        echo "FAIL (compile)"; FAIL=$((FAIL+1)); continue
    fi

    mkdir -p "test/work/test/input"
    (cd test/work && ./"${name}_extract" > /dev/null 2>&1)

    if [ -f "test/work/test/input/$name" ] && diff -q "$f" "test/work/test/input/$name" >/dev/null 2>&1; then
        extracted=$(wc -c < "test/work/test/input/$name")
        printf "OK  %d -> %d\n" "$orig" "$extracted"
        PASS=$((PASS+1))
    else
        echo "FAIL (extract/diff)"; FAIL=$((FAIL+1))
    fi
done

rm -rf test/work
echo "---"
echo "Passed: $PASS  Failed: $FAIL"
test $FAIL -eq 0

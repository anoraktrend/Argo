#!/bin/sh
set -e
CC="${CC:-cc}"
PASS=0 FAIL=0

echo "=== Multi-file archive test ==="

# Test 1: Multiple text files
printf "[multi: 3 text files] "
rm -rf test/work_multi
mkdir -p test/work_multi
./ccompress test/input/multi1.txt test/input/multi2.txt test/input/multi3.bin -o test/work_multi/multi.c
$CC -o test/work_multi/extract test/work_multi/multi.c
(cd test/work_multi && ./extract > /dev/null 2>&1)
if diff -q test/input/multi1.txt test/work_multi/test/input/multi1.txt && \
   diff -q test/input/multi2.txt test/work_multi/test/input/multi2.txt && \
   diff -q test/input/multi3.bin test/work_multi/test/input/multi3.bin; then
    echo "OK"
    PASS=$((PASS+1))
else
    echo "FAIL"
    FAIL=$((FAIL+1))
fi

# Test 2: Two large binary files
printf "[multi: 2 large binary] "
rm -rf test/work_multi2
mkdir -p test/work_multi2
./ccompress test/input/long_run.bin test/input/alt_runs.bin -o test/work_multi2/multi.c
$CC -o test/work_multi2/extract test/work_multi2/multi.c
(cd test/work_multi2 && ./extract > /dev/null 2>&1)
if diff -q test/input/long_run.bin test/work_multi2/test/input/long_run.bin && \
   diff -q test/input/alt_runs.bin test/work_multi2/test/input/alt_runs.bin; then
    echo "OK"
    PASS=$((PASS+1))
else
    echo "FAIL"
    FAIL=$((FAIL+1))
fi

# Test 3: Mixed text and binary
printf "[multi: mixed text/binary] "
rm -rf test/work_multi3
mkdir -p test/work_multi3
./ccompress test/input/mixed.txt test/input/random.bin test/input/README.md -o test/work_multi3/multi.c
$CC -o test/work_multi3/extract test/work_multi3/multi.c
(cd test/work_multi3 && ./extract > /dev/null 2>&1)
if diff -q test/input/mixed.txt test/work_multi3/test/input/mixed.txt && \
   diff -q test/input/random.bin test/work_multi3/test/input/random.bin && \
   diff -q test/input/README.md test/work_multi3/test/input/README.md; then
    echo "OK"
    PASS=$((PASS+1))
else
    echo "FAIL"
    FAIL=$((FAIL+1))
fi

echo "---"
echo "Multi-file tests: Passed: $PASS  Failed: $FAIL"
test $FAIL -eq 0

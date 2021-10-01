#!/bin/bash
# Unit test for main_zlib example.

source googletest.sh || exit 1

[[ -n "$COVERAGE" ]] && exit 0

BIN=$TEST_SRCDIR/com_google_sandboxed_api/sandboxed_api/examples/zlib/main_zlib
TESTDATA="$TEST_SRCDIR/com_google_sandboxed_api/sandboxed_api/examples/zlib/testdata"

echo "aaaa" | "$BIN" || die 'FAILED: it should have exited with 0'

capture_test_stdout
echo "This is a test string" | "$BIN"
diff_test_stdout "$TESTDATA/simple.out"

capture_test_stdout
cat "$TESTDATA/zlib_main" | "$BIN"
diff_test_stdout "$TESTDATA/complex.out"

echo 'PASS'

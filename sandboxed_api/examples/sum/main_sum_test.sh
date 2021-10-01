#!/bin/bash
# Unit test for main_sum example.

source googletest.sh || exit 1

[[ -n "$COVERAGE" ]] && exit 0

BIN=$TEST_SRCDIR/com_google_sandboxed_api/sandboxed_api/examples/sum/main_sum

"$BIN" || die 'FAILED: it should have exited with 0'

echo 'PASS'


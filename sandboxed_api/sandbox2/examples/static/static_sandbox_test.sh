#!/bin/bash
# Unit test for static_sandbox example.

source googletest.sh || exit 1

[[ -n "$COVERAGE" ]] && exit 0

BIN=$TEST_SRCDIR/com_google_sandboxed_api/sandboxed_api/sandbox2/examples/static/static_sandbox

"$BIN" || die 'FAILED: it should have exited with 0'

echo 'PASS'

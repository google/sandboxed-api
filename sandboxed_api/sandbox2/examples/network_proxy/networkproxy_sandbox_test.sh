#!/bin/bash

source googletest.sh || exit 1

# Find input files
BINDIR=$TEST_SRCDIR/com_google_sandboxed_api/sandboxed_api/sandbox2
EXE=$BINDIR/examples/network_proxy/networkproxy_sandbox

# test it
ls "${EXE}" || exit 2

"${EXE}" --connect_with_handler || die 'TEST1 FAILED: it should have exited with 0'
"${EXE}" --noconnect_with_handler || \
    die 'TEST2 FAILED: it should have exited with 0'

exit 0

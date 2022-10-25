#!/bin/bash
#
# Copyright 2021 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Unit test for sandbox2tool example.

die() {
  echo "$1" 1>&2
  exit 1
}

BIN=$TEST_SRCDIR/com_google_sandboxed_api/sandboxed_api/sandbox2/examples/tool/sandbox2tool

out=$("$BIN" \
      --sandbox2tool_resolve_and_add_libraries \
      --sandbox2tool_walltime_timeout=1 \
      -- /bin/sleep 60 2>&1)
result=$?
if [[ $result -ne 2 ]]; then
  echo "$out" >&2
  die 'sleep 60 should hit walltime 1 and return 2 (sandbox violation)'
fi
if [[ "$out" != *"Process TIMEOUT"* ]]; then
  echo "$out" >&2
  die 'sleep 60 should hit walltime 1 and timeout'
fi

out=$("$BIN" -sandbox2tool_resolve_and_add_libraries -sandbox2tool_pause_kill -- /bin/sleep 5 2>&1)
result=$?
if [[ $result -ne 2 ]]; then
  echo "$out" >&2
  die 'pausing and then killing the command should return 2 (sandbox violation)'
fi
if [[ "$out" != *"Process terminated with a SIGNAL"* ]]; then
  echo "$out" >&2
  die 'pausing and killing sleep command should be terminated with SIGKILL'
fi

out=$("$BIN" \
      --sandbox2tool_resolve_and_add_libraries \
      --sandbox2tool_additional_bind_mounts '/etc,/proc' \
      --sandbox2tool_mount_tmp \
      -- /bin/cat /proc/1/cmdline 2>&1)
result=$?
if [[ $result -ne 0 ]]; then
  echo "$out" >&2
  die 'reading /proc/1/cmdline should not fail'
fi

out=$("$BIN" \
      --sandbox2tool_resolve_and_add_libraries \
      --sandbox2tool_additional_bind_mounts '/etc,/proc' \
      --sandbox2tool_mount_tmp \
      -- /bin/ls /proc/1/fd/ 2>&1)
result=$?
if [[ $result -ne 0 ]]; then
  echo "$out" >&2
  die 'listing /proc/1/fd  should work'
fi

out=$("$BIN" \
      --sandbox2tool_resolve_and_add_libraries \
      --sandbox2tool_additional_bind_mounts '/etc' \
      -- /bin/ls /tmp 2>&1)
result=$?
if [[ $result -ne 1 ]]; then
  echo "$out" >&2
  die "ls /tmp should return 1 (child error) but was $result"
fi

out=$("$BIN" \
      --sandbox2tool_resolve_and_add_libraries \
      --sandbox2tool_additional_bind_mounts '/tmp' \
      -- /bin/sh -c 'echo "test" > /tmp/sb2tool_test_file' 2>&1)
result=$?
if [[ $result -ne 1 ]]; then
  echo "$out" >&2
  die "it shouldn't be possible to write to a ro-mapping. Result was: $result"
fi

SB2_TMP_DIR="$TEST_TMPDIR/sb2tool_test_dir"
mkdir "$SB2_TMP_DIR" || die "couldn't create tmp directory"

out=$("$BIN" \
      --sandbox2tool_resolve_and_add_libraries \
      --sandbox2tool_additional_bind_mounts "$SB2_TMP_DIR" \
      -sandbox2tool_mount_tmp \
      -- /bin/sh -c "cd $SB2_TMP_DIR" 2>&1)
result=$?
if [[ $result -ne 0 ]]; then
  echo "$out" >&2
  die "Nested mounts under tmpfs should work. Result was: $result"
fi


echo 'hello world' > "$SB2_TMP_DIR/hello"
out=$("$BIN" \
      --sandbox2tool_resolve_and_add_libraries \
      --sandbox2tool_additional_bind_mounts "/etc,$SB2_TMP_DIR/hello=>/etc/passwd" \
      -sandbox2tool_mount_tmp \
      -- /bin/grep "hello world" /etc/passwd)
result=$?
if [[ $result -ne 0 ]]; then
  echo "$out" >&2
  die "Nested mounts should work. Result was: $result"
fi

echo 'PASS'

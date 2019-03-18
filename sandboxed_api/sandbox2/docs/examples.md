# Examples

## Overview

We have prepared a few examples to demonstrate how to use sandbox2 depending on
your situation and how to write policies.

You can find them in [//sandboxed_api/sandbox2/examples](../examples), read on
for detailed explanations.

## CRC4

The CRC4 example is an intentionally buggy calculation of a CRC4 checksum, it
demonstrates how to sandbox another program and how to communicate with it.

* [crc4bin.cc](../examples/crc4/crc4bin.cc): is the program we want to sandbox
  (the *sandboxee*)
* [crc4sandbox.cc](../examples/crc4/crc4sandbox.cc): is the sandbox program that
  will run it (the *executor*).

How it works:
1. The *executor* starts the *sandboxee* from its file path using
   `::sandbox2::GetDataDependencyFilePath()`.
2. The *executor* sends input to the *sandboxee* over the communication channel
   `Comms` using `SendBytes()`.
3. The *sandboxee* calculates the CRC4 and sends its replies back to the
   *executor* over the communication channel `Comms` which receives it with
   `RecvUint32()`.

If the program makes any other syscall other than communicating (`read()` and
`write()`), it is killed for policy violation.


## static

The static example demonstrates how to sandbox a statically linked binary, such
as a third-party binary for which you do not have the source, so is not aware
that it will be sandboxed.

* [static_bin.cc](../examples/static/static_bin.cc): the *sandboxee* is a
  static C binary that converts ASCII text from standard input to uppercase.
* [static_sandbox.cc](../examples/static/static_sandbox.cc): the *executor*
  with its policy, limits and using a file descriptor for *sandboxee* input.

How it works:

1. The *executor* starts the *sandboxee* from its file path using
   `GetDataDependencyFilepath`, just like for **CRC4**.
2. It sets up limits, opens a file descriptor on `/proc/version` and marks it
   to be mapped in the *sandboxee* with `MapFd`.
3. The policy allows some syscalls (`open`) to return an error (`ENOENT`),
   rather than being killed for policy violation. This can be useful when
   sandboxing a third party program where we cannot modify which syscalls are
   made, but we can make them fail gracefully.

## tool

The tool example is both a tool to develop your own policies and experiment with
**sandbox2** APIs as well a demonstration of its features.

* [sandbox2tool.cc](..examples/tool/sandbox2tool.cc): the *executor*
  demonstrating
  * how to run another binary sandboxed,
  * how to set up filesystem checks, and
  * how the *executor* can run the *sandboxee* asynchronously to read its
    output progressively

Try it yourself:

```bash
bazel run //sandboxed_api/sandbox2/examples/tool:sandbox2tool -- \
  /bin/cat /etc/hostname
```

Flags:

* `--sandbox2tool_keep_env` to keep current environment variables
* `--sandbox2tool_redirect_fd1` to receive the *sandboxee* STDOUT_FILENO (1)
  and output it locally
* `--sandbox2tool_cpu_timeout` to set CPU timeout in seconds
* `--sandbox2tool_walltime_timeout` to set wall-time timeout in seconds
* `--sandbox2tool_file_size_creation_limit` to set the maximum size of created
  files
* `--sandbox2tool_cwd` to set sandbox current working directory

## custom_fork

The custom_fork example demonstrates how to create a sandbox, which will
initialize the binary, and then wait for `fork()` requests coming from the
parent executor.

This mode offers potentially increased performance with regard to other types of
sandboxing, as here, creating new instances of sandboxees doesn't require
executing new binaries, just fork()-ing the existing ones

* [custom_fork_bin.cc](../examples/custom_fork): is the custom fork-server,
  receiving requests to `fork()` (via `Client::WaitAndFork`) in order to spawn
  new sandboxees
* [custom_fork_sandbox.cc](../examples/custom_fork/custom_fork_sandbox.cc): is
  the executor, which starts a custom fork server. Then it sends requests to it
  (via new executors) to spawn (via `fork()`) new sandboxees.

## network

Enabling the network namespace prevents the sandboxed process from connecting to
the outside world. This example demonstrates how to deal with this problem.

Namespaces are enabled when either
`::sandbox2::PolicyBuilder::EnableNamespaces()` is called, or some other
function that enables namespaces like `AddFile()`. To deal with this problem,
we can initialize a connection inside the executor and pass the socket file
descriptor via `::sandbox2::Comms::SendFD()`. The sandboxee receives the socket
by using `::sandbox2::Comms::RecvFD()` and then it can use this socket to
exchange the data as usual.

* [network_bin.cc](examples/network/network_bin.cc): is the program we want to
  sandbox (the sandboxee).
* [network_sandbox.cc](examples/network/network_sandbox.cc): is the sandbox
  program that will run it (the executor).

# How it works

## Overview

The sandbox technology is organized around 2 processes:

* An **executor** sets up and runs the *monitor*:
  * Also known as *parent*, *supervisor* or *monitor*
  * By itself is not sandboxed
  * Is regular C++ code using the Sandbox2 API

* The **sandboxee**, a child program running in the sandboxed environment:
  * Also known as *child* or *sandboxed process*
  * Receives its policy from the executor and applies it
  * Can come in different shapes:
    * Another binary, like in the [crc4](../examples/crc4/crc4sandbox.cc) and
      [static](../examples/static/static_sandbox.cc) examples
    * A third party binary for which you do not have the source

Purpose/goal:

* Restrict the sandboxee to a set of allowed syscalls and their arguments
* The tighter the policy, the better

Example:

A really tight policy could deny all except reads and writes on standard
input and output file descriptors. Inside this sandbox, a program could take
input, process it, and send the output back.
* The processing is not allowed to make any other syscall, or else it is killed
  for policy violation.
* If the processing is compromised (code execution by a malicious user), it
  cannot do anything bad other than producing bad output (that the executor and
  others still need to handle correctly).


## Sandbox Policies

The sandbox relies on **seccomp-bpf** provided by the Linux kernel. **seccomp**
is a Linux kernel facility for sandboxing and **BPF** is a way to write syscall
filters (the very same used for network filters). Read more about
[seccomp-bpf on Wikipedia](https://en.wikipedia.org/wiki/Seccomp#seccomp-bpf).

In practice, you will generate your policy using our
[PolicyBuilder class](../policybuilder.h). If you need more complex rules, you
can specify raw BPF macros, like in the [crc4](../examples/crc4/crc4sandbox.cc)
example.

Filesystem accesses are restricted with the help of Linux
[user namespaces][(http://man7.org/linux/man-pages/man7/user_namespaces.7.html).
User namespaces allow to drop the sandboxee into a custom chroot environment
without requiring root privileges.

## Getting Started

Read our [Getting started](getting-started.md) page to set up your first
sandbox.

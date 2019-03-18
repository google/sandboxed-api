# Sandboxing Code

Sometimes, a piece of code carries a lot of security risk. Examples include:

* Commercial binary-only code to do document parsing. Document parsing often
  goes wrong, and binary-only means no opportunity to fix it up.
* A web browser's core HTML parsing and rendering. This is such a large amount
  of code that there will be security bugs.
* A JavaScript engine in Java. Accidents here would permit arbitrary calls to
  Java methods.

Where a piece of code is very risky, and directly exposed to untrusted users
and untrusted input, it is sometimes desirable to sandbox this code. The hardest
thing about sandboxing is making the call whether the risk warrants the effort
to sandbox.

There are many approaches to sandboxing, including virtualization, jail
environments, network segregation and restricting the permissions code runs
with. This page covers technologies available to do the latter: restrict the
permission code runs with. See the following depending on which technology you
are using:

## General Sandboxing

Project/technology                         | Description
-------------------------------------------|------------
[Sandbox2](../sandbox2/README.md)          | Linux sandboxing using namespaces, resource limits and seccomp-bpf syscall filters. Provides the underlying sandboxing technology for Sandboxed API.
[gVisor](https://github.com/google/gvisor) | Uses hardware virtualization and a small syscall emulation layer implemented in Go.


## Sandbox command-line tools

Project/technology   | Description
---------------------|------------
[Firejail](https://github.com/netblue30/firejail) | Lightweight sandboxing tool implemented as a SUID program with minimal dependencies.
[Minijail](https://android.googlesource.com/platform/external/minijail/) | The sandboxing and containment tool used in Chrome OS and Android. Provides an executable and a library that can be used to launch and sandbox other programs and code.
[NSJail](nsjail.com) | Process isolation for Linux using namespaces, resource limits and seccomp-bpf syscall filters. Can optionally make use of [Kafel](https://github.com/google/kafel/), a custom domain specific language, for specifying syscall policies.


## C/C++

Project/technology       | Description
-------------------------|------------
[Sandboxed API](..)      | Reusable sandboxes for C/C++ libraries using Sandbox2.
(Portable) Native Client | **(Deprecated)** Powerful technique to sandbox C/C++ binaries by compiling to a restricted subset of x86 (NaCl)/LLVM bytecode (PNaCl).


## Graphical/Desktop Applications

Project/technology                            | Description
----------------------------------------------|------------
[Flatpak](https://github.com/flatpak/flatpak) | Built on top of [Bubblewrap](https://github.com/projectatomic/bubblewrap), provides sandboxing for Linux desktop applications. Puts an emphasis on packaging and distribution of native apps.

# FAQ

## Can I use threads?

Yes, threads are supported in sandbox2.

### All threads must be sandboxed

Because of the way Linux works, the seccomp-bpf policy is applied to the current
thread only: this means other existing threads do not get the policy, but future
threads will inherit the policy.

If you are using sandbox2 in the
[default mode](getstarted.md#a-Execute-a-binary-with-sandboxing-already-enabled)
where sandboxing is enabled before `execve()`, all threads will inherit the
policy, and there is no problem. This is the preferred mode of sandboxing.

If you want to use the
[second mode](getstarted.md#b-Tell-the-executor-when-to-be-sandboxed) where the
executor has
`set_enable_sandbox_before_exec(false)` and the sandboxee tells the executor
when it wants to be sandboxed with `SandboxMeHere()`, then the filter still
needs to be applied to all threads. Otherwise, there is a risk of a sandbox
escape: malicious code could migrate from a sandboxed thread to an unsandboxed
thread.

The Linux kernel introduced the TSYNC flag in version 3.17, which allows
applying a policy to all threads. Before this flag, it was only possible to
apply the policy on a thread-by-thread basis.

If sandbox2 detects that it is running on a kernel without TSYNC-support and you
call `SandboxMeHere()` from multi-threaded program, sandbox2 will abort, since
this would compromise the safety of the sandbox.

## How should I compile my sandboxee?

If not careful, it is easy to inherit a lot of dependencies and side effects
(extra syscalls, file accesses or even network connections) which make
sandboxing harder (tracking down all side effects) and less safe (because the
syscall and file policies are wider). Some compile options can help reduce this:

* statically compile the sandboxee binary to avoid dynamic linking which uses a
  lot of syscalls (`open()`/`openat()`, `mmap()`, etc.). Also since Bazel adds
  `pie` by default but static is incompatible with it, use the features flag to
  force it off.
  That is, use the following options in
  [cc_binary](https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary)
  rules:

  ```python
  linkstatic = 1,
  features = [
    "fully_static_link",  # link libc statically
    "-pie",
  ],
  ```

  *However:* this has the downside of reducing ASLR heap entropy (from 30 bits
  to 8 bits), making exploits easier. Decide carefully what is preferable
  depending on your sandbox implementation and policy:

  * **not static**: good heap ASLR, potentially harder to get initial code
    execution but at the cost of a less effective sandbox policy, potentially
    easier to break out of.
  * **static**: bad heap ASLR, potentially easier to get initial code execution
    but a more effective sandbox policy, potentially harder to break out of.

  It is an unfortunate choice to make because the compiler does not support
  static PIE (Position Independent Executables). PIE is implemented by having
  the binary be a dynamic object, and the dynamic loader maps it at a random
  location before executing it. Then because the heap is traditionnally placed
  at a random offset after the base address of the binary (and expanded with
  `brk` syscall), it means for static binaries the heap ASLR entropy is only
  this offset because there is no PIE.

For examples of these compiling options, look at the
[static](examples.md#static) example
[BUILD.bazel](../examples/static/BUILD.bazel): `static_bin.cc` is compiled
statically, which allows us to have a very tight syscall policy. This works
nicely for sandboxing third party binaries too.

## Can I sandbox 32-bit x86 binaries?

Sandbox2 can only sandbox the same arch as it was compiled with.

In addition, support for 32-bit x86 has been removed from Sandbox2. If you try
to use a 64-bit x86 executor to sandbox a 32-bit x86 binary, or a 64-bit x86
binary making 32-bit syscalls (via `int 0x80`), both will generate a sandbox
violation that can be identified with the architecture label *[X86-32]*.

The reason behind this behavior is that syscall numbers are different between
architectures and since the syscall policy is written in the architecture of the
executor, it would be dangerous to allow a different architecture for the
sandboxee. Indeed, allowing an seemingly harmless syscall that in fact means
another more harmful syscall could open up the sandbox to an escape.

## Any limits on the number of sandboxes an executor process can request?

For each sandboxee instance (new process spawned from the forkserver) a new
thread is created - that's where the limitation would lie.

## Can an Executor request the creation of more than one Sandbox?

No. There is a 1:1 correspondence - an `Executor` instance stores the PID of the
sandboxee, manages the `Comms` instance to the `Sandbox` instance, etc.

## Can I use sandbox2 from Go?

Yes. Write your executor in C++ and expose it to Go via SWIG.

## Why do I get `Function not implemented` inside `forkserver.cc?`

Sandbox2 only supports running on reasonably new kernels. Our current cut-off is
the 3.19 kernel though that might change in the future. The reason for this is
that we are using relatively new kernel features including user namespaces and
seccomp with the TSYNC flag.

If you are running on prod, this should not be in issue, since almost the entire
fleet is running a new enough kernel. If you have any issues with this, please
contact us.

If you are running on Debian or Ubuntu, updating your kernel is as easy as
`apt-get install linux-image-[recent version]`.

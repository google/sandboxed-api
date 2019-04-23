# Getting started with Sandbox2

## Introduction

In this guide, you will learn how to create your own sandbox, policy and tweaks.
It is meant as a guide, alongside the [examples](examples.md) and code
documentation in the header files.


## 1. Choose an executor

Sandboxing starts with an *executor* (see [How it works](howitworks.md)), which
will be responsible for running the *sandboxee*. The API for this is in
[executor.h](../executor.h). It is very flexible to let you choose what works
best for your use case.

### a. Execute a binary with sandboxing already enabled

This is the simplest and safest way to use sandboxing. For examples see
[static](examples.md#static) and [sandboxed tool](examples.md#tool).

```c++
#include "sandboxed_api/sandbox2/executor.h"

std::string path = "path/to/binary";
std::vector<std::string> args = {path};  // args[0] will become the sandboxed
                                         // process' argv[0], typically the
                                         // path to the binary.
auto executor = absl::make_unique<sandbox2::Executor>(path, args);
```

### b. Tell the executor when to be sandboxed

This offers you the flexibility to be unsandboxed during initialization, then
choose when to enter sandboxing by calling
`::sandbox2::Client::SandboxMeHere()`. The code has to be careful to always
call this or it would be unsafe to proceed, and it has to be single-threaded
(read why in the [FAQ](faq.md#Can-I-use-threads)). For an example see
[crc4](examples.md#CRC4).

Note: The [filesystem restrictions](#Filesystem-checks) will be in effect right
from the start of your sandboxee. Using this mode allows you to enable the
syscall filter later on from the sandboxee.

```c++
#include "sandboxed_api/sandbox2/executor.h"

std::string path = "path/to/binary";
std::vector<std::string> args = {path};
auto executor = absl::make_unique<sandbox2::Executor>(path, args);
executor->set_enable_sandbox_before_exec(false);
```

### c. Prepare a binary, wait for fork requests, and sandbox on your own

This mode allows you to start a binary, prepare it for sandboxing, and - at the
specific moment of your binary's lifecycle - make it available for the
executor. The executor will send fork request to your binary, which will
`fork()` (via `::sandbox2::ForkingClient::WaitAndFork()`). The newly created
process will be ready to be sandboxed with
`::sandbox2::Client::SandboxMeHere()`. This mode comes with a few downsides,
however: For example, it pulls in more dependencies in your sandboxee and
does not play well with namespaces, so it is only recommended it if you have
tight performance requirements.

For an example see [custom_fork](examples.md#custom_fork).

```c++
#include "sandboxed_api/sandbox2/executor.h"

// Start the custom ForkServer
std::string path = "path/to/binary";
std::vector<std::string> args = {path};
auto fork_executor = absl::make_unique<sandbox2::Executor>(path, args);
fork_executor->StartForkServer();

// Initialize Executor with Comms channel to the ForkServer
auto executor = absl::make_unique<sandbox2::Executor>(
    fork_executor->ipc()->GetComms());
```

## 2. Creating a policy

Once you have an executor you need to define the policy for the sandboxee: this
will restrict the syscalls and arguments that the sandboxee can make as well as
the files it can access. For instance, a policy could allow `read()` on a given
file descriptor (e.g. `0` for stdin) but not another.

To create a [policy object][filter], use the
[PolicyBuilder](../policybuilder.h). It comes with helper functions that allow
many common operations (such as `AllowSystemMalloc()`), whitelist syscalls
(`AllowSyscall()`) or grant access to files (`AddFile()`).

If you want to restrict syscall arguments or need to perform more complicated
checks, you can specify a raw seccomp-bpf filter using the bpf helper macros
from the Linux kernel. See the [kernel documentation][filter] for more
information about BPF. If you find yourself writing repetitive BPF-code that
you think should have a usability-wrapper, feel free to file a feature request.

Coming up with the syscalls to whitelist is still a bit of manual work
unfortunately. Create a policy with the syscalls you know your binary needs and
run it with a common workload. If a violation gets triggered, whitelist the
syscall and repeat the process. If you run into a violation that you think might
be risky to whitelist and the program handles errors gracefullly, you can try to
make it return an error instead with `BlockSyscallWithErrno()`.

[filter]: https://www.kernel.org/doc/Documentation/networking/filter.txt

```c++
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"

std::unique_ptr<sandbox2::Policy> CreatePolicy() {
  return sandbox2::PolicyBuilder()
    .AllowSyscall(__NR_read)  // See also AllowRead()
    .AllowTime()              // Allow time, gettimeofday and clock_gettime
    .AddPolicyOnSyscall(__NR_write, {
        ARG(0),        // fd is the first argument of write (argument #0)
        JEQ(1, ALLOW), // allow write only on fd 1
        KILL,          // kill if not fd 1
    })
    .AddPolicyOnSyscall(__NR_mprotect, {
        ARG_32(2), // prot is a 32-bit wide argument, so it's OK to use *_32
                   // macro here
        JNE32(PROT_READ | PROT_WRITE, KILL), // prot must be the RW, otherwise
                                             // kill the process
        ARG(1), // len is a 64-bit argument
        JNE(0x1000, KILL),  // Allow single page syscalls only, otherwise kill
                            // the process
        ALLOW,              // Allow for the syscall to proceed, if prot and
                            // size match
    })
    // Allow the open() syscall but always return "not found".
    .BlockSyscallWithErrno(__NR_open, ENOENT)
    .BuildOrDie();
}
```

Tip: Test for the most used syscalls at the beginning so you can allow them
early without consulting the rest of the policy.


### Filesystem checks

The default way to grant access to files is by using the `AddFile()` class of
functions of the `PolicyBuilder`. This will automatically enable user namespace
support that allows us to create a custom chroot for the sandboxee and gives you
some other features such as creating tmpfs mounts.

```c++
  sandbox2::PolicyBuilder()
    // ...
    .AddFile("/etc/localtime")
    .AddDirectory("/usr/share/fonts")
    .AddTmpfs("/tmp")
    .BuildOrDie();
```

## 3. Adjusting limits

Sandboxing by restricting syscalls is one thing, but if the job can run
indefinitely or exhaust RAM and other resources that is not good either.
Therefore, by default the sandboxee runs under tight execution limits, which can
be adjusted using the [Limits](../limits.h) class, available by calling
`limits()` on the `Executor` object created earlier. For an example see [sandbox
tool](examples.md#tool).

```c++
// Restrict the address space size of the sandboxee to 4 GiB.
executor->limits()->set_rLimit_as(4ULL << 30);
// Kill sandboxee with SIGXFSZ if it writes more than 1 GiB to the filesystem.
executor->limits()->set_rLimit_fsize(1ULL << 30);
// Number of file descriptors which can be used by the sandboxee.
executor->limits()->set_rLimit_nofile(1ULL << 10);
// The sandboxee is not allowed to create core files.
executor->limits()->set_rLimit_core(0);
// Maximum 300s of real CPU time.
executor->limits()->set_rLimit_cpu(300);
// Maximum 120s of wall time.
executor->limits()->set_walltime_limit(absl::Seconds(120));
```

## 4. Running the sandboxee

With our executor and policy ready, we can now create the `Sandbox2` object and
run it synchronously. For an example see [static](examples.md#static).

```c++
#include "sandboxed_api/sandbox2/sandbox2.h"

sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));
auto result = s2.Run();  // Synchronous
LOG(INFO) << "Result of sandbox execution: " << result.ToString();
```

You can also run it asynchronously, for instance to communicate with the
sandboxee. For examples see [crc4](examples.md#CRC4) and [sandbox
tool](examples.md#tool).

```c++
#include "sandboxed_api/sandbox2/sandbox2.h"

sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));
if (s2.RunAsync()) {
  ...  // Communicate with sandboxee, use s2.Kill() to kill it if needed
}
auto result = s2.AwaitResult();
LOG(INFO) << "Final execution status: " << result.ToString();
```

## 5. Communicating with the sandboxee

The executor can communicate with the sandboxee with file descriptors.

Depending on your situation, that can be all that you need (e.g., to share a
file with the sandboxee or to read the sandboxee standard output).

If you need more communication logic, you can implement your own protocol or
reuse our convenient **comms** API able to send integers, strings, byte
buffers, protobufs or file descriptors. Bonus: in addition to C++, we also
provide a pure-C comms library, so it can be used easily when sandboxing C
third-party projects.

### a. Sharing file descriptors

Using the [IPC](../ipc.h) (*Inter-Process Communication*) API, you can either:

* use `MapFd()` to map file descriptors from the executor to the sandboxee, for
  instance to share a file opened from the executor for use in the sandboxee,
  as it is done in the [static](examples.md#static) example.

  ```c++
  // The executor opened /proc/version and passes it to the sandboxee as stdin
  executor->ipc()->MapFd(proc_version_fd, STDIN_FILENO);
  ```
  or

* use `ReceiveFd()` to create a socketpair endpoint, for instance to read the
  sandboxee standard output or standard error, as it is done in the
  [sandbox tool](examples.md#tool) example.

  ```c++
  // The executor receives a file descriptor of the sandboxee stdout
  int recv_fd1 = executor->ipc())->ReceiveFd(STDOUT_FILENO);
  ```

### b. Using the comms API

Using the [comms](../comms.h) API, you can send integers, strings or byte
buffers. For an example see [crc4](examples.md#CRC4).

To use comms, first get it from the executor IPC:

```c++
auto* comms = executor->ipc()->GetComms();
```

To send data to the sandboxee, use one of the `Send*` family of functions.
For instance in the case of [crc4](examples.md#CRC4), the executor sends an
`unsigned char buf[size]` with `SendBytes(buf, size)`:

```c++
if (!(comms->SendBytes(static_cast<const uint8_t*>(buf), sz))) {
  /* handle error */
}
```

To receive data from the sandboxee, use one of the `Recv*` functions. For
instance in the case of [crc4](examples.md#CRC4), the executor receives the
checksum into an 32-bit unsigned integer:

```c++
uint32_t crc4;
if (!(comms->RecvUint32(&crc4))) {
  /* handle error */
}
```

### c. Sharing data with buffers

In some situations, it can be useful to share data between executor and
sandboxee in order to share large amounts of data and to avoid expensive copies
that are sent back and forth. The [buffer API](../buffer.h) serves this use
case: the executor creates a `Buffer`, either by size and data to be passed, or
directly from a file descriptor, and passes it to the sandboxee using
`comms->SendFD()` in the executor and `comms->RecvFD()` in the sandboxee.

For example, to create a buffer in the executor, send its file descriptor to
the sandboxee, and afterwards see what the sandboxee did with it:

```c++
sandbox2::Buffer buffer;
buffer.Create(1ULL << 20);  // 1 MiB
s2.RunAsync();
comms->SendFD(buffer.GetFD());
auto result = s2.AwaitResult();
uint8_t* buf = buffer.buffer();  // As modified by sandboxee
size_t len = buffer.size();
```

On the other side the sandboxee receives the buffer file descriptor, creates the
buffer object and can work with it:

```c++
int fd;
comms.RecvFD(&fd);
sandbox2::Buffer buffer;
buffer.Setup(fd);
uint8_t *buf = buffer.GetBuffer();
memset(buf, 'X', buffer.GetSize()); /* work with the buffer */
```

## 6. Exiting

If running the sandbox synchronously, then `Run` will only return when it's
finished:

```c++
auto result = s2.Run();
LOG(INFO) << "Final execution status: " << result.ToString();
```

If running asynchronously, you can decide at anytime to kill the sandboxee:

```c++
s2.Kill()
```

Or just wait for completion and the final execution status:

```c++
auto result = s2.AwaitResult();
LOG(INFO) << "Final execution status: " << result.ToString();
```

## 7. Test

Like regular code, your sandbox implementation should have tests. Sandbox tests
are not meant to test the program correctness, but instead to check whether the
sandboxed program can run without issues like sandbox violations. This also
makes sure that the policy is correct.

A sandboxed program is tested the same way it would run in production, with the
arguments and input files it would normally process.

It can be as simple as a shell test or C++ tests using sub processes. Check out
[the examples](examples.md) for inspiration.

## Conclusion

Thanks for reading this far, we hope you liked our guide and now feel empowered
to create your own sandboxes to help keep your users safe.

Creating sandboxes and policies is a difficult task prone to subtle errors. To
remain on the safe side, have a security expert review your policy and code.

# Sandboxed API

Copyright 2019 Google LLC

![Sandbox](sandboxed_api/docs/images/playing-in-sand.png)


## What is Sandboxed API?

The Sandboxed API project (**SAPI**) aims to make sandboxing of C/C++ libraries
less burdensome: after initial setup of security policies and generation of
library interfaces, an almost-identical stub API is generated (using a
[templated based programming variable hierarchy system](sandboxed_api/docs/variables.md)),
transparently forwarding calls using a custom RPC layer to the real library
running inside a sandboxed environment.

Additionally, each SAPI library utilizes a tightly defined security policy, in
contrast to typical sandboxed project, where security policies must cover total
syscall/resource footprint of all utilized libraries.


## Intended audience

SAPI is designed to help you sandbox only a part of binary. That is, a library
or some other code with an unknown security posture.

See [Sandboxing Code](sandboxed_api/docs/sandbox-overview.md) to make sure this is the type of
sandboxing you are looking for.

## How does it work?

Navigate to our [How it works](sandboxed_api/docs/howitworks.md) page.


## Motivation

Sandboxes available for use in Google required additional implementation work
with each new instance of project which was intended to be sandboxed, even if
it reused the same software library. Sandbox security policies and other
restrictions applied to the sandboxed process had to be reimplemented each
time, and data exchange mechanisms between trusted and untrusted parts of
the code had to be designed from the scratch.

While designing the Sandboxed API project, our goal was to make this process
easy and straightforward. Our working motto is: **Sandbox once, use anywhere**.


## Is it proven technology?

The project has been designed, developed and is maintained by members of
the Google Sandbox Team. It also uses our field-tested
[Sandbox 2](sandboxed_api/sandbox2/README.md).

Currently, many internal projects are already using SAPI to isolate
their production workloads. You can read more about them in the
[Examples](sandboxed_api/docs/examples.md) section.

We've also prepared some more example SAPI implementations for your reference.


## Quick Start

Install the required dependencies, this assumes you are running Debian 10
"Buster":

```bash
echo "deb http://storage.googleapis.com/bazel-apt stable jdk1.8" | \
  sudo tee /etc/apt/sources.list.d/bazel.list
wget -qO - https://bazel.build/bazel-release.pub.gpg | sudo apt-key add -
sudo apt-get install -qy python-typing python-clang-7 libclang-7-dev
sudo apt-get install -qy build-essential linux-libc-dev bazel
```

Clone and run the build:
```bash
git clone github.com/google/sandboxed-api && cd sandboxed-api
bazel build ...
```

Try out one of the [examples](sandboxed_api/docs/examples.md):
```bash
bazel run //sandboxed_api/examples/stringop:main_stringop
```

There are also a more detailed instructions that should help you
**[getting started with SAPI](sandboxed_api/docs/getting-started.md)**.


## Getting Involved

If you want to contribute, please read [CONTRIBUTING.md](CONTRIBUTING.md) and
send us pull requests. You can also report bugs or file feature requests.

If you'd like to talk to the developers or get notified about major product
updates, you may want to subscribe to our
[mailing list](mailto:sandboxed-api-users@googlegroups.com) or sign up with this [link](https://groups.google.com/forum/#!forum/sandboxed-api-users).

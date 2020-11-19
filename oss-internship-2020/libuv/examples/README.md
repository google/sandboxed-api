# LibUV Sandbox Examples

Each example in this folder is the sandboxed version of a code snippet from
[LibUV's User Guide](https://docs.libuv.org/en/v1.x/guide.html). These examples
perform some basic tasks using LibUV, and can be useful both to understand how
to use LibUV Sandbox, but also to get an idea of how regular and sandboxed code
compare to each other.

This is the list of examples:

- **helloworld.cc**: sandboxed version of
[helloworld/main.c](https://docs.libuv.org/en/v1.x/guide/basics.html#hello-world).
It simply starts a loop that exits immediately. It shows how to run a simple
loop in LibUV Sandbox.
- **idle-basic.cc**: sandboxed version of
[idle-basic/main.c](https://docs.libuv.org/en/v1.x/guide/basics.html#handles-and-requests).
Creates an idle watcher that stops the loop after a certain number of
iterations. It shows how a simple callback can be used in LibUV Sandbox.
- **uvcat.cc**: sandboxed version of
[uvcat/main.c](http://docs.libuv.org/en/v1.x/guide/filesystem.html#reading-writing-files).
Takes a single argument, the absolute path of a file, and prints its contents
(it is a simplified version of the command line tootl `cat`). It shows how to
manage various complex callbacks for opening, reading and writing files.

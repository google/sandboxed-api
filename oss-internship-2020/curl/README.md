# LibCurl Sandbox

This library is a sandboxed version of curl's C API,
[libcurl](https://curl.haxx.se/libcurl/c/), implemented using Sandboxed API.

## Setup

The repository can be cloned using: `git clone --recursive <URL to this repo>`
The `--recursive` flag ensures that submodules are also cloned.

Alternatively, if the repository has already been cloned but the submodules have
not, these can be cloned using: `git submodule update --init --recursive`

The full list of Sandboxed API dependencies can be found on
[Sandboxed API Getting Started page](https://developers.google.com/code-sandboxing/sandboxed-api/getting-started).

The following commands, used from the current `curl/` directory, build the
library:

```bash
mkdir -p build && cd build
cmake .. -G Ninja -D SAPI_ROOT=<path to sandboxed-api>
cmake --build .
```

## Implementation details

All of libcurl's methods are supported by the library. However, a few of these
have different signatures defined in the sandboxed header `custom_curl.h`, which
wraps and extends libcurl.

This is necessary because Sandboxed API sandboxes most of libcurl correctly, but
encounters some issues when sandboxing a few methods. The simplest solution is
wrapping these methods into wrapper methods that accomplish the same tasks but
can also be sandboxed.

The next sections describe the issues encountered and contain some information
on the signatures of the wrapper methods solving these issues.

#### Variadic methods

Variadic methods are currently not supported by Sandboxed API. To solve this,
these methods are defined with an additional explicit parameter in
`custom_curl.h`.

The methods are:

-   `curl_easy_setopt`. Use `curl_easy_setopt_ptr`, `curl_easy_setopt_long` or
    `curl_easy_setopt_curl_off_t` instead.
-   `curl_easy_getinfo`. Use `curl_easy_getinfo_ptr` instead.
-   `curl_multi_setopt`. Use `curl_multi_setopt_ptr`, `curl_multi_setopt_long`
    or `curl_multi_setopt_curl_off_t` instead.
-   `curl_share_setopt`. Use `curl_share_setopt_ptr` or `curl_share_setopt_long`
    instead

#### Methods with incomplete array arguments

Incomplete array arguments are currently not supported by Sandboxed API. To
solve this, methods taking an incomplete array argument have a wrapper in
`custom_curl.h`, and take a pointer as the argument.

The methods are:

-   `curl_multi_poll`. Use `curl_multi_poll_sapi` instead.
-   `curl_multi_wait`. Use `curl_multi_wait_sapi` instead.

#### Methods with conflicts on the generated header

Some methods create conflicts on the generated header because of redefined
`#define` directives from files included by the header. To solve this, the
conflicting types and methods are redefined in `custom_curl.h`.

The types are:

-   `time_t`. Use `time_t_sapi` instead.
-   `fd_set`. Use `fd_set_sapi` instead.

The methods are:

-   `curl_getdate`. Use `curl_getdate_sapi` instead.
-   `curl_multi_fdset`. Use `curl_multi_fdset_sapi` instead.

#### Function pointers

The functions whose pointers will be passed to the library's methods
(*callbacks*) can't be implemented in the files making use of the library, but
must be in other files. These files must be compiled together with the library,
and this is done by adding their absolute path to the cmake variable
`CURL_SAPI_CALLBACKS`.

The pointers can then be obtained using an `RPCChannel` object, as shown in
`example2.cc`.

## Examples

The `examples` directory contains the sandboxed versions of example source codes
taken from [this page](https://curl.haxx.se/libcurl/c/example.html) on curl's
website. More information about each example can be found in the examples'
[README](examples/README.md).

To build these examples when building the library, the cmake variable
`CURL_SAPI_BUILD_EXAMPLES` must be set to `ON`. This enables Sandboxed API
examples as well.

## Policy

The `sandbox.h` file contains a policy allowing all is necessary for libcurl to
perform simple requests. It is used by all the examples, except by example3.
This example needs some additional policies and files in its namespace (since it
uses HTTPS), and the file `example3.cc` shows how to easily extend an existing
policy.

## Testing

The `tests` folder contains some test cases created using Google Test. The class
`CurlTestUtils` is used to facilitate some tasks that all test cases need,
including the setup of a mock local server on which test requests are performed.

To build these tests when building the library, the cmake variable
`CURL_SAPI_BUILD_TESTING` must be set to `ON`. This enables Sandboxed API tests
as well.

## Callbacks

The `callbacks.h` and `callbacks.cc` files implement all the callbacks used by
examples and tests.

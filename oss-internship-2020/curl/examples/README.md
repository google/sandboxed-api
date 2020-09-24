# LibCurl Sandbox Examples

Each example in this folder is the sandboxed version of a code snippet from
[this page](https://curl.haxx.se/libcurl/c/example.html) on curl's website.
These examples perform some basic tasks using libcurl, and can be useful both to
understand how to use LibCurl Sandbox, but also to get an idea of how regular
and sandboxed code compare to each other.

This is the list of the examples:

-   **example1**: sandboxed version of
    [simple.c](https://curl.haxx.se/libcurl/c/simple.html). Really simple HTTP
    request, downloads and prints out the page at
    [example.com](http://example.com).
-   **example2**: sandboxed version of
    [getinmemory.c](https://curl.haxx.se/libcurl/c/getinmemory.html). Same HTTP
    request as example1. The difference is that this example uses a callback to
    save the page directly in memory. Only the page size is printed out.
-   **example3**: sandboxed version of
    [simplessl.c](https://curl.haxx.se/libcurl/c/simplessl.html). HTTPS request
    of the [example.com](https://example.com) page, using SSL authentication.
    This script takes 4 arguments (SSL certificates file, SSL keys file, SSL
    keys password and CA certificates files), and prints out the page.
-   **example4**: sandboxed version of
    [multi-poll.c](https://curl.haxx.se/libcurl/c/multi-poll.html). Same HTTP
    request as example1, with the addition of a polling method that can be used
    to track the status of the request. The page is printed out after it is
    downloaded.
-   **example5**: sandboxed version of
    [multithread.c](https://curl.haxx.se/libcurl/c/multithread.html). Four HTTP
    request of the pages [example.com](http://example.com),
    [example.edu](http://example.edu), [example.net](http://example.net) and
    [example.org](http://example.org), performed at the same time using
    libcurl's multithreading methods. The pages are printed out.
-   **example6**: sandboxed version of
    [simple.c](https://curl.haxx.se/libcurl/c/simple.html). Performs the same
    tasks as example1, but Sandbox API Transactions are used to show how they
    can be used to perform a simple request.

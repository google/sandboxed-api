# Curl

This library is a sandboxed version of the original [curl](https://curl.haxx.se/libcurl/c/) C library, implemented using sandboxed-api.

## Supported methods

The library currently supports curl's [*Easy interface*](https://curl.haxx.se/libcurl/c/libcurl-easy.html). According to curl's website:

> The easy interface is a synchronous, efficient, quickly used and... yes, easy interface for file transfers. 
> Numerous applications have been built using this.

However, all of the methods using function pointers, are not yet supported.

## Examples

The `examples` directory contains the sandboxed versions of example source codes taken from [this](https://curl.haxx.se/libcurl/c/example.html) page on curl's website.

## Implementation details

Variadic methods are currently not supported by sandboxed-api. Because of this, the sandboxed header `custom_curl.h` wraps the curl library and explicitly defines the variadic methods.

For example, instead of using `curl_easy_setopt`, one of these methods can be used: `curl_easy_setopt_ptr`, `curl_easy_setopt_long` or `curl_easy_setopt_curl_off_t`.
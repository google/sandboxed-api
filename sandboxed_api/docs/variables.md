# Variables

Typically, you'll be able to use native C-types to deal with the SAPI Library,
but sometimes some special types will be required. This mainly happens when
passing pointers to simple types, and pointers to memory blocks (structures,
arrays). Because you operate on local process memory (of the host code), when
calling a function taking a pointer, it must be converted into a corresponding
pointer inside the sandboxed process (SAPI Library) memory.

Take a look at the [SAPI directory](..). The `var_*.h` files provide classes
and templates representing various types of data, e.g. `::sapi::v::UChar`
represents well-known `unsigned char` while `::sapi::v::Array<int>` represents
an array of integers (`int[]`).


## Pointers

When creating your host code, you'll be generally using functions exported by
an auto-generated SAPI interface header file from your SAPI Library. Most of
them will take simple types (or typedef'd types), but when a pointer is needed,
you need to wrap it with the `::sapi::v::Ptr` template class.

Most types that you will use, provide the following methods:

* `::PtrNone()`: this pointer, when passed to the SAPI Library function,
  doesn't synchronize the underlying memory between the host code process and
  the SAPI Library process.
* `::PtrBefore()`: when passed to the SAPI Library function, will synchronize
  memory of the object it points to, before the call takes place. This means,
  that the local memory of the pointed variable will be transferred to the
  SAPI Library process before the call is initiated.
* `::PtrAfter()`: this pointer will synchronize memory of the object it points
  to, after the call has taken place. This means, that the remote memory of a
  pointed variable will be transferred to the host code process' memory, after
  the call has been completed.
* `::PtrBoth()`: combines the functionality of both `::PtrBefore()` and
  `::PtrAfter()`


## Structures

When a pointer to a structure is used inside a call to a SAPI Library, that
structure needs to created with the `::sapi::v::Struct` template. You can use
the `PtrNone()`/`Before()`/`After()`/`Both()` methods of this template to obtain
a relevant `::sapi::v::Ptr` object that can be used in SAPI Library function
calls.


## Arrays

The `::sapi::v::Array` template allow to wrap both existing arrays of elements,
as well as dynamically create one for you (please take a look at its
constructor to decide which one you would like to use).

The use of pointers is analogous to [Structures](#structures).


## Examples

Our canonical [sum library](../examples/sum/main_sum.cc) demonstrates use of
pointers to call sandboxed functions in its corresponding SAPI Library.

You might also want to take a look at the [Examples](examples.md) page to
familiarize yourself with other working examples of libraries sandboxed
with SAPI.

* [sum library](../examples/sum/main_sum.cc)
* [stringop](../examples/stringop/main_stringop.cc)
* [zlib](../examples/zlib/main_zlib.cc)

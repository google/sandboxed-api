# Transactions

## Introduction

When using SAPI, there is another layer around library calls that might
fail, which is why all library function prototypes return `::sapi::StatusOr<T>`
instead of `T`. In the event that the library function invocation fails (e.g.
because of a sandbox violation), the return value will contain details about
the error that occurred.

In order to deal with those exceptional situations, the high-level
`::sapi::Transaction` module can be used.


### `::sapi::Transaction`

With SAPI we are trying to isolate the [host code](host-code.md) from such
problems in the sandboxed library, giving ability to the caller to restart or
abort the problematic data processing request.
The transaction class goes one step further and automatically repeats processes
that have failed.

The usual pattern when dealing with libraries looks like this:

```cpp
  LibInit();
  while (data = NextDataToProcess()) {
    result += LibProcessData(data);
  }
  LibClose();
```

This translates to this code when using SAPI:

```cpp
::sapi::Status Init(::sapi::Sandbox* sandbox) {
  LibraryAPI lib(sandbox);
  SAPI_RETURN_IF_ERROR(lib.LibInit());
  return ::sapi::OkStatus();
}

::sapi::Status Finish(::sapi::Sandbox *sandbox) {
  // ...
}

::sapi::Status handle_data(::sapi::Sandbox *sandbox, Data data_to_process,
                           Result *out) {
  LibraryAPI lib(sandbox);
  SAPI_ASSIGN_OR_RETURN(*out, lib.LibProcessData(data_to_process));
  return ::sapi::OkStatus();
}

void handle() {
  // ...
  ::sapi::BasicTransaction transaction(Init, Finish);
  while (data = NextDataToProcess()) {
    ::sandbox2::Result result;
    transaction.Run(handle_data, data, &result);
    // ...
  }
  // ...
}
```

The transaction class makes sure to reinitialize the library in the case that an
error occures during the `handle_data` invovcation - more on this later.

SAPI transaction can be used in two different ways, depending on your
requirements:

* Implementing a transaction class inheriting from `::sapi::Transaction`,
* Using function pointers passed to `::sapi::BasicTransaction`, see above.

Both methods allow you to specify the following three functions:

* `::sapi::Transaction::Init()`, which will be called **only once** during each
  transaction to the sandboxed library (and, also, during each restart of the
  transaction). It's similar to calling a `LibInit()` function from a typical
  C/C++ library.
* `::sapi::Transaction::Main()`, which will be called for each call to
  `::sapi::Transaction::Run()`.
* `::sapi::Transaction::Finish()`, which will be called during the
  `::sapi::Transaction` object destruction, resembling the call to a typical
  `LibClose()` function call.

### Transaction Restarts

If any kind of problem arises during execution of the
`Init()`/`Main()`/`Finish()` methods, e.g, they return a failure return code due
to library error, or sandboxed process crash, or a security sandbox violation,
the transaction will be restarted (by default, `kDefaultRetryCnt` times, see
[transaction.h](../transaction.h)).

During such restarts the `Init()`/`Main()` flow is observed (i.e, the `Init()`
function is called again), and if repeated calls to the
`::sapi::Transaction::Run()` method return errors, then the whole method
returns an error to its caller.

### Sandbox/RPC Error handling

Although the automatically generated [SAPI library
interface](library.md#Interface-Generation) tries to be as similar to the
original library function prototype we somehow need to signal Sandbox/RPC
errors. Instead of providing the return value directly, SAPI makes use of
`::sapi::StatusOr<T>` for return types `T` != `void` or `::sapi::Status` for
functions returning `void`.

Example of how to use the API (from the sum example):

```cpp
::sapi::Status SumTransaction::Main() {
  SumApi f(GetSandbox());
  // ::sapi::StatusOr<int> sum(int a, int b)
  SAPI_ASSIGN_OR_RETURN(int v, f.sum(1000, 337));
  ...
  // ::sapi::Status sums(sapi::v::Ptr* params)
  SumParams params;
  params.mutable_data()->a = 1111;
  params.mutable_data()->b = 222;
  params.mutable_data()->ret = 0;
  SAPI_RETURN_IF_ERROR(f.sums(params.PtrBoth()));
  ...
  int *ssaddr;
  SAPI_RETURN_IF_ERROR(GetSandbox()->Symbol(
      "sumsymbol", reinterpret_cast<void**>(&ssaddr)));
  ::sapi::v::Int sumsymbol;
  sumsymbol.SetRemote(ssaddr);
  SAPI_RETURN_IF_ERROR(GetSandbox()->TransferFromSandboxee(&sumsymbol));
  ...
  return ::sapi::OkStatus();
}
```


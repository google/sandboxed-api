#include "sandboxed_api/tests/testcases/replaced_library_struct_sync.h"

int kMixedStructSandboxData = 0x12345678;

void InitMixedStruct(MixedStruct* mixed) {
  mixed->host_data = 123;
  mixed->sandbox_opaque_data = &kMixedStructSandboxData;
}

int MungeMixedStruct(MixedStruct* mixed) {
  int result =
      mixed->host_data + *static_cast<int*>(mixed->sandbox_opaque_data);
  mixed->host_data = result;
  return result;
}

#include "sandboxed_api/annotations.h"
#include "sandboxed_api/tests/testcases/replaced_library_struct_sync.h"

extern "C" {

// By annotating the `sandbox_opaque_data` data member as opaque, we know
// it is okay to only shallowly copy the struct between host <-> sandbox.
// Since the field type is void*, we wouldn't know how to deeply copy it anyway.
SANDBOX_ANNOTATE_STRUCT(struct MixedStruct {
  int host_data;
  void* sandbox_opaque_data SANDBOX_OPAQUE_PTR;
};)

void InitMixedStruct(MixedStruct* mixed SANDBOX_OUT_PTR);

int MungeMixedStruct(MixedStruct* mixed SANDBOX_INOUT_PTR);

}  // extern "C"

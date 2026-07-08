#ifndef SANDBOXED_API_TESTS_REPLACED_LIBRARY_STRUCT_SYNC_H_
#define SANDBOXED_API_TESTS_REPLACED_LIBRARY_STRUCT_SYNC_H_

extern "C" {

// Struct with a mixture of host and sandbox (opaque) data, and typically
// allocated on the host side, so cannot be purely opaque (needs some sync'ing).
struct MixedStruct {
  int host_data;
  void* sandbox_opaque_data;
};

void InitMixedStruct(MixedStruct* mixed);

int MungeMixedStruct(MixedStruct* mixed);

}  // extern "C"

#endif  // SANDBOXED_API_TESTS_REPLACED_LIBRARY_STRUCT_SYNC_H_

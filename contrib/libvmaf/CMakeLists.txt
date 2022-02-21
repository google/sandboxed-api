# Copyright 2022 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.13..3.22)
project(libvmaf-sapi CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBVMAF REQUIRED IMPORTED_TARGET libvmaf)

add_sapi_library(libvmaf_sapi
  INPUTS "${LIBVMAF_INCLUDEDIR}/libvmaf/libvmaf.h"
  LIBRARY vmaf
  FUNCTIONS
    vmaf_ceiln
    vmaf_close
    vmaf_dictionary_compare
    vmaf_dictionary_copy
    vmaf_dictionary_free
    vmaf_dictionary_get
    vmaf_dictionary_merge
    vmaf_dictionary_set
    vmaf_feature_collector_append
    vmaf_feature_collector_append_templated
    vmaf_feature_collector_destroy
    vmaf_feature_collector_get_aggregate
    vmaf_feature_collector_get_score
    vmaf_feature_collector_init
    vmaf_feature_collector_set_aggregate
    vmaf_feature_dictionary_set
    vmaf_feature_extractor_context_close
    vmaf_feature_extractor_context_create
    vmaf_feature_extractor_context_destroy
    vmaf_feature_extractor_context_extract
    vmaf_feature_extractor_context_flush
    vmaf_feature_extractor_context_init
    vmaf_feature_name
    vmaf_feature_name_alias
    vmaf_feature_score_at_index
    vmaf_feature_score_pooled
    vmaf_fex_ctx_parse_options
    vmaf_fex_ctx_pool_aquire
    vmaf_fex_ctx_pool_create
    vmaf_fex_ctx_pool_destroy
    vmaf_fex_ctx_pool_flush
    vmaf_fex_ctx_pool_release
    vmaf_floorn
    vmaf_get_cpu_flags
    vmaf_get_cpu_flags_x86
    vmaf_get_feature_extractor_by_feature_name
    vmaf_get_feature_extractor_by_name
    vmaf_import_feature_score
    vmaf_init
    vmaf_init_cpu
    vmaf_log
    vmaf_model_collection_append
    vmaf_model_collection_destroy
    vmaf_model_collection_feature_overload
    vmaf_model_collection_load
    vmaf_model_collection_load_from_path
    vmaf_model_destroy
    vmaf_model_feature_overload
    vmaf_model_generate_name
    vmaf_model_load
    vmaf_model_load_from_path
    vmaf_option_set
    vmaf_picture_alloc
    vmaf_picture_ref
    vmaf_picture_unref
    vmaf_predict_score_at_index
    vmaf_predict_score_at_index_model_collection
    vmaf_read_json_model_collection_from_buffer
    vmaf_read_json_model_collection_from_path
    vmaf_read_json_model_from_buffer
    vmaf_read_json_model_from_path
    vmaf_read_pictures
    vmaf_ref_close
    vmaf_ref_fetch_decrement
    vmaf_ref_fetch_increment
    vmaf_ref_init
    vmaf_ref_load
    vmaf_score_at_index
    vmaf_score_at_index_model_collection
    vmaf_score_pooled
    vmaf_score_pooled_model_collection
    vmaf_set_cpu_flags_mask
    vmaf_set_log_level
    vmaf_thread_pool_create
    vmaf_thread_pool_destroy
    vmaf_thread_pool_enqueue
    vmaf_thread_pool_wait
    vmaf_use_feature
    vmaf_use_features_from_model
    vmaf_use_features_from_model_collection
    vmaf_use_vmafossexec_aliases
    vmaf_version
    vmaf_write_output
    vmaf_write_output_csv
    vmaf_write_output_json
    vmaf_write_output_sub
    vmaf_write_output_xml
  LIBRARY_NAME LibVMAF
  NAMESPACE "libvmaf_sapi"
)
add_library(sapi_contrib::vmaf ALIAS libvmaf_sapi)
target_include_directories(libvmaf_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)

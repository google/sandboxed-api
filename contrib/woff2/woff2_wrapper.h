#ifndef CONTRIB_WOFF2_WOFF2_WRAPPER_H
#define CONTRIB_WOFF2_WOFF2_WRAPPER_H

#include <cinttypes>
#include <cstddef>
#include <cstdlib>

extern "C" {
bool WOFF2_ConvertWOFF2ToTTF(const uint8_t* data, size_t length,
                             uint8_t** result, size_t* result_length,
                             size_t max_size) noexcept;
bool WOFF2_ConvertTTFToWOFF2(const uint8_t* data, size_t length,
                             uint8_t** result, size_t* result_length) noexcept;
void WOFF2_Free(uint8_t* data) noexcept;
}

#endif  // CONTRIB_WOFF2_WOFF2_WRAPPER_H

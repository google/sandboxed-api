#include <woff2/decode.h>
#include <woff2/encode.h>
#include <woff2/output.h>

#include <cinttypes>
#include <cstddef>
#include <memory>

extern "C" bool WOFF2_ConvertWOFF2ToTTF(const uint8_t* data, size_t length,
                                        uint8_t** result, size_t* result_length,
                                        size_t max_size) noexcept {
  if (result) *result = nullptr;
  if (result_length) *result_length = 0;
  if (!data || !length || !result || !result_length) return false;
  size_t final_size = ::woff2::ComputeWOFF2FinalSize(data, length);
  if (final_size > (max_size ? max_size : woff2::kDefaultMaxSize)) return false;
  std::unique_ptr<uint8_t[]> buffer = std::make_unique<uint8_t[]>(final_size);
  woff2::WOFF2MemoryOut output(buffer.get(), final_size);
  if (!::woff2::ConvertWOFF2ToTTF(data, length, &output)) return false;
  *result = buffer.release();
  *result_length = final_size;
  return true;
}

extern "C" bool WOFF2_ConvertTTFToWOFF2(const uint8_t* data, size_t length,
                                        uint8_t** result,
                                        size_t* result_length) noexcept {
  if (result) *result = nullptr;
  if (result_length) *result_length = 0;
  if (!data || !length || !result || !result_length) return false;
  size_t size = woff2::MaxWOFF2CompressedSize(data, length);
  std::unique_ptr<uint8_t[]> buffer = std::make_unique<uint8_t[]>(size);
  if (!buffer) return false;
  if (!woff2::ConvertTTFToWOFF2(data, length, buffer.get(), &size)) return false;
  *result = buffer.release();
  *result_length = size;
  return true;
}

extern "C" void WOFF2_Free(uint8_t* data) noexcept {
  std::unique_ptr<uint8_t[]> p{data};
}

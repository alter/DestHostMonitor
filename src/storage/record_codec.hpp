#pragma once

#include "types.hpp"

#include <cstddef>
#include <cstdint>

namespace pt {

// Encodes a RawRecord into exactly kRawRecordSize little-endian bytes.
// `out` must point to at least kRawRecordSize bytes.
void encode_record(const RawRecord& r, uint8_t* out);

// Decodes kRawRecordSize bytes written by encode_record.
RawRecord decode_record(const uint8_t* in);

}  // namespace pt

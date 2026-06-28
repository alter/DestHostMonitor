#include "storage/record_codec.hpp"

namespace pt {

namespace {
void put_u16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void put_u32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
uint16_t get_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
uint32_t get_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

void encode_record(const RawRecord& r, uint8_t* out) {
    put_u16(out + 0, r.target_id);
    put_u32(out + 2, r.t_offset_ms);
    out[6] = r.status;
    out[7] = r.reply_ttl;
    put_u16(out + 8, r.rtt_tenths_ms);
    put_u16(out + 10, r.src_id);
}

RawRecord decode_record(const uint8_t* in) {
    RawRecord r;
    r.target_id     = get_u16(in + 0);
    r.t_offset_ms   = get_u32(in + 2);
    r.status        = in[6];
    r.reply_ttl     = in[7];
    r.rtt_tenths_ms = get_u16(in + 8);
    r.src_id        = get_u16(in + 10);
    return r;
}

}  // namespace pt

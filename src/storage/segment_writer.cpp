#include "storage/segment_writer.hpp"

#include "storage/record_codec.hpp"
#include "util/log.hpp"

#include <windows.h>
#include <io.h>

#include <array>
#include <cstring>
#include <filesystem>

namespace pt {

namespace {

constexpr char    kMagic[4]  = {'P', 'T', 'S', 'G'};
constexpr uint16_t kVersion  = 1;

void write_u16(std::FILE* f, uint16_t v) { std::fwrite(&v, sizeof(v), 1, f); }
void write_u32(std::FILE* f, uint32_t v) { std::fwrite(&v, sizeof(v), 1, f); }
void write_u64(std::FILE* f, uint64_t v) { std::fwrite(&v, sizeof(v), 1, f); }

}  // namespace

SegmentWriter::~SegmentWriter() { close(); }

bool SegmentWriter::open(const std::string& dir, uint64_t start_utc_ms,
                         const std::vector<AddrEntry>& addrs) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        log_error("cannot create storage dir '" + dir + "': " + ec.message());
        return false;
    }

    path_ = dir + "/seg_" + std::to_string(start_utc_ms) + ".part";
    file_ = std::fopen(path_.c_str(), "wb+");
    if (!file_) {
        log_error("cannot open segment file '" + path_ + "'");
        return false;
    }
    start_utc_ms_ = start_utc_ms;
    record_count_ = 0;

    // Header (see segment_writer.hpp / plan §4).
    std::fwrite(kMagic, 1, 4, file_);
    write_u16(file_, kVersion);
    write_u16(file_, 0 /*flags*/);
    write_u64(file_, start_utc_ms);
    count_offset_ = std::ftell(file_);
    write_u32(file_, 0 /*record_count, patched on close*/);
    write_u16(file_, static_cast<uint16_t>(addrs.size()));
    for (const auto& a : addrs) {
        write_u16(file_, a.id);
        write_u32(file_, a.addr);
        write_u16(file_, static_cast<uint16_t>(a.name.size()));
        if (!a.name.empty()) std::fwrite(a.name.data(), 1, a.name.size(), file_);
    }
    std::fflush(file_);
    return true;
}

bool SegmentWriter::append(const RawRecord& r) {
    if (!file_) return false;
    std::array<uint8_t, kRawRecordSize> buf{};
    encode_record(r, buf.data());
    if (std::fwrite(buf.data(), 1, buf.size(), file_) != buf.size()) {
        log_error("segment append write failed");
        return false;
    }
    ++record_count_;
    return true;
}

void SegmentWriter::flush() {
    if (!file_) return;
    std::fflush(file_);
    // fsync equivalent on Windows.
    const HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file_)));
    if (h != INVALID_HANDLE_VALUE) FlushFileBuffers(h);
}

void SegmentWriter::close() {
    if (!file_) return;
    // Patch the record count into the header.
    std::fflush(file_);
    if (std::fseek(file_, count_offset_, SEEK_SET) == 0) {
        const uint32_t n = static_cast<uint32_t>(record_count_);
        std::fwrite(&n, sizeof(n), 1, file_);
    }
    std::fflush(file_);
    std::fclose(file_);
    file_ = nullptr;
}

}  // namespace pt

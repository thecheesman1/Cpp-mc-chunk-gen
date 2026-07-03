#ifndef NBT_WRITER_H
#define NBT_WRITER_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

//=============================================================================
// Minimal NBT binary writer for Minecraft chunk data.
// Writes big-endian. No readers — write-only for Anvil output.
//=============================================================================

// Tag types
enum TagType : uint8_t {
    TAG_End        = 0,
    TAG_Byte       = 1,
    TAG_Short      = 2,
    TAG_Int        = 3,
    TAG_Long       = 4,
    TAG_Float      = 5,
    TAG_Double     = 6,
    TAG_Byte_Array = 7,
    TAG_String     = 8,
    TAG_List       = 9,
    TAG_Compound   = 10,
    TAG_Int_Array  = 11,
    TAG_Long_Array = 12,
};

// Forward declaration
class NBTBuffer;

//=============================================================================
// Big-endian helpers
//=============================================================================
inline void be16(uint8_t* buf, uint16_t v) { buf[0] = v >> 8;  buf[1] = v; }
inline void be32(uint8_t* buf, uint32_t v) { buf[0] = v >> 24; buf[1] = v >> 16; buf[2] = v >> 8; buf[3] = v; }
inline void be64(uint8_t* buf, uint64_t v) { for (int i = 7; i >= 0; i--) { buf[i] = v & 0xFF; v >>= 8; } }

//=============================================================================
// NBTBuffer — builds NBT data in a contiguous buffer
//=============================================================================
class NBTBuffer {
public:
    NBTBuffer() { buf_.reserve(65536); }

    const uint8_t* data() const { return buf_.data(); }
    size_t size() const { return buf_.size(); }

    void write_byte(uint8_t v) { buf_.push_back(v); }
    void write_short(uint16_t v) {
        uint8_t tmp[2]; be16(tmp, v);
        buf_.insert(buf_.end(), tmp, tmp + 2);
    }
    void write_int(uint32_t v) {
        uint8_t tmp[4]; be32(tmp, v);
        buf_.insert(buf_.end(), tmp, tmp + 4);
    }
    void write_long(uint64_t v) {
        uint8_t tmp[8]; be64(tmp, v);
        buf_.insert(buf_.end(), tmp, tmp + 8);
    }
    void write_string(const std::string& s) {
        if (s.size() > 65535) throw std::runtime_error("NBT string too long");
        write_short((uint16_t)s.size());
        buf_.insert(buf_.end(), s.begin(), s.end());
    }
    void write_tag_header(TagType type, const std::string& name) {
        write_byte(type);
        write_string(name);
    }
    void write_tag_end() { write_byte(TAG_End); }

    // Compound helpers (open/close)
    void begin_compound(const std::string& name = "") {
        write_tag_header(TAG_Compound, name);
    }
    void end_compound() { write_tag_end(); }

    // List helpers
    void begin_list(const std::string& name, TagType elem_type, uint32_t count) {
        write_tag_header(TAG_List, name);
        write_byte(elem_type);
        write_int(count);
    }
    void end_list() { /* no terminator for lists */ }

    // Convenience tag writers
    void tag_byte(const std::string& name, uint8_t v) {
        write_tag_header(TAG_Byte, name);
        write_byte(v);
    }
    void tag_int(const std::string& name, uint32_t v) {
        write_tag_header(TAG_Int, name);
        write_int(v);
    }
    void tag_string(const std::string& name, const std::string& v) {
        write_tag_header(TAG_String, name);
        write_string(v);
    }
    void tag_long_array(const std::string& name, const uint64_t* data, uint32_t count) {
        write_tag_header(TAG_Long_Array, name);
        write_int(count);
        for (uint32_t i = 0; i < count; i++) write_long(data[i]);
    }
    void tag_byte_array(const std::string& name, const uint8_t* data, uint32_t count) {
        write_tag_header(TAG_Byte_Array, name);
        write_int(count);
        buf_.insert(buf_.end(), data, data + count);
    }

    // Raw data insertion (for compressed data)
    void write_raw(const uint8_t* data, size_t len) {
        buf_.insert(buf_.end(), data, data + len);
    }

private:
    std::vector<uint8_t> buf_;
};

#endif // NBT_WRITER_H

#pragma once

#include <cstdint>
#include <cstddef>

// WiiXLaunch::BotW::SARC - just enough of Nintendo's SARC archive format to
// find a named file inside one: header, SFAT node table, name hashing. No
// SFNT string walking - the lookup is by hash, which is what the game itself
// does, and it means the whole thing works on the first ~20 KB of an archive
// (header + SFAT) without ever needing the name table or the data.
//
// Layout (big-endian on Wii U):
//   0x00 "SARC" u16 headerSize(0x14) u16 BOM u32 fileSize u32 dataOffset u16 version u16 pad
//   0x14 "SFAT" u16 headerSize(0x0C) u16 nodeCount u32 hashMultiplier
//   0x20 nodes: u32 nameHash, u32 attributes, u32 dataStart, u32 dataEnd  (x nodeCount, sorted by hash)
//   then "SFNT" and the names, then file data at dataOffset + dataStart.
//
// Freestanding, no allocation.

namespace WiiXLaunch::BotW::SARC {

constexpr uint32_t kHeaderSize = 0x14;
constexpr uint32_t kSfatHeaderSize = 0x0C;
constexpr uint32_t kNodeSize = 0x10;
constexpr uint32_t kDefaultHashMultiplier = 0x65;

inline uint32_t ReadU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline uint16_t ReadU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

// The archive's own name hash: h = h * multiplier + c over the bytes of the
// path as stored in the archive (e.g. "timg/Nt_Cursor_00^t.bflim").
inline uint32_t Hash(const char* name, uint32_t multiplier = kDefaultHashMultiplier) {
    uint32_t h = 0;
    for (const char* p = name; *p; ++p) h = h * multiplier + static_cast<uint8_t>(*p);
    return h;
}

struct Header {
    uint32_t fileSize = 0;
    uint32_t dataOffset = 0;      // absolute offset of the data region
    uint32_t nodeCount = 0;
    uint32_t hashMultiplier = kDefaultHashMultiplier;
    bool valid = false;

    // Bytes from the start of the archive needed to hold the SFAT node table.
    uint32_t NodeTableEnd() const { return kHeaderSize + kSfatHeaderSize + nodeCount * kNodeSize; }
};

// Parses the SARC + SFAT headers out of the first bytes of an archive.
// Needs at least 0x20 bytes.
inline bool ParseHeader(const uint8_t* buf, size_t length, Header& out) {
    out = Header{};
    if (!buf || length < kHeaderSize + kSfatHeaderSize) return false;
    if (buf[0] != 'S' || buf[1] != 'A' || buf[2] != 'R' || buf[3] != 'C') return false;
    if (buf[0x14] != 'S' || buf[0x15] != 'F' || buf[0x16] != 'A' || buf[0x17] != 'T') return false;
    // BOM: Wii U archives are big-endian (FE FF). A little-endian archive
    // (Switch, FF FE) is not something this reader handles.
    if (!(buf[6] == 0xFE && buf[7] == 0xFF)) return false;
    out.fileSize = ReadU32BE(buf + 0x08);
    out.dataOffset = ReadU32BE(buf + 0x0C);
    // SFAT: magic (0x14), u16 headerSize (0x18), u16 nodeCount (0x1A), u32 multiplier (0x1C).
    // (An earlier revision read the node count from 0x18 - the header size, always 12 -
    // and every lookup past the first dozen nodes silently failed in-game.)
    out.nodeCount = ReadU16BE(buf + 0x1A);
    out.hashMultiplier = ReadU32BE(buf + 0x1C);
    out.valid = true;
    return true;
}

// Binary-searches the node table (nodes are stored sorted by hash) for the
// file with the given hash. `buf` must cover the archive up to
// header.NodeTableEnd(). On success returns the file's ABSOLUTE byte range
// [outOffset, outOffset + outSize) within the archive.
inline bool FindByHash(const uint8_t* buf, size_t length, const Header& header, uint32_t hash,
                       uint32_t& outOffset, uint32_t& outSize) {
    if (!header.valid || !buf || length < header.NodeTableEnd()) return false;
    const uint8_t* nodes = buf + kHeaderSize + kSfatHeaderSize;
    uint32_t lo = 0;
    uint32_t hi = header.nodeCount;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        const uint8_t* node = nodes + mid * kNodeSize;
        const uint32_t h = ReadU32BE(node);
        if (h == hash) {
            const uint32_t start = ReadU32BE(node + 8);
            const uint32_t end = ReadU32BE(node + 12);
            if (end < start) return false;
            outOffset = header.dataOffset + start;
            outSize = end - start;
            return true;
        }
        if (h < hash) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

inline bool Find(const uint8_t* buf, size_t length, const Header& header, const char* name,
                 uint32_t& outOffset, uint32_t& outSize) {
    return FindByHash(buf, length, header, Hash(name, header.hashMultiplier), outOffset, outSize);
}

} // namespace WiiXLaunch::BotW::SARC

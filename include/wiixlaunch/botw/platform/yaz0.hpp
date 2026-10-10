#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

// WiiXLaunch::BotW::Yaz0 - a STREAMING Yaz0 decoder.
//
// Why streaming: the assets the GUI borrows from the base game live inside
// Yaz0-compressed SARCs that are far bigger than anything this module can
// afford to hold in memory (Layout/Common.sblarc is 6 MB compressed and
// 31 MB decompressed; the Cemu payload's whole heap is under 4 MB). Yaz0 back-
// references reach at most 0x1000 bytes behind the output cursor, so a
// 4 KB history window is all the decoder state there is - compressed bytes
// can be fed in arbitrary chunks and the decompressed bytes are handed to a
// sink as they are produced, in order, tagged with their output offset. A
// sink that only wants a few byte ranges out of the archive copies those and
// discards the rest, so a 31 MB archive costs 4 KB of RAM plus whatever is
// actually kept.
//
// Format (all big-endian): "Yaz0", u32 decompressed size, 8 reserved bytes,
// then groups of one code byte followed by 8 chunks (MSB first): a 1 bit is a
// literal byte, a 0 bit is a back-reference of two bytes NR RR (N = count-2,
// or if N == 0 a third byte holds count-0x12; RRR = distance-1).
//
// Freestanding: no allocation, no libc beyond memcpy.

namespace WiiXLaunch::BotW::Yaz0 {

constexpr uint32_t kHeaderSize = 16;
constexpr uint32_t kWindowSize = 0x1000;

// Called with a run of decompressed bytes at absolute output offset `offset`.
// Runs are delivered in strictly increasing offset order with no gaps.
using Sink = void (*)(void* user, uint32_t offset, const uint8_t* data, uint32_t length);

class StreamDecoder {
public:
    void Reset(Sink sink, void* user) {
        m_Sink = sink;
        m_User = user;
        m_State = State::Header;
        m_HeaderFilled = 0;
        m_DecompressedSize = 0;
        m_OutPos = 0;
        m_CodeByte = 0;
        m_CodeBitsLeft = 0;
        m_Ref1 = 0;
        m_CopyLeft = 0;
        m_CopyDist = 0;
        m_EmitLen = 0;
        m_Error = false;
        memset(m_Window, 0, sizeof(m_Window));
    }

    bool HasError() const { return m_Error; }
    bool IsDone() const { return m_State != State::Header && m_OutPos >= m_DecompressedSize; }
    uint32_t DecompressedSize() const { return m_DecompressedSize; }
    uint32_t OutputPosition() const { return m_OutPos; }

    // Feeds `length` compressed bytes. Returns false once the stream is in
    // error (bad magic) - further calls are ignored. Bytes past the end of
    // the stream are ignored too.
    bool Feed(const uint8_t* data, size_t length) {
        if (m_Error) return false;
        size_t i = 0;
        while (i < length) {
            if (m_State == State::Header) {
                while (i < length && m_HeaderFilled < kHeaderSize) m_Header[m_HeaderFilled++] = data[i++];
                if (m_HeaderFilled < kHeaderSize) return true;
                if (m_Header[0] != 'Y' || m_Header[1] != 'a' || m_Header[2] != 'z' || m_Header[3] != '0') {
                    m_Error = true;
                    return false;
                }
                m_DecompressedSize = (static_cast<uint32_t>(m_Header[4]) << 24) | (static_cast<uint32_t>(m_Header[5]) << 16) |
                                     (static_cast<uint32_t>(m_Header[6]) << 8) | static_cast<uint32_t>(m_Header[7]);
                m_State = State::Code;
                continue;
            }
            if (m_OutPos >= m_DecompressedSize) { Flush(); return true; }

            const uint8_t b = data[i++];
            switch (m_State) {
            case State::Code:
                m_CodeByte = b;
                m_CodeBitsLeft = 8;
                m_State = State::Chunk;
                break;
            case State::Chunk:
                // Decide what this byte starts: called only when a new chunk begins.
                if (m_CodeByte & 0x80) {
                    Emit(b);
                    NextChunk();
                } else {
                    m_Ref1 = b;
                    m_State = State::Ref2;
                }
                break;
            case State::Ref2: {
                const uint32_t dist = ((static_cast<uint32_t>(m_Ref1 & 0x0F) << 8) | b) + 1;
                const uint32_t n = static_cast<uint32_t>(m_Ref1 >> 4);
                m_CopyDist = dist;
                if (n == 0) {
                    m_State = State::Ref3;
                } else {
                    m_CopyLeft = n + 2;
                    RunCopy();
                    NextChunk();
                }
                break;
            }
            case State::Ref3:
                m_CopyLeft = static_cast<uint32_t>(b) + 0x12;
                RunCopy();
                NextChunk();
                break;
            default:
                break;
            }
        }
        Flush();
        return true;
    }

    // Hands any buffered output to the sink. Feed() does this itself at the
    // end of every call, so this only matters if a caller wants the bytes
    // before feeding more.
    void Flush() {
        if (m_EmitLen && m_Sink) {
            m_Sink(m_User, m_OutPos - m_EmitLen, m_Emit, m_EmitLen);
        }
        m_EmitLen = 0;
    }

private:
    enum class State : uint8_t { Header, Code, Chunk, Ref2, Ref3 };

    void NextChunk() {
        m_CodeByte = static_cast<uint8_t>(m_CodeByte << 1);
        if (--m_CodeBitsLeft == 0) m_State = State::Code;
        else m_State = State::Chunk;
    }

    void Emit(uint8_t v) {
        if (m_OutPos >= m_DecompressedSize) return;
        m_Window[m_OutPos & (kWindowSize - 1)] = v;
        m_Emit[m_EmitLen++] = v;
        ++m_OutPos;
        if (m_EmitLen == sizeof(m_Emit)) Flush();
    }

    void RunCopy() {
        while (m_CopyLeft > 0 && m_OutPos < m_DecompressedSize) {
            const uint8_t v = m_Window[(m_OutPos - m_CopyDist) & (kWindowSize - 1)];
            Emit(v);
            --m_CopyLeft;
        }
        m_CopyLeft = 0;
    }

    Sink m_Sink = nullptr;
    void* m_User = nullptr;
    State m_State = State::Header;
    uint8_t m_Header[kHeaderSize]{};
    uint32_t m_HeaderFilled = 0;
    uint32_t m_DecompressedSize = 0;
    uint32_t m_OutPos = 0;
    uint8_t m_CodeByte = 0;
    uint8_t m_CodeBitsLeft = 0;
    uint8_t m_Ref1 = 0;
    uint32_t m_CopyLeft = 0;
    uint32_t m_CopyDist = 0;
    uint8_t m_Window[kWindowSize]{};
    uint8_t m_Emit[1024]{};
    uint32_t m_EmitLen = 0;
    bool m_Error = false;
};

// Copies the part of a delivered run that falls inside [rangeStart,
// rangeStart + rangeLength) to dst + (offset - rangeStart). Returns the
// number of bytes copied. The building block every range-extracting sink
// is made of.
inline uint32_t CopyOverlap(uint32_t rangeStart, uint32_t rangeLength, uint8_t* dst,
                            uint32_t offset, const uint8_t* data, uint32_t length) {
    const uint32_t rangeEnd = rangeStart + rangeLength;
    const uint32_t runEnd = offset + length;
    if (!dst || runEnd <= rangeStart || offset >= rangeEnd) return 0;
    const uint32_t from = offset > rangeStart ? offset : rangeStart;
    const uint32_t to = runEnd < rangeEnd ? runEnd : rangeEnd;
    memcpy(dst + (from - rangeStart), data + (from - offset), to - from);
    return to - from;
}

} // namespace WiiXLaunch::BotW::Yaz0

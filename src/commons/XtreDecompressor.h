#pragma once

#include <cstdint>
#include <cstddef>

// XTRE decompression routines reversed from wc3.exe (Origin Systems, 1994).
// Used by Wing Commander 3/4 XTRE archive format.
class XtreDecompressor {
public:
    // LZ77-variant sliding-window decompressor (TYPE1).
    // Returns number of bytes decompressed.
    static size_t decompressType1(const uint8_t* src, uint8_t* dst, size_t srcUpperBound);

    // LZW dictionary decompressor (TYPE2).
    // Returns number of bytes decompressed.
    static size_t decompressType2(const uint8_t* src, uint8_t* dst, size_t uncompressedSize);

    // Auto-detect and decompress. Returns bytes decompressed.
    static size_t decompress(const uint8_t* src, uint8_t* dst, size_t uncompressedSize);
};

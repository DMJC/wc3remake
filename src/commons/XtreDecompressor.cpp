#include "XtreDecompressor.h"

// TYPE1: LZ77-variant sliding-window decompressor.
// Reversed from WC3.O _xtre_decompress (Origin Systems, wc3.exe).
//
// Control byte bit patterns:
//   0xxxxxxx  - short back-ref (10-bit offset, length 3-10)
//   10xxxxxx  - medium back-ref (14-bit offset, length 4-67)
//   110xxxxx  - long back-ref (17-bit offset, variable length)
//   111xxxxx  - literal block; count > 112 signals end-of-stream
size_t XtreDecompressor::decompressType1(const uint8_t* src, uint8_t* dst, size_t srcUpperBound) {
    uint8_t* dstStart = dst;
    const uint8_t* srcEnd = src + srcUpperBound;

    // Bounded by srcEnd directly (not a blanket "-7 bytes early" margin — that
    // stopped the loop as soon as fewer than 7 bytes remained, even when the
    // next control byte was a perfectly ordinary back-reference needing only
    // 3-4 of them, silently dropping the rest of the stream, up to ~30 bytes
    // in practice). Each branch instead checks its own actual byte need,
    // including its variable literal-run length, right before consuming it.
    while (src < srcEnd) {
        uint8_t ctrl = *src++;

        if (!(ctrl & 0x80)) {
            if (src >= srcEnd) break;
            uint8_t next = *src++;
            int lit = ctrl & 0x03;
            if (src + lit > srcEnd) break;
            while (lit-- > 0) *dst++ = *src++;

            int offset = ((ctrl & 0x60) << 3) + next + 1;
            int length = ((ctrl & 0x1C) >> 2) + 3;
            uint8_t* ref = dst - offset;
            while (length-- > 0) *dst++ = *ref++;

        } else if (!(ctrl & 0x40)) {
            if (src + 2 > srcEnd) break;
            uint8_t b0 = *src++;
            uint8_t b1 = *src++;
            int lit = b0 >> 6;
            if (src + lit > srcEnd) break;
            while (lit-- > 0) *dst++ = *src++;

            int offset = ((b0 & 0x3F) << 8) + b1 + 1;
            int length = (ctrl & 0x3F) + 4;
            uint8_t* ref = dst - offset;
            while (length-- > 0) *dst++ = *ref++;

        } else if (!(ctrl & 0x20)) {
            if (src + 3 > srcEnd) break;
            uint8_t b0 = *src++;
            uint8_t b1 = *src++;
            uint8_t b2 = *src++;
            int lit = ctrl & 0x03;
            if (src + lit > srcEnd) break;
            while (lit-- > 0) *dst++ = *src++;

            int offset = ((ctrl & 0x10) << 12) + (b0 << 8) + b1 + 1;
            int length = b2 + 5 + ((ctrl & 0x0C) << 6);
            uint8_t* ref = dst - offset;
            while (length-- > 0) *dst++ = *ref++;

        } else {
            int count = ((ctrl & 0x1F) << 2) + 4;
            if (count > 112) {
                int tail = ctrl & 0x03;
                while (tail-- > 0 && src < srcEnd) *dst++ = *src++;
                break;
            }
            if (src + count > srcEnd) break;
            while (count-- > 0) *dst++ = *src++;
        }
    }

    return dst - dstStart;
}

// TYPE2: LZW dictionary decompressor.
// Reversed from WC3-2.O _xtre_decompress_2 (Origin Systems, wc3.exe).
//
// Variable-width codes starting at 9 bits, growing to 12.
// Code 0x100 = reset dictionary, 0x101 = end of stream.
// Codes are packed LSB-first in the byte stream.
size_t XtreDecompressor::decompressType2(const uint8_t* src, uint8_t* dst, size_t /*uncompressedSize*/) {
    uint8_t* dstStart = dst;
    const uint8_t* rp = src;
    int bitOff = 0;

    int codeBits  = 9;
    int codeMask  = 0x1FF;
    int nextBump  = 0x200;
    int nextFree  = 0x102;
    int prevCode  = 0;
    int lastChar  = 0;

    uint16_t dictPrefix[4096];
    uint8_t  dictSuffix[4096];
    uint8_t  stack[4096];

    auto readCode = [&]() -> int {
        uint32_t bits = rp[0] | (rp[1] << 8) | (rp[2] << 16);
        bits >>= bitOff;
        int code = bits & codeMask;
        int total = bitOff + codeBits;
        rp += total >> 3;
        bitOff = total & 7;
        return code;
    };

    for (;;) {
        int code = readCode();
        if (code == 0x101) break;

        if (code == 0x100) {
            codeBits = 9;
            codeMask = 0x1FF;
            nextBump = 0x200;
            nextFree = 0x102;
            code = readCode();
            if (code == 0x101) break;
            prevCode = code;
            lastChar = code;
            *dst++ = static_cast<uint8_t>(code);
            continue;
        }

        int newCode = code;
        int sp = 0;

        if (code >= nextFree) {
            stack[sp++] = static_cast<uint8_t>(lastChar);
            code = prevCode;
        }

        while (code >= 256) {
            stack[sp++] = dictSuffix[code];
            code = dictPrefix[code];
        }

        lastChar = code;
        *dst++ = static_cast<uint8_t>(code);
        for (int i = sp - 1; i >= 0; i--)
            *dst++ = stack[i];

        if (nextFree < 4096) {
            dictPrefix[nextFree] = static_cast<uint16_t>(prevCode);
            dictSuffix[nextFree] = static_cast<uint8_t>(lastChar);
            nextFree++;
        }

        prevCode = newCode;

        if (nextFree >= nextBump && codeBits < 12) {
            codeBits++;
            nextBump <<= 1;
            codeMask = (codeMask << 1) | 1;
        }
    }

    return dst - dstStart;
}

size_t XtreDecompressor::decompress(const uint8_t* src, uint8_t* dst, size_t uncompressedSize) {
    if ((src[0] & 0xE0) == 0xE0)
        return decompressType1(src, dst, uncompressedSize);
    else
        return decompressType2(src, dst, uncompressedSize);
}

#include "WC3MusicPak.h"
#include <cstring>
#include <cstdio>
#include <stdexcept>

uint32_t WC3MusicPak::u32le(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool WC3MusicPak::load(const uint8_t* data, size_t size) {
    rawTracks.clear();
    midiCache.clear();
    if (size < 12 || memcmp(data, "FORM", 4) != 0) return false;

    // FORM SOND > FORM MIDI > SOND — walk down to the SOND chunk's payload,
    // which holds the back-to-back [index][size]HMIMIDIP... track records.
    if (memcmp(data + 8, "SOND", 4) != 0) return false;
    size_t pos = 12;
    if (pos + 12 > size || memcmp(data + pos, "FORM", 4) != 0) return false;
    if (memcmp(data + pos + 8, "MIDI", 4) != 0) return false;
    pos += 12;
    if (pos + 8 > size || memcmp(data + pos, "SOND", 4) != 0) return false;
    uint32_t sondSize = (data[pos+4]<<24)|(data[pos+5]<<16)|(data[pos+6]<<8)|data[pos+7]; // big-endian IFF size
    size_t payloadStart = pos + 8;
    size_t payloadEnd = std::min(size, payloadStart + (size_t)sondSize);

    ownedData.assign(data, data + size);
    const uint8_t* d = ownedData.data();

    size_t p = payloadStart;
    while (p + 8 <= payloadEnd) {
        uint32_t index = u32le(d + p);
        uint32_t trackSize = u32le(d + p + 4);
        p += 8;
        if (p + trackSize > payloadEnd || trackSize < 8) break;
        if (memcmp(d + p, "HMIMIDIP", 8) != 0) {
            printf("WC3MusicPak: track %u missing HMIMIDIP signature, stopping scan\n", index);
            break;
        }
        rawTracks[(int)index] = { p, trackSize };
        p += trackSize;
    }

    printf("WC3MusicPak: loaded %zu tracks\n", rawTracks.size());
    return !rawTracks.empty();
}

const std::vector<uint8_t>* WC3MusicPak::getTrackMidi(int index) {
    auto cached = midiCache.find(index);
    if (cached != midiCache.end()) return &cached->second;

    auto it = rawTracks.find(index);
    if (it == rawTracks.end()) return nullptr;

    std::vector<uint8_t> midi;
    if (!convertHmiToMidi(ownedData.data() + it->second.offset, it->second.size, midi)) {
        printf("WC3MusicPak: failed to convert track %d to MIDI\n", index);
        return nullptr;
    }
    auto& slot = midiCache[index];
    slot = std::move(midi);
    return &slot;
}

// ---------------------------------------------------------------------------
// HMI SOS "HMIMIDIP" -> Standard MIDI File conversion.
//
// C++ port of the reference algorithm from
// https://github.com/NRS-NewRisingSun/hmi2mid (hmimidip.cpp), validated
// against real WC3 GMMUSIC.IFF track data (converted output parsed cleanly
// with the `mido` Python MIDI library: correct program changes, note on/off,
// plausible timing, matching declared track count) before porting.
//
// HMIMIDIP layout (offsets relative to the start of one track's blob):
//   0x00            "HMIMIDI" signature (+ 1 more byte, "HMIMIDIP")
//   0x30            numberOfTracks : u32LE
//   0x34            timeBase (MIDI ticks per quarter note) : u32LE
//   0x38            ticksPerSecond (HMI's own internal clock rate) : u32LE
//   0x3C            songDuration : u32LE
//   0x40..0x80      per-channel priority table, 4 bytes/channel (16 channels)
//   0x80..          per-track device-routing table, 20 bytes/track (5 slots
//                   of [device:u8][marker:u8][pad:u16]; marker 0xA0 = valid)
//   0x308           first per-track sub-header:
//                     [trackNumber:u32LE][trackLength:u32LE][channel:u32LE]
//                   followed by that track's HMI event stream.
//
// HMI event stream: a variable-length delta-time (7 bits/byte, continuation
// bit = top bit set, same shape as MIDI VLQ but little-endian bit order) then
// a standard MIDI channel/meta/sysex event, EXCEPT controller events with
// controller numbers 102-119 (excluding 110/111, which are HMI's loop
// start/end markers) are HMI-internal control commands with no MIDI
// equivalent and must be dropped rather than copied. HMI's delta-time unit is
// converted to a fixed 120-tick, 500000us/quarter-note (120 BPM) MIDI clock
// via ticksPerSecond, carrying a running remainder for precision.
// ---------------------------------------------------------------------------
bool WC3MusicPak::convertHmiToMidi(const uint8_t* hmi, size_t hmiSize, std::vector<uint8_t>& out) {
    if (hmiSize < 0x310 || memcmp(hmi, "HMIMIDI", 7) != 0) return false;

    auto at = [&](size_t off) -> uint8_t {
        if (off >= hmiSize) throw std::out_of_range("HMI read past end");
        return hmi[off];
    };
    auto u32 = [&](size_t off) -> uint32_t {
        return at(off) | (at(off+1)<<8) | (at(off+2)<<16) | ((uint32_t)at(off+3)<<24);
    };

    try {
        uint32_t numberOfTracks = u32(0x30);
        uint32_t timeBase       = u32(0x34);
        uint32_t ticksPerSecond = u32(0x38);
        if (ticksPerSecond == 0 || numberOfTracks == 0 || numberOfTracks > 64) return false;
        const uint8_t* tracksPriority = hmi + 0x40;
        const uint8_t* tracksDevices  = hmi + 0x80;
        size_t position = 0x308;

        out.clear();
        auto push = [&](uint8_t b) { out.push_back(b); };
        auto pushStr = [&](const char* s, size_t n) { for (size_t i=0;i<n;i++) out.push_back((uint8_t)s[i]); };

        pushStr("MThd", 4);
        push(0); push(0); push(0); push(6);
        push(0); push(1);
        push((numberOfTracks>>8)&0xFF); push(numberOfTracks&0xFF);
        push((timeBase>>8)&0xFF); push(timeBase&0xFF);

        uint32_t includedTracks = 0;
        for (uint32_t trackNumber = 0; trackNumber < numberOfTracks; trackNumber++) {
            size_t trackStartPosition = position;
            uint32_t foundTrackNumber = u32(position + 0);
            uint32_t trackLength      = u32(position + 4);
            uint32_t midiChannelNumber= u32(position + 8);
            if (foundTrackNumber != trackNumber)
                printf("WC3MusicPak: found track #%u differs from expected #%u\n", foundTrackNumber, trackNumber);
            position += 12;

            // Device-routing/priority bookkeeping (matches reference; only used
            // here to build the informational track-name meta-event).
            std::string deviceNames = "[";
            const uint8_t* trackDevices = tracksDevices + (size_t)trackNumber * 20;
            for (int i = 0; i < 5; i++) {
                if (at((trackDevices - hmi) + i*4 + 1) == 0xA0) {
                    uint8_t device = at((trackDevices - hmi) + i*4 + 0);
                    switch (device) {
                        case 0: deviceNames += "G"; break;
                        case 2: deviceNames += "F"; break;
                        case 3: deviceNames += "C"; break;
                        case 4: deviceNames += "M"; break;
                        case 5: deviceNames += "D"; break;
                        case 6: deviceNames += "I"; break;
                        case 7: deviceNames += "W"; break;
                        case 10: deviceNames += "U"; break;
                        default: break;
                    }
                }
            }
            uint32_t priority = (midiChannelNumber < 16)
                ? (tracksPriority[midiChannelNumber*4+0] | tracksPriority[midiChannelNumber*4+1]<<8 |
                   tracksPriority[midiChannelNumber*4+2]<<16 | (uint32_t)tracksPriority[midiChannelNumber*4+3]<<24)
                : 0;
            if (priority != 0 && trackNumber > 0) deviceNames += (char)(priority + '0');
            deviceNames += "]";

            // Always include every track (matches reference's default "no
            // --device filter" behavior — we want the full General MIDI mix).
            pushStr("MTrk", 4);
            push(0); push(0); push(0); push(0); // length placeholder
            size_t MTrkStart = out.size();

            if (trackNumber == 0) {
                push(0x00); push(0xFF); push(0x51); push(0x03);
                uint32_t smfTempo = 500000; // 120 BPM
                push((smfTempo>>16)&0xFF); push((smfTempo>>8)&0xFF); push(smfTempo&0xFF);
            }
            if (deviceNames.size() > 2) {
                push(0x00); push(0xFF); push(0x03); push((uint8_t)deviceNames.size());
                pushStr(deviceNames.data(), deviceNames.size());
            }

            uint8_t lastStatus = 0;
            uint32_t waitTimeCumulative = 0;
            uint32_t rest = 0;
            bool finished = false;
            while (!finished) {
                uint32_t waitTimeRaw = 0;
                int shift = 0;
                while (true) {
                    uint8_t c = at(position++);
                    waitTimeRaw |= (uint32_t)(c & 0x7F) << shift;
                    shift += 7;
                    if (c & 0x80) break;
                }
                waitTimeCumulative += waitTimeRaw;

                // HMI-internal loop/control commands on controllers 102-119
                // (excluding 110/111, the loop start/end markers) have no MIDI
                // equivalent — skip the whole 3-byte command and keep waiting.
                uint8_t peek0 = at(position + 0);
                uint8_t peek1 = at(position + 1);
                if (peek0 >= 0xB0 && peek0 <= 0xBF && peek1 >= 102 && peek1 <= 119 && peek1 != 110 && peek1 != 111) {
                    position += 3;
                    continue;
                }

                uint64_t scaled = (uint64_t)waitTimeCumulative * 2 * timeBase + rest;
                uint32_t waitTime = (uint32_t)(scaled / ticksPerSecond);
                rest = (uint32_t)(scaled % ticksPerSecond);
                int a = (waitTime & 0xf0000000) >> 28;
                int b = (waitTime & 0x0fe00000) >> 21;
                int c2 = (waitTime & 0x001fc000) >> 14;
                int d = (waitTime & 0x00003f80) >> 7;
                int e = (waitTime & 0x0000007f);
                if (a) push((uint8_t)(a | 0x80));
                if (a||b) push((uint8_t)(b | 0x80));
                if (a||b||c2) push((uint8_t)(c2 | 0x80));
                if (a||b||c2||d) push((uint8_t)(d | 0x80));
                push((uint8_t)e);
                waitTimeCumulative = 0;

                uint8_t command = at(position++);
                if (!(command & 0x80)) command = lastStatus;
                else lastStatus = command;

                if (command >= 0xB0 && command <= 0xBF && (at(position) == 110 || at(position) == 111)) {
                    const char* markerText = (at(position) == 111) ? "Loop end" : "Loop start";
                    size_t len = strlen(markerText);
                    push(0xFF); push(0x01); push((uint8_t)len);
                    pushStr(markerText, len);
                    position += 2;
                    continue;
                }

                push(command);
                if (command == 0xFF) {
                    uint8_t metaType = at(position++);
                    push(metaType);
                    uint32_t len = at(position++);
                    push((uint8_t)len);
                    while (len--) push(at(position++));
                    finished = (metaType == 0x2F);
                } else if (command == 0xF0) {
                    uint32_t len = at(position++);
                    push((uint8_t)len);
                    while (len--) push(at(position++));
                } else {
                    if (command > 0xF0) {
                        printf("WC3MusicPak: unknown command byte %02X\n", command);
                        return false;
                    }
                    push(at(position++));
                    if (command < 0xC0 || command >= 0xE0) push(at(position++));
                }
            }

            size_t trackSize = out.size() - MTrkStart;
            out[MTrkStart-4+0] = (trackSize>>24)&0xFF;
            out[MTrkStart-4+1] = (trackSize>>16)&0xFF;
            out[MTrkStart-4+2] = (trackSize>>8)&0xFF;
            out[MTrkStart-4+3] = trackSize&0xFF;
            includedTracks++;
            position = trackStartPosition + trackLength;
        }
        return includedTracks > 0;
    } catch (const std::out_of_range&) {
        return false;
    }
}

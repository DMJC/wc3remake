#pragma once
#include <cstdint>
#include <cstddef>
#include <unordered_map>

class RSImageSet;

// WC3 reuses a single "1.11"-versioned shape-pak format for both its
// GAMEFLOW room-sprite paks and its cockpit background/instrument art
// (DATA\COCKPITS\*.IFF's SHAP chunk). One entry always starts with the
// literal ASCII marker "1.11", followed by a frame count and a per-frame
// offset table; see RSWC3DecodeShapeEntry for the exact layout.
//
// `data` must point at the "1.11" marker itself. Never returns null (an
// empty RSImageSet is returned for malformed input, matching the rest of
// this codebase's tolerant-parser convention).
RSImageSet* RSWC3DecodeShapeEntry(const uint8_t* data, size_t size);

// Decodes a flat, back-to-back sequence of [id:u32][bodySize:u32]["1.11"
// entry of bodySize bytes] records into an id-keyed map of RSImageSet. This
// is the layout of a cockpit IFF's top-level SHAP chunk.
std::unordered_map<uint32_t, RSImageSet*> RSWC3DecodeShapePak(const uint8_t* data, size_t size);

#include "XtreArchive.h"
#include "../commons/XtreDecompressor.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <map>

XtreArchive::XtreArchive()
    : valid(false), data(nullptr), dataSize(0), initializedFromFile(false) {
    this->path[0] = '\0';
}

XtreArchive::~XtreArchive() {
    for (auto* entry : entries) {
        if (entry->data) {
            delete[] entry->data;
        }
        delete entry;
    }
}

uint32_t XtreArchive::readU32LE(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

uint32_t XtreArchive::xtreHash(const char* str) {
    uint32_t h = 0;
    while (*str) {
        h = ((h << 3) | (h >> 29)) + static_cast<uint8_t>(*str);
        str++;
    }
    return h;
}

std::string XtreArchive::pathToRelative(const std::string& path) {
    std::string normalized = path;
    for (char& c : normalized)
        if (c == '\\') c = '/';

    size_t last_slash = normalized.rfind('/');
    if (last_slash == std::string::npos)
        return normalized;

    size_t second_last = normalized.rfind('/', last_slash - 1);
    if (second_last == std::string::npos)
        return normalized;

    return normalized.substr(second_last + 1);
}

bool XtreArchive::InitFromFile(const char* filepath) {
    char fullPath[512];
    fullPath[0] = '\0';
    strcat(fullPath, GetBase());
    strcat(fullPath, filepath);

    FILE* file = fopen(fullPath, "rb");
    if (!file) {
        printf("Unable to open XTRE archive: '%s'.\n", filepath);
        return false;
    }

    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint8_t* fileData = new uint8_t[fileSize];
    fread(fileData, 1, fileSize, file);
    fclose(file);

    this->initializedFromFile = true;
    InitFromRAM(filepath, fileData, fileSize);
    return this->valid;
}

void XtreArchive::InitFromRAM(const char* name, uint8_t* data, size_t size) {
    strncpy(this->path, name, sizeof(this->path) - 1);
    this->path[sizeof(this->path) - 1] = '\0';
    this->data = data;
    this->dataSize = size;
    Parse();
}

// Build name map by parsing section 2 sequentially.
// This catches collision-chained entries that section 1 only points to the first of.
void XtreArchive::BuildNameMap(uint32_t sec1Off, uint32_t sec2Off, uint32_t sec3Off, uint32_t dataOff,
                                std::map<uint32_t, std::string>& s3offToName) {
    uint32_t pos = sec2Off;
    while (pos + 5 <= sec3Off) {
        uint8_t nameLen = data[pos];
        if (pos + 1 + nameLen + 4 > sec3Off) break;
        std::string rawName(reinterpret_cast<char*>(&data[pos + 1]), nameLen);
        uint32_t s3ptr = readU32LE(&data[pos + 1 + nameLen]);
        s3offToName[s3ptr] = pathToRelative(rawName);
        pos += 1 + nameLen + 4;
    }
}

// Resolve unnamed files using a companion .txt file with hash-to-name mappings.
// Format: "HEXHASH  path" per line, comments start with #.
void XtreArchive::ResolveNamesFromFile(uint32_t sec1Off, uint32_t sec2Off, uint32_t sec3Off, uint32_t dataOff,
                                        std::map<uint32_t, std::string>& s3offToName) {
    // Build CRC -> s3ptr map from section 1 for entries not yet named
    std::map<uint32_t, uint32_t> crcToS3off;
    uint32_t s1Count = (sec2Off - sec1Off) / 8;
    for (uint32_t i = 0; i < s1Count; i++) {
        uint32_t off = sec1Off + i * 8;
        uint32_t crc = readU32LE(&data[off]);
        uint32_t ptr = readU32LE(&data[off + 4]);
        if (crc == 0 && ptr == 0xFFFFFFFF) continue;

        uint32_t s3ptr;
        if (ptr >= sec2Off && ptr < sec3Off) {
            uint8_t nl = data[ptr];
            if (ptr + 1 + nl + 4 <= sec3Off)
                s3ptr = readU32LE(&data[ptr + 1 + nl]);
            else
                continue;
        } else if (ptr >= sec3Off && ptr < dataOff) {
            s3ptr = ptr;
        } else {
            continue;
        }

        if (s3offToName.find(s3ptr) == s3offToName.end())
            crcToS3off[crc] = s3ptr;
    }

    if (crcToS3off.empty()) return;

    // Construct .txt filename: UPPERCASE(basename).TRE.txt alongside the TRE
    std::string txtPath;
    {
        std::string base(GetBase());
        if (!base.empty() && base.back() != '/' && base.back() != '\\')
            base += '/';
        std::string fn(this->path);
        size_t slash = fn.find_last_of("/\\");
        if (slash != std::string::npos) fn = fn.substr(slash + 1);
        std::transform(fn.begin(), fn.end(), fn.begin(), ::toupper);
        // Ensure .TRE extension
        size_t dot = fn.rfind('.');
        if (dot != std::string::npos)
            fn = fn.substr(0, dot);
        txtPath = base + fn + ".TRE.txt";
    }

    FILE* f = fopen(txtPath.c_str(), "r");
    if (!f) {
        printf("XTRE: No name file '%s', %zu CRCs unresolved in '%s'\n",
               txtPath.c_str(), crcToS3off.size(), this->path);
        return;
    }

    char line[512];
    int resolved = 0;
    while (fgets(line, sizeof(line), f)) {
        // Skip comments and blank lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        // Parse: "HEXHASH  path" or "          path" (empty hash = collision chain, already resolved)
        char hashStr[16] = {};
        char pathStr[256] = {};

        // Try to parse hash + path
        int n = sscanf(line, "%15s %255s", hashStr, pathStr);
        if (n == 2 && strlen(hashStr) == 8) {
            uint32_t hash = (uint32_t)strtoul(hashStr, nullptr, 16);
            auto it = crcToS3off.find(hash);
            if (it != crcToS3off.end()) {
                s3offToName[it->second] = pathToRelative(pathStr);
                crcToS3off.erase(it);
                resolved++;
            }
        } else if (n == 1 && strlen(hashStr) != 8) {
            // Single token with no hash — the "path" ended up in hashStr
            // This is a collision-chain entry (empty hash), already resolved via section 2
        } else {
            // Try parsing indented path (empty hash field)
            char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p && *p != '\n' && *p != '\r') {
                // This is a collision chain entry — compute the hash ourselves
                char trimmed[256];
                sscanf(p, "%255s", trimmed);
                uint32_t hash = xtreHash(trimmed);
                auto it = crcToS3off.find(hash);
                if (it != crcToS3off.end()) {
                    s3offToName[it->second] = pathToRelative(trimmed);
                    crcToS3off.erase(it);
                    resolved++;
                }
            }
        }
    }
    fclose(f);

    if (!crcToS3off.empty())
        printf("XTRE: %zu CRCs still unresolved after '%s'\n", crcToS3off.size(), txtPath.c_str());
    else
        printf("XTRE: All names resolved from '%s' (%d from CRC)\n", txtPath.c_str(), resolved);
}

void XtreArchive::Parse(void) {
    if (dataSize < 24) {
        valid = false;
        return;
    }

    if (memcmp(data, "XTRE", 4) != 0) {
        valid = false;
        return;
    }

    uint32_t sec1Off = readU32LE(&data[8]);
    uint32_t sec2Off = readU32LE(&data[12]);
    uint32_t sec3Off = readU32LE(&data[16]);
    uint32_t dataOff = readU32LE(&data[20]);

    uint32_t s3Count = (dataOff - sec3Off) / 8;

    // Build name map from section 2 (sequential parse catches collision chains)
    std::map<uint32_t, std::string> s3offToName;
    BuildNameMap(sec1Off, sec2Off, sec3Off, dataOff, s3offToName);

    // Resolve unnamed files using companion .txt file with hash-to-name mappings
    ResolveNamesFromFile(sec1Off, sec2Off, sec3Off, dataOff, s3offToName);

    // Derive archive base name for numeric fallback
    std::string archBase(this->path);
    {
        size_t sep = archBase.find_last_of("/\\.");
        if (sep != std::string::npos && archBase[sep] == '.')
            archBase = archBase.substr(0, sep);
        sep = archBase.find_last_of("/\\");
        if (sep != std::string::npos)
            archBase = archBase.substr(sep + 1);
        std::transform(archBase.begin(), archBase.end(), archBase.begin(), ::toupper);
    }

    // Iterate section 3 sequentially — every entry is a file
    for (uint32_t i = 0; i < s3Count; i++) {
        uint32_t s3ptr = sec3Off + i * 8;
        uint32_t fileOffset = readU32LE(&data[s3ptr]);
        uint32_t fileSize   = readU32LE(&data[s3ptr + 4]) & 0x3FFFFFFF;

        uint32_t comprSize;
        if (i + 1 < s3Count)
            comprSize = readU32LE(&data[s3ptr + 8]) - fileOffset;
        else
            comprSize = static_cast<uint32_t>(dataSize) - fileOffset;

        if (fileOffset + comprSize > static_cast<uint32_t>(dataSize) || fileSize == 0)
            continue;

        uint8_t* fileData = new uint8_t[fileSize];
        bool compressed = comprSize < fileSize;

        if (!compressed) {
            memcpy(fileData, &data[fileOffset], fileSize);
        } else {
            size_t result = XtreDecompressor::decompress(&data[fileOffset], fileData, fileSize);
            if (result != fileSize) {
                printf("XTRE: Warning: file %u decompressed %zu bytes, expected %u\n", i, result, fileSize);
            }
        }

        // Resolve filename
        std::string relPath;
        auto nameIt = s3offToName.find(s3ptr);
        if (nameIt != s3offToName.end()) {
            relPath = nameIt->second;
        } else {
            char numBuf[64];
            snprintf(numBuf, sizeof(numBuf), "%s_%08u", archBase.c_str(), i);
            relPath = numBuf;
        }

        // Build the entry name in SC convention: ..\..\DATA\<relPath>
        // relPath is either "DIR/FILE.EXT" or "FILE.EXT" or "ARCHIVE_00000NNN"
        std::string entryName = "..\\..\\DATA\\";
        for (char c : relPath) {
            entryName += (c == '/') ? '\\' : c;
        }

        TreEntry* entry = new TreEntry();
        entry->unknownFlag = 0;
        memset(entry->name, 0, sizeof(entry->name));
        strncpy(entry->name, entryName.c_str(), sizeof(entry->name) - 1);
        entry->data = fileData;
        entry->size = fileSize;

        entries.push_back(entry);
        mappedEntries[entry->name] = entry;
    }

    valid = true;
    printf("XTRE: Parsed '%s' - %zu entries (%zu named).\n", path, entries.size(), s3offToName.size());
}

// ---------------------------------------------------------------------------
// Load one entry from an XTRE file without decompressing the entire archive.
//
// Reads only the metadata portion (sections 1-3, up to the dataOff boundary),
// builds the name map, finds the target entry, then reads+decompresses just
// that file's data from disk.
// ---------------------------------------------------------------------------
TreEntry* XtreArchive::LoadSingleEntry(const char* archivePath, const char* entryName) {
    // Build the full filesystem path the same way InitFromFile does
    char fullPath[512] = {};
    strcat(fullPath, GetBase());
    strcat(fullPath, archivePath);

    FILE* f = fopen(fullPath, "rb");
    if (!f) return nullptr;

    // Read the 24-byte XTRE header to locate sections
    uint8_t hdr[24];
    if (fread(hdr, 1, 24, f) != 24 || memcmp(hdr, "XTRE", 4) != 0) {
        fclose(f); return nullptr;
    }

    uint32_t sec1Off = readU32LE(&hdr[8]);
    uint32_t sec2Off = readU32LE(&hdr[12]);
    uint32_t sec3Off = readU32LE(&hdr[16]);
    uint32_t dataOff = readU32LE(&hdr[20]);

    if (dataOff < 24 || sec3Off >= dataOff || sec2Off >= sec3Off) {
        fclose(f); return nullptr;
    }

    // Read only the metadata (everything before the data section)
    uint32_t metaSize = dataOff;
    uint8_t* meta = new uint8_t[metaSize];
    fseek(f, 0, SEEK_SET);
    if (fread(meta, 1, metaSize, f) != metaSize) {
        delete[] meta; fclose(f); return nullptr;
    }

    // Build name→s3ptr map using the same helpers (they operate on this->data,
    // so temporarily point the instance at the metadata buffer)
    XtreArchive tmp;
    tmp.data     = meta;
    tmp.dataSize = metaSize;
    strncpy(tmp.path, archivePath, sizeof(tmp.path) - 1);

    std::map<uint32_t, std::string> s3offToName;
    tmp.BuildNameMap(sec1Off, sec2Off, sec3Off, dataOff, s3offToName);
    tmp.ResolveNamesFromFile(sec1Off, sec2Off, sec3Off, dataOff, s3offToName);

    // Find target entry in section 3 by matching the full asset name
    uint32_t s3Count = (dataOff - sec3Off) / 8;
    uint32_t found_s3ptr  = 0xFFFFFFFF;
    uint32_t found_offset = 0;
    uint32_t found_size   = 0;
    uint32_t found_comprsz = 0;

    for (auto& [s3ptr, relPath] : s3offToName) {
        std::string fullName = "..\\..\\DATA\\";
        for (char c : relPath) fullName += (c == '/') ? '\\' : c;
        if (fullName != entryName) continue;

        // s3ptr is the byte offset into the metadata buffer for this entry's s3 record
        if (s3ptr + 8 > metaSize) break;
        found_s3ptr  = s3ptr;
        found_offset = readU32LE(&meta[s3ptr]);
        found_size   = readU32LE(&meta[s3ptr + 4]) & 0x3FFFFFFF;

        // Determine compressed size from the next s3 entry (if any)
        uint32_t idx = (s3ptr - sec3Off) / 8;
        if (idx + 1 < s3Count) {
            uint32_t nextOff = readU32LE(&meta[sec3Off + (idx + 1) * 8]);
            found_comprsz = nextOff - found_offset;
        } else {
            // Last entry: compressed size extends to end of file
            fseek(f, 0, SEEK_END);
            found_comprsz = (uint32_t)ftell(f) - found_offset;
        }
        break;
    }

    delete[] meta;
    tmp.data = nullptr;  // don't let tmp's destructor free it (it was stack-local)

    if (found_s3ptr == 0xFFFFFFFF || found_size == 0) {
        fclose(f); return nullptr;
    }

    // Read only the compressed data for this entry
    uint8_t* compData = new uint8_t[found_comprsz];
    fseek(f, (long)found_offset, SEEK_SET);
    if (fread(compData, 1, found_comprsz, f) != found_comprsz) {
        delete[] compData; fclose(f); return nullptr;
    }
    fclose(f);

    // Decompress (or memcpy if stored verbatim)
    uint8_t* fileData = new uint8_t[found_size];
    if (found_comprsz < found_size) {
        size_t got = XtreDecompressor::decompress(compData, fileData, found_size);
        if (got != found_size)
            printf("XtreArchive::LoadSingleEntry: decompressed %zu, expected %u\n", got, found_size);
    } else {
        memcpy(fileData, compData, found_size);
    }
    delete[] compData;

    TreEntry* entry = new TreEntry();
    entry->unknownFlag = 0;
    memset(entry->name, 0, sizeof(entry->name));
    strncpy(entry->name, entryName, sizeof(entry->name) - 1);
    entry->data = fileData;
    entry->size = found_size;
    return entry;
}

void XtreArchive::List(FILE* output) {
    fprintf(output, "Listing content of XTRE archive '%s'.\n", this->path);
    fprintf(output, "    %zu entrie(s) found.\n", entries.size());

    for (size_t i = 0; i < entries.size(); i++) {
        TreEntry* entry = entries[i];
        fprintf(output, "    Entry [%3zu] '%s' size: %zu bytes.\n", i, entry->name, entry->size);
    }
}

TreEntry* XtreArchive::GetEntryByName(const char* entryName) {
    auto it = mappedEntries.find(entryName);
    if (it == mappedEntries.end())
        return nullptr;
    return it->second;
}

TreEntry* XtreArchive::GetEntryByID(size_t entryID) {
    if (entryID >= entries.size()) return nullptr;
    return entries[entryID];
}

size_t XtreArchive::GetNumEntries(void) {
    return entries.size();
}

#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include "Base.h"
#include "../commons/ByteStream.h"
#include "TreArchive.h"

// Wing Commander 3/4 XTRE archive format.
// Produces TreEntry objects compatible with the existing asset pipeline.
class XtreArchive {
public:
    XtreArchive();
    ~XtreArchive();

    bool InitFromFile(const char* filepath);
    void InitFromRAM(const char* name, uint8_t* data, size_t size);
    void List(FILE* output);

    TreEntry* GetEntryByName(const char* entryName);
    TreEntry* GetEntryByID(size_t entryID);
    size_t GetNumEntries(void);

    inline bool IsValid(void) { return this->valid; }

    // Load and decompress a single named entry from an XTRE file on disk.
    // Reads only the metadata header + the one file — does not load the full archive.
    // Returns a heap-allocated TreEntry (caller must delete it AND entry->data), or nullptr.
    static TreEntry* LoadSingleEntry(const char* archivePath, const char* entryName);

    std::vector<TreEntry*> entries;
    std::unordered_map<std::string, TreEntry*> mappedEntries;

private:
    bool valid;
    char path[512];
    uint8_t* data;
    size_t dataSize;
    bool initializedFromFile;

    void Parse(void);
    void BuildNameMap(uint32_t sec1Off, uint32_t sec2Off, uint32_t sec3Off, uint32_t dataOff,
                      std::map<uint32_t, std::string>& s3offToName);
    void ResolveNamesFromFile(uint32_t sec1Off, uint32_t sec2Off, uint32_t sec3Off, uint32_t dataOff,
                              std::map<uint32_t, std::string>& s3offToName);

    static uint32_t readU32LE(const uint8_t* p);
    static uint32_t xtreHash(const char* str);
    static std::string pathToRelative(const std::string& path);
};

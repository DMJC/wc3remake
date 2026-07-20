#include "IFFSaxLexer.h"
#include <cstring>
#include <string>

#include "../realspace/Base.h"
IFFSaxLexer::IFFSaxLexer() {
    this->stream = NULL;
    this->data = NULL;
    this->size = 0;
    this->path[0] = '\0';
}

IFFSaxLexer::~IFFSaxLexer() {}
bool IFFSaxLexer::InitFromFile(const char *filepath,
                               std::unordered_map<std::string, std::function<void(uint8_t *data, size_t size)>> events) {
    char fullPath[512];
    fullPath[0] = '\0';

    // Check if filepath contains directory separators
    if (strchr(filepath, '/') != NULL || strchr(filepath, '\\') != NULL) {
        strcat(fullPath, filepath);
    } else {
        strcat(fullPath, GetBase());
        strcat(fullPath, filepath);
    }
    
    FILE *file = fopen(fullPath, "r+b");
    printf("IFF SAX:opening %s\n", fullPath);

    if (!file) {
        printf("IFF SAX:Unable to open IFF archive: '%s'.\n", filepath);
        return false;
    }

    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint8_t *fileData = new uint8_t[fileSize];
    size_t t = fread(fileData, 1, fileSize, file);
    printf("file %s read %zu bytes should be %zu\n", fullPath, t, fileSize);
    strcpy(this->path, filepath);
    fclose(file);
    return InitFromRAM(fileData, fileSize, events);
}

bool IFFSaxLexer::InitFromRAM(
    uint8_t *data,
    size_t size,
    std::unordered_map<std::string, std::function<void(uint8_t *data, size_t size)>> events
) {
    this->data = data;
    this->size = size;
    this->stream = new ByteStream();
    this->stream->Set(this->data, size);
    Parse(events);
    return true;
}

void IFFSaxLexer::Parse(std::unordered_map<std::string, std::function<void(uint8_t *data, size_t size)>> events) {
    size_t read = 0;
    while (read < this->size) {
        std::vector<uint8_t> bname = this->stream->ReadBytes(4);
        read += 4;
        if (read >= this->size) {
            break;
        };
        std::string chunk_stype;
        chunk_stype.assign(bname.begin(), bname.end());

        if (chunk_stype == "FORM") {
            size_t chunk_size = this->stream->ReadUInt32BE();
            if (chunk_size % 2 != 0) {
                chunk_size++;
            }
            if ((chunk_size + 8 > this->size) && (read == 4)) {
                uint8_t *fixed_data = new uint8_t[chunk_size + 8];
                memset(fixed_data, 0, chunk_size + 8);
                memcpy(fixed_data, this->data, this->size);
                //uint8_t *dt = this->data;
                size_t currentPos = this->stream->GetCurrentPosition(); 
                this->data = fixed_data;
                this->size = chunk_size + 8;
                delete this->stream;
                this->stream = new ByteStream();
                this->stream->Set(this->data, this->size);
                this->stream->MoveForward(currentPos);
            }
            std::vector<uint8_t> bname = this->stream->ReadBytes(4);
            chunk_stype.assign(bname.begin(), bname.end());
            
            read += 8;
            if (events.count(chunk_stype) > 0) {
                uint8_t * chunk_data = (uint8_t *)calloc(chunk_size + size_offset, sizeof(uint8_t));
                this->stream->ReadBytes(chunk_data, chunk_size + size_offset);
                // EA-IFF-85: odd-sized chunks are always followed by exactly
                // one pad byte, unconditionally — its value is not
                // meaningful (e.g. compressed pixel data pads with whatever
                // byte the compressor happened to emit last), so it can't be
                // detected by inspecting it.
                if (chunk_size % 2 != 0) {
                    this->stream->MoveForward(1);
                    read++;
                }
                events.at(chunk_stype)(chunk_data, chunk_size + size_offset);
                read += (chunk_size + size_offset);
                free(chunk_data);
            } else {
                printf("%s not handled\n", chunk_stype.c_str());
                std::vector<uint8_t> dump = this->stream->ReadBytes(chunk_size + size_offset);
                if (chunk_size % 2 != 0) {
                    this->stream->MoveForward(1);
                    read++;
                }
                read += (chunk_size+ size_offset );
            }
        } else {
            size_t chunk_size = this->stream->ReadUInt32BE();
            if (chunk_size % 2 != 0) {
                //chunk_size++;
            }
			if (chunk_size > this->size) {
				chunk_size = size-4;
				stream->MoveBackward(4);
                read -= 4;
			}
            read += 4;
            // Look up by the full 4-char chunk_stype, not chunk_stype.c_str()
            // — a tag with an embedded null (e.g. WC3 profile's "FMV\0" or
            // "AI\0_") would otherwise implicitly convert through the
            // strlen-based std::string(const char*) constructor and get
            // silently truncated (to "FMV"/"AI"), so it could never match a
            // handler even if one were registered for it. No behavior change
            // for the ordinary case, since a chunk_stype without an embedded
            // null round-trips identically through .c_str().
            if (events.count(chunk_stype) > 0) {
                if (chunk_size > 0) {
                    uint8_t * chunk_data = (uint8_t *)calloc(chunk_size, sizeof(uint8_t));
                    this->stream->ReadBytes(chunk_data, chunk_size);
                    if (chunk_size % 2 != 0) {
                        this->stream->MoveForward(1);
                        read++;
                    }
                    events.at(chunk_stype)(chunk_data, chunk_size);
                    free(chunk_data);
                } else {
                    events.at(chunk_stype)(NULL, 0);
                }
                read += chunk_size;
            } else {
                printf("%s not handled\n", chunk_stype.c_str());
                if (chunk_size > 0) {
                    //std::vector<uint8_t> dump = this->stream->ReadBytes(chunk_size);
                    this->stream->MoveForward(chunk_size);
                    if (chunk_size % 2 != 0) {
                        this->stream->MoveForward(1);
                        read++;
                    }
                    read += chunk_size;
                }
            }
        }
    }
}
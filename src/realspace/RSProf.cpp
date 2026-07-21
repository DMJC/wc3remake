
#include "RSProf.h"
#include <cstring>
RSProf::RSProf() {

}
RSProf::~RSProf() {

}
void RSProf::InitFromRAM(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["PROF"] = std::bind(&RSProf::parsePROF, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSProf::parsePROF(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["VERS"] = std::bind(&RSProf::parsePROF_VERS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["RADI"] = std::bind(&RSProf::parsePROF_RADI, this, std::placeholders::_1, std::placeholders::_2);
    handlers["_AI_"] = std::bind(&RSProf::parsePROF__AI_, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSProf::parsePROF_VERS(uint8_t *data, size_t size) {
    this->version = *(uint16_t*)data;
}
void RSProf::parsePROF_RADI(uint8_t *data, size_t size){
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSProf::parsePROF_RADI_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SPCH"] = std::bind(&RSProf::parsePROF_RADI_SPCH, this, std::placeholders::_1, std::placeholders::_2);
    handlers["OPTS"] = std::bind(&RSProf::parsePROF_RADI_OPTS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MSGS"] = std::bind(&RSProf::parsePROF_RADI_MSGS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MSGG"] = std::bind(&RSProf::parsePROF_RADI_MSGG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MSGF"] = std::bind(&RSProf::parsePROF_RADI_MSGF, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ASKS"] = std::bind(&RSProf::parsePROF_RADI_ASKS, this, std::placeholders::_1, std::placeholders::_2);
    // WC3's own MISSIONS.TRE-embedded pilot profiles (unlike SC's
    // DATA\PROFILE\ ones) carry the radio audio inline instead of an
    // indirect spch+SPEECH.PAK reference, plus a table of radio-FMV clips.
    handlers["SOND"] = std::bind(&RSProf::parsePROF_RADI_SOND, this, std::placeholders::_1, std::placeholders::_2);
    // Tag is 'F','M','V',0x00 — the embedded null means the ordinary string
    // literal "FMV\0" (which implicitly converts via the strlen-based
    // const-char* constructor and silently truncates to "FMV") would never
    // match IFFSaxLexer's chunk_stype (always a true 4-char std::string,
    // built via assign(iterator,iterator), that preserves the null). Use the
    // explicit-length constructor instead — same fix applied to the
    // similarly-truncated handlers["AI\0_"] registration below.
    handlers[std::string("FMV\0", 4)] = std::bind(&RSProf::parsePROF_RADI_FMV, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSProf::parsePROF_RADI_INFO(uint8_t *data, size_t size){
    ByteStream stream;
    if (data == nullptr) {
        return;
    }
    stream.Set(data, size);
    // Fixed id(2)+name(15)+callsign(15) = 32-byte record, confirmed by direct
    // byte decode (e.g. PROFILE\TCRUISER.IFF: id=58624, name="Bruiser",
    // callsign="Ajax" at exactly these offsets). ReadStringNoSize(size) here
    // used to stop at the first null byte instead of consuming the fixed
    // 15-byte stride, so `name` under-read (e.g. "Bruiser\0" = 8 bytes, not
    // 15) and `callsign` then started mid-padding and immediately hit a null
    // byte — always reading empty, regardless of what a profile's real
    // callsign was.
    this->radi.info.id = stream.ReadShort();
    this->radi.info.name = stream.ReadString(15);
    this->radi.info.callsign = stream.ReadString(15);
}
void RSProf::parsePROF_RADI_OPTS(uint8_t *data, size_t size){
    ByteStream stream;
    if (data == nullptr) {
        return;
    }
    stream.Set(data, size);
    int read = 0;
    while (read < size) {
        char r = stream.ReadByte();
        if (r != '\0') {
            this->radi.opts.push_back(r);
        }
        read++;    
    }
    
}
void RSProf::parsePROF_RADI_MSGS(uint8_t *data, size_t size){
    parseRadiMessagesInto(data, size, this->radi.msgs);
}
// MSGG/MSGF are German/French text variants of MSGS, sharing the exact same
// per-key record shape (only the trailing message text itself, and hence
// the 2-byte duration-ish field noted below, differs by language). Seen as
// siblings of MSGS inside WC3's own MISSIONS.TRE-embedded pilot profiles.
void RSProf::parsePROF_RADI_MSGG(uint8_t *data, size_t size){
    parseRadiMessagesInto(data, size, this->radi.msgg);
}
void RSProf::parsePROF_RADI_MSGF(uint8_t *data, size_t size){
    parseRadiMessagesInto(data, size, this->radi.msgf);
}
void RSProf::parseRadiMessagesInto(uint8_t *data, size_t size, std::unordered_map<std::uint8_t, std::string> &target){
    ByteStream stream;
    if (data == nullptr) {
        return;
    }
    stream.Set(data, size);
    while (stream.GetPosition() < data + size) {
        uint8_t key = stream.ReadByte();
        // 13 bytes between the key and the null-terminated text that this
        // parser used to skip entirely, immediately hitting a 0x00 pad byte
        // and reading an empty string every time. Confirmed against
        // PROFILE\VICA1.IFF (the TCS Victory's own profile): key 0x14's
        // record is "14 00 00 00 00 00 10 27 00 00 00 00 00 00" (14 bytes)
        // followed directly by "Excellent work, Colonel...\0" — i.e. exactly
        // 13 more bytes after the key. Bytes 6-7 of that gap look like a
        // little-endian u16 (0x2710=10000 in the English track, 0x03e8=1000
        // in the French one for the same key) — possibly a voice-clip
        // duration; meaning of the rest is unknown.
        for (int i = 0; i < 13; i++) stream.ReadByte();
        std::string value = stream.ReadStringNoSize(size);
        target[key] = value;
    }
}
void RSProf::parsePROF_RADI_ASKS(uint8_t *data, size_t size){
    ByteStream stream;
    if (data == nullptr) {
        return;
    }
    stream.Set(data, size);
    while (stream.GetPosition() < data + size) {
        // Each record is a single null-terminated string whose first
        // character is the option key (matching the single-char keys in
        // RADI_OPTS, e.g. 'f'/'d') and the rest is the question text.
        // Confirmed against PROFILE\PLAYER.IFF's ASKS chunk: "dWhat's your
        // status\0fNeed clearance\0..." — the old code wrongly treated
        // consecutive strings as alternating key/value pairs.
        std::string entry = stream.ReadStringNoSize(size);
        if (entry.empty()) {
            continue;
        }
        std::string cat = entry.substr(0, 1);
        std::string value = entry.substr(1);
        this->radi.asks[cat] = value;
        this->radi.asks_vector.push_back(value);
    }
}
void RSProf::parsePROF_RADI_SPCH(uint8_t *data, size_t size){
    ByteStream stream;
    if (data == nullptr) {
        return;
    }
    stream.Set(data, size);
    this->radi.spch = (int16_t) stream.ReadByte();
}
// Repeated records packed back-to-back until the chunk ends: u32LE RADI
// code + u32LE PCM byte length + that many bytes of raw 8-bit unsigned mono
// PCM (no VOC/WAV container of its own). Reverse-engineered against
// missions.tre's own embedded pilot profiles: walking this shape consumes
// every SOND chunk exactly to its declared size with zero slack, and the
// codes encountered (e.g. 0x0A death scream, 0x0D enemy destroyed, 0x31
// enemy killed, 0x21 insult enemy) match the RADI code table exactly.
void RSProf::parsePROF_RADI_SOND(uint8_t *data, size_t size){
    if (data == nullptr) {
        return;
    }
    size_t pos = 0;
    while (pos + 8 <= size) {
        uint32_t code = data[pos] | (data[pos+1] << 8) | (data[pos+2] << 16) | ((uint32_t)data[pos+3] << 24);
        uint32_t len  = data[pos+4] | (data[pos+5] << 8) | (data[pos+6] << 16) | ((uint32_t)data[pos+7] << 24);
        pos += 8;
        if (pos + len > size) break;
        this->radi.sond[(uint8_t)code] = std::vector<uint8_t>(data + pos, data + pos + len);
        pos += len;
    }
}
// Fixed 19-byte records: u8 index + 4 bytes (unconfirmed — looks like a
// stale serialized pointer from the original engine, varies per record with
// no discernible pattern) + 1 zero pad byte + char[13] null-padded
// DATA\MOVIES\ filename (e.g. "sc_575a.mve").
void RSProf::parsePROF_RADI_FMV(uint8_t *data, size_t size){
    if (data == nullptr) {
        return;
    }
    constexpr size_t kRecord = 19;
    constexpr size_t kNameOff = 6;
    for (size_t pos = 0; pos + kRecord <= size; pos += kRecord) {
        RADI_FMV entry;
        entry.index = data[pos];
        const char* namePtr = reinterpret_cast<const char*>(data + pos + kNameOff);
        size_t maxLen = kRecord - kNameOff;
        size_t len = strnlen(namePtr, maxLen);
        entry.filename = std::string(namePtr, len);
        this->radi.fmv.push_back(entry);
    }
}
void RSProf::parsePROF__AI_(uint8_t *data, size_t size){
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    // Embedded-null tag ('A','I',0x00,0x00) — confirmed against a real
    // profile's hex dump (previously mistranscribed here as "AI\0_" with a
    // trailing underscore instead of a second null, which never matches
    // regardless of the truncation issue below). The plain string literal
    // "AI\0\0" would also implicitly convert via the strlen-based
    // const-char* constructor and silently truncate to "AI", which never
    // matches IFFSaxLexer's chunk_stype (a true 4-char std::string built via
    // assign(iterator,iterator) that preserves embedded nulls) — same fix as
    // parsePROF_RADI's "FMV\0" registration above.
    handlers[std::string("AI\0\0", 4)] = std::bind(&RSProf::parsePROF__AI_AI, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MVRS"] = std::bind(&RSProf::parsePROF__AI_MVRS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["GOAL"] = std::bind(&RSProf::parsePROF__AI_GOAL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ATRB"] = std::bind(&RSProf::parsePROF__AI_ATRB, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSProf::parsePROF__AI_AI(uint8_t *data, size_t size) {
    this->ai.isAI = true;
}
void RSProf::parsePROF__AI_MVRS(uint8_t *data, size_t size) {
    ByteStream stream;
    if (data == nullptr) {
        return;
    }
    stream.Set(data, size);

    while (stream.GetPosition() < data + size) {
        this->ai.mvrs.push_back(AI_STATE{stream.ReadByte(), stream.ReadByte()});
    }
}
void RSProf::parsePROF__AI_GOAL(uint8_t *data, size_t size) {
    ByteStream stream;
    if (data == nullptr) {
        return;
    }
    stream.Set(data, size);
    while (stream.GetPosition() < data + size) {
        this->ai.goal.push_back(stream.ReadByte());
    }
}
void RSProf::parsePROF__AI_ATRB(uint8_t *data, size_t size) {
    ByteStream stream;
    if (data == nullptr) {
        return;
    }
    if (size == 1) {
        this->ai.atrb.TH = 0;
        this->ai.atrb.CN = 0;
        this->ai.atrb.VB = 0;
        this->ai.atrb.LY = 0;
        this->ai.atrb.FL = 0;
        this->ai.atrb.AG = 0;
        this->ai.atrb.AA = 0;
        this->ai.atrb.SM = 0;
        this->ai.atrb.AR = 0;
        return;
    }
    stream.Set(data, size);
    this->ai.atrb.TH = stream.ReadByte();
    this->ai.atrb.CN = stream.ReadByte();
    this->ai.atrb.VB = stream.ReadByte();
    this->ai.atrb.LY = stream.ReadByte();
    this->ai.atrb.FL = stream.ReadByte();
    this->ai.atrb.AG = stream.ReadByte();
    this->ai.atrb.AA = stream.ReadByte();
    this->ai.atrb.SM = stream.ReadByte();
    this->ai.atrb.AR = stream.ReadByte();
}
const std::vector<uint8_t>* RSProf::getRadiSoundWav(uint8_t code) {
    auto cached = this->radiSoundWavCache.find(code);
    if (cached != this->radiSoundWavCache.end()) {
        return &cached->second;
    }
    auto it = this->radi.sond.find(code);
    if (it == this->radi.sond.end()) {
        return nullptr;
    }
    const std::vector<uint8_t> &pcm = it->second;

    std::vector<uint8_t> wav;
    wav.reserve(44 + pcm.size());
    auto push32 = [&](uint32_t v) {
        wav.push_back((uint8_t)(v & 0xFF)); wav.push_back((uint8_t)((v >> 8) & 0xFF));
        wav.push_back((uint8_t)((v >> 16) & 0xFF)); wav.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    auto push16 = [&](uint16_t v) {
        wav.push_back((uint8_t)(v & 0xFF)); wav.push_back((uint8_t)((v >> 8) & 0xFF));
    };
    auto pushStr = [&](const char* s) { wav.insert(wav.end(), s, s + 4); };

    // SOND carries no rate field of its own (unlike a real VOC file's time
    // constant byte) — 11025Hz matches the standard Sound Blaster VOC speech
    // rate and produces plausible clip durations for single-word/short-
    // phrase RADI lines (e.g. code 0x01 "Affirmative" = 4945 PCM bytes =>
    // ~0.45s). Not independently confirmed against original engine behavior.
    const uint32_t sampleRate = 11025;
    const uint16_t channels = 1, bitsPerSample = 8;
    const uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    const uint16_t blockAlign = channels * bitsPerSample / 8;
    uint32_t pcmSize = (uint32_t)pcm.size();

    pushStr("RIFF"); push32(36 + pcmSize); pushStr("WAVE");
    pushStr("fmt "); push32(16); push16(1) /* PCM */; push16(channels);
    push32(sampleRate); push32(byteRate); push16(blockAlign); push16(bitsPerSample);
    pushStr("data"); push32(pcmSize);
    wav.insert(wav.end(), pcm.begin(), pcm.end());

    auto &slot = this->radiSoundWavCache[code];
    slot = std::move(wav);
    return &slot;
}
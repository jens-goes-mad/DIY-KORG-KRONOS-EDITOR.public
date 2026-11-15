#include "KscFile.h"

#include <cctype>

namespace kronos {

namespace {

constexpr const char* kHeader = "#KORG Script Version 1.0";

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

bool endsWithNoCase(const std::string& s, const char* suffix) {
    const std::string suf(suffix);
    if (s.size() < suf.size()) return false;
    for (size_t i = 0; i < suf.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(s[s.size() - suf.size() + i])) != suf[i]) return false;
    return true;
}

std::string trimRight(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

// Reads a non-negative decimal at `pos`, then expects '.'; advances past it.
bool readNumberDot(const std::string& s, size_t& pos, int& out) {
    size_t p = pos;
    long value = 0;
    while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p])) && value < 1000000) value = value * 10 + (s[p++] - '0');
    if (p == pos || p >= s.size() || s[p] != '.') return false;
    out = static_cast<int>(value);
    pos = p + 1;
    return true;
}

// `MS<n>.<a>.<b>.<name>` or `DS<n>...` starting at `pos` (the `<a>.<b>`
// fields, `1.0` in every real file, are skipped). Names may contain dots
// ("E.Organ-Lower"), so the name is simply the rest of the line.
bool parseEntry(const std::string& s, size_t pos, bool& isMultisample, int& number, std::string& name) {
    if (s.compare(pos, 2, "MS") == 0) isMultisample = true;
    else if (s.compare(pos, 2, "DS") == 0) isMultisample = false;
    else return false;
    pos += 2;
    int a = 0, b = 0;
    if (!readNumberDot(s, pos, number) || !readNumberDot(s, pos, a) || !readNumberDot(s, pos, b)) return false;
    name = trimRight(s.substr(pos));
    return true;
}

// `<msSlots>.<dsSlots>.<name>` starting at `pos`.
bool parseFooter(const std::string& s, size_t pos, KscBank& bank) {
    int ms = 0, ds = 0;
    if (!readNumberDot(s, pos, ms) || !readNumberDot(s, pos, ds)) return false;
    bank.msSlots = ms;
    bank.dsSlots = ds;
    bank.name = trimRight(s.substr(pos));
    return true;
}

// `<uuid>.` starting at `pos`; on success `pos` is just past the dot and
// `uuid` holds the bank UUID with the stereo bit cleared.
bool readUuidDot(const std::string& s, size_t& pos, std::array<uint8_t, 16>& uuid) {
    constexpr size_t kUuidLength = 36;
    if (pos + kUuidLength >= s.size() || s[pos + kUuidLength] != '.') return false;
    if (!parseUuid(s.substr(pos, kUuidLength), uuid)) return false;
    uuid[15] &= 0xFE;
    pos += kUuidLength + 1;
    return true;
}

KscBank& bankWithUuid(KscFile& file, const std::array<uint8_t, 16>& uuid) {
    for (auto& bank : file.banks)
        if (bank.hasUuid && bank.uuid == uuid) return bank;
    file.banks.emplace_back();
    file.banks.back().uuid = uuid;
    file.banks.back().hasUuid = true;
    return file.banks.back();
}

KscBank& bankWithExsIndex(KscFile& file, int exsIndex) {
    for (auto& bank : file.banks)
        if (bank.exsIndex == exsIndex) return bank;
    file.banks.emplace_back();
    file.banks.back().exsIndex = exsIndex;
    return file.banks.back();
}

// Handles one line after the header; false if it matched no known shape.
bool parseLine(KscFile& file, const std::string& line) {
    if (line == "#v2") return true;
    std::array<uint8_t, 16> uuid{};

    if (startsWith(line, "#uuid:")) {  // LoadScript bank UUID
        if (!parseUuid(line.substr(6), uuid)) return false;
        uuid[15] &= 0xFE;
        file.kind = KscKind::LoadScript;
        bankWithUuid(file, uuid);
        return true;
    }
    if (startsWith(line, "#>User.")) return true;  // LoadScript load directive, repeats the file list
    if (line[0] != '#') {  // LoadScript file list
        if (file.kind != KscKind::LoadScript || file.banks.empty()) return false;
        if (endsWithNoCase(line, ".KMP")) file.banks.back().multisampleFiles.push_back(line);
        else if (endsWithNoCase(line, ".KSF")) file.banks.back().sampleFiles.push_back(line);
        else return false;
        return true;
    }

    bool isMultisample = false;
    int number = 0;
    std::string name;
    size_t pos = 0;
    if (startsWith(line, "#>>uuid:")) {  // Manifest entry
        pos = 8;
        if (!readUuidDot(line, pos, uuid) || !parseEntry(line, pos, isMultisample, number, name)) return false;
        file.kind = KscKind::Manifest;
        KscBank& bank = bankWithUuid(file, uuid);
        (isMultisample ? bank.multisamples : bank.samples)[number] = name;
        return true;
    }
    if (startsWith(line, "#>uuid:")) {  // Manifest footer
        pos = 7;
        if (!readUuidDot(line, pos, uuid)) return false;
        file.kind = KscKind::Manifest;
        return parseFooter(line, pos, bankWithUuid(file, uuid));
    }
    if (startsWith(line, "#>EXS")) {  // FullBackup entry
        pos = 5;
        int exsIndex = 0;
        if (!readNumberDot(line, pos, exsIndex) || !parseEntry(line, pos, isMultisample, number, name)) return false;
        file.kind = KscKind::FullBackup;
        KscBank& bank = bankWithExsIndex(file, exsIndex);
        (isMultisample ? bank.multisamples : bank.samples)[number] = name;
        return true;
    }
    if (startsWith(line, "#>Bank")) {  // FullBackup footer
        pos = 6;
        int exsIndex = 0;
        if (!readNumberDot(line, pos, exsIndex)) return false;
        file.kind = KscKind::FullBackup;
        return parseFooter(line, pos, bankWithExsIndex(file, exsIndex));
    }
    return false;
}

}  // namespace

bool parseUuid(const std::string& text, std::array<uint8_t, 16>& out) {
    if (text.size() != 36) return false;
    size_t byte = 0;
    for (size_t i = 0; i < text.size();) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (text[i] != '-') return false;
            ++i;
            continue;
        }
        int value = 0;
        for (int k = 0; k < 2; ++k, ++i) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
            if (c >= '0' && c <= '9') value = value * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') value = value * 16 + (c - 'a' + 10);
            else return false;
        }
        out[byte++] = static_cast<uint8_t>(value);
    }
    return byte == 16;
}

KscFile parseKsc(const std::string& text) {
    KscFile file;
    bool sawHeader = false;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (!sawHeader) {
            if (line != kHeader) return KscFile{};  // not a KSC script at all
            sawHeader = true;
            continue;
        }
        if (!parseLine(file, line)) file.unrecognizedLines++;
    }
    return file;
}

}  // namespace kronos

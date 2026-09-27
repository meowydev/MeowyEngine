// meowyrender - src/modules/rtext_util.cpp
// raylib-parity text/string utilities (TextFormat, TextSubtext, TextReplace,
// TextSplit, TextToUpper/Lower, etc.). These mirror raylib's convention of
// returning pointers into rotating static buffers so call sites can chain them
// within a single statement without managing lifetimes.
#include "meowyrender/meowyrender.hpp"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <array>
#include <string>
#include <vector>

namespace meowyrender {

namespace {
// raylib defaults: MAX_TEXTFORMAT_BUFFERS=4, MAX_TEXT_BUFFER_LENGTH=1024.
constexpr int kMaxBuffers = 4;
constexpr int kBufferLen = 1024;
constexpr int kMaxSplit = 128;
} // namespace

const char* TextFormat(const char* text, ...) {
    static std::array<std::array<char, kBufferLen>, kMaxBuffers> buffers{};
    static int index = 0;
    char* current = buffers[index].data();
    std::memset(current, 0, kBufferLen);
    va_list args;
    va_start(args, text);
    std::vsnprintf(current, kBufferLen, text, args);
    va_end(args);
    index = (index + 1) % kMaxBuffers;
    return current;
}

int TextLength(const char* text) { return text ? static_cast<int>(std::strlen(text)) : 0; }

bool TextIsEqual(const char* a, const char* b) {
    if (!a || !b) return a == b;
    return std::strcmp(a, b) == 0;
}

const char* TextSubtext(const char* text, int position, int length) {
    static std::array<char, kBufferLen> buffer{};
    buffer.fill(0);
    if (!text) return buffer.data();
    const int total = static_cast<int>(std::strlen(text));
    if (position < 0) position = 0;
    if (position >= total) return buffer.data();
    if (length < 0) length = 0;
    if (position + length > total) length = total - position;
    if (length > kBufferLen - 1) length = kBufferLen - 1;
    std::memcpy(buffer.data(), text + position, static_cast<std::size_t>(length));
    buffer[length] = '\0';
    return buffer.data();
}

char* TextReplace(const char* text, const char* replace, const char* by) {
    if (!text || !replace || !by || replace[0] == '\0') return nullptr;
    const std::size_t replaceLen = std::strlen(replace);
    const std::size_t byLen = std::strlen(by);
    std::string out;
    const char* cursor = text;
    const char* found;
    while ((found = std::strstr(cursor, replace)) != nullptr) {
        out.append(cursor, found);
        out.append(by, byLen);
        cursor = found + replaceLen;
    }
    out.append(cursor);
    char* result = static_cast<char*>(std::malloc(out.size() + 1));
    std::memcpy(result, out.c_str(), out.size() + 1);
    return result;
}

char* TextInsert(const char* text, const char* insert, int position) {
    if (!text || !insert) return nullptr;
    const int textLen = static_cast<int>(std::strlen(text));
    if (position < 0) position = 0;
    if (position > textLen) position = textLen;
    const std::size_t insertLen = std::strlen(insert);
    char* result = static_cast<char*>(std::malloc(textLen + insertLen + 1));
    std::memcpy(result, text, static_cast<std::size_t>(position));
    std::memcpy(result + position, insert, insertLen);
    std::memcpy(result + position + insertLen, text + position, static_cast<std::size_t>(textLen - position) + 1);
    return result;
}

const char* TextJoin(const char** textList, int count, const char* delimiter) {
    static std::array<char, kBufferLen> buffer{};
    buffer.fill(0);
    if (!textList || count <= 0) return buffer.data();
    int pos = 0;
    const int delimLen = delimiter ? static_cast<int>(std::strlen(delimiter)) : 0;
    for (int i = 0; i < count && pos < kBufferLen - 1; ++i) {
        if (textList[i]) {
            int len = static_cast<int>(std::strlen(textList[i]));
            if (pos + len > kBufferLen - 1) len = kBufferLen - 1 - pos;
            std::memcpy(buffer.data() + pos, textList[i], static_cast<std::size_t>(len));
            pos += len;
        }
        if (delimiter && i < count - 1 && pos + delimLen < kBufferLen - 1) {
            std::memcpy(buffer.data() + pos, delimiter, static_cast<std::size_t>(delimLen));
            pos += delimLen;
        }
    }
    buffer[pos] = '\0';
    return buffer.data();
}

const char** TextSplit(const char* text, char delimiter, int* count) {
    static std::array<const char*, kMaxSplit> result{};
    static std::array<char, kBufferLen> buffer{};
    buffer.fill(0);
    result.fill(nullptr);
    result[0] = buffer.data();
    int counter = 1;
    if (text) {
        int len = 0;
        for (int i = 0; i < kBufferLen - 1 && text[i] != '\0'; ++i) {
            if (text[i] == delimiter) {
                buffer[len++] = '\0';
                if (counter < kMaxSplit) result[counter++] = buffer.data() + len;
                else break;
            } else {
                buffer[len++] = text[i];
            }
        }
    }
    if (count) *count = counter;
    return result.data();
}

void TextAppend(char* text, const char* append, int* position) {
    if (!text || !append) return;
    const int start = position ? *position : static_cast<int>(std::strlen(text));
    const std::size_t appendLen = std::strlen(append);
    std::memcpy(text + start, append, appendLen);
    if (position) *position += static_cast<int>(appendLen);
    text[start + appendLen] = '\0';
}

int TextFindIndex(const char* text, const char* find) {
    if (!text || !find) return -1;
    const char* p = std::strstr(text, find);
    return p ? static_cast<int>(p - text) : -1;
}

const char* TextToUpper(const char* text) {
    static std::array<char, kBufferLen> buffer{};
    buffer.fill(0);
    if (text)
        for (int i = 0; i < kBufferLen - 1 && text[i]; ++i)
            buffer[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
    return buffer.data();
}
const char* TextToLower(const char* text) {
    static std::array<char, kBufferLen> buffer{};
    buffer.fill(0);
    if (text)
        for (int i = 0; i < kBufferLen - 1 && text[i]; ++i)
            buffer[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
    return buffer.data();
}
const char* TextToPascal(const char* text) {
    static std::array<char, kBufferLen> buffer{};
    buffer.fill(0);
    if (!text) return buffer.data();
    int out = 0;
    bool upperNext = true;
    for (int i = 0; text[i] && out < kBufferLen - 1; ++i) {
        if (text[i] == ' ' || text[i] == '_' || text[i] == '-') { upperNext = true; continue; }
        buffer[out++] = upperNext ? static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])))
                                  : text[i];
        upperNext = false;
    }
    return buffer.data();
}
int TextToInteger(const char* text) { return text ? std::atoi(text) : 0; }
float TextToFloat(const char* text) { return text ? static_cast<float>(std::atof(text)) : 0.0f; }

int TextCopy(char* dst, const char* src) {
    if (!dst || !src) return 0;
    int i = 0;
    while (src[i]) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
    return i;
}

const char* TextToCamel(const char* text) {
    static std::array<char, kBufferLen> buffer{};
    buffer.fill(0);
    if (!text) return buffer.data();
    int out = 0;
    bool upperNext = false;
    for (int i = 0; text[i] && out < kBufferLen - 1; ++i) {
        if (text[i] == ' ' || text[i] == '_' || text[i] == '-') { upperNext = true; continue; }
        char c = text[i];
        buffer[out++] = (out == 0) ? static_cast<char>(std::tolower((unsigned char)c))
                       : upperNext ? static_cast<char>(std::toupper((unsigned char)c)) : c;
        upperNext = false;
    }
    return buffer.data();
}
const char* TextToSnake(const char* text) {
    static std::array<char, kBufferLen> buffer{};
    buffer.fill(0);
    if (!text) return buffer.data();
    int out = 0;
    for (int i = 0; text[i] && out < kBufferLen - 2; ++i) {
        char c = text[i];
        if (c == ' ' || c == '-') { buffer[out++] = '_'; continue; }
        if (std::isupper((unsigned char)c)) {
            if (out > 0 && buffer[out - 1] != '_') buffer[out++] = '_';
            c = static_cast<char>(std::tolower((unsigned char)c));
        }
        buffer[out++] = c;
    }
    return buffer.data();
}

// Heap-returning variants of the buffer helpers.
char* TextReplaceAlloc(const char* text, const char* replace, const char* by) {
    return TextReplace(text, replace, by);
}
char* TextInsertAlloc(const char* text, const char* insert, int position) {
    return TextInsert(text, insert, position);
}

namespace {
// Return the substring between the first `start` and the following `end`.
std::string BetweenImpl(const char* text, const char* start, const char* end) {
    if (!text || !start || !end) return {};
    const char* s = std::strstr(text, start);
    if (!s) return {};
    s += std::strlen(start);
    const char* e = std::strstr(s, end);
    if (!e) return {};
    return std::string(s, e);
}
} // namespace

const char* TextGetBetween(const char* text, const char* start, const char* end) {
    static std::array<char, kBufferLen> buffer{};
    buffer.fill(0);
    std::string r = BetweenImpl(text, start, end);
    std::strncpy(buffer.data(), r.c_str(), kBufferLen - 1);
    return buffer.data();
}
const char* GetTextBetween(const char* text, const char* begin, const char* end) {
    return TextGetBetween(text, begin, end);
}
char* TextReplaceBetween(const char* text, const char* start, const char* end, const char* by) {
    if (!text || !start || !end || !by) return nullptr;
    const char* s = std::strstr(text, start);
    if (!s) { char* c = static_cast<char*>(std::malloc(std::strlen(text) + 1)); std::strcpy(c, text); return c; }
    const char* inner = s + std::strlen(start);
    const char* e = std::strstr(inner, end);
    if (!e) { char* c = static_cast<char*>(std::malloc(std::strlen(text) + 1)); std::strcpy(c, text); return c; }
    std::string out(text, inner);
    out += by;
    out += e;
    char* result = static_cast<char*>(std::malloc(out.size() + 1));
    std::memcpy(result, out.c_str(), out.size() + 1);
    return result;
}
char* TextReplaceBetweenAlloc(const char* text, const char* start, const char* end, const char* by) {
    return TextReplaceBetween(text, start, end, by);
}
const char* TextRemoveSpaces(const char* text) {
    static std::array<char, kBufferLen> buffer{};
    buffer.fill(0);
    if (!text) return buffer.data();
    int out = 0;
    for (int i = 0; text[i] && out < kBufferLen - 1; ++i)
        if (!std::isspace((unsigned char)text[i])) buffer[out++] = text[i];
    return buffer.data();
}

// ---------------------------------------------------------------------------
// Unicode / codepoints
// ---------------------------------------------------------------------------
int GetCodepoint(const char* text, int* codepointSize) {
    // Alias of GetCodepointNext (raylib compatibility name).
    return GetCodepointNext(text, codepointSize);
}
int GetCodepointPrevious(const char* text, int* codepointSize) {
    // Walk backwards over UTF-8 continuation bytes (0b10xxxxxx).
    const auto* s = reinterpret_cast<const unsigned char*>(text);
    int back = 0;
    do { --s; ++back; } while ((*s & 0xC0) == 0x80 && back < 4);
    int size = 0;
    const int cp = GetCodepointNext(reinterpret_cast<const char*>(s), &size);
    if (codepointSize) *codepointSize = size;
    return cp;
}
int* LoadCodepoints(const std::string& text, int* count) {
    std::vector<int> cps;
    const char* p = text.c_str();
    const char* end = p + text.size();
    while (p < end) {
        int sz = 1;
        cps.push_back(GetCodepointNext(p, &sz));
        p += sz;
    }
    if (count) *count = static_cast<int>(cps.size());
    if (cps.empty()) return nullptr;
    int* out = static_cast<int*>(std::malloc(sizeof(int) * cps.size()));
    std::memcpy(out, cps.data(), sizeof(int) * cps.size());
    return out;
}
void UnloadCodepoints(int* codepoints) { std::free(codepoints); }
char* LoadUTF8(const int* codepoints, int length) {
    if (!codepoints || length <= 0) return nullptr;
    std::string out;
    for (int i = 0; i < length; ++i) {
        int sz = 0;
        const char* enc = CodepointToUTF8(codepoints[i], &sz);
        out.append(enc, sz);
    }
    char* result = static_cast<char*>(std::malloc(out.size() + 1));
    std::memcpy(result, out.c_str(), out.size() + 1);
    return result;
}
void UnloadUTF8(char* text) { std::free(text); }

// ---------------------------------------------------------------------------
// Text lines
// ---------------------------------------------------------------------------
namespace { int g_textLineSpacing = 2; }
void SetTextLineSpacing(int spacing) { g_textLineSpacing = spacing; }

char** LoadTextLines(const std::string& text, int* count) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    lines.push_back(cur);
    if (count) *count = static_cast<int>(lines.size());
    char** out = static_cast<char**>(std::malloc(sizeof(char*) * lines.size()));
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out[i] = static_cast<char*>(std::malloc(lines[i].size() + 1));
        std::memcpy(out[i], lines[i].c_str(), lines[i].size() + 1);
    }
    return out;
}
void UnloadTextLines(char** lines, int count) {
    if (!lines) return;
    for (int i = 0; i < count; ++i) std::free(lines[i]);
    std::free(lines);
}

} // namespace meowyrender

#include "DocumentStore.h"

#include <algorithm>
#include <utility>

namespace lsp {
namespace {

/// Number of bytes and UTF-16 code units occupied by the code point starting at
/// `offset`. Malformed input is treated as a single 1-byte, 1-unit character so
/// the mapping can never throw or loop forever on a file that is being typed.
inline std::pair<std::size_t, int> decodeCodePoint(std::string_view text, std::size_t offset) {
    const auto lead = static_cast<unsigned char>(text[offset]);

    std::size_t length = 1;
    if (lead < 0x80) {
        length = 1;
    } else if ((lead & 0xE0) == 0xC0) {
        length = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
    } else {
        return {1, 1};
    }

    if (offset + length > text.size())
        return {1, 1};

    for (std::size_t i = 1; i < length; ++i) {
        if ((static_cast<unsigned char>(text[offset + i]) & 0xC0) != 0x80)
            return {1, 1};
    }

    // Only 4-byte sequences are outside the BMP, and those are the only ones
    // that need a surrogate pair (2 UTF-16 code units).
    return {length, length == 4 ? 2 : 1};
}

/// Byte offset one past the last character of `line`, excluding `\r\n`.
std::size_t lineContentEnd(const DocumentStore::Document &document, std::size_t line) {
    std::size_t end = line + 1 < document.lineStarts.size() ? document.lineStarts[line + 1] : document.text.size();

    if (end > document.lineStarts[line] && end <= document.text.size() && document.text[end - 1] == '\n') {
        --end;
        if (end > document.lineStarts[line] && document.text[end - 1] == '\r')
            --end;
    }

    return end;
}

std::size_t clampedLine(const DocumentStore::Document &document, int line) {
    if (document.lineStarts.empty())
        return 0;

    const auto requested = static_cast<std::size_t>(std::max(line, 0));
    return std::min(requested, document.lineStarts.size() - 1);
}

} // namespace

void DocumentStore::open(Document document) {
    rebuildLineIndex(document);
    const std::string uri = document.uri;
    m_Documents.insert_or_assign(uri, std::move(document));
}

bool DocumentStore::change(const std::string &uri, const std::string &text, int version) {
    const auto entry = m_Documents.find(uri);
    if (entry == m_Documents.end())
        return false;

    entry->second.text = text;
    entry->second.version = version;
    rebuildLineIndex(entry->second);
    return true;
}

bool DocumentStore::applyChange(const std::string &uri, const std::optional<Range> &range, const std::string &text,
                                int version) {
    const auto entry = m_Documents.find(uri);
    if (entry == m_Documents.end())
        return false;

    Document &document = entry->second;

    if (!range) {
        document.text = text;
    } else {
        std::size_t start = positionToOffset(document, range->start);
        std::size_t end = positionToOffset(document, range->end);
        if (end < start)
            end = start;

        document.text.replace(start, end - start, text);
    }

    document.version = version;
    rebuildLineIndex(document);
    return true;
}

bool DocumentStore::close(const std::string &uri) { return m_Documents.erase(uri) > 0; }

const DocumentStore::Document *DocumentStore::find(const std::string &uri) const {
    const auto entry = m_Documents.find(uri);
    return entry == m_Documents.end() ? nullptr : &entry->second;
}

bool DocumentStore::contains(const std::string &uri) const { return m_Documents.contains(uri); }

void DocumentStore::rebuildLineIndex(Document &document) {
    document.lineStarts.clear();
    document.lineStarts.push_back(0);

    for (std::size_t i = 0; i < document.text.size(); ++i) {
        if (document.text[i] == '\n')
            document.lineStarts.push_back(i + 1);
    }
}

std::size_t DocumentStore::positionToOffset(const Document &document, Position position) {
    if (document.lineStarts.empty())
        return 0;

    const std::size_t line = clampedLine(document, position.line);
    const std::size_t contentEnd = lineContentEnd(document, line);

    std::size_t offset = document.lineStarts[line];
    int units = 0;
    const int target = std::max(position.character, 0);

    while (offset < contentEnd && units < target) {
        const auto [bytes, codeUnits] = decodeCodePoint(document.text, offset);
        if (offset + bytes > contentEnd) {
            offset = contentEnd;
            break;
        }

        offset += bytes;
        units += codeUnits;
    }

    return offset;
}

Position DocumentStore::offsetToPosition(const Document &document, std::size_t offset) {
    if (document.lineStarts.empty())
        return {};

    offset = std::min(offset, document.text.size());

    const auto upper = std::upper_bound(document.lineStarts.begin(), document.lineStarts.end(), offset);
    const auto line = static_cast<std::size_t>(upper - document.lineStarts.begin()) - 1;

    std::size_t cursor = document.lineStarts[line];
    int character = 0;

    while (cursor < offset) {
        const auto [bytes, codeUnits] = decodeCodePoint(document.text, cursor);
        if (cursor + bytes > offset)
            break;

        cursor += bytes;
        character += codeUnits;
    }

    return {static_cast<int>(line), character};
}

Range DocumentStore::rangeFor(const Document &document, std::size_t offset, std::size_t length) {
    return {offsetToPosition(document, offset), offsetToPosition(document, offset + length)};
}

std::string_view DocumentStore::lineText(const Document &document, int line) {
    if (document.lineStarts.empty())
        return {};

    const std::size_t index = clampedLine(document, line);
    const std::size_t start = document.lineStarts[index];
    const std::size_t end = lineContentEnd(document, index);

    return std::string_view(document.text).substr(start, end - start);
}

} // namespace lsp

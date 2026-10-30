#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lsp {

/// A position as defined by the LSP: zero-based line, and a character offset
/// counted in UTF-16 code units (not bytes).
struct Position {
    int line = 0;
    int character = 0;
};

struct Range {
    Position start;
    Position end;
};

/// Owns the open documents and translates between LSP positions (UTF-16 code
/// units) and byte offsets into the UTF-8 text.
class DocumentStore {
public:
    struct Document {
        std::string uri;
        std::string languageId;
        std::string text;
        int version = 0;
        /// Byte offset at which each line starts; always has at least one entry.
        std::vector<std::size_t> lineStarts;
    };

    /// Inserts the document, replacing any existing one with the same URI.
    void open(Document document);

    /// Replaces the whole text. Returns false when the document is unknown.
    bool change(const std::string &uri, const std::string &text, int version);

    /// Applies one `textDocument/didChange` entry. A nullopt range replaces the
    /// whole document; otherwise `range` (relative to the current text) is
    /// replaced with `text`. Returns false when the document is unknown.
    bool applyChange(const std::string &uri, const std::optional<Range> &range, const std::string &text, int version);

    /// Returns false when the document is unknown.
    bool close(const std::string &uri);

    const Document *find(const std::string &uri) const;
    bool contains(const std::string &uri) const;
    std::size_t size() const { return m_Documents.size(); }

    /// Recomputes `lineStarts` from `text`. Also skips a `\r` before each `\n`.
    static void rebuildLineIndex(Document &document);

    /// Clamps out-of-range lines/characters instead of failing. A character
    /// inside a surrogate pair rounds up to the next code point boundary.
    static std::size_t positionToOffset(const Document &document, Position position);

    /// Byte offsets inside a multi-byte code point or inside a `\r\n` terminator
    /// have no exact position and are reported at the nearest boundary, so this
    /// is the inverse of positionToOffset only at code point boundaries.
    static Position offsetToPosition(const Document &document, std::size_t offset);
    static Range rangeFor(const Document &document, std::size_t offset, std::size_t length);

    /// The text of `line` without its line terminator.
    static std::string_view lineText(const Document &document, int line);

private:
    std::unordered_map<std::string, Document> m_Documents;
};

} // namespace lsp

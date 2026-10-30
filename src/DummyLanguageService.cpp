#include "DummyLanguageService.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string_view>
#include <utility>

namespace lsp {
namespace {

bool isWordChar(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

/// Byte range of the word surrounding `offset`; empty when the offset is not
/// inside a word. Identifiers are scanned byte-wise, which is enough while the
/// real lexer is not linked in.
std::pair<std::size_t, std::size_t> wordBoundsAt(std::string_view text, std::size_t offset) {
    offset = std::min(offset, text.size());

    std::size_t start = offset;
    std::size_t end = offset;

    while (start > 0 && isWordChar(text[start - 1]))
        --start;
    while (end < text.size() && isWordChar(text[end]))
        ++end;

    return {start, end};
}

} // namespace

std::vector<Diagnostic> DummyLanguageService::diagnostics(const DocumentStore::Document &document) {
    // Flag `import` statements that do not name a package.
    static const std::regex importPattern(R"(\bimport (?!\w))");

    std::vector<Diagnostic> diagnostics;
    for (auto i = std::sregex_iterator(document.text.begin(), document.text.end(), importPattern);
         i != std::sregex_iterator(); ++i) {
        const std::smatch &match = *i;
        diagnostics.push_back({DocumentStore::rangeFor(document, static_cast<std::size_t>(match.position()),
                                                       static_cast<std::size_t>(match.length())),
                               1, "No package name provided", "Swirl"});
    }

    return diagnostics;
}

std::optional<HoverInfo> DummyLanguageService::hover(const DocumentStore::Document &document, Position position) {
    const std::size_t offset = DocumentStore::positionToOffset(document, position);
    const auto [start, end] = wordBoundsAt(document.text, offset);

    if (start == end)
        return HoverInfo{"No info available", std::nullopt};

    const std::string word = document.text.substr(start, end - start);
    return HoverInfo{"Hover info for `" + word + "`", DocumentStore::rangeFor(document, start, end - start)};
}

std::vector<CompletionItem> DummyLanguageService::completion(const DocumentStore::Document &, Position) {
    return {
            {"Hello", 1, "0001", "Hello from swirl C++ lsp server", "Text completion from swirl LSP server"},
            {"from", 2, "0002", "from swirl C++ lsp server", "Method completion from swirl LSP server"},
            {"swirl", 3, "0003", "swirl C++ lsp server", "Function completion from swirl LSP server"},
            {"C++", 6, "0004", "C++ lsp server", "Class completion from swirl LSP server"},
            {"lsp", 7, "0005", "lsp server", "Interface completion from swirl LSP server"},
            {"server", 9, "0006", "server", "Module completion from swirl LSP server"},
    };
}

std::optional<CompletionItem> DummyLanguageService::resolve(CompletionItem item) {
    // Dropping sortText lets the client fall back to its own ordering.
    item.sortText.reset();
    return item;
}

} // namespace lsp

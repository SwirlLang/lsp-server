#pragma once

#include <optional>
#include <string>
#include <vector>

#include "DocumentStore.h"

namespace lsp {

struct Diagnostic {
    Range range;
    int severity = 1; // 1 = Error, 2 = Warning, 3 = Information, 4 = Hint
    std::string message;
    std::string source = "Swirl";
};

struct HoverInfo {
    std::string markdown;
    std::optional<Range> range;
};

struct CompletionItem {
    std::string label;
    int kind = 1; // 1 = Text, 2 = Method, 3 = Function, 6 = Class, 7 = Interface, 9 = Module
    std::optional<std::string> sortText;
    std::optional<std::string> detail;
    std::optional<std::string> documentation;
};

/// Boundary between the protocol layer and everything that understands Swirl.
/// The server only ever talks to this interface, so a compiler-backed
/// implementation can replace the dummy one without touching JSON handling.
class LanguageService {
public:
    virtual ~LanguageService() = default;

    virtual void didOpen(const DocumentStore::Document &) {}
    virtual void didChange(const DocumentStore::Document &) {}
    virtual void didClose(const std::string &) {}

    virtual std::vector<Diagnostic> diagnostics(const DocumentStore::Document &document) = 0;
    virtual std::optional<HoverInfo> hover(const DocumentStore::Document &document, Position position) = 0;
    virtual std::vector<CompletionItem> completion(const DocumentStore::Document &document, Position position) = 0;
    virtual std::optional<CompletionItem> resolve(CompletionItem item) = 0;
};

} // namespace lsp

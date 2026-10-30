#pragma once

#include <optional>
#include <vector>

#include "LanguageService.h"

namespace lsp {

/// Placeholder implementation: the regex "import" check and the fixed
/// completion list that used to live in Server. It exists so the protocol layer
/// can be finished before the compiler front end is linked in.
class DummyLanguageService final : public LanguageService {
public:
    std::vector<Diagnostic> diagnostics(const DocumentStore::Document &document) override;
    std::optional<HoverInfo> hover(const DocumentStore::Document &document, Position position) override;
    std::vector<CompletionItem> completion(const DocumentStore::Document &document, Position position) override;
    std::optional<CompletionItem> resolve(CompletionItem item) override;
};

} // namespace lsp

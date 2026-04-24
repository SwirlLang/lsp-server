#pragma once
#include <string>
#include <unordered_map>
#include "json.hpp"

using json = nlohmann::json;

namespace lsp {

class Server {
public:
    Server();
    ~Server();
    void run();

private:
    // Document manger: URI: content
    std::unordered_map<std::string, std::string> documents;

    // Track document version
    std::unordered_map<std::string, int> document_versions;

    void storeDocument(const std::string &uri, const std::string &content, int version = 0);
    void updateDocument(const std::string &uri, const std::string &newContent, int version);
    void removeDocument(const std::string &uri);
    std::string getDocument(const std::string &uri);
    bool hasDocument(const std::string &uri);

    // helper functions
    void processRequest(const json &request);
    void sendResponse(const json &response);
    void parseMessage(const std::string &jsonContent);

    // Request handlers
    void onInitialize(const json &request);
    void onDidOpen(const json &request);
    void onDidChangeContent(const json &request);
    void onCompletion(const json &request);
    void onCompletionResolve(const json &request);
    void onHover(const json &request);
    void onSetTrace(const json &request);

    void validateDocument(const std::string &uri);
    std::pair<int, int> calculatePosition(const std::string &content, size_t offset);

    enum class LspMethod {
        Initialize,
        TextDocumentDidOpen,
        TextDocumentDidChange,
        TextDocumentDidSave,
        TextDocumentDidClose,
        TextDocumentCompletion,
        CompletionItemResolve,
        TextDocumentHover,
        SetTrace
    };
    std::unordered_map<std::string_view, LspMethod> LspMethods = {
            {"initialize", LspMethod::Initialize},
            {"textDocument/didOpen", LspMethod::TextDocumentDidOpen},
            {"textDocument/didChange", LspMethod::TextDocumentDidChange},
            {"textDocument/didSave", LspMethod::TextDocumentDidSave},
            {"textDocument/didClose", LspMethod::TextDocumentDidClose},
            {"textDocument/completion", LspMethod::TextDocumentCompletion},
            {"completionItem/resolve", LspMethod::CompletionItemResolve},
            {"textDocument/hover", LspMethod::TextDocumentHover},
            {"$/setTrace", LspMethod::SetTrace}};
};
} // namespace lsp

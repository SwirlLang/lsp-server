#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "DocumentStore.h"
#include "json.hpp"

using json = nlohmann::json;

namespace lsp {

class LanguageService;
class Transport;

class Server {
public:
    Server();
    ~Server();
    void run(std::istream &in, std::ostream &out);

    /// 0 when the session ended after a `shutdown`, 1 otherwise.
    int exitCode() const;

private:
    enum class LspMethod {
        Initialize,
        Initialized,
        Shutdown,
        Exit,
        TextDocumentDidOpen,
        TextDocumentDidChange,
        TextDocumentDidSave,
        TextDocumentDidClose,
        TextDocumentCompletion,
        CompletionItemResolve,
        TextDocumentHover,
        SetTrace
    };

    inline static const std::unordered_map<std::string_view, LspMethod> LspMethods = {
            {"initialize", LspMethod::Initialize},
            {"initialized", LspMethod::Initialized},
            {"shutdown", LspMethod::Shutdown},
            {"exit", LspMethod::Exit},
            {"textDocument/didOpen", LspMethod::TextDocumentDidOpen},
            {"textDocument/didChange", LspMethod::TextDocumentDidChange},
            {"textDocument/didSave", LspMethod::TextDocumentDidSave},
            {"textDocument/didClose", LspMethod::TextDocumentDidClose},
            {"textDocument/completion", LspMethod::TextDocumentCompletion},
            {"completionItem/resolve", LspMethod::CompletionItemResolve},
            {"textDocument/hover", LspMethod::TextDocumentHover},
            {"$/setTrace", LspMethod::SetTrace}};

    DocumentStore m_Documents;
    std::unique_ptr<LanguageService> m_LanguageService;

    // Active transport for the duration of run(); null outside it.
    Transport *m_Transport = nullptr;

    bool m_ShutdownRequested = false;
    bool m_ExitRequested = false;

    // Transport helpers
    void sendResponse(const json &response);
    void sendResult(const json &id, const json &result);
    void sendError(const json &id, int code, const std::string &message);

    // Dispatch
    void parseMessage(const std::string &jsonContent);
    void dispatch(LspMethod method, const json &message);
    static bool isRequestMethod(LspMethod method);

    // Request handlers
    void onInitialize(const json &request);
    void onDidOpen(const json &message);
    void onDidChangeContent(const json &message);
    void onDidSave(const json &message);
    void onDidClose(const json &message);
    void onCompletion(const json &request);
    void onCompletionResolve(const json &request);
    void onHover(const json &request);
    void onSetTrace(const json &message);

    void validateDocument(const std::string &uri);
};
} // namespace lsp

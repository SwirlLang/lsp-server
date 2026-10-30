#include "LSPServer.h"

#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "DummyLanguageService.h"
#include "Transport.h"

namespace lsp {
namespace {

std::string documentUri(const json &message) {
    return message.at("params").at("textDocument").at("uri").get<std::string>();
}

json toJson(Position position) { return {{"line", position.line}, {"character", position.character}}; }

json toJson(const Range &range) { return {{"start", toJson(range.start)}, {"end", toJson(range.end)}}; }

json toJson(const Diagnostic &diagnostic) {
    return {{"severity", diagnostic.severity},
            {"range", toJson(diagnostic.range)},
            {"message", diagnostic.message},
            {"source", diagnostic.source}};
}

json toJson(const HoverInfo &hover) {
    json result = {{"contents", {{"kind", "markdown"}, {"value", hover.markdown}}}};
    if (hover.range)
        result["range"] = toJson(*hover.range);
    return result;
}

json toJson(const CompletionItem &item) {
    json result = {{"label", item.label}, {"kind", item.kind}};
    if (item.sortText)
        result["sortText"] = *item.sortText;
    if (item.detail)
        result["detail"] = *item.detail;
    if (item.documentation)
        result["documentation"] = *item.documentation;
    return result;
}

CompletionItem completionItemFromJson(const json &value) {
    CompletionItem item;
    item.label = value.at("label").get<std::string>();
    item.kind = value.value("kind", 1);

    if (const auto it = value.find("sortText"); it != value.end() && it->is_string())
        item.sortText = it->get<std::string>();
    if (const auto it = value.find("detail"); it != value.end() && it->is_string())
        item.detail = it->get<std::string>();
    if (const auto it = value.find("documentation"); it != value.end() && it->is_string())
        item.documentation = it->get<std::string>();

    return item;
}

std::optional<Range> parseRange(const json &value) {
    if (!value.is_object())
        return std::nullopt;

    const json &start = value.at("start");
    const json &end = value.at("end");

    return Range{{start.at("line").get<int>(), start.at("character").get<int>()},
                 {end.at("line").get<int>(), end.at("character").get<int>()}};
}

Position parsePosition(const json &value) {
    return {value.at("line").get<int>(), value.at("character").get<int>()};
}

} // namespace

Server::Server() : m_LanguageService(std::make_unique<DummyLanguageService>()) {
    std::cerr << "LSP Server initialized" << std::endl;
}

Server::~Server() = default;

void Server::run(std::istream &in, std::ostream &out) {
    Transport transport(in, out);
    m_Transport = &transport;

    while (!m_ExitRequested) {
        const auto message = transport.read();
        if (!message)
            break;

        parseMessage(*message);
    }

    m_Transport = nullptr;
}

int Server::exitCode() const { return m_ShutdownRequested ? 0 : 1; }

void Server::parseMessage(const std::string &jsonContent) {
    json message;
    try {
        message = json::parse(jsonContent);
    } catch (const json::parse_error &e) {
        std::cerr << "[JSON] Parse error: " << e.what() << std::endl;
        sendError(json(), -32700, "Parse error");
        return;
    }

    if (!message.is_object()) {
        std::cerr << "[Protocol] Ignoring non-object message" << std::endl;
        sendError(json(), -32600, "Invalid Request");
        return;
    }

    const bool isRequest = message.contains("id");
    const json id = isRequest ? message.at("id") : json();

    if (!message.contains("method") || !message.at("method").is_string()) {
        std::cerr << "[Protocol] Message without a method" << std::endl;
        if (isRequest)
            sendError(id, -32600, "Invalid Request");
        return;
    }

    const std::string method = message.at("method").get<std::string>();
    std::cerr << "[Received " << (isRequest ? "Request" : "Notification") << "] " << method << std::endl;

    const auto entry = LspMethods.find(method);
    if (entry == LspMethods.end()) {
        if (isRequest) {
            sendError(id, -32601, "Method not found: " + method);
        } else {
            std::cerr << "[Ignored] Unknown notification: " << method << std::endl;
        }
        return;
    }

    if (m_ShutdownRequested && entry->second != LspMethod::Shutdown && entry->second != LspMethod::Exit) {
        std::cerr << "[Protocol] Ignoring " << method << " after shutdown" << std::endl;
        if (isRequest)
            sendError(id, -32600, "Invalid Request: server is shutting down");
        return;
    }

    if (isRequestMethod(entry->second) != isRequest) {
        std::cerr << "[Protocol] " << method << " sent as a "
                  << (isRequest ? "request but it is a notification" : "notification but it is a request")
                  << "; ignoring" << std::endl;
        return;
    }

    try {
        dispatch(entry->second, message);
    } catch (const json::exception &e) {
        std::cerr << "[Protocol] Malformed params for " << method << ": " << e.what() << std::endl;
        if (isRequest)
            sendError(id, -32602, "Invalid params");
    } catch (const std::exception &e) {
        std::cerr << "[Internal] " << method << ": " << e.what() << std::endl;
        if (isRequest)
            sendError(id, -32603, "Internal error");
    }
}

bool Server::isRequestMethod(LspMethod method) {
    switch (method) {
        case LspMethod::Initialize:
        case LspMethod::Shutdown:
        case LspMethod::TextDocumentCompletion:
        case LspMethod::CompletionItemResolve:
        case LspMethod::TextDocumentHover:
            return true;

        case LspMethod::Initialized:
        case LspMethod::Exit:
        case LspMethod::TextDocumentDidOpen:
        case LspMethod::TextDocumentDidChange:
        case LspMethod::TextDocumentDidSave:
        case LspMethod::TextDocumentDidClose:
        case LspMethod::SetTrace:
            return false;
    }

    return false;
}

void Server::dispatch(LspMethod method, const json &message) {
    switch (method) {
        case LspMethod::Initialize:
            onInitialize(message);
            break;
        case LspMethod::Initialized:
            std::cerr << "[Initialized] Client has completed initialization." << std::endl;
            break;
        case LspMethod::Shutdown:
            m_ShutdownRequested = true;
            sendResult(message.at("id"), json());
            break;
        case LspMethod::Exit:
            m_ExitRequested = true;
            break;
        case LspMethod::TextDocumentDidOpen:
            onDidOpen(message);
            break;
        case LspMethod::TextDocumentDidChange:
            onDidChangeContent(message);
            break;
        case LspMethod::TextDocumentDidSave:
            onDidSave(message);
            break;
        case LspMethod::TextDocumentDidClose:
            onDidClose(message);
            break;
        case LspMethod::TextDocumentCompletion:
            onCompletion(message);
            break;
        case LspMethod::CompletionItemResolve:
            onCompletionResolve(message);
            break;
        case LspMethod::TextDocumentHover:
            onHover(message);
            break;
        case LspMethod::SetTrace:
            onSetTrace(message);
            break;
    }
}

void Server::sendResponse(const json &response) {
    if (m_Transport == nullptr) {
        std::cerr << "[Server] Cannot send a response without an active transport" << std::endl;
        return;
    }
    m_Transport->write(response.dump());
}

void Server::sendResult(const json &id, const json &result) {
    sendResponse({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

void Server::sendError(const json &id, int code, const std::string &message) {
    sendResponse({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}});
}

void Server::onInitialize(const json &request) {
    json response = {{"jsonrpc", "2.0"},
                     {"id", request.at("id")},
                     {"result",
                      {{"capabilities",
                        {{"textDocumentSync", {{"openClose", true}, {"change", 1}, {"save", false}}},
                         {"completionProvider", {{"resolveProvider", true}, {"triggerCharacters", {".", "@"}}}},
                         {"hoverProvider", true}}},
                       {"serverInfo", {{"name", "swirl-lsp"}, {"version", "0.1.0"}}}}}};
    sendResponse(response);
}

void Server::onDidOpen(const json &message) {
    const json &textDocument = message.at("params").at("textDocument");
    const std::string uri = textDocument.at("uri").get<std::string>();

    DocumentStore::Document document;
    document.uri = uri;
    document.languageId = textDocument.value("languageId", std::string());
    document.text = textDocument.at("text").get<std::string>();
    document.version = textDocument.value("version", 0);

    const std::size_t length = document.text.length();
    m_Documents.open(std::move(document));

    if (const DocumentStore::Document *stored = m_Documents.find(uri))
        m_LanguageService->didOpen(*stored);

    validateDocument(uri);

    std::cerr << "[Did Open] " << uri << " (length: " << length << ")" << std::endl;
}

void Server::onDidChangeContent(const json &message) {
    const json &params = message.at("params");
    const std::string uri = params.at("textDocument").at("uri").get<std::string>();
    const int version = params.at("textDocument").value("version", -1);

    if (!m_Documents.contains(uri)) {
        std::cerr << "[Did Change] Ignoring unknown document: " << uri << std::endl;
        return;
    }

    for (const auto &change: params.at("contentChanges")) {
        const std::string text = change.at("text").get<std::string>();
        const std::optional<Range> range = change.contains("range") ? parseRange(change.at("range")) : std::nullopt;

        if (!m_Documents.applyChange(uri, range, text, version)) {
            std::cerr << "[Did Change] Ignoring unknown document: " << uri << std::endl;
            return;
        }
    }

    if (const DocumentStore::Document *stored = m_Documents.find(uri))
        m_LanguageService->didChange(*stored);

    validateDocument(uri);

    std::cerr << "[Did Change] " << uri << " (version: " << version << ")" << std::endl;
}

void Server::onDidSave(const json &message) {
    std::cerr << "[Did Save] " << documentUri(message) << std::endl;
}

void Server::onDidClose(const json &message) {
    const std::string uri = documentUri(message);
    m_LanguageService->didClose(uri);
    if (!m_Documents.close(uri))
        std::cerr << "[Did Close] Unknown document: " << uri << std::endl;
}

void Server::onCompletion(const json &request) {
    const std::string uri = documentUri(request);
    const Position position = parsePosition(request.at("params").at("position"));

    const DocumentStore::Document *document = m_Documents.find(uri);
    if (document == nullptr) {
        sendResult(request.at("id"), nullptr);
        return;
    }

    json items = json::array();
    for (const CompletionItem &item: m_LanguageService->completion(*document, position))
        items.push_back(toJson(item));

    sendResult(request.at("id"), {{"isIncomplete", false}, {"items", items}});
}

void Server::onCompletionResolve(const json &request) {
    const std::optional<CompletionItem> resolved = m_LanguageService->resolve(completionItemFromJson(request.at("params")));
    sendResult(request.at("id"), resolved ? toJson(*resolved) : request.at("params"));
}

void Server::onHover(const json &request) {
    const std::string uri = documentUri(request);
    const Position position = parsePosition(request.at("params").at("position"));

    const DocumentStore::Document *document = m_Documents.find(uri);
    if (document == nullptr) {
        sendResult(request.at("id"), nullptr);
        return;
    }

    const std::optional<HoverInfo> hover = m_LanguageService->hover(*document, position);
    if (!hover) {
        sendResult(request.at("id"), nullptr);
        return;
    }

    sendResult(request.at("id"), toJson(*hover));
}

void Server::onSetTrace(const json &message) {
    const json &params = message.at("params");
    std::cerr << "[Set Trace] " << params.value("value", "off") << std::endl;
}

void Server::validateDocument(const std::string &uri) {
    const DocumentStore::Document *document = m_Documents.find(uri);
    if (document == nullptr)
        return;

    const std::vector<Diagnostic> reported = m_LanguageService->diagnostics(*document);

    json diagnostics = json::array();
    for (const Diagnostic &diagnostic: reported)
        diagnostics.push_back(toJson(diagnostic));

    json diagnosticsNotification = {{"jsonrpc", "2.0"},
                                    {"method", "textDocument/publishDiagnostics"},
                                    {"params", {{"uri", uri}, {"diagnostics", diagnostics}}}};
    sendResponse(diagnosticsNotification);

    std::cerr << "[Diagnostics] Found " << reported.size() << " issues in " << uri << std::endl;
}

} // namespace lsp

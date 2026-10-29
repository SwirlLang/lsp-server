#include "LSPServer.h"

#include <iostream>
#include <regex>
#include <string>

#include "Transport.h"
#include "utils.h"

namespace lsp {
namespace {

std::string documentUri(const json &message) {
    return message.at("params").at("textDocument").at("uri").get<std::string>();
}

} // namespace

Server::Server() { std::cerr << "LSP Server initialized" << std::endl; }

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
    const std::string text = textDocument.at("text").get<std::string>();
    const int version = textDocument.value("version", 0);

    storeDocument(uri, text, version);
    validateDocument(uri);

    std::cerr << "[Did Open] " << uri << " (length: " << text.length() << ")" << std::endl;
}

void Server::onDidChangeContent(const json &message) {
    const json &params = message.at("params");
    const std::string uri = params.at("textDocument").at("uri").get<std::string>();
    const int version = params.at("textDocument").value("version", -1);

    if (!hasDocument(uri)) {
        std::cerr << "[Did Change] Ignoring unknown document: " << uri << std::endl;
        return;
    }

    for (const auto &change: params.at("contentChanges")) {
        const std::string text = change.at("text").get<std::string>();

        if (change.contains("range")) {
            std::cerr << "[Did Change] Incremental updates are not implemented yet" << std::endl;
            continue;
        }

        updateDocument(uri, text, version);
    }

    validateDocument(uri);

    std::cerr << "[Did Change] " << uri << " (version: " << version << ")" << std::endl;
}

void Server::onDidSave(const json &message) {
    std::cerr << "[Did Save] " << documentUri(message) << std::endl;
}

void Server::onDidClose(const json &message) { removeDocument(documentUri(message)); }

void Server::onCompletion(const json &request) {
    json response = {{"jsonrpc", "2.0"},
                     {"id", request.at("id")},
                     {"result",
                      {{"isIncomplete", false},
                       {"items",
                        {{{"label", "Hello"},
                          {"kind", 1}, // Text
                          {"sortText", "0001"},
                          {"detail", "Hello from swirl C++ lsp server"},
                          {"documentation", "Text completion from swirl LSP server"}},
                         {{"label", "from"},
                          {"kind", 2}, // Method
                          {"sortText", "0002"},
                          {"detail", "from swirl C++ lsp server"},
                          {"documentation", "Method completion from swirl LSP server"}},
                         {{"label", "swirl"},
                          {"kind", 3}, // Function
                          {"sortText", "0003"},
                          {"detail", "swirl C++ lsp server"},
                          {"documentation", "Function completion from swirl LSP server"}},
                         {{"label", "C++"},
                          {"kind", 6}, // Class
                          {"sortText", "0004"},
                          {"detail", "C++ lsp server"},
                          {"documentation", "Class completion from swirl LSP server"}},
                         {{"label", "lsp"},
                          {"kind", 7}, // Interface
                          {"sortText", "0005"},
                          {"detail", "lsp server"},
                          {"documentation", "Interface completion from swirl LSP server"}},
                         {{"label", "server"},
                          {"kind", 9}, // Module
                          {"sortText", "0006"},
                          {"detail", "server"},
                          {"documentation", "Module completion from swirl LSP server"}}}}}}};
    sendResponse(response);
}

void Server::onCompletionResolve(const json &request) {
    json result = request.at("params");
    if (result.contains("sortText"))
        result.erase("sortText");

    sendResult(request.at("id"), result);
}

void Server::onHover(const json &request) {
    const std::string uri = documentUri(request);
    const json &position = request.at("params").at("position");
    const int line = position.at("line").get<int>();
    const int character = position.at("character").get<int>();

    const std::string content = getDocument(uri);
    const std::string_view word = getWordAt(content, line, character);

    std::string hoverText = "No info available";
    if (!word.empty())
        hoverText = "Hover info for `" + std::string(word) + "`";

    sendResult(request.at("id"), {{"contents", {{"kind", "markdown"}, {"value", hoverText}}}});
}

void Server::onSetTrace(const json &message) {
    const json &params = message.at("params");
    std::cerr << "[Set Trace] " << params.value("value", "off") << std::endl;
}

void Server::storeDocument(const std::string &uri, const std::string &content, int version) {
    documents[uri] = content;
    document_versions[uri] = version;
    std::cerr << "[Document Stored] " << uri << " (version: " << version << ")" << std::endl;
}

void Server::updateDocument(const std::string &uri, const std::string &newContent, int version) {
    if (!hasDocument(uri))
        return;

    documents[uri] = newContent;
    document_versions[uri] = version;
    std::cerr << "[Document Updated] " << uri << " (version: " << version << ")" << std::endl;
}

void Server::removeDocument(const std::string &uri) {
    documents.erase(uri);
    document_versions.erase(uri);
    std::cerr << "[Document Removed] " << uri << std::endl;
}

std::string Server::getDocument(const std::string &uri) {
    const auto entry = documents.find(uri);
    return entry == documents.end() ? std::string() : entry->second;
}

bool Server::hasDocument(const std::string &uri) { return documents.contains(uri); }

void Server::validateDocument(const std::string &uri) {
    if (!hasDocument(uri))
        return;

    const std::string content = getDocument(uri);

    // Find the import statements and check if they don't provide any
    // package
    std::regex importPattern(R"(\bimport (?!\w))");
    std::sregex_iterator start(content.begin(), content.end(), importPattern);
    std::sregex_iterator end;

    json diagnostics = json::array();

    for (std::sregex_iterator i = start; i != end; ++i) {
        const std::smatch match = *i;
        const size_t matchPos = match.position();
        const size_t matchLength = match.length();

        // Calculate line and character positions
        const auto [startLine, startCharacter] = calculatePosition(content, matchPos);
        const auto [endLine, endCharacter] = calculatePosition(content, matchPos + matchLength);

        json diagnostic = {{"severity", 1}, // Error severity
                           {"range",
                            {{"start", {{"line", startLine}, {"character", startCharacter}}},
                             {"end", {{"line", endLine}, {"character", endCharacter}}}}},
                           {"message", "No package name provided"},
                           {"source", "Swirl"}};

        diagnostics.push_back(diagnostic);
    }

    json diagnosticsNotification = {{"jsonrpc", "2.0"},
                                    {"method", "textDocument/publishDiagnostics"},
                                    {"params", {{"uri", uri}, {"diagnostics", diagnostics}}}};
    sendResponse(diagnosticsNotification);

    std::cerr << "[Diagnostics] Found " << diagnostics.size() << " issues in " << uri << std::endl;
}

} // namespace lsp

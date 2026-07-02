#include "LSPServer.h"
#include <iostream>
#include <regex>
#include <utility>

size_t positionToOffset(const std::string &content, int line, int character) {
    int currentLine = 0;
    int currentChar = 0;
    for (size_t i = 0; i < content.length(); ++i) {
        if (currentLine == line && currentChar == character) {
            return i;
        }
        if (content[i] == '\n') {
            currentLine++;
            currentChar = 0;
        } else {
            currentChar++;
        }
    }
    return content.length(); // Return end of file if not found
}

std::string_view getWordAt(const std::string &content, int line, int character) {
    if (content.empty())
        return "";

    size_t offset = positionToOffset(content, line, character);

    size_t start = offset, end = offset;
    while (start > 0 && std::isalnum(content[start - 1]))
        start--;
    while (end < content.size() && std::isalnum(content[end]))
        end++;

    if (start >= end)
        return "";
    // ;
    return std::string_view(content.c_str() + start, end - start);
}

namespace lsp {

Server::Server() { std::cerr << "LSP Server initialized" << std::endl; }

Server::~Server() {
    // std::cerr << "LSP Server shutting down." << std::endl;
}

void Server::run() {
    // Start the server and listen for incoming requests

    while (true) {
        // Read the "Content-Length" header
        std::string contentLengthHeader;
        std::getline(std::cin, contentLengthHeader);
        if (contentLengthHeader.empty()) {
            // End of stream or client closed connection
            break;
        }

        int contentLength = 0;
        try {
            contentLength = std::stoi(contentLengthHeader.substr(16));
        } catch (const std::exception &e) {
            // Log an error
            continue;
        }

        // Read the mandatory blank line after the header
        std::string blankLine;
        std::getline(std::cin, blankLine);

        // Read the JSON content itself
        std::vector<char> jsonBuffer(contentLength);
        std::cin.read(jsonBuffer.data(), contentLength);
        std::string jsonContent(jsonBuffer.begin(), jsonBuffer.end());

        // Parse and process the message
        parseMessage(jsonContent);
    }
}

void Server::parseMessage(const std::string &jsonContent) {
    try {
        json request = json::parse(jsonContent);
        if (request.contains("method")) {
            std::string method = request["method"];

            std::cerr << "[Received Request] " << method << std::endl;
            
            if (!LspMethods.contains(method)) {
                json errorResponse = {{"jsonrpc", "2.0"},
                                      {"id", 1},
                                      {"error",
                                       {{"code", -32601},
                                        {"message", "Method not found: " + method}}}};
                sendResponse(errorResponse);
            } else {
                switch (LspMethods[method]) {
                    case LspMethod::Initialize:
                        onInitialize(request);
                        break;
                    case LspMethod::Initialized:
                        std::cerr << "[Initialized] Client has completed initialization." << std::endl;
                        break;
                    case LspMethod::TextDocumentDidOpen:
                        onDidOpen(request);
                        break;
                    case LspMethod::TextDocumentDidChange:
                        onDidChangeContent(request);
                        break;
                    case LspMethod::TextDocumentDidSave:
                        std::cerr << "[Did Save] " << request["params"]["textDocument"]["uri"] << std::endl;
                        break;
                    case LspMethod::TextDocumentCompletion:
                        onCompletion(request);
                        break;
                    case LspMethod::CompletionItemResolve:
                        onCompletionResolve(request);
                        break;
                    case LspMethod::TextDocumentHover:
                        onHover(request);
                        std::cerr << "[Hover] " << request["params"]["textDocument"]["uri"] << std::endl;
                        break;
                    case LspMethod::SetTrace:
                        onSetTrace(request);
                        break;
                    default:
                        std::cerr << "Hit default case" << std::endl;
                        json errorResponse = {{"jsonrpc", "2.0"},
                                              {"id", 1},
                                              {"error",
                                               {{"code", -32601},
                                                {"message", "Method not found: " + method}}}};
                        sendResponse(errorResponse);
                }
            }
        }
    } catch (const json::parse_error &e) {
        // Log JSON parsing error
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    }
}

void Server::sendResponse(const json &response) {
    std::string responseStr = response.dump();
    std::cout << "Content-Length: " << responseStr.size() << "\r\n\r\n" << responseStr;
    std::cout.flush();
    // std::cerr << "[Sent Response] " << response.dump(4) << std::endl;
}

void Server::sendWindowMessage(const std::string &message) {
    json response = {{"jsonrpc", "2.0"},
                     {"method", "window/showMessage"},
                     {"params",
                      {{"type", 3}, // Information message
                       {"message", message}}}};
    sendResponse(response);
}

void Server::onInitialize(const json &request) {
    // Handle the "initialize" request
    json response = {{"jsonrpc", "2.0"},
                     {"id", request["id"]},
                     {"result",
                      {{"capabilities",
                        {// Advertise the features your server supports
                         {"textDocumentSync", 1}, // 1 = Full sync
                         {"completionProvider", {{"resolveProvider", true}, {"triggerCharacters", {".", "@"}}}},
                         {"diagnosticsProvider", {{"interFileDependencies", true}, {"workspaceDiagnostics", true}}},
                         {"hoverProvider", true}}}}}};
    sendResponse(response);
    sendWindowMessage("Hello from LSP");
}

void Server::onDidOpen(const json &request) {
    // Handle the "didOpen" notification
    if (request.contains("params") && request["params"].contains("textDocument")) {
        std::string uri = request["params"]["textDocument"]["uri"];
        std::string text = request["params"]["textDocument"]["text"];
        int version = request["params"]["textDocument"]["version"];

        // Store the document
        storeDocument(uri, text, version);

        // Validate the document
        validateDocument(uri);

        std::cerr << "[Did Open] " << uri << " (length: " << text.length() << ")" << std::endl;
    }
}

void Server::onDidChangeContent(const json &request) {
    // Handle the "didChangeContent" notification
    if (request.contains("params")) {
        std::string uri = request["params"]["textDocument"]["uri"];
        int version = request["params"]["textDocument"]["version"];

        // Handle incremental changes
        if (request["params"].contains("contentChanges")) {
            auto changes = request["params"]["contentChanges"];

            for (const auto &change: changes) {
                if (change.contains("text") && !change.contains("range")) {
                    // Full document update
                    std::string newText = change["text"];
                    updateDocument(uri, newText, version);
                } else if (change.contains("range") && change.contains("text")) {
                    // Incremental update - you'd need to implement
                    // range-based updates
                    std::cerr << "[Incremental Change] Not implemented yet" << std::endl;
                }
            }
        }

        // Validate the document after changes
        validateDocument(uri);

        std::cerr << "[Did Change Content] " << uri << " (version: " << version << ")" << std::endl;
    }
}
void Server::onCompletion(const json &request) {
    // Handle the "completion" request
    json response = {{"jsonrpc", "2.0"},
                     {"id", request["id"]},
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
    // Handle the "completionResolve" request
    json result = request["params"];
    if (result.contains("sortText")) {
        result.erase("sortText");
    }

    json response = {{"jsonrpc", "2.0"}, {"id", request["id"]}, {"result", result}};
    sendResponse(response);
}
void Server::onSetTrace(const json &request) {
    // Handle the "setTrace" notification
    std::string traceValue = request["params"]["value"];
    std::cerr << "[Set Trace] " << traceValue << std::endl;
}

void Server::storeDocument(const std::string &uri, const std::string &content, int version) {
    documents[uri] = content;
    document_versions[uri] = version;
    std::cerr << "[Document Stored] " << uri << " (version: " << version << ")" << std::endl;
}

void Server::updateDocument(const std::string &uri, const std::string &newContent, int version) {
    if (hasDocument(uri)) {
        documents[uri] = newContent;
        document_versions[uri] = version;
        std::cerr << "[Document Updated] " << uri << " (version: " << version << ")" << std::endl;
    }
}

void Server::removeDocument(const std::string &uri) {
    documents.erase(uri);
    document_versions.erase(uri);
    std::cerr << "[Document Removed] " << uri << std::endl;
}

std::string Server::getDocument(const std::string &uri) { return documents.contains(uri) ? documents[uri] : ""; }

bool Server::hasDocument(const std::string &uri) { return documents.contains(uri); }

void Server::validateDocument(const std::string &uri) {
    std::string content = getDocument(uri);
    if (content.empty()) {
        return;
    }

    // Find the import statements and check if they don't provide any
    // package
    std::regex import_pattern(R"(\bimport (?!\w))");
    std::sregex_iterator start(content.begin(), content.end(), import_pattern);
    std::sregex_iterator end;

    json diagnostics = json::array();

    for (std::sregex_iterator i = start; i != end; ++i) {
        std::smatch match = *i;
        size_t match_pos = match.position();
        size_t match_length = match.length();

        // Calculate line and character positions
        auto start_pos = calculatePosition(content, match_pos);
        auto end_pos = calculatePosition(content, match_pos + match_length);

        json diagnostic = {{"severity", 1}, // Error severity
                           {"range",
                            {{"start", {{"line", start_pos.first}, {"character", start_pos.second}}},
                             {"end", {{"line", end_pos.first}, {"character", end_pos.second}}}}},
                           {"message", "No package name provided"},
                           {"source", "Swirl"}};

        diagnostics.push_back(diagnostic);
    }

    // Send the computed diagnostics
    json diagnosticsNotification = {{"jsonrpc", "2.0"},
                                    {"method", "textDocument/publishDiagnostics"},
                                    {"params", {{"uri", uri}, {"diagnostics", diagnostics}}}};

    sendResponse(diagnosticsNotification);

    std::cerr << "[Diagnostics] Found " << diagnostics.size() << " issues in " << uri << std::endl;
}

std::pair<int, int> Server::calculatePosition(const std::string &content, size_t offset) {
    int line = 0;
    int character = 0;

    for (size_t i = 0; i < offset && i < content.length(); ++i) {
        if (content[i] == '\n') {
            line++;
            character = 0;
        } else {
            character++;
        }
    }

    return {line, character};
}

void Server::onHover(const json &request) {
    // Handle the "hover" request
    std::string uri = request["params"]["textDocument"]["uri"];
    int line = request["params"]["position"]["line"];
    int character = request["params"]["position"]["character"];
    std::string content = getDocument(uri);

    std::string_view word = getWordAt(content, line, character);

    std::string hoverText = "No info available";
    if (!word.empty()) {
        hoverText = "Hover info for `" + std::string(word) + "`";
    }

    json response = {{"jsonrpc", "2.0"},
                     {"id", request["id"]},
                     {"result", {{"contents", {{"kind", "markdown"}, {"value", hoverText}}}}}};
    sendResponse(response);
}
} // namespace lsp

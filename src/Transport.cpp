#include "Transport.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iostream>
#include <string_view>
#include <system_error>

namespace lsp {
namespace {

constexpr std::size_t kMaxContentLength = 64 * 1024 * 1024; // 64 MiB

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

std::string lowercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

} // namespace

Transport::Transport(std::istream &in, std::ostream &out) : m_In(in), m_Out(out) {}

std::optional<std::string> Transport::read() {
    std::optional<std::size_t> contentLength;
    bool sawHeader = false;
    bool endOfHeaders = false;

    for (std::string line; std::getline(m_In, line);) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty()) {
            endOfHeaders = true;
            break;
        }

        sawHeader = true;

        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            std::cerr << "[Transport] Ignoring malformed header: " << line << '\n';
            continue;
        }

        const std::string name = lowercase(trim(std::string_view(line).substr(0, colon)));
        const std::string_view value = trim(std::string_view(line).substr(colon + 1));

        // Content-Type and any other headers are ignored.
        if (name != "content-length")
            continue;

        std::size_t parsed = 0;
        const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (error != std::errc{} || ptr != value.data() + value.size()) {
            std::cerr << "[Transport] Invalid Content-Length: " << value << '\n';
            return std::nullopt;
        }
        contentLength = parsed;
    }

    // getline failed immediately: clean EOF between messages.
    if (!sawHeader && !endOfHeaders)
        return std::nullopt;

    if (!endOfHeaders) {
        std::cerr << "[Transport] Unexpected EOF while reading headers\n";
        return std::nullopt;
    }

    if (!sawHeader) {
        std::cerr << "[Transport] Empty header block\n";
        return std::nullopt;
    }

    if (!contentLength) {
        std::cerr << "[Transport] Message is missing Content-Length\n";
        return std::nullopt;
    }

    if (*contentLength > kMaxContentLength) {
        std::cerr << "[Transport] Content-Length exceeds limit: " << *contentLength << '\n';
        return std::nullopt;
    }

    std::string body(*contentLength, '\0');
    if (*contentLength > 0) {
        m_In.read(body.data(), static_cast<std::streamsize>(*contentLength));
        if (static_cast<std::size_t>(m_In.gcount()) != *contentLength) {
            std::cerr << "[Transport] Truncated message body\n";
            return std::nullopt;
        }
    }

    return body;
}

void Transport::write(const std::string &payload) {
    m_Out << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
    m_Out.flush();
}

} // namespace lsp

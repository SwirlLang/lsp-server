#pragma once

#include <iosfwd>
#include <optional>
#include <string>

namespace lsp {

/// Reads and writes LSP messages using the base protocol framing:
/// a set of `Name: value` headers, a blank line, then a body of exactly
/// `Content-Length` bytes.
class Transport {
public:
    Transport(std::istream &in, std::ostream &out);

    /// Reads one framed message body. Returns std::nullopt on a clean EOF.
    /// Malformed framing is logged to stderr and also returns std::nullopt,
    /// since the stream can no longer be trusted.
    std::optional<std::string> read();

    /// Writes `payload` as a framed message and flushes the stream.
    void write(const std::string &payload);

private:
    std::istream &m_In;
    std::ostream &m_Out;
};

} // namespace lsp

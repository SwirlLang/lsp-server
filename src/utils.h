#include <cctype>
#include <string>
#include <string_view>

inline size_t positionToOffset(const std::string &content, int line, int character) {
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

inline std::string_view getWordAt(const std::string &content, int line, int character) {
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

inline std::pair<int, int> calculatePosition(const std::string &content, size_t offset) {
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

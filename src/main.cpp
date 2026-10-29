#include <iostream>

#include "LSPServer.h"

int main() {
    lsp::Server server;
    server.run(std::cin, std::cout);
    return 0;
}

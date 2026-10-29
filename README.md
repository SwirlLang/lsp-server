# Swirl Language Server

Next Gen Language Server for Swirl written in C++. Currently only has dummy implementations for lsp methods.

## Prerequisites

-   A C++26-capable compiler (e.g., g++, clang++)
-   CMake (3.30 or higher)

## Getting Started

### Build the Language Server

This project uses CMake to build the C++ server. The executable will be created at `build/swirl_lsp`.

```bash
# Configure the build
cmake -B build -S .

# Compile the server
cmake --build build
```

### Run

The server speaks LSP over stdio (JSON-RPC with `Content-Length` framing). Launch `build/swirl_lsp` with stdin/stdout connected to your LSP client; logs are written to stderr.

### Integrate with VSCode

To use this language server in VSCode, set up an extension (or use the `vscode-languageclient` library) that spawns the server executable and communicates with it over stdio.

## Currently Supported Methods

-   initialize
-   textDocument/didOpen
-   textDocument/didChange
-   textDocument/didSave
-   textDocument/completion
-   textDocument/completionItem/resolve
-   textDocument/hover
-   textDocument/publishDiagnostics
-   $/setTrace

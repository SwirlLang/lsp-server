// client.ts
import * as cp from "child_process";
import * as rpc from "vscode-jsonrpc/node";
import type {
    InitializeParams,
    InitializeResult,
} from "vscode-languageserver-protocol";

// Path to your compiled C++ executable
const serverPath = "./build/swirl_lsp";

// 1. Spawn the server process
const serverProcess = cp.spawn(serverPath, [], {
    // Use 'pipe' for stdio to connect streams, 'inherit' to see output in terminal
    stdio: ["pipe", "pipe", "pipe"],
});

// If the server fails to start
serverProcess.on("error", (err) => {
    console.error("Failed to start server process.", err);
});

// 2. Listen for debug logs from the server's stderr
serverProcess.stderr.on("data", (data: Buffer) => {
    console.log(`[Server stderr]: ${data.toString()}`);
});

// 3. Create a JSON-RPC connection using the process's streams
// The vscode-jsonrpc library handles the Content-Length headers for you!
const connection = rpc.createMessageConnection(
    // Read from the server's stdout
    new rpc.StreamMessageReader(serverProcess.stdout),
    // Write to the server's stdin
    new rpc.StreamMessageWriter(serverProcess.stdin),
);

async function runClient() {
    connection.listen();
    const initializeParams: InitializeParams = {
        processId: process.pid,
        rootUri: null,
        capabilities: {},
        // ... other params
    };

    console.log('Sending "initialize" request...');
    const result = await connection.sendRequest<InitializeResult>(
        "initialize",
        initializeParams,
    );
    // console.log('Received "initialize" response:', result);

    // Start listening for messages
    // send autocomplete request
    const autocompleteParams = {
        textDocument: {
            uri: "file:///path/to/document",
        },
        position: {
            line: 0,
            character: 0,
        },
    };
    const autocompleteResult = await connection.sendRequest(
        "textDocument/completion",
        autocompleteParams,
    );
    // console.log("Received autocomplete result:", autocompleteResult);
    const content =
        "import stuff from 'module';\n\nfn example() {\n    // Example function\n}\n";
    const didChangeContentParams = {
        textDocument: {
            uri: "file:///path/to/document",
            version: 1, // Increment this version for each change
        },
        contentChanges: [
            {
                text: content, // The new content of the document
            },
        ],
    };
    // send didChangeContent notification
    console.log("Sending 'didChangeContent' notification...");
    connection
        .sendNotification("textDocument/didChange", didChangeContentParams)
        .then((result) => {
            console.log(
                "Received 'didChangeContent' notification response:\n"
            );
        });
    // show diagnostics sent by server in response to didChangeContent notification
    connection.onNotification("textDocument/publishDiagnostics", (params) => {
        console.log("Received diagnostics from server:", params);
    });
}

runClient().catch(console.error);
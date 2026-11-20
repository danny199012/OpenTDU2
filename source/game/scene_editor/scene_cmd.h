#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

class GMSceneEditor;

// The scene editor's command interface (docs/studio_ai_api.md): one JSON
// command object in, one JSON result out. Two transports:
//   - batch: -editorscript <file.json> (a JSON array of commands, executed
//     right after the island loads; combine with -screenshot/-exitframe)
//   - live: the localhost JSON-lines TCP server (one client, commands run on
//     the editor thread between frames)
std::string SceneEditorExecuteCommand( GMSceneEditor &editor, const char *pJsonText, std::string &outError );
void SceneEditorRunScript( GMSceneEditor &editor, const char *pPath );

// The live server. 127.0.0.1 only (an AI on the same machine drives the
// editor while the user watches); -noserver turns it off, -editorport <n>
// moves it (default 9377).
class SceneCommandServer {
public:
    ~SceneCommandServer() { stop(); }

    bool start( int32_t port, GMSceneEditor *pEditor );
    void stop();
    // Polls the socket thread's inbox, executes the commands on the editor
    // thread and queues the responses (the socket thread sends them).
    void tick();

private:
    void socketThread();

    GMSceneEditor *pEditor = nullptr;
    std::thread thread;
    std::mutex mutex;
    std::deque<std::string> inbox; // raw JSON texts from the client
    std::deque<std::string> outbox; // JSON results for the client
    std::atomic<bool> bRun{ false };
    uintptr_t listenSocket = ~0ull;
    uintptr_t clientSocket = ~0ull;
    int32_t serverPort = 0;
};

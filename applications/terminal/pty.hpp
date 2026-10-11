#pragma once

// A pseudo-terminal running the user's shell. Output is read on a worker thread and handed to the UI
// thread in batches; input is written without ever blocking the UI (large pastes are queued).

#include <mayonnaise/mayonnaise.hpp>

#include <sys/types.h>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace term {

class Pty {
public:
    explicit Pty(mayo::App& app) : m_app(app) {}
    ~Pty();
    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;

    struct Options {
        std::string shell;  ///< empty: $SHELL, then the passwd entry, then /bin/sh
        std::string cwd;    ///< empty: $HOME
        int cols = 80;
        int rows = 24;
    };
    bool start(const Options& options, std::string& error);

    void write(std::string_view data);
    void resize(int cols, int rows, int pixelWidth, int pixelHeight);

    /// Up to `max` bytes of the output read so far (UI thread). `more` tells whether some is left; then
    /// onOutput won't fire again until the caller has taken the rest.
    std::string takeOutput(size_t max, bool& more);
    /// UI thread: new output is waiting (once per batch).
    std::function<void()> onOutput;
    /// UI thread: the shell exited.
    std::function<void(int status)> onExit;

    [[nodiscard]] pid_t pid() const { return m_pid; }
    [[nodiscard]] bool running() const { return m_pid > 0 && !m_exited; }
    /// Name of the program in the foreground (what the user is running), e.g. "vim" or "zsh".
    [[nodiscard]] std::string foregroundProcess() const;
    /// Working directory of the foreground program (for new tabs and the window title).
    [[nodiscard]] std::string currentDirectory() const;
    /// True if something other than the shell itself is running (closing would interrupt it).
    [[nodiscard]] bool busy() const;

private:
    void run();
    void flushWrites();  // under m_mutex

    mayo::App& m_app;
    int m_fd = -1;
    int m_wake = -1;
    pid_t m_pid = -1;
    bool m_exited = false;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_drained;
    std::string m_output;   // read; the UI has taken everything before m_taken
    size_t m_taken = 0;
    std::string m_pending;  // to write
    bool m_posted = false;
    bool m_stop = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

/// Quotes a path for pasting into a shell ('it'\''s').
std::string shellQuote(const std::string& s);

}  // namespace term

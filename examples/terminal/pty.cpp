#include "pty.hpp"

#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

extern char** environ;

namespace term {

namespace {
constexpr size_t kMaxBuffered = 4u << 20;  // stop reading until the UI catches up (`yes`, `cat huge.log`)

std::string userShell() {
    if (const char* s = std::getenv("SHELL"); s && *s && ::access(s, X_OK) == 0) return s;
    if (passwd* pw = getpwuid(getuid()); pw && pw->pw_shell && *pw->pw_shell) return pw->pw_shell;
    return "/bin/sh";
}
}  // namespace

std::string shellQuote(const std::string& s) {
    bool plain = !s.empty();
    for (char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || std::strchr("/._-+=:,@%", c))) plain = false;
    }
    if (plain) return s;
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

Pty::~Pty() {
    *m_alive = false;
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_drained.notify_all();
    if (m_wake >= 0) {
        std::uint64_t one = 1;
        (void)::write(m_wake, &one, sizeof one);
    }
    if (m_thread.joinable()) m_thread.join();
    if (m_pid > 0 && !m_exited) {
        // Like closing a terminal window: the shell gets SIGHUP and passes it on to its jobs.
        ::kill(m_pid, SIGHUP);
        // Reap it without blocking the UI; a stubborn child is left to init.
        for (int i = 0; i < 20 && ::waitpid(m_pid, nullptr, WNOHANG) == 0; ++i) ::usleep(5000);
    }
    if (m_fd >= 0) ::close(m_fd);
    if (m_wake >= 0) ::close(m_wake);
}

bool Pty::start(const Options& options, std::string& error) {
    const std::string shell = options.shell.empty() ? userShell() : options.shell;
    std::string cwd = options.cwd;
    if (cwd.empty()) {
        const char* home = std::getenv("HOME");
        cwd = home ? home : "/";
    }
    // Everything the child needs is prepared before fork: after it only async-signal-safe calls happen.
    std::vector<std::string> env;
    auto replaced = [](std::string_view v) {
        for (const char* name : {"TERM=", "COLORTERM=", "TERM_PROGRAM=", "COLUMNS=", "LINES=", "VTE_VERSION=", "TERMINAL_EMULATOR="}) {
            if (v.rfind(name, 0) == 0) return true;
        }
        return false;
    };
    for (char** e = environ; *e; ++e) {
        if (!replaced(*e)) env.emplace_back(*e);
    }
    env.emplace_back("TERM=xterm-256color");
    env.emplace_back("COLORTERM=truecolor");
    env.emplace_back("TERM_PROGRAM=Mayonnaise");
    std::vector<char*> envp;
    for (auto& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);
    // A leading '-' makes it a login shell, as macOS Terminal does.
    const std::string base = shell.substr(shell.find_last_of('/') + 1);
    std::string argv0 = "-" + base;
    char* argv[] = {argv0.data(), nullptr};

    winsize ws{};
    ws.ws_col = static_cast<unsigned short>(options.cols);
    ws.ws_row = static_cast<unsigned short>(options.rows);
    int fd = -1;
    const pid_t pid = forkpty(&fd, nullptr, nullptr, &ws);
    if (pid < 0) {
        error = std::string("Couldn't create a pseudo-terminal: ") + std::strerror(errno);
        return false;
    }
    if (pid == 0) {
        if (::chdir(cwd.c_str()) != 0) (void)::chdir("/");
        // The UI ignores or blocks some signals; the shell must start with defaults.
        struct sigaction sa {};
        sa.sa_handler = SIG_DFL;
        for (int sig : {SIGPIPE, SIGINT, SIGTERM, SIGQUIT, SIGCHLD, SIGHUP, SIGTSTP, SIGTTIN, SIGTTOU}) ::sigaction(sig, &sa, nullptr);
        sigset_t none;
        sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        ::execve(shell.c_str(), argv, envp.data());
        ::execve("/bin/sh", argv, envp.data());
        ::_exit(127);
    }
    m_pid = pid;
    m_fd = fd;
    ::fcntl(m_fd, F_SETFD, FD_CLOEXEC);
    ::fcntl(m_fd, F_SETFL, ::fcntl(m_fd, F_GETFL) | O_NONBLOCK);
    m_wake = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    m_thread = std::thread([this] { run(); });
    return true;
}

void Pty::run() {
    std::vector<char> buf(64 * 1024);
    for (;;) {
        {
            // Back-pressure: a flood of output waits for the UI instead of growing memory without bound.
            std::unique_lock lock(m_mutex);
            m_drained.wait(lock, [&] { return m_stop || m_output.size() < kMaxBuffered; });
            if (m_stop) return;
        }
        bool wantWrite = false;
        {
            std::lock_guard lock(m_mutex);
            wantWrite = !m_pending.empty();
        }
        pollfd fds[2] = {{m_fd, static_cast<short>(POLLIN | (wantWrite ? POLLOUT : 0)), 0}, {m_wake, POLLIN, 0}};
        if (::poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (fds[1].revents & POLLIN) {
            std::uint64_t v = 0;
            (void)::read(m_wake, &v, sizeof v);
            std::lock_guard lock(m_mutex);
            if (m_stop) return;
        }
        if (fds[0].revents & POLLOUT) {
            std::lock_guard lock(m_mutex);
            flushWrites();
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            const ssize_t n = ::read(m_fd, buf.data(), buf.size());
            if (n > 0) {
                std::lock_guard lock(m_mutex);
                m_output.append(buf.data(), static_cast<size_t>(n));
                if (!m_posted) {
                    m_posted = true;
                    m_app.post([this, alive = m_alive] {
                        if (*alive && onOutput) onOutput();
                    });
                }
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            // EOF / EIO: the shell (and everything holding the terminal) is gone.
            int status = 0;
            ::waitpid(m_pid, &status, 0);
            m_app.post([this, alive = m_alive, status] {
                if (!*alive) return;
                m_exited = true;
                if (onOutput) onOutput();  // anything still buffered
                if (onExit) onExit(status);
            });
            return;
        }
    }
}

std::string Pty::takeOutput() {
    std::string out;
    {
        std::lock_guard lock(m_mutex);
        out.swap(m_output);
        m_posted = false;
    }
    m_drained.notify_all();
    return out;
}

void Pty::flushWrites() {
    while (!m_pending.empty()) {
        const ssize_t n = ::write(m_fd, m_pending.data(), m_pending.size());
        if (n <= 0) break;  // EAGAIN: the reader thread retries on POLLOUT
        m_pending.erase(0, static_cast<size_t>(n));
    }
}

void Pty::write(std::string_view data) {
    if (m_fd < 0 || m_exited || data.empty()) return;
    std::lock_guard lock(m_mutex);
    m_pending.append(data);
    flushWrites();
    if (!m_pending.empty() && m_wake >= 0) {
        std::uint64_t one = 1;
        (void)::write(m_wake, &one, sizeof one);
    }
}

void Pty::resize(int cols, int rows, int pixelWidth, int pixelHeight) {
    if (m_fd < 0) return;
    winsize ws{};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_xpixel = static_cast<unsigned short>(pixelWidth);
    ws.ws_ypixel = static_cast<unsigned short>(pixelHeight);
    ::ioctl(m_fd, TIOCSWINSZ, &ws);
}

std::string Pty::foregroundProcess() const {
    if (m_fd < 0) return {};
    pid_t pgid = ::tcgetpgrp(m_fd);
    if (pgid <= 0) pgid = m_pid;
    std::ifstream in("/proc/" + std::to_string(pgid) + "/comm");
    std::string name;
    std::getline(in, name);
    return name;
}

std::string Pty::currentDirectory() const {
    if (m_fd < 0) return {};
    pid_t pgid = ::tcgetpgrp(m_fd);
    if (pgid <= 0) pgid = m_pid;
    char buf[4096];
    const ssize_t n = ::readlink(("/proc/" + std::to_string(pgid) + "/cwd").c_str(), buf, sizeof buf - 1);
    return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
}

bool Pty::busy() const {
    if (m_fd < 0 || m_exited) return false;
    const pid_t pgid = ::tcgetpgrp(m_fd);
    return pgid > 0 && pgid != m_pid;
}

}  // namespace term

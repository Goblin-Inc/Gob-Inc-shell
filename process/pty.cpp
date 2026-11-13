#include "pty.hpp"

#include <pty.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>

PTY::PTY()
    : master_fd_(-1), child_pid_(-1)
{
}

PTY::~PTY()
{
    close();
}

bool PTY::start(const char* program, int rows, int cols)
{
    struct winsize ws {};
    ws.ws_row = static_cast<unsigned short>(rows > 0 ? rows : 30);
    ws.ws_col = static_cast<unsigned short>(cols > 0 ? cols : 100);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    pid_t pid = forkpty(&master_fd_, nullptr, nullptr, &ws);
    if (pid == -1)
    {
        master_fd_ = -1;
        child_pid_ = -1;
        return false;
    }

    if (pid == 0)
    {
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("LANG", "ru_RU.UTF-8", 0);
        execlp(program, program, nullptr);
        _exit(127);
    }

    child_pid_ = pid;

    int flags = fcntl(master_fd_, F_GETFL, 0);
    if (flags == -1 || fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        close();
        return false;
    }

    return true;
}

std::string PTY::read_output()
{
    if (master_fd_ == -1)
    {
        return {};
    }

    std::string result;
    char buffer[8192];

    while (true)
    {
        ssize_t bytes_read = read(master_fd_, buffer, sizeof(buffer));
        if (bytes_read > 0)
        {
            result.append(buffer, static_cast<size_t>(bytes_read));
            if (result.size() >= 65536) break;
        }
        else
        {
            break;
        }
    }

    return result;
}

bool PTY::write_input(const std::string& input)
{
    if (master_fd_ == -1 || input.empty())
    {
        return false;
    }

    ssize_t bytes_written = write(master_fd_, input.data(), input.size());
    return bytes_written == static_cast<ssize_t>(input.size());
}

void PTY::resize(int rows, int cols)
{
    if (master_fd_ == -1 || rows <= 0 || cols <= 0)
    {
        return;
    }

    struct winsize ws {};
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    ioctl(master_fd_, TIOCSWINSZ, &ws);
}

void PTY::close()
{
    if (master_fd_ != -1)
    {
        ::close(master_fd_);
        master_fd_ = -1;
    }
    if (child_pid_ > 0)
    {
        kill(child_pid_, SIGHUP);
        int status = 0;
        waitpid(child_pid_, &status, WNOHANG);
        child_pid_ = -1;
    }
}

int PTY::master_fd() const
{
    return master_fd_;
}

pid_t PTY::child_pid() const
{
    return child_pid_;
}

bool PTY::is_alive() const
{
    if (child_pid_ <= 0) return false;
    int status = 0;
    pid_t res = waitpid(child_pid_, &status, WNOHANG);
    return res == 0;
}

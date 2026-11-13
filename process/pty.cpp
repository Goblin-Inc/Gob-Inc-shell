#include "pty.hpp"

#include <pty.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <fcntl.h>

PTY::PTY()
    : master_fd_(-1)
{
}

PTY::~PTY()
{
    close();
}

bool PTY::start(const char* program)
{
    pid_t pid = forkpty(
        &master_fd_,
        nullptr,
        nullptr,
        nullptr
    );

    if (pid == -1)
    {
        master_fd_ = -1;
        return false;
    }

    if (pid == 0)
    {
        execlp(program, program, nullptr);

        _exit(127);
    }

    int flags = fcntl(master_fd_, F_GETFL, 0);

    if (flags == -1)
    {
        close();
        return false;
    }

    if (fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK) == -1)
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

    char buffer[4096];

    ssize_t bytes_read = read(
        master_fd_,
        buffer,
        sizeof(buffer)
    );

    if (bytes_read > 0)
    {
        return std::string(buffer, bytes_read);
    }

    if (bytes_read == -1 &&
        (errno == EAGAIN || errno == EWOULDBLOCK)) 
    {
        return {};
    }

    return {};
}

bool PTY::write_input(const std::string& input)
{
    if (master_fd_ == -1)
    {
        return false;
    }

    ssize_t bytes_written = write(
        master_fd_,
        input.c_str(),
        input.size()
    );

    return bytes_written == static_cast<ssize_t>(input.size());
}

void PTY::close()
{
    if (master_fd_ != -1)
    {
        ::close(master_fd_);
        master_fd_ = -1;
    }
}

int PTY::master_fd() const
{
    return master_fd_;
}
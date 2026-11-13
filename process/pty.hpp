#pragma once

#include <string>
#include <sys/types.h>

class PTY
{
public:
    PTY();
    ~PTY();

    bool start(const char* program, int rows = 30, int cols = 100);

    std::string read_output();
    bool write_input(const std::string& input);
    void resize(int rows, int cols);

    int master_fd() const;
    pid_t child_pid() const;
    bool is_alive() const;

    void close();

private:
    int master_fd_;
    pid_t child_pid_;
};

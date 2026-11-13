#pragma once

#include <string>

class PTY
{
public:
    PTY();
    ~PTY();

    bool start(const char* program);

    std::string read_output();
    bool write_input(const std::string& input);

    int master_fd() const;

    void close();

private:
    int master_fd_;
};
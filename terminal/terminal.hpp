#pragma once

#include "../process/pty.hpp"

#include <string>

class Terminal
{
public:
    Terminal();

    bool start(const char* program);

    void update();

    void send_input(const std::string& input);

    const std::string& output() const;

private:
    PTY pty_;
    std::string output_;
};
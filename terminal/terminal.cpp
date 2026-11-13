#include "terminal.hpp"

Terminal::Terminal()
{
}

bool Terminal::start(const char* program)
{
    return pty_.start(program);
}

void Terminal::update()
{
    std::string data = pty_.read_output();

    if (!data.empty())
    {
        output_ += data;
    }
}

void Terminal::send_input(const std::string& input)
{
    pty_.write_input(input);
}

const std::string& Terminal::output() const
{
    return output_;
}
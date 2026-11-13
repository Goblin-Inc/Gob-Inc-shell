#ifndef SHELL_HPP
#define SHELL_HPP

#include <vector>
#include <string>

std::vector<std::string> split(const std::string& str);
std::string execute_command(const std::vector<std::string>& args, bool& should_clear);

#endif
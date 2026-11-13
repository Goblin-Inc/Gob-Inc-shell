#include "shell.hpp"
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>

std::vector<std::string> split(const std::string& str) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (ss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string execute_command(const std::vector<std::string>& args, bool& should_clear) {
    if (args.empty()) return "";

    // Сбрасываем флаг очистки по умолчанию
    should_clear = false;

    // 1. Встроенная команда: exit
    if (args[0] == "exit") {
        exit(0);
    }

    // 2. Встроенная команда: clear (очистка истории в графическом терминале)
    if (args[0] == "clear") {
        should_clear = true;
        return "";
    }

    // 3. Встроенная команда: cd (смена директории)
    if (args[0] == "cd") {
        if (args.size() < 2) {
            return "myshell: expected argument to \"cd\"\n";
        } else {
            if (chdir(args[1].c_str()) != 0) {
                return "myshell: no such file or directory\n";
            }
        }
        return "";
    }

    // Предупреждение для sudo, чтобы оно не падало и не уводило фокус в фоновый терминал
    if (args[0] == "sudo") {
        return "myshell: interactive sudo inside GUI shell is not supported yet. Run command via system terminal.\n";
    }

    // 4. Запуск внешних программ через pipe
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return "Error: pipe failed!\n";
    }

    pid_t pid = fork();
    if (pid < 0) {
        return "Error: fork failed!\n";
    } 
    else if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        std::vector<char*> c_args;
        for (auto& arg : const_cast<std::vector<std::string>&>(args)) {
            c_args.push_back(&arg[0]);
        }
        c_args.push_back(nullptr);

        if (execvp(c_args[0], c_args.data()) < 0) {
            perror("myshell");
        }
        _exit(1);
    } 
    else {
        close(pipefd[1]);
        
        std::string output;
        char buffer[128];
        ssize_t bytesRead;
        while ((bytesRead = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytesRead] = '\0';
            output += buffer;
        }
        close(pipefd[0]);

        int status;
        waitpid(pid, &status, 0);
        
        return output;
    }
}
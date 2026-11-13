#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include "shell.hpp"
#include "terminal/terminal.hpp"

struct CommandHistory {
    std::string command;
    std::string output;
};

// Функция-колбэк для реализации поведения командной строки в ImGui InputText
int TextEditCallbackStub(ImGuiInputTextCallbackData* data) {
    return 0;
}

int main() {
    if (!glfwInit()) return 1;

    GLFWwindow* window = glfwCreateWindow(800, 500, "Gob-Inc Shell", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.1f, 1.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.0f, 1.0f, 0.4f, 1.0f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    char input_buf[256] = "";
    std::vector<CommandHistory> history;
    bool scroll_to_bottom = false;

    Terminal terminal;

    if (!terminal.start("bash")) {
        return 1;
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        terminal.update();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | 
                                        ImGuiWindowFlags_NoMove | 
                                        ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("TerminalShell", nullptr, window_flags);

        // Область вывода истории и ввода, скроллируемая целиком
        if (ImGui::BeginChild("ConsoleScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
            
            ImGui::TextUnformatted("Welcome to Gob-Inc Shell (ImGui Edition)");
            ImGui::TextUnformatted(terminal.output().c_str());

            ImGui::TextUnformatted("Type commands below (e.g. 'ls', 'pwd', 'cd <dir>', 'exit')\n--------------------------------------------------");

            // 1. Выводим всю прошлую историю
            for (const auto& item : history) {
                // Команда пользователя
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                ImGui::Text("myshell> %s", item.command.c_str());
                ImGui::PopStyleColor();

                // Ответ системы
                if (!item.output.empty()) {
                    ImGui::TextUnformatted(item.output.c_str());
                }
            }

            // 2. Текущая активная строка ввода прямо в потоке терминала
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
            ImGui::Text("myshell> ");
            ImGui::PopStyleColor();
            
            ImGui::SameLine(0, 0); // Прижимаем поле ввода вплотную к надписи "myshell> "

            ImGui::PushItemWidth(-1);
            
            // Делаем поле ввода прозрачным без рамок, чтобы оно выглядело как обычный текст
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            
            bool reclaim_focus = false;
            char input_label[32];
            snprintf(input_label, sizeof(input_label), "##ActiveInput");

            if (ImGui::InputText(input_label, input_buf, IM_ARRAYSIZE(input_buf), 
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion, 
                                            &TextEditCallbackStub)) 
            {
                std::string cmd_str(input_buf);
                if (!cmd_str.empty()) {
                    std::vector<std::string> args = split(cmd_str);
                    
                    bool should_clear = false; // <--- ДОБАВЬ ЭТО
                    std::string command_output = execute_command(args, should_clear); // <--- И ПЕРЕДАЙ СЮДА

                    // ЕСЛИ ВВЕЛИ CLEAR — ОЧИЩАЕМ ИСТОРИЮ
                    if (should_clear) {
                        history.clear();
                    } else {
                        history.push_back({cmd_str, command_output});
                    }

                    memset(input_buf, 0, sizeof(input_buf));
                }
                reclaim_focus = true;
            }
            ImGui::PopStyleColor(); // Возвращаем цвет рамки
            ImGui::PopItemWidth();

            // Автофокус на поле ввода
            if (ImGui::IsWindowAppearing() || reclaim_focus || (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(0))) {
                ImGui::SetKeyboardFocusHere(-1);
            }

            // Автопрокрутка вниз при добавлении новой строки
            if (scroll_to_bottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
                scroll_to_bottom = false;
            }
        }
        ImGui::EndChild();

        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
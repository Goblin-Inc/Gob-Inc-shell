#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "terminal/terminal.hpp"

struct ThemePalette
{
    const char* name;
    ImU32 bg;
    ImU32 topbar_bg;
    ImU32 statusbar_bg;
    ImU32 fg;
    ImU32 accent;
    ImU32 cursor;
    ImU32 border;
};

static const ThemePalette THEMES[] = {
    {
        "Cyberpunk Neon",
        IM_COL32(9, 13, 22, 255),
        IM_COL32(15, 23, 42, 255),
        IM_COL32(12, 19, 34, 255),
        IM_COL32(226, 232, 240, 255),
        IM_COL32(0, 255, 204, 255),
        IM_COL32(0, 255, 204, 210),
        IM_COL32(0, 255, 204, 90)
    },
    {
        "Tokyo Night",
        IM_COL32(26, 27, 38, 255),
        IM_COL32(22, 22, 30, 255),
        IM_COL32(22, 22, 30, 255),
        IM_COL32(192, 202, 245, 255),
        IM_COL32(122, 162, 247, 255),
        IM_COL32(187, 154, 247, 210),
        IM_COL32(122, 162, 247, 90)
    },
    {
        "Matrix Phosphor",
        IM_COL32(5, 11, 7, 255),
        IM_COL32(8, 19, 12, 255),
        IM_COL32(8, 19, 12, 255),
        IM_COL32(74, 246, 110, 255),
        IM_COL32(16, 185, 129, 255),
        IM_COL32(74, 246, 110, 210),
        IM_COL32(16, 185, 129, 95)
    },
    {
        "Catppuccin Mocha",
        IM_COL32(30, 30, 46, 255),
        IM_COL32(24, 24, 37, 255),
        IM_COL32(24, 24, 37, 255),
        IM_COL32(205, 214, 244, 255),
        IM_COL32(203, 166, 247, 255),
        IM_COL32(245, 194, 231, 210),
        IM_COL32(203, 166, 247, 90)
    }
};

static void encode_utf8(char32_t cp, std::string& out)
{
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static void save_framebuffer_ppm(const char* path, int w, int h)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(w * h * 3));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return;
    ofs << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y)
    {
        ofs.write(reinterpret_cast<const char*>(&pixels[static_cast<size_t>(y * w * 3)]), w * 3);
    }
}

struct TabSession
{
    std::unique_ptr<Terminal> term;
    std::string label;
    int id;
};

static void forward_keyboard_to_terminal(GLFWwindow* window, Terminal& term)
{
    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = io.KeyCtrl;
    bool shift = io.KeyShift;
    bool alt = io.KeyAlt;

    // Clipboard shortcuts: Ctrl+Shift+C / Ctrl+Shift+V
    if (ctrl && shift)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_C, false))
        {
            std::string dump = term.dump_plain_text();
            glfwSetClipboardString(window, dump.c_str());
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false))
        {
            const char* clip = glfwGetClipboardString(window);
            if (clip && *clip)
            {
                if (term.bracketed_paste())
                {
                    term.send_input(std::string("\x1b[200~") + clip + "\x1b[201~");
                }
                else
                {
                    term.send_input(clip);
                }
            }
            return;
        }
    }

    // Control key combinations Ctrl+A .. Ctrl+Z (vital for nano: Ctrl+O, Ctrl+X, Ctrl+W, Ctrl+K, etc. and vim)
    if (ctrl && !shift && !alt)
    {
        for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k)
        {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), true))
            {
                char ctrl_byte = static_cast<char>((k - ImGuiKey_A) + 1);
                term.send_input(std::string(1, ctrl_byte));
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true))
        {
            term.send_input("\x1b");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backslash, true))
        {
            term.send_input("\x1c");
        }
        return;
    }

    // Special navigation and editing keys
    bool app_cur = term.app_cursor_keys();
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, true))
        term.send_input("\r");
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true))
        term.send_input("\x7f");
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, true))
        term.send_input(shift ? "\x1b[Z" : "\t");
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, true))
        term.send_input("\x1b");

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
        term.send_input(app_cur ? "\x1bOA" : "\x1b[A");
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
        term.send_input(app_cur ? "\x1bOB" : "\x1b[B");
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
        term.send_input(app_cur ? "\x1bOC" : "\x1b[C");
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
        term.send_input(app_cur ? "\x1bOD" : "\x1b[D");

    if (ImGui::IsKeyPressed(ImGuiKey_Home, true))
        term.send_input(app_cur ? "\x1bOH" : "\x1b[H");
    if (ImGui::IsKeyPressed(ImGuiKey_End, true))
        term.send_input(app_cur ? "\x1bOF" : "\x1b[F");
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
        term.send_input("\x1b[5~");
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
        term.send_input("\x1b[6~");
    if (ImGui::IsKeyPressed(ImGuiKey_Insert, true))
        term.send_input("\x1b[2~");
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, true))
        term.send_input("\x1b[3~");

    // Function keys F1..F12
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))  term.send_input("\x1bOP");
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false))  term.send_input("\x1bOQ");
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false))  term.send_input("\x1bOR");
    if (ImGui::IsKeyPressed(ImGuiKey_F4, false))  term.send_input("\x1bOS");
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false))  term.send_input("\x1b[15~");
    if (ImGui::IsKeyPressed(ImGuiKey_F6, false))  term.send_input("\x1b[17~");
    if (ImGui::IsKeyPressed(ImGuiKey_F7, false))  term.send_input("\x1b[18~");
    if (ImGui::IsKeyPressed(ImGuiKey_F8, false))  term.send_input("\x1b[19~");
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false))  term.send_input("\x1b[20~");
    if (ImGui::IsKeyPressed(ImGuiKey_F10, false)) term.send_input("\x1b[21~");
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) term.send_input("\x1b[23~");
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) term.send_input("\x1b[24~");

    // Regular text input (UTF-8 characters)
    if (!ctrl && io.InputQueueCharacters.Size > 0)
    {
        std::string typed;
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
        {
            ImWchar wc = io.InputQueueCharacters[i];
            if (wc >= 0x20 && wc != 0x7F)
            {
                if (alt) typed.push_back('\x1b');
                encode_utf8(static_cast<char32_t>(wc), typed);
            }
        }
        if (!typed.empty())
        {
            term.send_input(typed);
        }
    }
}

static void glfw_error_callback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description ? description : "(null)");
}

int main(int argc, char** argv)
{
    const char* screenshot_out = nullptr;
    const char* demo_cmd = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
        {
            screenshot_out = argv[++i];
        }
        else if (std::strcmp(argv[i], "--cmd") == 0 && i + 1 < argc)
        {
            demo_cmd = argv[++i];
        }
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1080, 680, "Gob-Inc Terminal // VT100 + TrueColor", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    // Build glyph ranges: ASCII + Cyrillic + Box Drawing + Block Elements + Arrows
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    static const ImWchar extra_ranges[] = {
        0x2000, 0x206F, // General Punctuation
        0x2190, 0x21FF, // Arrows
        0x2500, 0x257F, // Box Drawing (for nano, vim, mc, htop, tmux)
        0x2580, 0x259F, // Block Elements
        0x25A0, 0x25FF, // Geometric Shapes
        0x2700, 0x27BF, // Dingbats
        0xE000, 0xF8FF, // Private Use Area (Nerd Fonts / Powerline)
        0,
    };
    builder.AddRanges(extra_ranges);
    ImVector<ImWchar> glyph_ranges;
    builder.BuildRanges(&glyph_ranges);

    const char* font_paths[] = {
        "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
        "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Regular.ttf",
        "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
        "/usr/share/fonts/jetbrains-mono/JetBrainsMono-Regular.ttf",
        "/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/Adwaita/AdwaitaMono-Regular.ttf"
    };

    ImFont* mono_font = nullptr;
    for (const char* path : font_paths)
    {
        FILE* f = std::fopen(path, "rb");
        if (f)
        {
            std::fclose(f);
            mono_font = io.Fonts->AddFontFromFileTTF(path, 16.0f, nullptr, glyph_ranges.Data);
            if (mono_font) break;
        }
    }
    if (!mono_font)
    {
        mono_font = io.Fonts->AddFontDefault();
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 6.0f;
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    std::vector<TabSession> tabs;
    int next_tab_id = 1;
    int active_tab = 0;
    int theme_idx = 0;

    auto spawn_tab = [&]() {
        auto t = std::make_unique<Terminal>(32, 110);
        const char* shell_env = std::getenv("SHELL");
        const char* prog = (shell_env && *shell_env) ? shell_env : "bash";
        t->start(prog);
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "Terminal %d", next_tab_id);
        tabs.push_back({std::move(t), lbl, next_tab_id++});
        active_tab = static_cast<int>(tabs.size()) - 1;
    };

    spawn_tab();

    if (demo_cmd && !tabs.empty())
    {
        tabs[0].term->send_input(std::string(demo_cmd) + "\r");
    }

    int frame_counter = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        for (auto& tab : tabs)
        {
            tab.term->update();
        }

        // Remove dead tabs if closed by exit
        for (int i = static_cast<int>(tabs.size()) - 1; i >= 0; --i)
        {
            if (!tabs[i].term->is_alive() && tabs.size() > 1)
            {
                tabs.erase(tabs.begin() + i);
                if (active_tab >= static_cast<int>(tabs.size()))
                {
                    active_tab = static_cast<int>(tabs.size()) - 1;
                }
            }
        }
        if (!tabs.empty() && !tabs[0].term->is_alive() && tabs.size() == 1 && !screenshot_out)
        {
            break;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Global shortcuts for tabs
        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_T, false))
        {
            spawn_tab();
        }
        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_W, false) && tabs.size() > 1)
        {
            tabs.erase(tabs.begin() + active_tab);
            active_tab = std::clamp(active_tab, 0, static_cast<int>(tabs.size()) - 1);
        }

        const ThemePalette& theme = THEMES[theme_idx];
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);

        ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoDecoration |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(theme.bg));
        ImGui::Begin("GobIncRoot", nullptr, win_flags);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const float topbar_h = 40.0f;
        const float statusbar_h = 28.0f;
        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 win_size = ImGui::GetWindowSize();

        // 1. Top Header & Tab Bar
        draw_list->AddRectFilled(
            win_pos,
            ImVec2(win_pos.x + win_size.x, win_pos.y + topbar_h),
            theme.topbar_bg
        );
        draw_list->AddLine(
            ImVec2(win_pos.x, win_pos.y + topbar_h),
            ImVec2(win_pos.x + win_size.x, win_pos.y + topbar_h),
            theme.border,
            1.0f
        );

        ImGui::SetCursorPos(ImVec2(12.0f, 7.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.accent));
        ImGui::TextUnformatted("● GOB-INC // SHELL");
        ImGui::PopStyleColor();

        ImGui::SameLine(0.0f, 16.0f);

        for (int i = 0; i < static_cast<int>(tabs.size()); ++i)
        {
            bool is_active = (i == active_tab);
            ImGui::PushID(tabs[i].id);
            ImGui::PushStyleColor(
                ImGuiCol_Button,
                is_active ? ImGui::ColorConvertU32ToFloat4(theme.border) : ImVec4(0.12f, 0.16f, 0.24f, 0.6f)
            );
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                ImGui::ColorConvertU32ToFloat4(theme.border)
            );
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                is_active ? ImGui::ColorConvertU32ToFloat4(theme.accent) : ImGui::ColorConvertU32ToFloat4(theme.fg)
            );

            std::string btn_text = " " + tabs[i].label + (tabs[i].term->use_alt_screen() ? " [TUI] " : " ");
            if (ImGui::Button(btn_text.c_str(), ImVec2(0, 26)))
            {
                active_tab = i;
            }
            ImGui::PopStyleColor(3);
            ImGui::PopID();
            ImGui::SameLine(0.0f, 6.0f);
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.20f, 0.30f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.accent));
        if (ImGui::Button("+ Вкладка", ImVec2(0, 26)))
        {
            spawn_tab();
        }
        ImGui::SameLine(win_size.x - 190.0f);
        if (ImGui::Button(theme.name, ImVec2(176, 26)))
        {
            theme_idx = (theme_idx + 1) % 4;
        }
        ImGui::PopStyleColor(2);

        // 2. Terminal Grid Canvas
        Terminal& term = *tabs[active_tab].term;
        float canvas_x = win_pos.x + 10.0f;
        float canvas_y = win_pos.y + topbar_h + 8.0f;
        float canvas_w = std::max(100.0f, win_size.x - 20.0f);
        float canvas_h = std::max(80.0f, win_size.y - topbar_h - statusbar_h - 16.0f);

        ImVec2 char_sz = mono_font->CalcTextSizeA(16.0f, FLT_MAX, 0.0f, "M");
        float cell_w = std::max(7.0f, std::floor(char_sz.x));
        float cell_h = std::max(14.0f, std::floor(char_sz.y + 3.0f));

        int target_cols = std::max(20, static_cast<int>(canvas_w / cell_w));
        int target_rows = std::max(5, static_cast<int>(canvas_h / cell_h));
        term.resize(target_rows, target_cols);

        // Forward keyboard input to the active PTY
        forward_keyboard_to_terminal(window, term);

        const auto& grid = term.active_grid();
        int r_count = term.rows();
        int c_count = term.cols();

        // Pass 1: Cell backgrounds & inverse video
        for (int r = 0; r < r_count; ++r)
        {
            float y0 = canvas_y + r * cell_h;
            float y1 = y0 + cell_h;
            for (int c = 0; c < c_count; ++c)
            {
                const TermCell& cell = grid[r][c];
                ImU32 cell_bg = cell.bg ? cell.bg : theme.bg;
                ImU32 cell_fg = cell.fg ? cell.fg : theme.fg;
                if (cell.inverse)
                {
                    std::swap(cell_fg, cell_bg);
                }
                if (cell_bg != theme.bg)
                {
                    float x0 = canvas_x + c * cell_w;
                    draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + cell_w, y1), cell_bg);
                }
            }
        }

        // Pass 2: Characters & underlines
        char utf8_glyph[8];
        for (int r = 0; r < r_count; ++r)
        {
            float y0 = canvas_y + r * cell_h;
            for (int c = 0; c < c_count; ++c)
            {
                const TermCell& cell = grid[r][c];
                float x0 = canvas_x + c * cell_w;
                ImU32 cell_bg = cell.bg ? cell.bg : theme.bg;
                ImU32 cell_fg = cell.fg ? cell.fg : theme.fg;
                if (cell.inverse)
                {
                    std::swap(cell_fg, cell_bg);
                }
                if (cell.bold && cell.fg == 0 && !cell.inverse)
                {
                    cell_fg = IM_COL32(255, 255, 255, 255);
                }

                if (cell.ch > U' ')
                {
                    std::string s;
                    encode_utf8(cell.ch, s);
                    draw_list->AddText(mono_font, 16.0f, ImVec2(x0, y0 + 1.0f), cell_fg, s.c_str(), s.c_str() + s.size());
                }
                if (cell.underline)
                {
                    draw_list->AddLine(
                        ImVec2(x0, y0 + cell_h - 1.0f),
                        ImVec2(x0 + cell_w, y0 + cell_h - 1.0f),
                        cell_fg,
                        1.0f
                    );
                }
            }
        }

        // Pass 3: Cursor
        if (term.cursor_visible())
        {
            int cr = term.cursor_row();
            int cc = term.cursor_col();
            if (cr >= 0 && cr < r_count && cc >= 0 && cc < c_count)
            {
                float cx = canvas_x + cc * cell_w;
                float cy = canvas_y + cr * cell_h;
                draw_list->AddRectFilled(
                    ImVec2(cx, cy),
                    ImVec2(cx + cell_w, cy + cell_h),
                    theme.cursor,
                    2.0f
                );
                const TermCell& cur_cell = grid[cr][cc];
                if (cur_cell.ch > U' ')
                {
                    std::string s;
                    encode_utf8(cur_cell.ch, s);
                    draw_list->AddText(mono_font, 16.0f, ImVec2(cx, cy + 1.0f), theme.bg, s.c_str(), s.c_str() + s.size());
                }
            }
        }

        // 3. Bottom Status Bar
        float sb_y = win_pos.y + win_size.y - statusbar_h;
        draw_list->AddRectFilled(
            ImVec2(win_pos.x, sb_y),
            ImVec2(win_pos.x + win_size.x, win_pos.y + win_size.y),
            theme.statusbar_bg
        );
        draw_list->AddLine(
            ImVec2(win_pos.x, sb_y),
            ImVec2(win_pos.x + win_size.x, sb_y),
            theme.border,
            1.0f
        );

        char status_left[256];
        std::snprintf(
            status_left,
            sizeof(status_left),
            " PTY PID: %d  |  Grid: %dx%d  |  Mode: %s  |  TERM=xterm-256color",
            static_cast<int>(term.child_pid()),
            term.cols(),
            term.rows(),
            term.use_alt_screen() ? "ALT-SCREEN (nano / vim / TUI)" : "PRIMARY SHELL"
        );
        draw_list->AddText(
            mono_font,
            13.5f,
            ImVec2(win_pos.x + 10.0f, sb_y + 6.0f),
            theme.accent,
            status_left
        );

        const char* shortcuts_hint = "Ctrl+Shift+T: Новая вкладка  |  Ctrl+Shift+C/V: Буфер  |  nano / vim / htop Ready ";
        ImVec2 hint_sz = mono_font->CalcTextSizeA(13.5f, FLT_MAX, 0.0f, shortcuts_hint);
        draw_list->AddText(
            mono_font,
            13.5f,
            ImVec2(win_pos.x + win_size.x - hint_sz.x - 10.0f, sb_y + 6.0f),
            theme.fg,
            shortcuts_hint
        );

        ImGui::End();
        ImGui::PopStyleColor();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.04f, 0.05f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        frame_counter++;
        if (screenshot_out && frame_counter == 45)
        {
            save_framebuffer_ppm(screenshot_out, display_w, display_h);
            break;
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

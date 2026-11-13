#pragma once

#include "../process/pty.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct TermCell
{
    char32_t ch = U' ';
    uint32_t fg = 0; // 0 = default theme fg, otherwise 0xAABBGGRR (ImGui packed color)
    uint32_t bg = 0; // 0 = default theme bg, otherwise 0xAABBGGRR
    bool bold = false;
    bool dim = false;
    bool underline = false;
    bool inverse = false;
};

class Terminal
{
public:
    Terminal(int rows = 30, int cols = 100);

    bool start(const char* program);
    void update();
    void send_input(const std::string& input);
    void resize(int new_rows, int new_cols);
    void clear_screen();

    int rows() const { return rows_; }
    int cols() const { return cols_; }
    int cursor_row() const { return cursor_row_; }
    int cursor_col() const { return cursor_col_; }
    bool cursor_visible() const { return cursor_visible_; }
    bool use_alt_screen() const { return use_alt_screen_; }
    bool app_cursor_keys() const { return app_cursor_keys_; }
    bool bracketed_paste() const { return bracketed_paste_; }
    pid_t child_pid() const { return pty_.child_pid(); }
    bool is_alive() const { return pty_.is_alive(); }
    const std::string& title() const { return title_; }

    const std::vector<std::vector<TermCell>>& active_grid() const;
    const std::deque<std::vector<TermCell>>& scrollback() const { return scrollback_; }

    std::string dump_plain_text() const;

private:
    enum class ParserState
    {
        Normal,
        Esc,
        Csi,
        Osc,
        Charset
    };

    void feed_byte(uint8_t byte);
    void process_codepoint(char32_t cp);
    void put_char(char32_t cp);
    void newline();
    void reverse_index();
    void scroll_up(int top, int bottom, int count);
    void scroll_down(int top, int bottom, int count);
    void handle_csi(char final_byte);
    void handle_sgr(const std::vector<int>& params);
    void erase_in_display(int mode);
    void erase_in_line(int mode);
    void save_cursor();
    void restore_cursor();
    std::vector<std::vector<TermCell>>& grid_mut();
    TermCell blank_cell() const;
    uint32_t ansi_color_to_abgr(int idx) const;

    PTY pty_;
    int rows_;
    int cols_;
    int cursor_row_ = 0;
    int cursor_col_ = 0;
    int scroll_top_ = 0;
    int scroll_bottom_ = 29;
    bool pending_wrap_ = false;
    bool auto_wrap_ = true;
    bool cursor_visible_ = true;
    bool use_alt_screen_ = false;
    bool app_cursor_keys_ = false;
    bool bracketed_paste_ = false;
    bool dec_graphics_mode_ = false;

    uint32_t cur_fg_ = 0;
    uint32_t cur_bg_ = 0;
    bool cur_bold_ = false;
    bool cur_dim_ = false;
    bool cur_underline_ = false;
    bool cur_inverse_ = false;

    int saved_row_ = 0;
    int saved_col_ = 0;
    uint32_t saved_fg_ = 0;
    uint32_t saved_bg_ = 0;
    bool saved_bold_ = false;
    bool saved_dim_ = false;
    bool saved_underline_ = false;
    bool saved_inverse_ = false;

    ParserState state_ = ParserState::Normal;
    std::string csi_buf_;
    std::string osc_buf_;

    // UTF-8 decoder state
    uint32_t utf8_codepoint_ = 0;
    int utf8_expected_ = 0;

    std::string title_ = "Gob-Inc Terminal";
    std::vector<std::vector<TermCell>> primary_grid_;
    std::vector<std::vector<TermCell>> alt_grid_;
    std::deque<std::vector<TermCell>> scrollback_;
};

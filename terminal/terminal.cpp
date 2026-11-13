#include "terminal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

static uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           static_cast<uint32_t>(r);
}

static void append_utf8(std::string& out, char32_t cp)
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

Terminal::Terminal(int rows, int cols)
    : rows_(std::max(5, rows)),
      cols_(std::max(20, cols)),
      scroll_top_(0),
      scroll_bottom_(rows_ - 1)
{
    primary_grid_.assign(rows_, std::vector<TermCell>(cols_));
    alt_grid_.assign(rows_, std::vector<TermCell>(cols_));
}

bool Terminal::start(const char* program)
{
    return pty_.start(program, rows_, cols_);
}

void Terminal::update()
{
    std::string data = pty_.read_output();
    for (unsigned char c : data)
    {
        feed_byte(c);
    }
}

void Terminal::send_input(const std::string& input)
{
    pty_.write_input(input);
}

void Terminal::resize(int new_rows, int new_cols)
{
    new_rows = std::max(5, new_rows);
    new_cols = std::max(20, new_cols);
    if (new_rows == rows_ && new_cols == cols_)
    {
        return;
    }

    auto resize_grid = [&](std::vector<std::vector<TermCell>>& g) {
        g.resize(new_rows, std::vector<TermCell>(new_cols));
        for (auto& row : g)
        {
            row.resize(new_cols);
        }
    };

    resize_grid(primary_grid_);
    resize_grid(alt_grid_);

    rows_ = new_rows;
    cols_ = new_cols;
    scroll_top_ = 0;
    scroll_bottom_ = rows_ - 1;
    cursor_row_ = std::clamp(cursor_row_, 0, rows_ - 1);
    cursor_col_ = std::clamp(cursor_col_, 0, cols_ - 1);
    pending_wrap_ = false;

    pty_.resize(rows_, cols_);
}

void Terminal::clear_screen()
{
    for (auto& row : grid_mut())
    {
        std::fill(row.begin(), row.end(), TermCell{});
    }
    cursor_row_ = 0;
    cursor_col_ = 0;
    pending_wrap_ = false;
}

const std::vector<std::vector<TermCell>>& Terminal::active_grid() const
{
    return use_alt_screen_ ? alt_grid_ : primary_grid_;
}

std::vector<std::vector<TermCell>>& Terminal::grid_mut()
{
    return use_alt_screen_ ? alt_grid_ : primary_grid_;
}

TermCell Terminal::blank_cell() const
{
    TermCell c;
    c.ch = U' ';
    c.fg = cur_fg_;
    c.bg = cur_bg_;
    c.bold = false;
    c.dim = false;
    c.underline = false;
    c.inverse = cur_inverse_;
    return c;
}

std::string Terminal::dump_plain_text() const
{
    std::string out;
    const auto& g = active_grid();
    for (int r = 0; r < rows_; ++r)
    {
        std::string line;
        for (int c = 0; c < cols_; ++c)
        {
            append_utf8(line, g[r][c].ch ? g[r][c].ch : U' ');
        }
        while (!line.empty() && line.back() == ' ')
        {
            line.pop_back();
        }
        out += line;
        out.push_back('\n');
    }
    return out;
}

void Terminal::feed_byte(uint8_t byte)
{
    // Handle control characters that can interrupt even inside escape sequences
    if (state_ != ParserState::Osc)
    {
        if (byte == 0x1B)
        {
            state_ = ParserState::Esc;
            utf8_expected_ = 0;
            return;
        }
    }

    switch (state_)
    {
    case ParserState::Normal:
        if (utf8_expected_ > 0)
        {
            if ((byte & 0xC0) == 0x80)
            {
                utf8_codepoint_ = (utf8_codepoint_ << 6) | (byte & 0x3F);
                utf8_expected_--;
                if (utf8_expected_ == 0)
                {
                    process_codepoint(static_cast<char32_t>(utf8_codepoint_));
                }
                return;
            }
            else
            {
                utf8_expected_ = 0;
            }
        }

        if (byte < 0x80)
        {
            process_codepoint(static_cast<char32_t>(byte));
        }
        else if ((byte & 0xE0) == 0xC0)
        {
            utf8_codepoint_ = byte & 0x1F;
            utf8_expected_ = 1;
        }
        else if ((byte & 0xF0) == 0xE0)
        {
            utf8_codepoint_ = byte & 0x0F;
            utf8_expected_ = 2;
        }
        else if ((byte & 0xF8) == 0xF0)
        {
            utf8_codepoint_ = byte & 0x07;
            utf8_expected_ = 3;
        }
        break;

    case ParserState::Esc:
        if (byte == '[')
        {
            state_ = ParserState::Csi;
            csi_buf_.clear();
        }
        else if (byte == ']')
        {
            state_ = ParserState::Osc;
            osc_buf_.clear();
        }
        else if (byte == '(' || byte == ')' || byte == '*' || byte == '+')
        {
            state_ = ParserState::Charset;
        }
        else if (byte == '7')
        {
            save_cursor();
            state_ = ParserState::Normal;
        }
        else if (byte == '8')
        {
            restore_cursor();
            state_ = ParserState::Normal;
        }
        else if (byte == 'D')
        {
            newline();
            state_ = ParserState::Normal;
        }
        else if (byte == 'M')
        {
            reverse_index();
            state_ = ParserState::Normal;
        }
        else if (byte == 'E')
        {
            cursor_col_ = 0;
            pending_wrap_ = false;
            newline();
            state_ = ParserState::Normal;
        }
        else if (byte == 'c')
        {
            cur_fg_ = 0;
            cur_bg_ = 0;
            cur_bold_ = false;
            cur_dim_ = false;
            cur_underline_ = false;
            cur_inverse_ = false;
            scroll_top_ = 0;
            scroll_bottom_ = rows_ - 1;
            clear_screen();
            state_ = ParserState::Normal;
        }
        else
        {
            state_ = ParserState::Normal;
        }
        break;

    case ParserState::Charset:
        if (byte == '0')
        {
            dec_graphics_mode_ = true;
        }
        else if (byte == 'B')
        {
            dec_graphics_mode_ = false;
        }
        state_ = ParserState::Normal;
        break;

    case ParserState::Osc:
        if (byte == 0x07)
        {
            if (osc_buf_.rfind("0;", 0) == 0 || osc_buf_.rfind("2;", 0) == 0)
            {
                title_ = osc_buf_.substr(2);
            }
            state_ = ParserState::Normal;
        }
        else if (byte == 0x1B)
        {
            if (osc_buf_.rfind("0;", 0) == 0 || osc_buf_.rfind("2;", 0) == 0)
            {
                title_ = osc_buf_.substr(2);
            }
            state_ = ParserState::Esc;
        }
        else if (osc_buf_.size() < 256)
        {
            osc_buf_.push_back(static_cast<char>(byte));
        }
        break;

    case ParserState::Csi:
        if (byte >= 0x40 && byte <= 0x7E)
        {
            handle_csi(static_cast<char>(byte));
            state_ = ParserState::Normal;
        }
        else if (csi_buf_.size() < 128)
        {
            csi_buf_.push_back(static_cast<char>(byte));
        }
        else
        {
            state_ = ParserState::Normal;
        }
        break;
    }
}

void Terminal::process_codepoint(char32_t cp)
{
    switch (cp)
    {
    case U'\r':
        cursor_col_ = 0;
        pending_wrap_ = false;
        break;
    case U'\n':
    case U'\v':
    case U'\f':
        newline();
        break;
    case U'\b':
        if (cursor_col_ > 0) cursor_col_--;
        pending_wrap_ = false;
        break;
    case U'\t':
    {
        int next_tab = ((cursor_col_ / 8) + 1) * 8;
        cursor_col_ = std::min(cols_ - 1, next_tab);
        pending_wrap_ = false;
        break;
    }
    case 0x07: // BEL
        break;
    case 0x0E: // SO
        dec_graphics_mode_ = true;
        break;
    case 0x0F: // SI
        dec_graphics_mode_ = false;
        break;
    default:
        if (cp >= 0x20)
        {
            if (dec_graphics_mode_ && cp >= 0x60 && cp <= 0x7E)
            {
                switch (cp)
                {
                case 'j': cp = U'┘'; break;
                case 'k': cp = U'┐'; break;
                case 'l': cp = U'┌'; break;
                case 'm': cp = U'└'; break;
                case 'n': cp = U'┼'; break;
                case 'q': cp = U'─'; break;
                case 't': cp = U'├'; break;
                case 'u': cp = U'┤'; break;
                case 'v': cp = U'┴'; break;
                case 'w': cp = U'┬'; break;
                case 'x': cp = U'│'; break;
                case 'a': cp = U'▒'; break;
                default: break;
                }
            }
            put_char(cp);
        }
        break;
    }
}

void Terminal::put_char(char32_t cp)
{
    if (pending_wrap_)
    {
        if (auto_wrap_)
        {
            cursor_col_ = 0;
            newline();
        }
        pending_wrap_ = false;
    }

    auto& g = grid_mut();
    if (cursor_row_ >= 0 && cursor_row_ < rows_ && cursor_col_ >= 0 && cursor_col_ < cols_)
    {
        TermCell& cell = g[cursor_row_][cursor_col_];
        cell.ch = cp;
        cell.fg = cur_fg_;
        cell.bg = cur_bg_;
        cell.bold = cur_bold_;
        cell.dim = cur_dim_;
        cell.underline = cur_underline_;
        cell.inverse = cur_inverse_;
    }

    if (cursor_col_ + 1 >= cols_)
    {
        pending_wrap_ = true;
    }
    else
    {
        cursor_col_++;
    }
}

void Terminal::newline()
{
    pending_wrap_ = false;
    if (cursor_row_ == scroll_bottom_)
    {
        scroll_up(scroll_top_, scroll_bottom_, 1);
    }
    else if (cursor_row_ < rows_ - 1)
    {
        cursor_row_++;
    }
}

void Terminal::reverse_index()
{
    pending_wrap_ = false;
    if (cursor_row_ == scroll_top_)
    {
        scroll_down(scroll_top_, scroll_bottom_, 1);
    }
    else if (cursor_row_ > 0)
    {
        cursor_row_--;
    }
}

void Terminal::scroll_up(int top, int bottom, int count)
{
    top = std::clamp(top, 0, rows_ - 1);
    bottom = std::clamp(bottom, top, rows_ - 1);
    count = std::clamp(count, 1, bottom - top + 1);

    auto& g = grid_mut();
    for (int i = 0; i < count; ++i)
    {
        if (!use_alt_screen_ && top == 0)
        {
            scrollback_.push_back(g[0]);
            if (scrollback_.size() > 2000)
            {
                scrollback_.pop_front();
            }
        }
        for (int r = top; r < bottom; ++r)
        {
            g[r] = std::move(g[r + 1]);
        }
        g[bottom].assign(cols_, blank_cell());
    }
}

void Terminal::scroll_down(int top, int bottom, int count)
{
    top = std::clamp(top, 0, rows_ - 1);
    bottom = std::clamp(bottom, top, rows_ - 1);
    count = std::clamp(count, 1, bottom - top + 1);

    auto& g = grid_mut();
    for (int i = 0; i < count; ++i)
    {
        for (int r = bottom; r > top; --r)
        {
            g[r] = std::move(g[r - 1]);
        }
        g[top].assign(cols_, blank_cell());
    }
}

void Terminal::save_cursor()
{
    saved_row_ = cursor_row_;
    saved_col_ = cursor_col_;
    saved_fg_ = cur_fg_;
    saved_bg_ = cur_bg_;
    saved_bold_ = cur_bold_;
    saved_dim_ = cur_dim_;
    saved_underline_ = cur_underline_;
    saved_inverse_ = cur_inverse_;
}

void Terminal::restore_cursor()
{
    cursor_row_ = std::clamp(saved_row_, 0, rows_ - 1);
    cursor_col_ = std::clamp(saved_col_, 0, cols_ - 1);
    cur_fg_ = saved_fg_;
    cur_bg_ = saved_bg_;
    cur_bold_ = saved_bold_;
    cur_dim_ = saved_dim_;
    cur_underline_ = saved_underline_;
    cur_inverse_ = saved_inverse_;
    pending_wrap_ = false;
}

void Terminal::erase_in_display(int mode)
{
    auto& g = grid_mut();
    TermCell blank = blank_cell();

    if (mode == 0)
    {
        for (int c = cursor_col_; c < cols_; ++c) g[cursor_row_][c] = blank;
        for (int r = cursor_row_ + 1; r < rows_; ++r) std::fill(g[r].begin(), g[r].end(), blank);
    }
    else if (mode == 1)
    {
        for (int r = 0; r < cursor_row_; ++r) std::fill(g[r].begin(), g[r].end(), blank);
        for (int c = 0; c <= cursor_col_; ++c) g[cursor_row_][c] = blank;
    }
    else if (mode == 2 || mode == 3)
    {
        for (int r = 0; r < rows_; ++r) std::fill(g[r].begin(), g[r].end(), blank);
        if (mode == 3 && !use_alt_screen_) scrollback_.clear();
    }
}

void Terminal::erase_in_line(int mode)
{
    auto& g = grid_mut();
    TermCell blank = blank_cell();

    if (mode == 0)
    {
        for (int c = cursor_col_; c < cols_; ++c) g[cursor_row_][c] = blank;
    }
    else if (mode == 1)
    {
        for (int c = 0; c <= cursor_col_; ++c) g[cursor_row_][c] = blank;
    }
    else if (mode == 2)
    {
        std::fill(g[cursor_row_].begin(), g[cursor_row_].end(), blank);
    }
}

void Terminal::handle_csi(char final_byte)
{
    pending_wrap_ = false;
    bool private_mode = false;
    size_t idx = 0;
    if (!csi_buf_.empty() && (csi_buf_[0] == '?' || csi_buf_[0] == '>' || csi_buf_[0] == '='))
    {
        private_mode = (csi_buf_[0] == '?');
        idx = 1;
    }

    std::vector<int> params;
    int current = 0;
    bool has_digit = false;
    for (; idx < csi_buf_.size(); ++idx)
    {
        char ch = csi_buf_[idx];
        if (std::isdigit(static_cast<unsigned char>(ch)))
        {
            current = current * 10 + (ch - '0');
            has_digit = true;
        }
        else if (ch == ';' || ch == ':')
        {
            params.push_back(has_digit ? current : 0);
            current = 0;
            has_digit = false;
        }
    }
    if (has_digit || !params.empty())
    {
        params.push_back(has_digit ? current : 0);
    }

    auto p = [&](size_t i, int def) -> int {
        if (i < params.size() && params[i] > 0) return params[i];
        return def;
    };
    auto p_zero = [&](size_t i, int def) -> int {
        if (i < params.size()) return params[i];
        return def;
    };

    if (private_mode)
    {
        if (final_byte == 'h' || final_byte == 'l')
        {
            bool enable = (final_byte == 'h');
            for (int mode : params)
            {
                switch (mode)
                {
                case 1:
                    app_cursor_keys_ = enable;
                    break;
                case 7:
                    auto_wrap_ = enable;
                    break;
                case 25:
                    cursor_visible_ = enable;
                    break;
                case 47:
                case 1047:
                case 1049:
                    if (enable && !use_alt_screen_)
                    {
                        if (mode == 1049) save_cursor();
                        use_alt_screen_ = true;
                        for (auto& row : alt_grid_) std::fill(row.begin(), row.end(), TermCell{});
                        cursor_row_ = 0;
                        cursor_col_ = 0;
                    }
                    else if (!enable && use_alt_screen_)
                    {
                        use_alt_screen_ = false;
                        if (mode == 1049) restore_cursor();
                    }
                    break;
                case 2004:
                    bracketed_paste_ = enable;
                    break;
                default:
                    break;
                }
            }
        }
        return;
    }

    auto& g = grid_mut();

    switch (final_byte)
    {
    case 'A': // CUU
        cursor_row_ = std::max(0, cursor_row_ - p(0, 1));
        break;
    case 'B': // CUD
        cursor_row_ = std::min(rows_ - 1, cursor_row_ + p(0, 1));
        break;
    case 'C': // CUF
        cursor_col_ = std::min(cols_ - 1, cursor_col_ + p(0, 1));
        break;
    case 'D': // CUB
        cursor_col_ = std::max(0, cursor_col_ - p(0, 1));
        break;
    case 'E': // CNL
        cursor_col_ = 0;
        cursor_row_ = std::min(rows_ - 1, cursor_row_ + p(0, 1));
        break;
    case 'F': // CPL
        cursor_col_ = 0;
        cursor_row_ = std::max(0, cursor_row_ - p(0, 1));
        break;
    case 'G': // CHA
    case '`':
        cursor_col_ = std::clamp(p(0, 1) - 1, 0, cols_ - 1);
        break;
    case 'd': // VPA
        cursor_row_ = std::clamp(p(0, 1) - 1, 0, rows_ - 1);
        break;
    case 'H': // CUP
    case 'f':
        cursor_row_ = std::clamp(p(0, 1) - 1, 0, rows_ - 1);
        cursor_col_ = std::clamp(p(1, 1) - 1, 0, cols_ - 1);
        break;
    case 'J': // ED
        erase_in_display(p_zero(0, 0));
        break;
    case 'K': // EL
        erase_in_line(p_zero(0, 0));
        break;
    case 'X': // ECH
    {
        int n = p(0, 1);
        TermCell blank = blank_cell();
        for (int c = cursor_col_; c < std::min(cols_, cursor_col_ + n); ++c)
        {
            g[cursor_row_][c] = blank;
        }
        break;
    }
    case 'L': // IL - Insert Lines
        if (cursor_row_ >= scroll_top_ && cursor_row_ <= scroll_bottom_)
        {
            scroll_down(cursor_row_, scroll_bottom_, p(0, 1));
        }
        break;
    case 'M': // DL - Delete Lines
        if (cursor_row_ >= scroll_top_ && cursor_row_ <= scroll_bottom_)
        {
            scroll_up(cursor_row_, scroll_bottom_, p(0, 1));
        }
        break;
    case '@': // ICH - Insert Blank Characters
    {
        int n = std::min(p(0, 1), cols_ - cursor_col_);
        auto& row = g[cursor_row_];
        for (int c = cols_ - 1; c >= cursor_col_ + n; --c)
        {
            row[c] = row[c - n];
        }
        TermCell blank = blank_cell();
        for (int c = cursor_col_; c < cursor_col_ + n; ++c)
        {
            row[c] = blank;
        }
        break;
    }
    case 'P': // DCH - Delete Characters
    {
        int n = std::min(p(0, 1), cols_ - cursor_col_);
        auto& row = g[cursor_row_];
        for (int c = cursor_col_; c < cols_ - n; ++c)
        {
            row[c] = row[c + n];
        }
        TermCell blank = blank_cell();
        for (int c = cols_ - n; c < cols_; ++c)
        {
            row[c] = blank;
        }
        break;
    }
    case 'S': // SU
        scroll_up(scroll_top_, scroll_bottom_, p(0, 1));
        break;
    case 'T': // SD
        scroll_down(scroll_top_, scroll_bottom_, p(0, 1));
        break;
    case 'r': // DECSTBM
    {
        int top = p(0, 1) - 1;
        int bottom = p(1, rows_) - 1;
        if (top >= 0 && bottom < rows_ && top < bottom)
        {
            scroll_top_ = top;
            scroll_bottom_ = bottom;
            cursor_row_ = 0;
            cursor_col_ = 0;
        }
        break;
    }
    case 's':
        save_cursor();
        break;
    case 'u':
        restore_cursor();
        break;
    case 'm':
        handle_sgr(params);
        break;
    case 'n': // DSR
        if (p_zero(0, 0) == 6)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "\x1b[%d;%dR", cursor_row_ + 1, cursor_col_ + 1);
            pty_.write_input(buf);
        }
        break;
    case 'c': // DA
        pty_.write_input("\x1b[?1;2c");
        break;
    default:
        break;
    }
}

uint32_t Terminal::ansi_color_to_abgr(int idx) const
{
    static const uint8_t base16[16][3] = {
        { 22,  27,  38}, // 0 Black
        {247, 118, 142}, // 1 Red
        {158, 206, 106}, // 2 Green
        {224, 175, 104}, // 3 Yellow
        {122, 162, 247}, // 4 Blue
        {187, 154, 247}, // 5 Magenta
        {125, 207, 255}, // 6 Cyan
        {192, 202, 245}, // 7 White
        { 65,  72, 104}, // 8 Bright Black
        {255, 137, 157}, // 9 Bright Red
        {185, 242, 124}, // 10 Bright Green
        {255, 208, 128}, // 11 Bright Yellow
        {147, 184, 255}, // 12 Bright Blue
        {209, 184, 255}, // 13 Bright Magenta
        {157, 225, 255}, // 14 Bright Cyan
        {248, 250, 252}  // 15 Bright White
    };

    if (idx < 0) return 0;
    if (idx < 16)
    {
        return pack_rgba(base16[idx][0], base16[idx][1], base16[idx][2]);
    }
    if (idx < 232)
    {
        int n = idx - 16;
        int r = (n / 36) % 6;
        int g = (n / 6) % 6;
        int b = n % 6;
        auto conv = [](int v) -> uint8_t { return v == 0 ? 0 : static_cast<uint8_t>(55 + v * 40); };
        return pack_rgba(conv(r), conv(g), conv(b));
    }
    if (idx < 256)
    {
        uint8_t gray = static_cast<uint8_t>(8 + (idx - 232) * 10);
        return pack_rgba(gray, gray, gray);
    }
    return 0;
}

void Terminal::handle_sgr(const std::vector<int>& params)
{
    if (params.empty())
    {
        cur_fg_ = 0;
        cur_bg_ = 0;
        cur_bold_ = false;
        cur_dim_ = false;
        cur_underline_ = false;
        cur_inverse_ = false;
        return;
    }

    for (size_t i = 0; i < params.size(); ++i)
    {
        int code = params[i];
        switch (code)
        {
        case 0:
            cur_fg_ = 0;
            cur_bg_ = 0;
            cur_bold_ = false;
            cur_dim_ = false;
            cur_underline_ = false;
            cur_inverse_ = false;
            break;
        case 1:
            cur_bold_ = true;
            cur_dim_ = false;
            break;
        case 2:
            cur_dim_ = true;
            cur_bold_ = false;
            break;
        case 4:
            cur_underline_ = true;
            break;
        case 7:
            cur_inverse_ = true;
            break;
        case 22:
            cur_bold_ = false;
            cur_dim_ = false;
            break;
        case 24:
            cur_underline_ = false;
            break;
        case 27:
            cur_inverse_ = false;
            break;
        case 39:
            cur_fg_ = 0;
            break;
        case 49:
            cur_bg_ = 0;
            break;
        case 38:
        case 48:
        {
            bool is_fg = (code == 38);
            if (i + 2 < params.size() && params[i + 1] == 5)
            {
                uint32_t col = ansi_color_to_abgr(params[i + 2]);
                if (is_fg) cur_fg_ = col; else cur_bg_ = col;
                i += 2;
            }
            else if (i + 4 < params.size() && params[i + 1] == 2)
            {
                uint8_t r = static_cast<uint8_t>(std::clamp(params[i + 2], 0, 255));
                uint8_t g = static_cast<uint8_t>(std::clamp(params[i + 3], 0, 255));
                uint8_t b = static_cast<uint8_t>(std::clamp(params[i + 4], 0, 255));
                uint32_t col = pack_rgba(r, g, b);
                if (is_fg) cur_fg_ = col; else cur_bg_ = col;
                i += 4;
            }
            break;
        }
        default:
            if (code >= 30 && code <= 37)
            {
                cur_fg_ = ansi_color_to_abgr(code - 30 + (cur_bold_ ? 8 : 0));
            }
            else if (code >= 40 && code <= 47)
            {
                cur_bg_ = ansi_color_to_abgr(code - 40);
            }
            else if (code >= 90 && code <= 97)
            {
                cur_fg_ = ansi_color_to_abgr(code - 90 + 8);
            }
            else if (code >= 100 && code <= 107)
            {
                cur_bg_ = ansi_color_to_abgr(code - 100 + 8);
            }
            break;
        }
    }
}

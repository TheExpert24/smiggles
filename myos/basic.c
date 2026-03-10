#include "kernel.h"

#define BASIC_MAX_LINES 64
#define BASIC_LINE_LEN 80
#define BASIC_MAX_STEPS 10000

typedef struct {
    int used;
    int number;
    char text[BASIC_LINE_LEN];
} BasicLine;

static BasicLine basic_program[BASIC_MAX_LINES];
static int basic_vars[26];

static int tb_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int tb_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int tb_is_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static char tb_upper(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static void tb_skip_spaces(const char** s) {
    while (**s && tb_is_space(**s)) (*s)++;
}

static int tb_match_kw(const char** s, const char* kw) {
    const char* p = *s;
    int i = 0;
    while (kw[i]) {
        if (tb_upper(p[i]) != kw[i]) return 0;
        i++;
    }
    if (tb_is_alpha(p[i]) || tb_is_digit(p[i])) return 0;
    *s = p + i;
    return 1;
}

static int tb_parse_int(const char** s, int* out) {
    int value = 0;
    int neg = 0;
    int any = 0;

    tb_skip_spaces(s);
    if (**s == '-') {
        neg = 1;
        (*s)++;
    }

    while (tb_is_digit(**s)) {
        any = 1;
        value = value * 10 + (**s - '0');
        (*s)++;
    }

    if (!any) return 0;
    *out = neg ? -value : value;
    return 1;
}

static int tb_parse_expr(const char** s, int* out);

static int tb_parse_factor(const char** s, int* out) {
    tb_skip_spaces(s);

    if (**s == '(') {
        (*s)++;
        if (!tb_parse_expr(s, out)) return 0;
        tb_skip_spaces(s);
        if (**s != ')') return 0;
        (*s)++;
        return 1;
    }

    if (tb_is_alpha(**s)) {
        char var = tb_upper(**s);
        (*s)++;
        if (var < 'A' || var > 'Z') return 0;
        *out = basic_vars[var - 'A'];
        return 1;
    }

    return tb_parse_int(s, out);
}

static int tb_parse_term(const char** s, int* out) {
    int lhs = 0;
    if (!tb_parse_factor(s, &lhs)) return 0;

    while (1) {
        int rhs = 0;
        tb_skip_spaces(s);
        if (**s == '*') {
            (*s)++;
            if (!tb_parse_factor(s, &rhs)) return 0;
            lhs *= rhs;
        } else if (**s == '/') {
            (*s)++;
            if (!tb_parse_factor(s, &rhs)) return 0;
            if (rhs == 0) return 0;
            lhs /= rhs;
        } else {
            break;
        }
    }

    *out = lhs;
    return 1;
}

static int tb_parse_expr(const char** s, int* out) {
    int lhs = 0;
    if (!tb_parse_term(s, &lhs)) return 0;

    while (1) {
        int rhs = 0;
        tb_skip_spaces(s);
        if (**s == '+') {
            (*s)++;
            if (!tb_parse_term(s, &rhs)) return 0;
            lhs += rhs;
        } else if (**s == '-') {
            (*s)++;
            if (!tb_parse_term(s, &rhs)) return 0;
            lhs -= rhs;
        } else {
            break;
        }
    }

    *out = lhs;
    return 1;
}

static void tb_print(const char* s, char* video, int* cursor, unsigned char color) {
    print_string(s, -1, video, cursor, color);
}

static int tb_find_line(int number) {
    for (int i = 0; i < BASIC_MAX_LINES; i++) {
        if (basic_program[i].used && basic_program[i].number == number) return i;
    }
    return -1;
}

static int tb_next_used_from(int idx) {
    for (int i = idx + 1; i < BASIC_MAX_LINES; i++) {
        if (basic_program[i].used) return i;
    }
    return -1;
}

static void tb_sort_program(void) {
    for (int i = 0; i < BASIC_MAX_LINES - 1; i++) {
        for (int j = i + 1; j < BASIC_MAX_LINES; j++) {
            if (!basic_program[i].used && basic_program[j].used) {
                BasicLine tmp = basic_program[i];
                basic_program[i] = basic_program[j];
                basic_program[j] = tmp;
            } else if (basic_program[i].used && basic_program[j].used && basic_program[j].number < basic_program[i].number) {
                BasicLine tmp = basic_program[i];
                basic_program[i] = basic_program[j];
                basic_program[j] = tmp;
            }
        }
    }
}

static void tb_clear_program(void) {
    for (int i = 0; i < BASIC_MAX_LINES; i++) {
        basic_program[i].used = 0;
        basic_program[i].number = 0;
        basic_program[i].text[0] = 0;
    }
}

static int tb_store_line(int line_no, const char* text) {
    int existing = tb_find_line(line_no);

    if (!text || text[0] == 0) {
        if (existing >= 0) basic_program[existing].used = 0;
        tb_sort_program();
        return 1;
    }

    if (existing >= 0) {
        str_copy(basic_program[existing].text, text, BASIC_LINE_LEN);
        return 1;
    }

    for (int i = 0; i < BASIC_MAX_LINES; i++) {
        if (!basic_program[i].used) {
            basic_program[i].used = 1;
            basic_program[i].number = line_no;
            str_copy(basic_program[i].text, text, BASIC_LINE_LEN);
            tb_sort_program();
            return 1;
        }
    }

    return 0;
}

static void tb_list_program(char* video, int* cursor) {
    char line[128];
    char num[16];
    for (int i = 0; i < BASIC_MAX_LINES; i++) {
        if (!basic_program[i].used) continue;
        line[0] = 0;
        int_to_str(basic_program[i].number, num);
        str_concat(line, num);
        str_concat(line, " ");
        str_concat(line, basic_program[i].text);
        tb_print(line, video, cursor, COLOR_LIGHT_CYAN);
    }
}

static int tb_eval_condition(const char** s, int* out_true) {
    int lhs = 0;
    int rhs = 0;
    char op1 = 0;
    char op2 = 0;

    if (!tb_parse_expr(s, &lhs)) return 0;

    tb_skip_spaces(s);
    op1 = **s;
    if (!(op1 == '=' || op1 == '<' || op1 == '>')) return 0;
    (*s)++;
    op2 = **s;
    if ((op1 == '<' || op1 == '>') && (op2 == '=' || op2 == '>')) {
        (*s)++;
    } else {
        op2 = 0;
    }

    if (!tb_parse_expr(s, &rhs)) return 0;

    if (op1 == '=' && op2 == 0) *out_true = (lhs == rhs);
    else if (op1 == '<' && op2 == 0) *out_true = (lhs < rhs);
    else if (op1 == '>' && op2 == 0) *out_true = (lhs > rhs);
    else if (op1 == '<' && op2 == '=') *out_true = (lhs <= rhs);
    else if (op1 == '>' && op2 == '=') *out_true = (lhs >= rhs);
    else if (op1 == '<' && op2 == '>') *out_true = (lhs != rhs);
    else return 0;

    return 1;
}

typedef enum {
    TB_OK = 0,
    TB_END = 1,
    TB_ERR = -1
} TbResult;

static TbResult tb_exec_line(const char* text, int* pc, char* video, int* cursor) {
    const char* s = text;
    int value = 0;

    tb_skip_spaces(&s);
    if (*s == 0) return TB_OK;

    if (tb_match_kw(&s, "REM")) return TB_OK;

    if (tb_match_kw(&s, "PRINT")) {
        tb_skip_spaces(&s);
        if (*s == '"') {
            char out[96];
            int i = 0;
            s++;
            while (*s && *s != '"' && i < 95) out[i++] = *s++;
            out[i] = 0;
            tb_print(out, video, cursor, COLOR_LIGHT_GREEN);
            return TB_OK;
        }
        if (!tb_parse_expr(&s, &value)) return TB_ERR;
        char num[16];
        int_to_str(value, num);
        tb_print(num, video, cursor, COLOR_LIGHT_GREEN);
        return TB_OK;
    }

    if (tb_match_kw(&s, "LET")) {
        tb_skip_spaces(&s);
        if (!tb_is_alpha(*s)) return TB_ERR;
        char var = tb_upper(*s++);
        tb_skip_spaces(&s);
        if (*s != '=') return TB_ERR;
        s++;
        if (!tb_parse_expr(&s, &value)) return TB_ERR;
        basic_vars[var - 'A'] = value;
        return TB_OK;
    }

    if (tb_match_kw(&s, "IF")) {
        int cond = 0;
        int line_no = 0;
        if (!tb_eval_condition(&s, &cond)) return TB_ERR;
        tb_skip_spaces(&s);
        if (!tb_match_kw(&s, "THEN")) return TB_ERR;
        if (!tb_parse_int(&s, &line_no)) return TB_ERR;
        if (cond) {
            int idx = tb_find_line(line_no);
            if (idx < 0) return TB_ERR;
            *pc = idx;
        }
        return TB_OK;
    }

    if (tb_match_kw(&s, "GOTO")) {
        int line_no = 0;
        if (!tb_parse_int(&s, &line_no)) return TB_ERR;
        int idx = tb_find_line(line_no);
        if (idx < 0) return TB_ERR;
        *pc = idx;
        return TB_OK;
    }

    if (tb_match_kw(&s, "END")) {
        return TB_END;
    }

    if (tb_is_alpha(*s)) {
        char var = tb_upper(*s);
        if (var >= 'A' && var <= 'Z') {
            s++;
            tb_skip_spaces(&s);
            if (*s == '=') {
                s++;
                if (!tb_parse_expr(&s, &value)) return TB_ERR;
                basic_vars[var - 'A'] = value;
                return TB_OK;
            }
        }
    }

    return TB_ERR;
}

static void tb_run_program(char* video, int* cursor) {
    int pc = -1;
    int steps = 0;

    for (int i = 0; i < BASIC_MAX_LINES; i++) {
        if (basic_program[i].used) {
            pc = i;
            break;
        }
    }

    if (pc < 0) {
        tb_print("No program loaded", video, cursor, COLOR_LIGHT_RED);
        return;
    }

    while (pc >= 0 && pc < BASIC_MAX_LINES && basic_program[pc].used) {
        int original_pc = pc;
        int next_pc = tb_next_used_from(pc);
        TbResult r;

        if (++steps > BASIC_MAX_STEPS) {
            tb_print("Runtime error: step limit reached", video, cursor, COLOR_LIGHT_RED);
            return;
        }

        r = tb_exec_line(basic_program[pc].text, &pc, video, cursor);
        if (r == TB_END) return;
        if (r == TB_ERR) {
            tb_print("Runtime error", video, cursor, COLOR_LIGHT_RED);
            return;
        }

        if (pc == original_pc) {
            pc = next_pc;
        }
    }
}

void basic_repl(char* video, int* cursor) {
    extern void shell_read_line(char* prompt, char* buf, int max_len, char* video, int* cursor);
    char line[BASIC_LINE_LEN];

    tb_print("BASIC mode. Commands: RUN, LIST, NEW, EXIT", video, cursor, COLOR_YELLOW);

    while (1) {
        const char* s;
        int line_no = 0;

        shell_read_line("basic> ", line, BASIC_LINE_LEN, video, cursor);
        s = line;
        tb_skip_spaces(&s);

        if (*s == 0) continue;

        if (tb_parse_int(&s, &line_no)) {
            tb_skip_spaces(&s);
            if (!tb_store_line(line_no, s)) {
                tb_print("Out of line slots", video, cursor, COLOR_LIGHT_RED);
            }
            continue;
        }

        if (tb_match_kw(&s, "RUN")) {
            tb_run_program(video, cursor);
            continue;
        }

        if (tb_match_kw(&s, "LIST")) {
            tb_list_program(video, cursor);
            continue;
        }

        if (tb_match_kw(&s, "NEW")) {
            tb_clear_program();
            for (int i = 0; i < 26; i++) basic_vars[i] = 0;
            tb_print("Program cleared", video, cursor, COLOR_LIGHT_GREEN);
            continue;
        }

        if (tb_match_kw(&s, "HELP")) {
            tb_print("Line format: <num> <stmt>", video, cursor, COLOR_LIGHT_CYAN);
            tb_print("Stmt: LET, PRINT, IF..THEN, GOTO, END", video, cursor, COLOR_LIGHT_CYAN);
            tb_print("Commands: RUN, LIST, NEW, EXIT", video, cursor, COLOR_LIGHT_CYAN);
            continue;
        }

        if (tb_match_kw(&s, "EXIT") || tb_match_kw(&s, "QUIT")) {
            tb_print("Leaving BASIC", video, cursor, COLOR_YELLOW);
            return;
        }

        {
            int fake_pc = 0;
            TbResult r = tb_exec_line(s, &fake_pc, video, cursor);
            if (r == TB_ERR) {
                tb_print("Syntax error", video, cursor, COLOR_LIGHT_RED);
            }
        }
    }
}

#include "kernel.h"

// --- Calculator Parser ---

// Forward declarations
static int parse_expr(const char **s);
static int parse_term(const char **s);
static int parse_power(const char **s);
static int parse_factor(const char **s);
static int parse_number(const char **s);
static void skip_spaces(const char **s);

static void skip_spaces(const char **s) {
    while (**s == ' ') (*s)++;
}

static int parse_number(const char **s) {
    skip_spaces(s);
    int val = 0;
    int neg = 0;
    if (**s == '-') {
        neg = 1;
        (*s)++;
    }
    while (**s >= '0' && **s <= '9') {
        val = val * 10 + (**s - '0');
        (*s)++;
    }
    return neg ? -val : val;
}

static int parse_factor(const char **s) {
    skip_spaces(s);
    int val;
    if (**s == '(') {
        (*s)++;
        val = parse_expr(s);
        skip_spaces(s);
        if (**s == ')') (*s)++;
        return val;
    } else {
        return parse_number(s);
    }
}

static int parse_power(const char **s) {
    int base = parse_factor(s);
    skip_spaces(s);
    while (**s == '^') {
        (*s)++;
        int exp = parse_power(s); // right-associative
        int result = 1;
        for (int i = 0; i < exp; i++) result *= base;
        base = result;
        skip_spaces(s);
    }
    return base;
}

static int parse_term(const char **s) {
    int val = parse_power(s);
    skip_spaces(s);
    while (**s == '*' || **s == '/') {
        char op = **s;
        (*s)++;
        int rhs = parse_power(s);
        if (op == '*') val *= rhs;
        else if (op == '/') val /= rhs;
        skip_spaces(s);
    }
    return val;
}

static int parse_expr(const char **s) {
    int val = parse_term(s);
    skip_spaces(s);
    while (**s == '+' || **s == '-') {
        char op = **s;
        (*s)++;
        int rhs = parse_term(s);
        if (op == '+') val += rhs;
        else if (op == '-') val -= rhs;
        skip_spaces(s);
    }
    return val;
}

// tells if a string is a math expression or not
int is_math_expr(const char* s) {
    skip_spaces(&s);
    if ((*s >= '0' && *s <= '9') || *s == '(' || *s == '-') return 1;
    return 0;
}

void handle_calc_command(const char* expr, char* video, int* cursor) {
    const char* s = expr;
    int result = parse_expr(&s);
    char buf[32];
    int n = 0, r = result;
    int neg = 0;
    if (r < 0) { neg = 1; r = -r; }
    do {
        buf[n++] = '0' + (r % 10);
        r /= 10;
    } while (r > 0 && n < 30);
    if (neg) buf[n++] = '-';
    for (int i = 0; i < n/2; i++) {
        char t = buf[i]; buf[i] = buf[n-1-i]; buf[n-1-i] = t;
    }
    buf[n] = 0;
    print_string(buf, n, video, cursor, 0x0A); // green
}
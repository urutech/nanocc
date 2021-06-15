#include <stdio.h>
#include <stdlib.h>

// nanocc treats all # lines as comments
#include "nanocc-itf.h"
int _sys_read(int fd, char *buf, int count);
int _sys_write(int fd, char *s, int n);
int _sys_exit(int code);
void gen_write_binary(char *emit_buffer, int emit_pos, char *string_table_buffer, int string_table_size);
void gen_library(int emit_pos, char *symbol_name[], int *symbol_type, int *symbol_address, int symbol_count);

#ifdef _MSC_VER
#include <io.h>
#include <fcntl.h>
#define ssize_t int
#define _sys_read  read
#define _sys_write(fd, buf, cnt) do { setmode(fd, _O_BINARY); /* no 0xa->0xd 0xa conv */ write(fd, buf, cnt); } while (0)
#define _sys_exit  exit
#else
#include <unistd.h>
#define _sys_read  read
#define _sys_write write
#define _sys_exit  exit
#endif

enum Sizes {
    MAX_SYMBOLS             = 1024,
    MAX_STRING_TABLE_BUFFER = 64*1024,
    EMIT_BUFFER_SIZE        = 2*1024*1024,
    EXPR_STACK_SIZE         = 128,
    MAX_BACKPATCH           = 1024,
    MAX_POSTFIX             = 10
};

enum Token {
    CHAR  = 1,
    INT, VOID, RETURN, IF, WHILE, CONTINUE, BREAK, ELSE, ENUM, DO, GOTO,
    IDENTIFIER  = 128,
    NUMBER, GE, EQ, RSH, NEQ, LOGAND, LOGOR, LSH, PLUSPLUS, PLUSASSIGN,
    MINUSASSIGN, MINUSMINUS, DIVASSIGN, MULASSIGN, ORASSIGN,
    MODASSIGN, XORASSIGN, ANDASSIGN, LE, RSHASSIGN, LSHASSIGN, STRING, ARRAY_SUBSCRIPT
};

enum ErrorCode {
    E_MISSING_TYPE = 1,
    E_MISSING_IDENTIFIER,
    E_MISSING_FUNCTION_BLOCK,
    E_IF_MISSING_OPENING_PARANTHESIS,
    E_IF_MISSING_CLOSING_PARANTHESIS,
    E_WHILE_MISSING_OPENING_PARANTHESIS,
    E_WHILE_MISSING_CLOSING_PARANTHESIS,
    E_LABEL_MISSING_COLON,
    E_GOTO_MISSING_IDENTIFIER,
    E_MISSING_SEMICOLON,
    E_BAD_INITIALIZATION,
    E_ENUM_MISSING_OPENING_BRACES,
    E_ENUM_MISSING_IDENTIFIER,
    E_ENUM_MISSING_NUMBER,
    E_MISSING_ARRAY_SIZE,
    E_WRONG_ARRAY_DEF,
    E_BAD_EXPRESSION,
    E_FCT_MISSING_PARANTHESIS,
    E_PARAM_DEF,
    E_UNDEFINED_IDENTIFIER,
    E_EXPRESSION_NOT_CONST,
    E_CONTINUE_OUTSIDE_LOOP,
    E_BREAK_OUTSIDE_LOOP,
    E_DO_MISSING_WHILE
};

enum TypeAttr {
    ARRAY = 0x200,
    POINTER = 0x400,
    GLOBAL = 0x800,
    UNARY = 0x1000,
    LOCAL = 0x2000,
    IDATA = GLOBAL + LOCAL,
    PARAM = 0x4000,
    FUNCTION = 0x8000,
    OPERATOR = 0x10000,
};

// lexer variables
int  current_char, previous_char;
int  token_value;
char token_text[256];
int  token_text_len;
int  lineno;
int  pushed_token;

// strings and symbol names
char string_table_buffer[MAX_STRING_TABLE_BUFFER]; // end up in the generated file
int  string_table_size;
char symbol_name_buffer[MAX_STRING_TABLE_BUFFER];  // only used during compilation
int  symbol_name_buffer_size;

// symbol table
char *symbol_name[MAX_SYMBOLS];  // points into string buffer that holds the name of the symbol
int  symbol_type[MAX_SYMBOLS];
int  symbol_address[MAX_SYMBOLS];
int  symbol_size[MAX_SYMBOLS];
int  symbol_count;
int  num_keywords;  // number of keywords in the symbol table

// backpatching
int  backpatch[MAX_BACKPATCH];
int  backpatch_type[MAX_BACKPATCH];
int  backpatch_count;

// parser data
int  global_variable_space, local_variable_space;
int  postfix_stack[1+2*MAX_POSTFIX];

// resulting binary code
int  emit_pos;
char emit_buffer[EMIT_BUFFER_SIZE];

void gen_write_dword_into_buffer(char *buffer, int pos, int dword)
{
    buffer[pos+0] = dword & 0xff;
    buffer[pos+1] = (dword & 0xff00) >> 8;
    buffer[pos+2] = (dword & 0xff0000) >> 16;
    buffer[pos+3] = (dword & 0xff000000) >> 24;
}

int gen_read_dword_from_buffer(char *buffer, int pos)
{
    int value;

    value = (buffer[pos+0] & 0xff)
        + ((buffer[pos+1] & 0xff) << 8)
        + ((buffer[pos+2] & 0xff) << 16)
        + ((buffer[pos+3] & 0xff) << 24)
        ;
    return value;
}

void gen_add_backpatch(int type, int offset)
{
    backpatch[backpatch_count] = offset;
    backpatch_type[backpatch_count] = type;
    ++backpatch_count;
}

void gen_backpatching(int string_base, int idata_base, int data_base)
{
    int i;

    i = 0;
    while (i < backpatch_count) {
        int offset, value;

        offset = backpatch[i];
        value = gen_read_dword_from_buffer(emit_buffer, offset);
        if (backpatch_type[i] == STRING)
            value += string_base;
        else if (backpatch_type[i] == GLOBAL)
            value += data_base;
        else if (backpatch_type[i] == IDATA)
            value += idata_base;
        else
            value = symbol_address[value] - (offset + 4);
        gen_write_dword_into_buffer(emit_buffer, offset, value);
        ++i;
    }
}

int lex_readchar(void)
{
    char buffer[4];
    int rc;

    rc = _sys_read(0, buffer, 1);
    if (rc)
        return buffer[0];

    return 0;
}

int lex_shift()
{
    int ch;

    // shift one char back in inout stream
    ch = current_char;
    if (previous_char != 0)
        ch = previous_char;

    previous_char = current_char;
    current_char = 0;
    return ch;
}

void lex_clear(void) { lex_shift(); previous_char = 0; } // no memory of previous char

int streq(char *s, char *t)
{
    if (!s || !t) {
        return 0;
    }

    while (*s && *s == *t) {
        ++s; ++t;
    }
    return *s == *t;
}

int parse_lookup_symbol(char *name)
{
    int symidx;

    // need to search from bottom to top in order to find most recently added symbol first
    symidx = symbol_count - 1;
    while (symidx >= 0) {
        if (streq(name, symbol_name[symidx]))
            return symidx;

        --symidx;
    }

    return 0;
}

int parse_add_string(char *s, int len)
{
    int stridx;
    stridx = string_table_size;

    while (len > 0) {
       string_table_buffer[string_table_size++] = *s++;
        --len;
    }
    return stridx;
}

int parse_add_symbol_name(char *s)
{
    int stridx;
    stridx = symbol_name_buffer_size;
    while ((symbol_name_buffer[symbol_name_buffer_size++] = *s++))
        ;
    return stridx;
}

// void trace(char *msg, int num);

int parse_add_internal_label(void)
{
    // same as parse_add_symbol just without name
    return symbol_count++;
}

int parse_add_symbol(char *name)
{
    int stridx, symidx;

    stridx = parse_add_symbol_name(name);
    symidx = parse_add_internal_label();

    symbol_name[symidx] = &symbol_name_buffer[stridx];

    return symidx;
}

int lex_optriple(int s0, int s1, int s2, int v0, int v1, int v2, int v3)
{
    int rc;

    current_char = lex_readchar();
    if (current_char == s1) {
        current_char = lex_readchar();
        if (current_char == s2) {
            lex_clear();
            rc = v2;
        }
        else {
            lex_shift();
            rc = v1;
        }
    }
    else if (current_char == s2) {
        lex_clear();
        rc = v3;
    }
    else {
        lex_shift();
        rc = v0;
    }

    if (rc < 0)
        rc = rc * -1;
    return rc;
}

int lex_opdouble(int s0, int s1, int s2, int v0, int v1, int v2)
{
    current_char = lex_readchar();
    if (current_char == s1) {
        lex_clear();
        return v1;
    }
    else if (v2 != 0 && current_char == s2) {
        lex_clear();
        return v2;
    }
    else {
        lex_shift();
        return v0;
    }
}

void lex_push_token(int tok)
{
    // remember token for reading it again
    pushed_token = tok;
}

int lex_next_token(void)
{
    if (pushed_token) {
        int tok;
        tok = pushed_token;
        pushed_token = 0;
        return tok;
    }

    while (1) {
        if (previous_char != 0)
            current_char = lex_shift();
        else
            current_char = lex_readchar();

        if (current_char == 0)
            return 0;

        if (current_char == ' ' || current_char == '\t' || current_char == '\r' || current_char == '\n') {
            if (current_char == 10)
                ++lineno;
            continue;
        }

        if (current_char == '>')      return lex_optriple('>', '>', '=', '>', RSH, RSHASSIGN, GE);
        else if (current_char == '<') return lex_optriple('<', '<', '=', '<', LSH, LSHASSIGN, LE);
        else if (current_char == '+') return lex_opdouble('+', '+', '=', '+', PLUSPLUS, PLUSASSIGN);
        else if (current_char == '-') return lex_opdouble('-', '-', '=', '-', MINUSMINUS, MINUSASSIGN);
        else if (current_char == '*') return lex_opdouble('*', '=', 0, '*', MULASSIGN, 0);
        else if (current_char == '=') return lex_opdouble('=', '=', 0, '=', EQ, 0);
        else if (current_char == '!') return lex_opdouble('!', '=', 0, '!', NEQ, 0);
        else if (current_char == '|') return lex_opdouble('|', '|', '=', '|', LOGOR, ORASSIGN);
        else if (current_char == '%') return lex_opdouble('%', '=', 0, '%', MODASSIGN, 0);
        else if (current_char == '^') return lex_opdouble('^', '=', 0, '^', XORASSIGN, 0);
        else if (current_char == '&') return lex_opdouble('&', '&', '=', '&', LOGAND, ANDASSIGN);
        else if (current_char == '#') {
            // single line comment
            while ((current_char = lex_readchar())) {
                if (current_char == 10) {
                    ++lineno;
                    break;
                }
            }
            lex_clear();
            continue;
        }
        else if (current_char == '/') {
            current_char = lex_readchar();

            if (current_char == '/') {
                // single line comment
                while ((current_char = lex_readchar())) {
                    if (current_char == 10) {
                        ++lineno;
                        break;
                    }
                }
                lex_clear();
                continue;
            }
            else if (current_char == '=') {
                lex_clear();
                return DIVASSIGN;
            }
            else if (current_char == '*') {
                // comment
                while ((current_char = lex_readchar())) {
                    if (current_char == 10)
                        ++lineno;

                    if (current_char != '*')
                        continue;

                    current_char = lex_readchar();
                    if (current_char == '/')
                        break;
                }
            }
            else {
                lex_shift();
                return '/';
            }
        }
        else if (current_char == '\"' || current_char == '\'' ) {
            int i, delim;
            // strings and characters

            delim = current_char;

            i = 0;
            current_char = lex_readchar();
            while (current_char != '\0' && current_char != delim) {
                if (current_char == '\\') {
                    // escape characters
                    current_char = lex_readchar();
                    if (current_char == 'x' || current_char == 'X') {
                        int c1, c2;
                        c1 = lex_readchar();
                        c2 = lex_readchar();

                        current_char =  (c1 & 0xf) + (9 * (c1 >> 6));
                        current_char = current_char << 4;
                        current_char +=  (c2 & 0xf) + (9 * (c2 >> 6));
                    }
                    else if (current_char == 'n')
                        current_char = 0x0a;
                    else if (current_char == 'r')
                        current_char = 0x0d;
                    else if (current_char == '0')
                        current_char = 0;
                    else if (current_char == '\'')
                        current_char = 0x27;
                    else if (current_char == 't')
                        current_char = 0x9;
                }
                token_text[i++] = current_char;
                current_char = lex_readchar();
            }
            token_text[i] = '\0';
            lex_clear();
            if (delim == '\'') {
                token_value = token_text[0];
                return NUMBER;
            }
            else {
                token_text_len = i+1;
                return STRING;
            }
        }
        else if ((current_char >= 'a' && current_char <= 'z') || (current_char >= 'A' && current_char <= 'Z') || (current_char == '_')) {
            int i;
            // keywords and identifiers

            i = 0;
            do {
                token_text[i++] = current_char;
                current_char = lex_readchar();
            } while ((current_char >= 'a' && current_char <= 'z') || (current_char >= 'A' && current_char <= 'Z') || (current_char == '_') || (current_char >= '0' && current_char <= '9'));
            token_text[i] = '\0';
            lex_shift();
            i = parse_lookup_symbol(token_text);
            if (i != 0 && i < num_keywords)
                return i;
            return IDENTIFIER;
        }
        else if (current_char >= '1' && current_char <= '9') {
            // decimal numbers
            token_value = 0;
            while (current_char >= '0' && current_char <= '9') {
                token_value *= 10;
                token_value += (current_char & 0xf) + (9 * (current_char >> 6));
                current_char = lex_readchar();
            }
            lex_shift();
            return NUMBER;
        }
        else if (current_char == '0') {
            current_char = lex_readchar();

            if (current_char == 'x') {
                // hex number
                current_char = lex_readchar();

                token_value = 0;
                while ((current_char >= '0' && current_char <= '9') || (current_char >= 'A' && current_char <= 'F') || (current_char >= 'a' && current_char <= 'f')) {
                    token_value <<= 4;
                    token_value += (current_char & 0xf) + (9 * (current_char >> 6));
                    current_char = lex_readchar();
                }
            }
            else {
                // octal number
                token_value = 0;
                while ((current_char >= '0' && current_char <= '7')) {
                    token_value <<= 3;
                    token_value += (current_char & 0xf) + (9 * (current_char >> 6));
                    current_char = lex_readchar();
                }
            }
            lex_shift();
            return NUMBER;
        }
        else {
            // any other individual character is a symbol
            int ch;

            ch = current_char;
            lex_clear();
            return ch;
        }
    }
}


int mystrlen(char *p)
{
    int n;
    n = 0;
    while (*p++)
        ++n;

    return n;
}

void reverse(char *buffer)
{
    int n, i;
    char temp;

    n = mystrlen(buffer);
    i = 0;

    while (--n > i) {
        temp = buffer[i];
        buffer[i] = buffer[n];
        buffer[n] = temp;
        ++i;
    }
}

void myitoa(char *buffer, int n, int value)
{
    int i, x;

    i = 0;

    if (value == 0) {
        buffer[i++] = '0';
    }
    else {
        while (value > 0) {
            x = value % 10;
            buffer[i++] = '0' + x;
            value /= 10;
        }
    }

    buffer[i] = '\0';
    reverse(buffer);
}

void parse_error(int err)
{
    char buffer[10];

    _sys_write(2, "error: ", 7);
    myitoa(buffer, 8, err);
    _sys_write(2, buffer, mystrlen(buffer));
    _sys_write(2, " in line ", 9);
    myitoa(buffer, 8, lineno);
    _sys_write(2, buffer, mystrlen(buffer));
    _sys_write(2, "\n", 1);

    _sys_exit(1);
}

// void trace(char *msg, int num)
// {
//     char buffer[10];
//
//     _sys_write(2, msg, mystrlen(msg));
//     myitoa(buffer, 8, num);
//     buffer[8] = 0xa;
//     _sys_write(2, buffer, 9);
// }

int gen_emitbyte(int byte)
{
    emit_buffer[emit_pos++] = byte & 0xff;
    return 1;
}

int gen_emitbytes(int count, int b1, int b2, int b3, int b4)
{
    if (count >= 1) gen_emitbyte(b1);
    if (count >= 2) gen_emitbyte(b2);
    if (count >= 3) gen_emitbyte(b3);
    if (count >= 4) gen_emitbyte(b4);
    return count;
}

int gen_emitdword(int dword)
{
    gen_write_dword_into_buffer(emit_buffer, emit_pos, dword);
    emit_pos += 4;
    return 4;
}

int parse_args(int tok)
{
    if (tok != '(')
        parse_error(E_FCT_MISSING_PARANTHESIS);

    tok = lex_next_token();

    if (tok == VOID) {
        tok = lex_next_token();
        if (tok != ')')
            parse_error(E_FCT_MISSING_PARANTHESIS);
    }
    else {
        int type, address, symidx;

        address = 8;

        while (tok == INT || tok == CHAR || tok == VOID) {
           type = tok;
           tok = lex_next_token();
           if (tok != IDENTIFIER) {
                if (tok != '*')
                    parse_error(E_MISSING_IDENTIFIER);

                type |= POINTER;
                tok = lex_next_token();
            }

            if (tok != IDENTIFIER)
                parse_error(E_MISSING_IDENTIFIER);

            symidx = parse_add_symbol(token_text);
            symbol_address[symidx] = address;
            address += 4;

            tok = lex_next_token();

            if (tok == '[') {
                tok = lex_next_token();

                type |= ARRAY;

                if (tok != ']')
                    parse_error(E_WRONG_ARRAY_DEF);
                tok = lex_next_token();
            }

            symbol_type[symidx] = PARAM | type;
            symbol_size[symidx] = 4;

            if (tok == ',') {
                tok = lex_next_token();
                continue;
            }
            else if (tok == ')')
                return lex_next_token();
            else
                parse_error(E_PARAM_DEF);
        }
    }
    return lex_next_token();
}

int parse_calcexpr(int tok, int is_const, int *retval, int delim);

int parse_enum_decl(int tok)
{
    int enumval;

    enumval = 0;

    if (tok == ENUM) {
        tok = lex_next_token();
        if (tok == IDENTIFIER)
            tok = lex_next_token();

        if (tok != '{')
            parse_error(E_ENUM_MISSING_OPENING_BRACES);

        tok = lex_next_token();
        while (tok != '}') {
            int symidx;

            if (tok != IDENTIFIER)
                parse_error(E_ENUM_MISSING_IDENTIFIER);

            symidx = parse_add_symbol(token_text);

            tok = lex_next_token();
            if (tok == '=') {
                int number;
                tok = lex_next_token();
                tok = parse_calcexpr(tok, 1, &number, 0);
                enumval = number;
            }

            symbol_address[symidx] = enumval;
            symbol_type[symidx]    = ENUM;
            symbol_size[symidx]    = 4;

            if (tok == ',') {
                ++enumval;
                tok = lex_next_token();
            }
        }
        tok = lex_next_token();
        if (tok != ';')
            parse_error(E_MISSING_SEMICOLON);
    }
    return lex_next_token();
}

void mystrcpy(char *dst, char *src)
{
    while ((*dst++ = *src++))
        ;
}

int parse_vardecl2(int tok, int type)
{
    int symidx;
    char name[256];

    while (1) {
        int array_count, size, padded_size;

        mystrcpy(name, token_text);

        array_count = 0;

        if (tok == '[') {
            // we use a trick here to use the expression parser
            // instead of [] we parse (]
            tok = parse_calcexpr('(', 1, &array_count, ']');

            type |= ARRAY;
        }

        size = 1;
        if (type & POINTER || (type & 0xff) == INT)
            size = 4;
        if (array_count > 1)
            size *= array_count;
        padded_size = size;
        while (padded_size % 4 != 0)
            ++padded_size;

        if (type & LOCAL) {
            symidx = parse_add_symbol(name);
            local_variable_space += padded_size;
            symbol_address[symidx] = local_variable_space;
        }
        else {
            // global
            symidx = parse_lookup_symbol(name);
            if (symidx == 0)
                symidx = parse_add_symbol(name);

            symbol_address[symidx] = global_variable_space;
            global_variable_space += padded_size;
        }

        symbol_size[symidx] = size;
        symbol_type[symidx] = type;

        if (tok == ',') {
            type &= ~(POINTER|ARRAY);  // no pointer and no array flag

            tok = lex_next_token();
            if (tok != IDENTIFIER) {
                if (tok != '*')
                    parse_error(E_MISSING_IDENTIFIER);

                type |= POINTER;
                tok = lex_next_token();
            }
            if (tok != IDENTIFIER)
                parse_error(E_MISSING_IDENTIFIER);

            tok = lex_next_token();
            continue;
        }
        else if (tok != ';')
            parse_error(E_MISSING_SEMICOLON);

        return lex_next_token();
    }
}

int parse_vardecl(int tok)
{
    int type;

    if (tok == ENUM)
        return parse_enum_decl(tok);

    type = tok;
    if (tok != VOID && tok != INT && tok != CHAR)
        parse_error(E_MISSING_TYPE);

    tok = lex_next_token();
    if (tok != IDENTIFIER) {
        if (tok != '*')
            parse_error(E_MISSING_IDENTIFIER);

        type |= POINTER;
        tok = lex_next_token();
    }

    return parse_vardecl2(lex_next_token(), type | LOCAL);
}

int stack_empty(int *stack)
{
    return stack[0] == 0;
}

void stack_push(int *stack, int value)
{
    stack[++stack[0]] = value;
}

int stack_top(int *stack, int pop)
{
    int retval;

    retval = -1;
    if (!stack_empty(stack)) {
        retval = stack[stack[0]];

        if (pop)
            stack[0]--;
    }

    return retval;
}

int stack_pop(int *stack) { return stack_top(stack, 1); }

int parse_precedence(int op, int *assoc)
{
    *assoc = 0; // left to right

    if (op == '=' || op == MULASSIGN || op == MINUSASSIGN
        || op == PLUSASSIGN || op == DIVASSIGN || op == MODASSIGN
        || op == LSHASSIGN || op == RSHASSIGN || op == ANDASSIGN || op == XORASSIGN || op == ORASSIGN)
    {
        *assoc = 1; // right to left
        return 24;
    }
    else if (op == ARRAY_SUBSCRIPT || op == FUNCTION || op == PLUSPLUS || op == MINUSMINUS) return 11;
    else if (op == PLUSPLUS || op == MINUSMINUS) return 11;
    else if (op == ',') {
        return 25;
    }
    else if (op == (UNARY | '-') || op == (UNARY | '+') || op == '~' || op == '!'
        || op == (UNARY | '!') || op == '~' || op == (UNARY |'~') || op == (UNARY | '*') || op == (UNARY | '&')
        || /* prefix ++/-- */op == (UNARY | MINUSMINUS) || op == (UNARY | PLUSPLUS)
        )
    {
        *assoc = 1; // right to left
        return 12;
    }
    else if (op == '*' || op == '/' || op == '%') return 13;
    else if (op == '+' || op == '-') return 14;
    else if (op == LSH || op == RSH) return 15;
    else if (op == '<' || op == LE || op == '>' || op == GE) return 16;
    else if (op == EQ || op == NEQ) return 17;
    else if (op == '&') return 18;
    else if (op == '^') return 19;
    else if (op == '|') return 20;
    else if (op == LOGAND) return 21;
    else if (op == LOGOR) return 22;

    return 1000;
}

int parse_add_elem(int *expr_table, int type, int value)
{
    int nextfree;

    nextfree = expr_table[1];
    expr_table[4*nextfree+0] = type;
    expr_table[4*nextfree+1] = value;
    expr_table[4*nextfree+2] = 0; // child1
    expr_table[4*nextfree+3] = 0; // child2
    expr_table[1] = nextfree+1;

    if (expr_table[0] == 0)
        expr_table[0] = nextfree;
    return nextfree;
}

void parse_reduce(int *expr_table, int *operator_stack, int *arg_stack)
{
    int idx, op, child1, child2;

    op = stack_pop(operator_stack);

    child2 = 0;
    if (!(op & UNARY) && op != MINUSMINUS && op != PLUSPLUS) {
        child2 = stack_pop(arg_stack);
    }
    child1 = stack_pop(arg_stack);
    if (child1 == -1 && op & FUNCTION) {
        // -1 indicates the end of parameter list: ignore
        child1 = stack_pop(arg_stack);
    }

    if (op == ARRAY_SUBSCRIPT) {
        int idx2;

        idx2 = parse_add_elem(expr_table, OPERATOR, '+');
        expr_table[4*idx2+2] = child1;
        expr_table[4*idx2+3] = child2;

        idx = parse_add_elem(expr_table, OPERATOR, UNARY | '*');
        expr_table[4*idx+2] = idx2;
    }
    else {
        idx = parse_add_elem(expr_table, OPERATOR, op);
        expr_table[4*idx+2] = child1;
        expr_table[4*idx+3] = child2;
    }

    expr_table[0] = idx;
    stack_push(arg_stack, idx);
}

int parse_check_unary(int c, int last)
{
    if (c == '!' || c == '~')
        return UNARY;

    if (c == '-' || c == '+' || c == '*' || c == '&' || c == PLUSPLUS || c == MINUSMINUS) {
        if (last == 0)
            return UNARY;

        if (last == NUMBER || last == IDENTIFIER || last == ')' || last == ']' || last == MINUSMINUS || last == PLUSPLUS)
            return 0;

        return UNARY;
    }

    return 0;
}

void parse_simplify_expression(int *expr_table, int root)
{
    int op;

    if (root != 0) {
        if (expr_table[4*root+0] == ENUM) {
            int symidx;

            symidx = expr_table[4*root+1];
            expr_table[4*root+0] = NUMBER;
            expr_table[4*root+1] = symbol_address[symidx];
        }

        if (expr_table[4*root+0] != OPERATOR)
            return; // is already a constant or variable

        op = expr_table[4*root+1];

        if (op & UNARY) {
            int child;

            parse_simplify_expression(expr_table, expr_table[4*root+2]);
            child = expr_table[4*root+2];

            if (op == (UNARY | '-')) {
                if (expr_table[4*child+0] == NUMBER) {
                    expr_table[4*root+1] = -expr_table[4*child+1];
                    expr_table[4*root+0] = NUMBER;
                }
            }
        }
        else /* binary */ {
            int child1, child2;

            child1 = expr_table[4*root+2];
            parse_simplify_expression(expr_table, child1); // expr_table may have changed during simplification
            child1 = expr_table[4*root+2];
            child2 = expr_table[4*root+3];
            parse_simplify_expression(expr_table, child2); // expr_table may have changed during simplification
            child2 = expr_table[4*root+3];

            if (expr_table[4*child1+0] == NUMBER && expr_table[4*child2+0] == NUMBER) {
                if (op == '+') {
                    expr_table[4*root+1] = expr_table[4*child1+1] + expr_table[4*child2+1];
                    expr_table[4*root+0] = NUMBER;
                }
                else if (op == '*') {
                    expr_table[4*root+1] = expr_table[4*child1+1] * expr_table[4*child2+1];
                    expr_table[4*root+0] = NUMBER;
                }
                else if (op == '-') {
                    expr_table[4*root+1] = expr_table[4*child1+1] - expr_table[4*child2+1];
                    expr_table[4*root+0] = NUMBER;
                }
                else if (op == '/') {
                    expr_table[4*root+1] = expr_table[4*child1+1] / expr_table[4*child2+1];
                    expr_table[4*root+0] = NUMBER;
                }
                else if (op == NEQ) {
                    expr_table[4*root+1] = expr_table[4*child1+1] != expr_table[4*child2+1];
                    expr_table[4*root+0] = NUMBER;
                }
            }
        }
    }
}

enum Flags_genexpr {
    ADDR_ONLY  = 0x100,    // result must be the address (don't load value)
};

int parse_deref(int type)
{
    if (type & ARRAY)
        return type & ~ARRAY;

    if (type & POINTER)
        return type & ~POINTER;

    return type;
}

int parse_get_size(int type)
{
    if (type & ARRAY)
        return 4;
    if (type & POINTER)
        return 4;
    if ((type & 0xff) == INT)
        return 4;

    return 1;
}

void gen_expr(int *expr_table, int root, int *comma_count, int flags, int *expr_type)
{
    int type, op, dummy;

    type = expr_table[4*root+0];
    op = expr_table[4*root+1];

    if (root == 0)
        return;

    if (type == OPERATOR) {
        if (!(op & UNARY) && op == ',') {
            // right to left
            ++*comma_count;

            gen_expr(expr_table, expr_table[4*root+3], comma_count, flags & ~ADDR_ONLY, &dummy);
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags & ~ADDR_ONLY, &dummy);
        }
        else if (op == (UNARY | '*')) {
            // dereference operator

            if (!(flags & ADDR_ONLY)) {
                gen_expr(expr_table, expr_table[4*root+2], comma_count, flags & ~ADDR_ONLY, expr_type);
                *expr_type = parse_deref(*expr_type);

                gen_emitbyte(0x58);                  // pop eax
                if ((*expr_type & POINTER) || (*expr_type & 0xff) == INT) {
                    gen_emitbytes(2, 0xff, 0x30, 0, 0);  // push   DWORD PTR [eax]
                }
                else {
                    gen_emitbytes(3, 0x0f, 0xb6, 0x00, 0);  // movzx  eax,BYTE PTR [eax]
                    gen_emitbyte(0x50);                  // push eax
                }
            }
            else {
                gen_expr(expr_table, expr_table[4*root+2], comma_count, flags & ~ADDR_ONLY, expr_type);
                *expr_type = parse_deref(*expr_type);
            }
        }
        else if (op == (UNARY | '-')) {
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags & ~ADDR_ONLY, expr_type);
            gen_emitbyte(0x58);                     // pop eax
            gen_emitbytes(3, 0x83, 0xf0, 0xff, 0);  // xor eax,0xffffffff
            gen_emitbyte(0x40);                     // inc eax
            gen_emitbyte(0x50);                     // push eax
        }
        else if (op == (UNARY | '&')) {
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags | ADDR_ONLY, expr_type);
            *expr_type |= POINTER;
        }
        else if (op == (UNARY | '!')) {
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags & ~ADDR_ONLY, expr_type);
            gen_emitbyte(0x5b);                     // pop ebx
            gen_emitbytes(2, 0x31, 0xc0, 0, 0);     // xor eax,eax
            gen_emitbytes(2, 0x09, 0xdb, 0, 0);     // or ebx,ebx
            gen_emitbytes(3, 0x0f, 0x94, 0xc0, 0);  // sete  al
            gen_emitbyte(0x50);                     // push eax
            *expr_type = INT;
        }
        else if (op == (UNARY | '~') || op == '~') {
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags & ~ADDR_ONLY, expr_type);
            gen_emitbyte(0x5b);                     // pop ebx
            gen_emitbytes(2, 0xf7, 0xd3, 0, 0);     // not ebx
            gen_emitbyte(0x53);                     // push ebx
        }
        else if (op & FUNCTION) {
            int symidx, param_count;
            symidx = expr_table[4*root+2];
            *expr_type = expr_table[4*symidx+1];
            symidx = expr_table[4*symidx+1];

            param_count = 0;
            if (expr_table[4*root+3] != 0) {
                param_count = 1;
                gen_expr(expr_table, expr_table[4*root+3], &param_count, flags & ~ADDR_ONLY, &dummy);
            }

            gen_emitbyte(0xe8);  // call
            if (symbol_address[symidx] != 0) {
                // we know the target address already
                gen_emitdword(symbol_address[symidx] - (emit_pos + 4));
            }
            else {
                gen_add_backpatch(0, emit_pos);
                gen_emitdword(symidx);
            }

            if (param_count > 0) {
                gen_emitbytes(2, 0x81, 0xc4, 0, 0); // add esp, n
                gen_emitdword(param_count*4);
            }
            gen_emitbyte(0x50);   // push eax
        }
        else if (op == '=') {
            int type_left;

            gen_expr(expr_table, expr_table[4*root+3], comma_count, flags & ~ADDR_ONLY, expr_type);
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags | ADDR_ONLY, &type_left);
            gen_emitbyte(0x5b);                  // pop ebx
            gen_emitbyte(0x58);                  // pop eax
            if ((type_left & (ARRAY|POINTER)) || (type_left & 0xff) == INT)
                gen_emitbytes(2, 0x89, 0x03, 0, 0);  // mov DWORD PTR [ebx],eax
            else
                gen_emitbytes(2, 0x88, 0x03, 0, 0);  // mov BYTE PTR [ebx],al
            gen_emitbyte(0x50);                  // push eax
        }
        else if (op == NEQ || op == EQ || op == '<' || op == '>' || op == LE || op == GE) {
            gen_expr(expr_table, expr_table[4*root+3], comma_count, flags & ~ADDR_ONLY, &dummy);
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags & ~ADDR_ONLY, expr_type);
            gen_emitbyte(0x58);                    // pop eax
            gen_emitbyte(0x5b);                    // pop ebx
            gen_emitbytes(2, 0x31, 0xc9, 0, 0);    // xor    ecx,ecx
            gen_emitbytes(2, 0x39, 0xd8, 0, 0);    // cmp    eax,ebx
            if (op == NEQ) gen_emitbytes(3, 0x0f, 0x95, 0xc1, 0); // setne  cl
            if (op == EQ) gen_emitbytes(3, 0x0f, 0x94, 0xc1, 0);  // sete  cl
            if (op == '<') gen_emitbytes(3, 0x0f, 0x9c, 0xc1, 0); // setl  cl
            if (op == '>') gen_emitbytes(3, 0x0f, 0x9f, 0xc1, 0); // setg  cl
            if (op == LE) gen_emitbytes(3, 0x0f, 0x9e, 0xc1, 0);  // setle  cl
            if (op == GE) gen_emitbytes(3, 0x0f, 0x9d, 0xc1, 0);  // setge  cl

            gen_emitbyte(0x51);                    // push ecx
        }
        else if (op == '*' || op == '%' || op == '/') {
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags & ~ADDR_ONLY, &dummy);
            gen_expr(expr_table, expr_table[4*root+3], comma_count, flags & ~ADDR_ONLY, expr_type);
            gen_emitbyte(0x59);                          // pop ecx
            gen_emitbyte(0x58);                          // pop eax

            if (op == '*')
                gen_emitbytes(2, 0xf7, 0xe9, 0, 0);     // imul   ecx
            else {
                gen_emitbytes(2, 0x31, 0xd2, 0, 0);     // xor    edx,edx
                gen_emitbytes(2, 0xf7, 0xf9, 0, 0);     // idiv   ecx
            }
            if (op == '%')
                gen_emitbyte(0x52);     // push edx
            else
                gen_emitbyte(0x50);     // push eax
        }
        else if (op == '+' || op == '-' || op == '^' || op == '|' || op == '&' || op == LSH || op == RSH) {
            int type_left, type_right, aithmetic_done;
            int left_is_pointer, right_is_pointer;

            aithmetic_done = 0;
            left_is_pointer = 0;
            right_is_pointer = 0;

            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags & ~ADDR_ONLY, &type_left);
            gen_expr(expr_table, expr_table[4*root+3], comma_count, flags & ~ADDR_ONLY, &type_right);
            gen_emitbyte(0x59);                                     //        pop    ecx

            if (op == '+' || op == '-') {
                left_is_pointer  = (type_left & ARRAY) || (type_left & POINTER);
                right_is_pointer = (type_right & ARRAY) || (type_right & POINTER);

                if (left_is_pointer && !right_is_pointer) {
                    aithmetic_done = 1;
                    *expr_type = type_left;

                    if (op == '-') {
                        // ecx = -ecx
                        gen_emitbytes(3, 0x83, 0xf1, 0xff, 0);  // xor ecx,0xffffffff
                        gen_emitbyte(0x41);                     // inc ecx
                    }
                    gen_emitbyte(0x5b);                        //                pop ebx
                    if ((type_left & (ARRAY | POINTER)) && parse_get_size(parse_deref(type_left)) == 4)
                        gen_emitbytes(3, 0x8d, 0x1c, 0x8b, 0); //                lea    ebx,DWORD PTR [ebx+ecx*4]
                    else
                        gen_emitbytes(3, 0x8d, 0x1c, 0x0b, 0); //                lea    ebx,DWORD PTR [ebx+ecx*1]

                    gen_emitbyte(0x53);                        //                push   ebx
                }
                else if (!left_is_pointer && right_is_pointer) {
                    aithmetic_done = 1;
                    *expr_type = type_right;

                    gen_emitbyte(0x5b);                        //                pop ebx
                    if (op == '-') {
                        // ebx = -ebx
                        gen_emitbytes(3, 0x83, 0xf3, 0xff, 0);  // xor ebx,0xffffffff
                        gen_emitbyte(0x43);                     // inc ebx
                    }

                    if ((type_right & (ARRAY | POINTER)) && parse_get_size(parse_deref(type_right)) == 4)
                        gen_emitbytes(3, 0x8d, 0x1c, 0x99, 0); //                lea    ebx,DWORD PTR [ecx+ebx*4]
                    else
                        gen_emitbytes(3, 0x8d, 0x1c, 0x19, 0); //                lea    ebx,DWORD PTR [ecx+ebx*1]

                    gen_emitbyte(0x53);                        //                push   ebx
                }
            }

            if (!aithmetic_done) {
                *expr_type = INT;

                if (op == '+') gen_emitbytes(3, 0x01, 0x0c, 0x24, 0);   //  add    DWORD PTR [esp],ecx
                if (op == '|') gen_emitbytes(3, 0x09, 0x0c, 0x24, 0);   //  or     DWORD PTR [esp,ecx
                if (op == '-') gen_emitbytes(3, 0x29, 0x0c, 0x24, 0);   //  sub    DWORD PTR [esp],ecx
                if (op == '&') gen_emitbytes(3, 0x21, 0x0c, 0x24, 0);   //  and    DWORD PTR [esp],ecx
                if (op == '^') gen_emitbytes(3, 0x31, 0x0c, 0x24, 0);   //  xor    DWORD PTR [esp],ecx
                if (op == LSH) gen_emitbytes(3, 0xd3, 0x24, 0x24, 0);   //  shl    DWORD PTR [esp],cl
                if (op == RSH) gen_emitbytes(3, 0xd3, 0x2c, 0x24, 0);   //  shr    DWORD PTR [esp],cl

                if (left_is_pointer && right_is_pointer) {
                    if ((type_left & (ARRAY | POINTER)) && parse_get_size(parse_deref(type_left)) == 4) {
                        // we are subtracting pointer to int: result is 4 times too high
                        gen_emitbytes(4, 0xc1, 0x2c, 0x24,  0x02);  //  shr    DWORD PTR [esp],0x2
                    }
                }
            }
        }
        else if (op == DIVASSIGN || op == MODASSIGN || op == MULASSIGN) {
            gen_expr(expr_table, expr_table[4*root+3], comma_count, flags & ~ADDR_ONLY, expr_type);
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags | ADDR_ONLY, &dummy);
            gen_emitbyte(0x5b);                         // pop ebx
            gen_emitbytes(2, 0x8b, 0x03, 0, 0);         // mov eax,DWORD PTR [ebx]

            if (op == MULASSIGN)
                gen_emitbytes(3, 0xf7, 0x2c, 0x24, 0);  // imul DWORD PTR [esp]
            else {
                gen_emitbytes(2, 0x31, 0xd2, 0, 0);     // xor    edx,edx
                gen_emitbytes(3, 0xf7, 0x3c, 0x24, 0);  // idiv DWORD PTR [esp]
            }
            if (op == MODASSIGN) {
                gen_emitbytes(2, 0x89, 0x13, 0, 0);     // mov DWORD PTR [ebx],edx
                gen_emitbytes(3, 0x89, 0x14, 0x24, 0);  // mov DWORD PTR [esp],edx
            }
            else {
                gen_emitbytes(2, 0x89, 0x03, 0, 0);     // mov DWORD PTR [ebx],eax
                gen_emitbytes(3, 0x89, 0x04, 0x24, 0);  // mov DWORD PTR [esp],eax
            }
        }
        else if (op == PLUSASSIGN || op == MINUSASSIGN || op == LSHASSIGN || op == RSHASSIGN || op == ANDASSIGN || op == XORASSIGN || op == ORASSIGN) {
            int type_left;

            gen_expr(expr_table, expr_table[4*root+3], comma_count, flags & ~ADDR_ONLY, expr_type);
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags | ADDR_ONLY, &type_left);
            gen_emitbyte(0x5b);                  // pop ebx
            gen_emitbyte(0x59);                  // pop ecx

            if ((op == PLUSASSIGN || op == MINUSASSIGN) && (type_left & (ARRAY | POINTER)) && parse_get_size(parse_deref(type_left)) == 4) {
                // pointer arithmetic in += and -=
                gen_emitbytes(3, 0xc1, 0xe1, 0x02, 0);                   //  shl    ecx,0x2
            }

            if (op == PLUSASSIGN)  gen_emitbytes(2, 0x01, 0x0b, 0, 0);   //  add    DWORD PTR [ebx],ecx
            if (op == ORASSIGN)    gen_emitbytes(2, 0x09, 0x0b, 0, 0);   //  or     DWORD PTR [ebx],ecx
            if (op == MINUSASSIGN) gen_emitbytes(2, 0x29, 0x0b, 0, 0);   //  sub    DWORD PTR [ebx],ecx
            if (op == ANDASSIGN)   gen_emitbytes(2, 0x21, 0x0b, 0, 0);   //  and    DWORD PTR [ebx],ecx
            if (op == XORASSIGN)   gen_emitbytes(2, 0x31, 0x0b, 0, 0);   //  xor    DWORD PTR [ebx],ecx
            if (op == LSHASSIGN)   gen_emitbytes(2, 0xd3, 0x23, 0, 0);   //  shl    DWORD PTR [ebx],cl
            if (op == RSHASSIGN)   gen_emitbytes(2, 0xd3, 0x2b, 0, 0);   //  shr    DWORD PTR [ebx],cl

            gen_emitbytes(2, 0xff, 0x33, 0, 0);  // push   DWORD PTR [ebx]
        }
        else if ((op & 0xff) == MINUSMINUS || (op & 0xff) == PLUSPLUS) {
            // -- or ++
            int b3, type_left;
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags | ADDR_ONLY, &type_left);
            gen_emitbyte(0x58);                     // pop eax

            *expr_type = type_left;

            if ((op & 0xff) == MINUSMINUS) {
                if ((type_left & (ARRAY | POINTER)) && parse_get_size(parse_deref(type_left)) == 4)
                    b3 = 0xfc;   // -4
                else
                    b3 = 0xff;   // -1
            }
            else {
                if ((type_left & (ARRAY | POINTER)) && parse_get_size(parse_deref(type_left)) == 4)
                    b3 = 4;     // +4
                else
                    b3 = 1;     // +1
            }

            if (op & UNARY) {
                // prefix
                gen_emitbytes(3, 0x83, 0x00, b3, 0); // add  DWORD PTR [eax],b3
            }
            else {
                // postfix
                // remember to inc (or dec) later
                int temp;

                local_variable_space += 4;
                temp = -local_variable_space;

                stack_push(postfix_stack, b3);
                stack_push(postfix_stack, temp);

                gen_emitbytes(2, 0x89, 0x85, 0, 0);   // mov    DWORD PTR [ebp+X],eax
                gen_emitdword(temp);
            }
            gen_emitbytes(2, 0xff, 0x30, 0, 0);  // push DWORD PTR [eax]
        }
        else if (op == LOGAND || op == LOGOR) {
            int end_label;

            *expr_type = INT;

            end_label = parse_add_internal_label();
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags & ~ADDR_ONLY, &dummy);
            gen_emitbyte(0x58);                     // pop eax
            gen_emitbytes(2, 0x09, 0xc0, 0, 0);     // or eax,eax
            if (op == LOGAND) gen_emitbytes(2, 0x0f, 0x84, 0, 0);  /* jz */  else gen_emitbytes(2, 0x0f, 0x85, 0, 0);  /* jnz */
            gen_add_backpatch(0, emit_pos);
            gen_emitdword(end_label);

            gen_expr(expr_table, expr_table[4*root+3], comma_count, flags & ~ADDR_ONLY, &dummy);
            gen_emitbyte(0x58);                     // pop eax

            symbol_address[end_label] = emit_pos;  // jump to this position on end;

            gen_emitbytes(2, 0x31, 0xc9, 0, 0);    //  xor    ecx,ecx
            gen_emitbytes(2, 0x09, 0xc0, 0, 0);    //  or     eax,eax
            gen_emitbytes(3, 0x0f, 0x95, 0xc1, 0); //  setne  cl
            gen_emitbyte(0x51);                    //  push   ecx
        }
        else
            gen_expr(expr_table, expr_table[4*root+2], comma_count, flags & ~ADDR_ONLY, &dummy);
    }
    else if (type == NUMBER || type == STRING) {
        // not an operator
        gen_emitbyte(0x68);  // push
        *expr_type = INT;
        if (type != NUMBER) {
            gen_add_backpatch(STRING, emit_pos);
            *expr_type = POINTER | CHAR;
        }

        gen_emitdword(expr_table[4*root+1]);
    }
    else {
        // variable
        int symidx, address;

        symidx = expr_table[4*root+1];
        address = symbol_address[symidx];

        if (!(symbol_type[symidx] & PARAM) && symbol_type[symidx] & ARRAY)
            flags |= ADDR_ONLY;  // array name is the address if it's not a parameter

        *expr_type = symbol_type[symidx];
        if (type & LOCAL || type & PARAM) {
            if (type & LOCAL)
                address = -address;

            if (flags & ADDR_ONLY) {
                gen_emitbytes(2, 0x8d, 0x85, 0, 0);   // lea eax, dword ptr [ebp+X]
                gen_emitdword(address);
                gen_emitbyte(0x50);                  // push eax
            }
            else if (symbol_size[symidx] >= 4) {
                gen_emitbytes(2, 0xff, 0xb5, 0, 0);  // push dword ptr [ebp+X]
                gen_emitdword(address);
            }
            else {
                gen_emitbytes(3, 0x0f, 0xb6, 0x85, 0);  // movzx eax, byte ptr[ebp+X]
                gen_emitdword(address);
                gen_emitbyte(0x50);                     // push eax
            }
        }
        else {
            // global variable
            if (flags & ADDR_ONLY) {
                gen_emitbyte(0x68);  // push absolute
                gen_add_backpatch(GLOBAL, emit_pos);
                gen_emitdword(address);
            }
            else if (symbol_size[symidx] >= 4) {
                gen_emitbytes(2, 0xff, 0x35, 0, 0);  // push ds:[X]
                gen_add_backpatch(GLOBAL, emit_pos);
                gen_emitdword(address);
            }
            else {
                gen_emitbytes(3, 0x0f, 0xb6, 0x05, 0);  // movzx  eax,BYTE PTR ds:X
                gen_add_backpatch(GLOBAL, emit_pos);
                gen_emitdword(address);
                gen_emitbyte(0x50);                  // push eax
            }
        }
    }
}

int parse_calcexpr(int tok, int is_const, int *retval, int delim)
{
    int operator_stack[EXPR_STACK_SIZE];
    int arg_stack[EXPR_STACK_SIZE];
    int expr_table[4*(1+EXPR_STACK_SIZE)];  // 4 integers per entry 0:type, 1:value, 2:child1, 3:child2
    int last_sym, assoc, dummy;

    last_sym = 0;
    arg_stack[0] = 0;           // stack empty
    operator_stack[0] = 0;      // stack_empty

    expr_table[0] = 0;  // root
    expr_table[1] = 1;  // nextfree
    expr_table[2] = 0;  // unused
    expr_table[3] = 0;  // unused

    while (1) {
        if (tok == NUMBER || tok == STRING) {
            if (tok == STRING)
                token_value = parse_add_string(token_text, token_text_len);
            stack_push(arg_stack, parse_add_elem(expr_table, tok, token_value));
            last_sym = tok;
            tok = lex_next_token();
        }
        else if (tok == IDENTIFIER) {
            int symidx;

            symidx = parse_lookup_symbol(token_text);
            if (symidx < num_keywords)
                parse_error(E_UNDEFINED_IDENTIFIER);

            stack_push(arg_stack, parse_add_elem(expr_table, symbol_type[symidx], symidx));
            if (symbol_type[symidx] & FUNCTION) {
                stack_push(operator_stack, FUNCTION);
                stack_push(arg_stack, -1);
            }

            last_sym = tok;
            tok = lex_next_token();
        }
        else {
            if (tok != ',' && tok != ')' && tok != ']' && tok != ';' && tok != '}' && tok >= num_keywords && stack_empty(operator_stack)) {
                // shift
                if (tok == '[')
                    stack_push(operator_stack, ARRAY_SUBSCRIPT);
                stack_push(operator_stack, tok | parse_check_unary(tok, last_sym));
                last_sym = tok | parse_check_unary(tok, last_sym);
                tok = lex_next_token();
            }
            else if (!stack_empty(operator_stack)) {
                int prec_top, prec_cur;

                if (tok == ')' || tok == ']') {
                    while (!stack_empty(operator_stack) && '(' != stack_top(operator_stack, 0) && '[' != stack_top(operator_stack, 0))
                        parse_reduce(expr_table, operator_stack, arg_stack);

                    if (stack_empty(operator_stack)) {
                        parse_error(E_BAD_EXPRESSION);
                        return 0;
                    }

                    if (arg_stack[0] == 1 && operator_stack[0] == 1 && delim == tok) {
                        // we have reached the end of the expression: args reduced to 1 no ops
                        tok = lex_next_token();
                        stack_pop(operator_stack);
                        break;
                    }

                    stack_pop(operator_stack);
                    if (FUNCTION == stack_top(operator_stack, 0) && -1 == stack_top(arg_stack, 0)) {
                        // function without parameters
                        stack_pop(operator_stack);
                        stack_push(operator_stack, FUNCTION | UNARY);
                        parse_reduce(expr_table, operator_stack, arg_stack);
                    }

                    last_sym = tok;
                    tok = lex_next_token();
                    if (tok == NUMBER || tok == STRING || tok == IDENTIFIER || tok == ';' || tok == '{' || tok == '(' || tok == '[' || tok < num_keywords || parse_check_unary(tok, last_sym)) {
                        // ')' must follow a binary operator
                        break;
                    }
                    continue;
                }

                prec_top = parse_precedence(stack_top(operator_stack, 0), &assoc);
                prec_cur = parse_precedence(tok | parse_check_unary(tok, last_sym), &dummy);

                if (tok == '(' || tok == '[' || prec_top > prec_cur || (prec_top == prec_cur && assoc == 1)) {
                    // shift
                     if (tok == '[')
                        stack_push(operator_stack, ARRAY_SUBSCRIPT);

                    stack_push(operator_stack, tok | parse_check_unary(tok, last_sym));
                    last_sym = tok | parse_check_unary(tok, last_sym);
                    tok = lex_next_token();
                }
                else {
                    // reduce
                    parse_reduce(expr_table, operator_stack, arg_stack);
                }
            }
            else
                break;
        }
    }

    while (!stack_empty(operator_stack)) {
        parse_reduce(expr_table, operator_stack, arg_stack);
    }

    parse_simplify_expression(expr_table, expr_table[0]);

    if (is_const) {
        int root;

        root = expr_table[0];
        *retval = expr_table[4*root+1];
        if (expr_table[4*root+0] != NUMBER)
            parse_error(E_EXPRESSION_NOT_CONST);
    }
    else {
        int dummy;

        gen_expr(expr_table, expr_table[0], &dummy, 0, &dummy);
    }

    return tok;
}

int parse_expr(int tok, int delim)
{
    int dummy, rc, b3, temp;
    postfix_stack[0] = 0; // postfix stack empty

    rc = parse_calcexpr(tok, 0, &dummy, delim);

    while (!stack_empty(postfix_stack)) {
        temp = stack_pop(postfix_stack);
        b3   = stack_pop(postfix_stack);

        gen_emitbytes(2, 0x8b, 0x85, 0, 0);  // mov eax,DWORD PTR [ebp+temp]
        gen_emitdword(temp);
        gen_emitbytes(3, 0x83, 0x00, b3, 0); // add  DWORD PTR [eax],b3
    }
    return rc;
}

int parse_stmtblock(int tok, int continue_label, int break_label);

int parse_stmt(int tok, int continue_label, int break_label)
{
    if (tok == '{')
        return parse_stmtblock(lex_next_token(), continue_label, break_label);

    if (tok == IF) {
        int jz_pos;

        tok = lex_next_token();
        if (tok != '(')
            parse_error(E_IF_MISSING_OPENING_PARANTHESIS);

        tok = parse_expr(tok, ')');
        gen_emitbyte(0x58);  // pop eax
        gen_emitbytes(2, 0x09, 0xc0, 0, 0);   // or     eax,eax
        gen_emitbytes(2, 0x0f, 0x84, 0, 0);   // jz
        jz_pos = emit_pos;
        gen_emitdword(0);

        // then
        tok = parse_stmt(tok, continue_label, break_label);

        if (tok == ELSE) {
            // else
            int jmp_pos;

            gen_emitbyte(0xe9);   // jmp
            jmp_pos = emit_pos;
            gen_emitdword(0);
            gen_write_dword_into_buffer(emit_buffer, jz_pos, emit_pos - (jz_pos + 4));
            tok = parse_stmt(lex_next_token(), continue_label, break_label);
            gen_write_dword_into_buffer(emit_buffer, jmp_pos, emit_pos - (jmp_pos + 4));
        }
        else
            gen_write_dword_into_buffer(emit_buffer, jz_pos, emit_pos - (jz_pos + 4));
    }
    else if (tok == WHILE) {
        int new_break_label, new_continue_label;

        tok = lex_next_token();
        if (tok != '(')
            parse_error(E_WHILE_MISSING_OPENING_PARANTHESIS);

        new_continue_label = parse_add_internal_label();
        symbol_address[new_continue_label] = emit_pos;  // jump to this position on continue;
        new_break_label = parse_add_internal_label();

        tok = parse_expr(tok, ')');
        gen_emitbyte(0x58);  // pop eax
        gen_emitbytes(2, 0x09, 0xc0, 0, 0);  // or     eax,eax
        gen_emitbytes(2, 0x0f, 0x84, 0, 0);  // jz
        gen_add_backpatch(0, emit_pos);
        gen_emitdword(new_break_label);

        tok = parse_stmt(tok, new_continue_label, new_break_label);
        gen_emitbyte(0xe9);   // jmp
        gen_add_backpatch(0, emit_pos);
        gen_emitdword(new_continue_label);
        symbol_address[new_break_label] = emit_pos; // jump to this position on break;
    }
    else if (tok == DO) {
        int new_break_label, new_continue_label, start_label;

        new_continue_label = parse_add_internal_label();
        new_break_label = parse_add_internal_label();
        start_label = parse_add_internal_label();
        symbol_address[start_label] = emit_pos;  // jump to this position for loop iteration

        tok = parse_stmt(lex_next_token(), new_continue_label, new_break_label);

        if (tok != WHILE)
            parse_error(E_DO_MISSING_WHILE);

        symbol_address[new_continue_label] = emit_pos;  // jump to this position on continue;
        tok = parse_expr(lex_next_token(), 0);
        if (tok != ';')
            parse_error(E_MISSING_SEMICOLON);
        tok = lex_next_token();
        gen_emitbyte(0x58);  // pop eax
        gen_emitbytes(2, 0x09, 0xc0, 0, 0);  // or     eax,eax
        gen_emitbytes(2, 0x0f, 0x85, 0, 0);  // jnz
        gen_add_backpatch(0, emit_pos);
        gen_emitdword(start_label);
        symbol_address[new_break_label] = emit_pos; // jump to this position on break;
    }
    else if (tok == CONTINUE) {
        tok = lex_next_token();
        if (tok != ';')
            parse_error(E_MISSING_SEMICOLON);
        tok = lex_next_token();

        if (continue_label == 0)
            parse_error(E_CONTINUE_OUTSIDE_LOOP);

        gen_emitbyte(0xe9);   // jmp
        gen_add_backpatch(0, emit_pos);
        gen_emitdword(continue_label);
    }
    else if (tok == BREAK) {
        tok = lex_next_token();
        if (tok != ';')
            parse_error(E_MISSING_SEMICOLON);
        tok = lex_next_token();

        if (break_label == 0)
            parse_error(E_BREAK_OUTSIDE_LOOP);

        gen_emitbyte(0xe9);   // jmp
        gen_add_backpatch(0, emit_pos);
        gen_emitdword(break_label);
    }
    else if (tok == GOTO) {
        int symidx;

        tok = lex_next_token();
        if (tok != IDENTIFIER)
            parse_error(E_GOTO_MISSING_IDENTIFIER);

        symidx = parse_lookup_symbol(token_text);
        if (symidx == 0) {
            symidx = parse_add_symbol(token_text);
            symbol_type[symidx] = GOTO | GLOBAL;
        }

        gen_emitbyte(0xe9);   // jmp
        gen_add_backpatch(0, emit_pos);
        gen_emitdword(symidx);

        tok = lex_next_token();
        if (tok != ';')
            parse_error(E_MISSING_SEMICOLON);
    }
    else {
        if (tok == RETURN) {
            // next comes the expression
            tok = lex_next_token();
            if (tok != ';') {
                tok = parse_expr(tok, 0);
                gen_emitbyte(0x58);  // pop eax
            }

            gen_emitbytes(4, 0x89, 0xec, 0x5d, 0xc3); // mov esp, ebp  pop ebp  ret
        }
        else if (tok != ';') {
            if (tok == IDENTIFIER) {
                tok = lex_next_token();
                if (tok == ':') {
                    // labeled statement
                    int symidx;

                    symidx = parse_lookup_symbol(token_text);
                    if (symidx == 0)
                        symidx = parse_add_symbol(token_text);

                    symbol_type[symidx] = GOTO | GLOBAL;
                    symbol_address[symidx] = emit_pos;
                    return parse_stmt(lex_next_token(), continue_label, break_label);
                }
                else {
                    lex_push_token(tok);
                    tok = IDENTIFIER;
                }
            }
            tok = parse_expr(tok, 0);
            gen_emitbytes(3, 0x83, 0xc4, 0x04, 0);   // add esp, 4 ;  remove from stack
        }

        if (tok != ';')
            parse_error(E_MISSING_SEMICOLON);
        tok = lex_next_token();
    }
    return tok;
}

int parse_stmtblock(int tok, int continue_label, int break_label)
{
    int symidx_old;
    symidx_old = symbol_count;

    while (tok != '}') {
        if (tok == INT || tok == CHAR || tok == VOID || tok == ENUM)
            tok = parse_vardecl(tok);
        else
            tok = parse_stmt(tok, continue_label, break_label);
    }

    while (symidx_old < symbol_count) {
        // forget all symbols that belong to that block only
        if (!(symbol_type[symidx_old] & GLOBAL))
            symbol_name[symidx_old] = 0;
        ++symidx_old;
    }

    return lex_next_token();
}

void parse(void)
{
    int tok, type, symidx;

    type = 0;

    tok = lex_next_token();

    do {
        if (tok == 0)
            break;

        if (tok != INT && tok != CHAR && tok != VOID && tok != ENUM) {
            parse_error(E_MISSING_TYPE);
        }

        if (tok != ENUM) {
            type = tok;
            tok = lex_next_token();
            if (tok != IDENTIFIER) {
                if (tok != '*')
                    parse_error(E_MISSING_IDENTIFIER);

                type |= POINTER;
                tok = lex_next_token();
            }

            if (tok != IDENTIFIER)
                parse_error(E_MISSING_IDENTIFIER);

            symidx = parse_lookup_symbol(token_text);
            if (symidx == 0)
                symidx = parse_add_symbol(token_text);
            symbol_type[symidx] = type | GLOBAL;

            tok = lex_next_token();
            if (tok == '(') {
                int symidx_old;

                symidx_old = symbol_count;

                // function declaration
                symbol_type[symidx] |= FUNCTION;

                tok = parse_args(tok);
                if (tok == ';') {
                    // only a function definition
                    tok = lex_next_token();
                }
                else if (tok == '{') {
                    int link;

                    local_variable_space = 4;  // minimum 4 bytes
                    symbol_address[symidx] = emit_pos;
                    gen_emitbytes(3, 0x55, 0x89, 0xe5, 0);  // function prolog
                    gen_emitbytes(2, 0x81, 0xec, 0, 0);     // sub esp
                    link = emit_pos;
                    gen_emitdword(0);

                    tok = parse_stmtblock(lex_next_token(), 0, 0);
                    gen_write_dword_into_buffer(emit_buffer, link, local_variable_space);
                    gen_emitbytes(4, 0x89, 0xec, 0x5d, 0xc3); // function epilog
                }
                else
                    parse_error(E_MISSING_FUNCTION_BLOCK);

                while (symidx_old < symbol_count) {
                    // forget all symbols that belong to that function incl. parameters
                    symbol_name[symidx_old] = 0;
                    ++symidx_old;
                }
            }
            else {
                // global variable declaration
                tok = parse_vardecl2(tok, type | GLOBAL);
            }
        }
        else {
            // enum declaration
            tok = parse_enum_decl(tok);
        }
    } while (tok != 0);
}

int gen_write_pad(int count)
{
    int n;
    char buffer[2];

    buffer[0] = 0;

    n = count;
    while (n-- > 0)
        _sys_write(1, buffer, 1);

    return count;
}

int main(void)
{
    int mainidx, exitidx;

    parse_add_symbol("");
    if (CHAR != parse_add_symbol("char")) parse_error(E_BAD_INITIALIZATION);
    if (INT != parse_add_symbol("int")) parse_error(E_BAD_INITIALIZATION);
    if (VOID != parse_add_symbol("void")) parse_error(E_BAD_INITIALIZATION);
    if (RETURN != parse_add_symbol("return")) parse_error(E_BAD_INITIALIZATION);
    if (IF != parse_add_symbol("if")) parse_error(E_BAD_INITIALIZATION);
    if (WHILE != parse_add_symbol("while")) parse_error(E_BAD_INITIALIZATION);
    if (CONTINUE != parse_add_symbol("continue")) parse_error(E_BAD_INITIALIZATION);
    if (BREAK != parse_add_symbol("break")) parse_error(E_BAD_INITIALIZATION);
    if (ELSE != parse_add_symbol("else")) parse_error(E_BAD_INITIALIZATION);
    if (ENUM != parse_add_symbol("enum")) parse_error(E_BAD_INITIALIZATION);
    if (DO != parse_add_symbol("do")) parse_error(E_BAD_INITIALIZATION);
    if (GOTO != parse_add_symbol("goto")) parse_error(E_BAD_INITIALIZATION);

    num_keywords = symbol_count;

    // generate prolog to call main and exit
    gen_emitbyte(0xe8);  // call main
    mainidx = parse_add_symbol("main");
    gen_add_backpatch(0, emit_pos);
    gen_emitdword(mainidx);

    gen_emitbyte(0x50);  // push eax
    gen_emitbyte(0xe8);  // call _sys_exit
    exitidx = parse_add_symbol("_sys_exit");
    gen_add_backpatch(0, emit_pos);
    gen_emitdword(exitidx);

    lineno = 1;
    parse();

    gen_library(emit_pos, symbol_name, symbol_type, symbol_address, symbol_count);
    gen_write_binary(emit_buffer, emit_pos, string_table_buffer, string_table_size);

    return 0;
}

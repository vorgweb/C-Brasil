/*
 * Cbr Translator 1.0 Estável
 * Traduz Cbr para C e gera o arquivo C de saída automaticamente.
 *
 * Compilação:
 *   gcc -std=c11 -Wall -Wextra -Werror -O2 cbr.c -lm -o cbr
 *
 * Uso:
 *   ./cbr programa.cbr [programa_gerado.c]
 *
 * Recursos testados nesta versão estável:
 *   - Tipos inferidos, reatribuição tipada e expressões com precedência.
 *   - printf/scan com interpolação tipada.
 *   - if/else, while, for range, for clássico, break, continue e switch/case.
 *   - Funções numéricas de usuário com até oito parâmetros e return.
 *   - Arrays mistos, capacidade de 256 elementos, mutação, adicionar/remover/limpar.
 *   - Biblioteca de texto e funções matemáticas.
 *   - Diagnósticos do tradutor com arquivo e número de linha.
 *
 * Limitações conhecidas: import, dicionários, try/catch e depurador interativo
 * ainda não fazem parte desta versão beta.
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 4096
#define MAX_VARS 256
#define MAX_NAME 128
#define MAX_FUNCTIONS 128
#define MAX_SOURCE_LINES 8192
#define MAX_FUNCTION_LINES 128
#define MAX_ARRAYS 256
#define MAX_BUILTIN_ARGS 8

typedef enum {
    TYPE_STRING,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,
    TYPE_ARRAY
} VarType;

typedef struct {
    char name[MAX_NAME];
    VarType type;
} Variable;

static void fail(const char *filename, int line, const char *message);

typedef struct {
    char name[MAX_NAME];
    size_t length;
    int source_line;
} ArrayInfo;

static ArrayInfo arrays[MAX_ARRAYS];
static size_t array_count = 0;

/* Registro de arrays para validação estática de tamanho e indexação. */
static int find_array(const char *name) {
    for (size_t i = 0; i < array_count; i++) {
        if (strcmp(arrays[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static void add_array(const char *filename, int line, const char *name, size_t length) {
    if (array_count >= MAX_ARRAYS) fail(filename, line, "limite de arrays excedido");
    if (find_array(name) >= 0) fail(filename, line, "array já declarado");
    snprintf(arrays[array_count].name, sizeof arrays[array_count].name, "%s", name);
    arrays[array_count].length = length;
    arrays[array_count].source_line = line;
    array_count++;
}

typedef struct {
    char name[MAX_NAME];
    char params[8][MAX_NAME];
    size_t param_count;
    char body[MAX_FUNCTION_LINES][MAX_LINE];
    int body_lines;
    int source_line;
} Function;

static Function functions[MAX_FUNCTIONS];
static size_t function_count = 0;

static Variable vars[MAX_VARS];
static size_t var_count = 0;

static void fail(const char *filename, int line, const char *message) {
    fprintf(stderr, "%s:%d: erro: %s\n", filename, line, message);
    exit(EXIT_FAILURE);
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return s;
}

static int valid_identifier(const char *s) {
    if (!s || !(isalpha((unsigned char)*s) || *s == '_')) return 0;
    for (s++; *s; s++) {
        if (!(isalnum((unsigned char)*s) || *s == '_')) return 0;
    }
    return 1;
}

static int find_var(const char *name) {
    for (size_t i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static void add_var(const char *filename, int line, const char *name, VarType type) {
    if (!valid_identifier(name)) {
        fail(filename, line, "nome de variável inválido");
    }
    if (find_var(name) >= 0) {
        fail(filename, line, "variável já declarada; reatribuição ainda não é suportada");
    }
    if (var_count >= MAX_VARS) {
        fail(filename, line, "limite de variáveis excedido");
    }

    snprintf(vars[var_count].name, sizeof vars[var_count].name, "%s", name);
    vars[var_count].type = type;
    var_count++;
}

static int is_integer_literal(const char *s) {
    if (*s == '+' || *s == '-') s++;
    if (!*s) return 0;
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s)) return 0;
    }
    return 1;
}

/* Retorna 1 se s for um literal de texto entre aspas duplas. */
static int is_string_literal(const char *s) {
    size_t n = strlen(s);
    if (n < 2 || s[0] != '"' || s[n - 1] != '"') return 0;

    int escaped = 0;
    for (size_t i = 1; i + 1 < n; i++) {
        if (escaped) {
            escaped = 0;
        } else if (s[i] == '\\') {
            escaped = 1;
        } else if (s[i] == '"') {
            return 0;
        }
    }
    return !escaped;
}

static void emit_escaped_c_string(FILE *out, const char *s, size_t n) {
    fputc('"', out);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '\\': fputs("\\\\", out); break;
            case '"':  fputs("\\\"", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (c < 32 || c == 127) {
                    fprintf(out, "\\x%02x", c);
                } else {
                    fputc(c, out);
                }
        }
    }
    fputc('"', out);
}

static void translate_printf(const char *filename, int line, char *statement, FILE *out) {
    char *p = statement + strlen("printf");
    p = trim(p);

    if (*p != '(') fail(filename, line, "esperado '(' depois de printf");
    p++;
    p = trim(p);

    if (*p != '"') fail(filename, line, "printf v0.1 exige uma string entre aspas");

    char *string_start = p + 1;
    char *q = string_start;
    int escaped = 0;
    while (*q) {
        if (escaped) {
            escaped = 0;
        } else if (*q == '\\') {
            escaped = 1;
        } else if (*q == '"') {
            break;
        }
        q++;
    }
    if (*q != '"') fail(filename, line, "string não terminada em printf");

    char *after = trim(q + 1);
    if (*after != ')') fail(filename, line, "esperado ')' depois da string de printf");
    after = trim(after + 1);
    if (*after != '\0' && *after != ';') {
        fail(filename, line, "texto inesperado depois de printf");
    }

    /* Primeiro constrói o formato printf e registra os argumentos. */
    char format[MAX_LINE * 2];
    char args[MAX_LINE * 2];
    size_t fpos = 0, apos = 0;
    format[0] = '\0';
    args[0] = '\0';

    const char *s = string_start;
    while (s < q) {
        if (*s == '{') {
            const char *close = memchr(s, '}', (size_t)(q - s));
            if (!close) fail(filename, line, "marcador de interpolação sem '}'");

            char marker[2 * MAX_NAME];
            size_t mlen = (size_t)(close - s - 1);
            if (mlen == 0 || mlen >= sizeof marker) {
                fail(filename, line, "marcador de interpolação inválido");
            }
            memcpy(marker, s + 1, mlen);
            marker[mlen] = '\0';

            char *dot = strchr(marker, '.');
            if (!dot) fail(filename, line, "use {str.nome} ou {int.nome}");
            *dot++ = '\0';

            char *bracket = strchr(dot, '[');
            long long array_index = -1;
            if (bracket) {
                char *endbr = strchr(bracket, ']');
                if (!endbr || endbr[1] != '\0') fail(filename, line, "índice de array inválido");
                *bracket = '\0';
                *endbr = '\0';
                if (!is_integer_literal(bracket + 1)) fail(filename, line, "índice de array precisa ser inteiro literal");
                array_index = strtoll(bracket + 1, NULL, 10);
            }
            if (!valid_identifier(dot)) fail(filename, line, "nome inválido no marcador de interpolação");
            int index = find_var(dot);
            if (index < 0) fail(filename, line, "variável usada na interpolação não foi declarada");
            char argexpr[2 * MAX_NAME];
            char finalexpr[3 * MAX_NAME];
            if (bracket) snprintf(argexpr, sizeof argexpr, "cbr_at(%s, %s_len, %lld)", dot, dot, array_index);
            else snprintf(argexpr, sizeof argexpr, "%s", dot);
            snprintf(finalexpr, sizeof finalexpr, "%s", argexpr);
            VarType actual_type = vars[index].type;
            if (bracket && actual_type != TYPE_ARRAY) fail(filename, line, "acesso por índice exige um array");
            if (!bracket && actual_type == TYPE_ARRAY) fail(filename, line, "array precisa de índice na interpolação");

            if (strcmp(marker, "str") == 0) {
                if ((!bracket && vars[index].type != TYPE_STRING) || (bracket && actual_type != TYPE_ARRAY))
                    fail(filename, line, "{str.nome} exige texto ou elemento de array");
                if (fpos + 2 >= sizeof format) fail(filename, line, "string de formato muito longa");
                format[fpos++] = '%';
                format[fpos++] = 's';
                if (bracket) snprintf(finalexpr, sizeof finalexpr, "cbr_as_string(%s)", argexpr); else snprintf(finalexpr, sizeof finalexpr, "%s", dot);
                apos += (size_t)snprintf(args + apos, sizeof args - apos, ", %s", finalexpr);
            } else if (strcmp(marker, "int") == 0) {
                if ((!bracket && vars[index].type != TYPE_INT) || (bracket && actual_type != TYPE_ARRAY))
                    fail(filename, line, "{int.nome} exige inteiro ou elemento de array");
                if (fpos + 2 >= sizeof format) fail(filename, line, "string de formato muito longa");
                if (bracket) { format[fpos++] = '%'; format[fpos++] = 'l'; format[fpos++] = 'l'; format[fpos++] = 'd'; }
                else { format[fpos++] = '%'; format[fpos++] = 'd'; }
                if (bracket) snprintf(finalexpr, sizeof finalexpr, "cbr_as_int(%s)", argexpr); else snprintf(finalexpr, sizeof finalexpr, "%s", dot);
                apos += (size_t)snprintf(args + apos, sizeof args - apos, ", %s", finalexpr);
            } else if (strcmp(marker, "float") == 0) {
                if ((!bracket && vars[index].type != TYPE_FLOAT) || (bracket && actual_type != TYPE_ARRAY))
                    fail(filename, line, "{float.nome} exige decimal ou elemento de array");
                if (fpos + 2 >= sizeof format) fail(filename, line, "string de formato muito longa");
                format[fpos++] = '%';
                format[fpos++] = 'g';
                if (bracket) snprintf(finalexpr, sizeof finalexpr, "cbr_as_float(%s)", argexpr); else snprintf(finalexpr, sizeof finalexpr, "%s", dot);
                apos += (size_t)snprintf(args + apos, sizeof args - apos, ", %s", finalexpr);
            } else if (strcmp(marker, "bool") == 0) {
                if ((!bracket && vars[index].type != TYPE_BOOL) || (bracket && actual_type != TYPE_ARRAY))
                    fail(filename, line, "{bool.nome} exige booleano ou elemento de array");
                const char *bool_format = "%s";
                if (fpos + 2 >= sizeof format) fail(filename, line, "string de formato muito longa");
                format[fpos++] = bool_format[0];
                format[fpos++] = bool_format[1];
                if (bracket) snprintf(finalexpr, sizeof finalexpr, "(cbr_as_bool(%s) ? \"true\" : \"false\")", argexpr);
                else snprintf(finalexpr, sizeof finalexpr, "(%s ? \"true\" : \"false\")", dot);
                apos += (size_t)snprintf(args + apos, sizeof args - apos, ", %s", finalexpr);
            } else {
                fail(filename, line, "tipo de interpolação desconhecido; use str, int, float ou bool");
            }
            if (apos >= sizeof args) fail(filename, line, "argumentos de printf muito longos");
            s = close + 1;
        } else {
            if (fpos + 1 >= sizeof format) fail(filename, line, "string de formato muito longa");
            if (*s == '\\' && s + 1 < q) {
                s++;
                switch (*s++) {
                    case 'n': format[fpos++] = '\n'; break;
                    case 'r': format[fpos++] = '\r'; break;
                    case 't': format[fpos++] = '\t'; break;
                    case '\\': format[fpos++] = '\\'; break;
                    case '"': format[fpos++] = '"'; break;
                    default:
                        fail(filename, line, "escape desconhecido na string; use \\\\n, \\\\t, \\\\r, \\\\\\\\ ou \\\\\" ");
                }
            } else {
                format[fpos++] = *s++;
            }
        }
    }
    format[fpos] = '\0';

    fputs("    printf(", out);
    emit_escaped_c_string(out, format, fpos);
    fputs(args, out);
    fputs(");\n", out);
}

/* scan() recebe uma mensagem e um marcador tipado de variável já existente. */
static void translate_scan(const char *filename, int line, char *statement, FILE *out) {
    char *p = trim(statement + strlen("scan"));
    if (*p != '(') fail(filename, line, "esperado '(' depois de scan");
    p = trim(p + 1);
    if (*p != '"') fail(filename, line, "scan exige uma mensagem entre aspas");

    char prompt[MAX_LINE];
    size_t plen = 0;
    char *q = p + 1;
    int escaped = 0;
    while (*q) {
        if (escaped) {
            char c;
            switch (*q) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                default: fail(filename, line, "escape desconhecido na mensagem de scan");
            }
            if (plen + 1 >= sizeof prompt) fail(filename, line, "mensagem de scan muito longa");
            prompt[plen++] = c;
            escaped = 0;
            q++;
        } else if (*q == '\\') {
            escaped = 1;
            q++;
        } else if (*q == '"') {
            break;
        } else {
            if (plen + 1 >= sizeof prompt) fail(filename, line, "mensagem de scan muito longa");
            prompt[plen++] = *q++;
        }
    }
    if (*q != '"') fail(filename, line, "mensagem não terminada em scan");
    prompt[plen] = '\0';
    p = trim(q + 1);
    if (*p != '{') fail(filename, line, "esperado marcador {tipo.variavel} depois da mensagem");
    char *close = strchr(p + 1, '}');
    if (!close) fail(filename, line, "marcador de scan sem '}'");
    *close = '\0';
    char marker[2 * MAX_NAME];
    if (strlen(p + 1) >= sizeof marker) fail(filename, line, "marcador de scan muito longo");
    strcpy(marker, trim(p + 1));
    char *dot = strchr(marker, '.');
    if (!dot) fail(filename, line, "use scan(\"mensagem\" {str.nome})");
    *dot++ = '\0';
    if (!valid_identifier(dot)) fail(filename, line, "nome inválido no marcador de scan");
    char *tail = trim(close + 1);
    if (*tail++ != ')') fail(filename, line, "esperado ')' depois do marcador de scan");
    tail = trim(tail);
    if (*tail != '\0') fail(filename, line, "texto inesperado depois de scan");

    int index = find_var(dot);
    if (index < 0) fail(filename, line, "scan exige uma variável declarada anteriormente");
    VarType type = vars[index].type;
    const char *expected = type == TYPE_STRING ? "str" : type == TYPE_INT ? "int" : type == TYPE_FLOAT ? "float" : "bool";
    if (strcmp(marker, expected) != 0)
        fail(filename, line, "tipo do marcador de scan não corresponde ao tipo da variável");

    char buf[MAX_LINE];
    snprintf(buf, sizeof buf, "__cbr_scanbuf_%d", line);
    fputs("    printf(", out);
    emit_escaped_c_string(out, prompt, plen);
    fputs(");\n", out);
    fprintf(out, "    static char %s[%d];\n", buf, MAX_LINE);
    fprintf(out, "    if (!fgets(%s, sizeof %s, stdin)) { fprintf(stderr, \"Erro ao ler entrada.\\n\"); return 1; }\n", buf, buf);
    fprintf(out, "    %s[strcspn(%s, \"\\n\")] = '\\0';\n", buf, buf);

    if (type == TYPE_STRING) {
        fprintf(out, "    %s = %s;\n", dot, buf);
    } else if (type == TYPE_INT) {
        fprintf(out, "    if (sscanf(%s, \"%%d\", &%s) != 1) { fprintf(stderr, \"Entrada inválida para inteiro.\\n\"); return 1; }\n", buf, dot);
    } else if (type == TYPE_FLOAT) {
        fprintf(out, "    if (sscanf(%s, \"%%lf\", &%s) != 1) { fprintf(stderr, \"Entrada inválida para decimal.\\n\"); return 1; }\n", buf, dot);
    } else {
        fprintf(out, "    if (strcmp(%s, \"true\") == 0 || strcmp(%s, \"1\") == 0) %s = true;\n", buf, buf, dot);
        fprintf(out, "    else if (strcmp(%s, \"false\") == 0 || strcmp(%s, \"0\") == 0) %s = false;\n", buf, buf, dot);
        fputs("    else { fprintf(stderr, \"Entrada inválida para booleano; use true/false ou 1/0.\\n\"); return 1; }\n", out);
    }
}

/* Parser de expressões tipadas. Valida tipos e precedência; o C executa a expressão. */
static int find_function(const char *name) {
    for (size_t i = 0; i < function_count; i++)
        if (strcmp(functions[i].name, name) == 0) return (int)i;
    return -1;
}

typedef struct {
    const char *p;
    const char *filename;
    int line;
} ExprParser;

static void expr_space(ExprParser *e) {
    while (isspace((unsigned char)*e->p)) e->p++;
}

static VarType expr_parse_or(ExprParser *e);

static VarType expr_parse_factor(ExprParser *e) {
    expr_space(e);
    if (*e->p == '+' || *e->p == '-') {
        e->p++;
        VarType t = expr_parse_factor(e);
        if (t != TYPE_INT && t != TYPE_FLOAT)
            fail(e->filename, e->line, "operador unário exige um número");
        return t;
    }
    if (*e->p == '!') {
        e->p++;
        VarType t = expr_parse_factor(e);
        if (t != TYPE_BOOL) fail(e->filename, e->line, "operador ! exige um booleano");
        return TYPE_BOOL;
    }
    if (*e->p == '(') {
        e->p++;
        VarType t = expr_parse_or(e);
        expr_space(e);
        if (*e->p != ')') fail(e->filename, e->line, "faltou ')' na expressão");
        e->p++;
        return t;
    }
    if (isdigit((unsigned char)*e->p) || (*e->p == '.' && isdigit((unsigned char)e->p[1]))) {
        int decimal = 0;
        if (*e->p == '.') { decimal = 1; e->p++; }
        while (isdigit((unsigned char)*e->p)) e->p++;
        if (*e->p == '.') {
            decimal = 1; e->p++;
            if (!isdigit((unsigned char)*e->p))
                fail(e->filename, e->line, "literal decimal precisa de dígitos depois do ponto");
            while (isdigit((unsigned char)*e->p)) e->p++;
        }
        if (*e->p == 'e' || *e->p == 'E') {
            decimal = 1; e->p++;
            if (*e->p == '+' || *e->p == '-') e->p++;
            if (!isdigit((unsigned char)*e->p)) fail(e->filename, e->line, "expoente decimal inválido");
            while (isdigit((unsigned char)*e->p)) e->p++;
        }
        return decimal ? TYPE_FLOAT : TYPE_INT;
    }
    if (isalpha((unsigned char)*e->p) || *e->p == '_') {
        char name[MAX_NAME]; size_t n = 0;
        while (isalnum((unsigned char)*e->p) || *e->p == '_') {
            if (n + 1 >= sizeof name) fail(e->filename, e->line, "nome de variável muito longo");
            name[n++] = *e->p++;
        }
        name[n] = '\0';
        expr_space(e);
        if (*e->p == '(') {
            int fi = find_function(name);
            if (fi < 0) fail(e->filename, e->line, "função não declarada");
            e->p++;
            size_t argument_count = 0;
            expr_space(e);
            if (*e->p != ')') {
                for (;;) {
                    VarType arg = expr_parse_or(e);
                    if (arg != TYPE_INT && arg != TYPE_FLOAT)
                        fail(e->filename, e->line, "funções de usuário aceitam apenas argumentos numéricos");
                    argument_count++;
                    expr_space(e);
                    if (*e->p == ',') { e->p++; continue; }
                    break;
                }
            }
            expr_space(e);
            if (*e->p != ')') fail(e->filename, e->line, "faltou ')' ou separador na chamada da função");
            if (argument_count != functions[fi].param_count) fail(e->filename, e->line, "quantidade de argumentos diferente da definição da função");
            e->p++;
            return TYPE_FLOAT;
        }
        if (strcmp(name, "true") == 0 || strcmp(name, "false") == 0) return TYPE_BOOL;
        int index = find_var(name);
        if (index < 0) fail(e->filename, e->line, "variável usada na expressão não foi declarada");
        if (vars[index].type == TYPE_STRING) fail(e->filename, e->line, "texto não pode ser usado em expressão matemática");
        return vars[index].type;
    }
    fail(e->filename, e->line, "esperado número, booleano, variável ou '(' na expressão");
    return TYPE_INT;
}

static VarType expr_parse_power(ExprParser *e) {
    VarType left = expr_parse_factor(e);
    expr_space(e);
    if (e->p[0] == '*' && e->p[1] == '*') {
        e->p += 2;
        VarType right = expr_parse_power(e);
        if ((left != TYPE_INT && left != TYPE_FLOAT) || (right != TYPE_INT && right != TYPE_FLOAT))
            fail(e->filename, e->line, "** exige operandos numéricos");
        return (left == TYPE_FLOAT || right == TYPE_FLOAT) ? TYPE_FLOAT : TYPE_INT;
    }
    return left;
}

static VarType expr_parse_product(ExprParser *e) {
    VarType left = expr_parse_power(e);
    for (;;) {
        expr_space(e);
        if (*e->p != '*' && *e->p != '/' && *e->p != '%') break;
        char op = *e->p++;
        VarType right = expr_parse_power(e);
        if ((left != TYPE_INT && left != TYPE_FLOAT) || (right != TYPE_INT && right != TYPE_FLOAT))
            fail(e->filename, e->line, "*, / e % exigem operandos numéricos");
        if (op == '%' && (left != TYPE_INT || right != TYPE_INT))
            fail(e->filename, e->line, "%% exige operandos inteiros");
        if (left == TYPE_FLOAT || right == TYPE_FLOAT) left = TYPE_FLOAT;
    }
    return left;
}

static VarType expr_parse_sum(ExprParser *e) {
    VarType left = expr_parse_product(e);
    for (;;) {
        expr_space(e);
        if (*e->p != '+' && *e->p != '-') break;
        e->p++;
        VarType right = expr_parse_product(e);
        if ((left != TYPE_INT && left != TYPE_FLOAT) || (right != TYPE_INT && right != TYPE_FLOAT))
            fail(e->filename, e->line, "+ e - exigem operandos numéricos");
        if (left == TYPE_FLOAT || right == TYPE_FLOAT) left = TYPE_FLOAT;
    }
    return left;
}

static VarType expr_parse_compare(ExprParser *e) {
    VarType left = expr_parse_sum(e);
    expr_space(e);
    const char *p = e->p;
    int comparison = 0;
    if ((p[0] == '=' && p[1] == '=') || (p[0] == '!' && p[1] == '=') ||
        (p[0] == '<' && p[1] == '=') || (p[0] == '>' && p[1] == '=') ||
        p[0] == '<' || p[0] == '>') {
        comparison = 1;
        if (p[1] == '=' && (p[0] == '=' || p[0] == '!' || p[0] == '<' || p[0] == '>')) e->p += 2;
        else if (p[0] == '<' || p[0] == '>') e->p++;
    }
    if (!comparison) return left;
    VarType right = expr_parse_sum(e);
    if (!((left == TYPE_INT || left == TYPE_FLOAT) && (right == TYPE_INT || right == TYPE_FLOAT)) && left != right)
        fail(e->filename, e->line, "comparação exige tipos compatíveis");
    expr_space(e);
    if (strstr(e->p, "==") == e->p || strstr(e->p, "!=") == e->p || strstr(e->p, "<=") == e->p || strstr(e->p, ">=") == e->p || *e->p == '<' || *e->p == '>')
        fail(e->filename, e->line, "comparações encadeadas não são suportadas; use && ou ||");
    return TYPE_BOOL;
}

static VarType expr_parse_and(ExprParser *e) {
    VarType left = expr_parse_compare(e);
    for (;;) {
        expr_space(e);
        if (e->p[0] != '&' || e->p[1] != '&') break;
        e->p += 2;
        VarType right = expr_parse_compare(e);
        if (left != TYPE_BOOL || right != TYPE_BOOL)
            fail(e->filename, e->line, "&& exige expressões booleanas; use uma comparação");
        left = TYPE_BOOL;
    }
    return left;
}

static VarType expr_parse_or(ExprParser *e) {
    VarType left = expr_parse_and(e);
    for (;;) {
        expr_space(e);
        if (e->p[0] != '|' || e->p[1] != '|') break;
        e->p += 2;
        VarType right = expr_parse_and(e);
        if (left != TYPE_BOOL || right != TYPE_BOOL)
            fail(e->filename, e->line, "|| exige expressões booleanas; use uma comparação");
        left = TYPE_BOOL;
    }
    return left;
}

static VarType parse_expression(const char *value, const char *filename, int line) {
    ExprParser e = { value, filename, line };
    VarType type = expr_parse_or(&e);
    expr_space(&e);
    if (*e.p != '\0') fail(filename, line, "símbolo inesperado na expressão; confira operadores e tipos");
    return type;
}

static int array_literal(const char *value) {
    size_t n = strlen(value);
    return n >= 2 && value[0] == '[' && value[n - 1] == ']';
}

static void emit_array_value(const char *filename, int line, char *value, FILE *out) {
    char *v = trim(value);
    if (is_string_literal(v)) { fputs("cbr_string(", out); fputs(v, out); fputc(')', out); return; }
    if (strcmp(v, "true") == 0 || strcmp(v, "false") == 0) { fprintf(out, "cbr_bool(%s)", v); return; }
    VarType t = parse_expression(v, filename, line);
    if (t == TYPE_INT) fprintf(out, "cbr_int(%s)", v);
    else if (t == TYPE_FLOAT) fprintf(out, "cbr_float(%s)", v);
    else fail(filename, line, "array misto aceita apenas literais string, booleanos e números simples nesta versão");
}

static void translate_array_assignment(const char *filename, int line, const char *name, char *value, FILE *out) {
    size_t n = strlen(value);
    value[n - 1] = '\0';
    char *inside = trim(value + 1);
    char *items[256]; size_t count = 0;
    if (*inside) {
        int quoted = 0, escaped = 0, depth = 0;
        char *start = inside;
        for (char *p = inside;; p++) {
            char c = *p;
            if (escaped) escaped = 0;
            else if (quoted && c == '\\') escaped = 1;
            else if (c == '"') quoted = !quoted;
            else if (!quoted && (c == '(' || c == '[')) depth++;
            else if (!quoted && (c == ')' || c == ']')) depth--;
            if ((!c || (c == ',' && !quoted && depth == 0))) {
                if (count >= 256) fail(filename, line, "array excede 256 elementos");
                if (c) *p = '\0';
                char *item = trim(start);
                if (!*item) fail(filename, line, "elemento vazio no array");
                items[count++] = item;
                if (!c) break;
                start = p + 1;
            }
            if (!c) break;
        }
    }
    if (find_var(name) >= 0) fail(filename, line, "reatribuição de array ainda não é suportada");
    add_var(filename, line, name, TYPE_ARRAY);
    add_array(filename, line, name, count);
    fprintf(out, "    CbrValue %s[256] = {", name);
    if (count == 0) fputc('0', out);
    for (size_t i = 0; i < count; i++) {
        if (i) fputs(", ", out);
        emit_array_value(filename, line, items[i], out);
    }
    fprintf(out, "};\n    size_t %s_len = %zu;\n", name, count);
}

static void convert_power(char *expr) {
    /* Converte bases/ expoentes simples: a ** b ou (a + b) ** 2 para pow(base, expoente). */
    char *op = strstr(expr, "**");
    if (!op) return;
    char left[512], right[512];
    char *lend = op;
    while (lend > expr && isspace((unsigned char)lend[-1])) lend--;
    char *lstart = lend;
    while (lstart > expr && (isalnum((unsigned char)lstart[-1]) || lstart[-1] == '_' || lstart[-1] == '.')) lstart--;
    char *rstart = op + 2;
    while (isspace((unsigned char)*rstart)) rstart++;
    char *rend = rstart;
    if (*rend == '-') rend++;
    while (isalnum((unsigned char)*rend) || *rend == '_' || *rend == '.') rend++;
    if (lstart == lend || rstart == rend) return;
    size_t ln = (size_t)(lend - lstart), rn = (size_t)(rend - rstart);
    if (ln >= sizeof left || rn >= sizeof right) return;
    memcpy(left, lstart, ln); left[ln] = 0;
    memcpy(right, rstart, rn); right[rn] = 0;
    char replacement[1200];
    snprintf(replacement, sizeof replacement, "pow((%s), (%s))", left, right);
    size_t prefix = (size_t)(lstart - expr), suffix = strlen(rend);
    if (prefix + strlen(replacement) + suffix + 1 >= MAX_LINE) return;
    memmove(expr + prefix + strlen(replacement), rend, suffix + 1);
    memcpy(expr + prefix, replacement, strlen(replacement));
}

static int parse_string_arg(const char *filename, int line, const char *arg, char *c_expr, size_t cap) {
    char tmp[MAX_LINE];
    if (strlen(arg) >= sizeof tmp) fail(filename, line, "argumento de texto muito longo");
    strcpy(tmp, arg);
    char *v = trim(tmp);
    if (is_string_literal(v)) {
        if (snprintf(c_expr, cap, "%s", v) >= (int)cap) fail(filename, line, "argumento muito longo");
        return 1;
    }
    if (valid_identifier(v)) {
        int i = find_var(v);
        if (i >= 0 && vars[i].type == TYPE_STRING) {
            if (snprintf(c_expr, cap, "%s", v) >= (int)cap) fail(filename, line, "argumento muito longo");
            return 1;
        }
    }
    fail(filename, line, "função de texto exige texto literal ou variável str");
    return 0;
}

/* Built-ins de texto implementados no tradutor: tamanho, juntar, maiusculas, minusculas. */
static int translate_text_builtin(const char *filename, int line, const char *name,
                                  const char *value, char *ctype, size_t ctype_cap,
                                  char *cexpr, size_t cexpr_cap) {
    const char *open = strchr(value, '(');
    size_t namelen = strlen(name);
    if (strncmp(value, name, namelen) != 0 || !open || open != value + namelen) return 0;
    const char *close = strrchr(open + 1, ')');
    if (!close || *trim((char *)close + 1) != '\0') fail(filename, line, "chamada de função de texto inválida");
    char args[MAX_LINE]; size_t n = (size_t)(close - open - 1);
    if (n >= sizeof args) fail(filename, line, "argumentos muito longos");
    memcpy(args, open + 1, n); args[n] = '\0';
    char *arg = trim(args);
    if (!*arg) fail(filename, line, "função exige argumento");
    if (strcmp(name, "tamanho") == 0) {
        char a[MAX_LINE];
        if (valid_identifier(arg) && find_array(arg) >= 0) {
            if (snprintf(a, sizeof a, "%s", arg) >= (int)sizeof a) fail(filename, line, "nome de array longo demais");
            snprintf(ctype, ctype_cap, "int");
            if (snprintf(cexpr, cexpr_cap, "(int)%s_len", a) >= (int)cexpr_cap) fail(filename, line, "expressão longa demais");
            return 1;
        }
        parse_string_arg(filename, line, arg, a, sizeof a);
        snprintf(ctype, ctype_cap, "int");
        if (snprintf(cexpr, cexpr_cap, "(int)cbr_length(%s)", a) >= (int)cexpr_cap) fail(filename, line, "expressão longa demais");
        return 1;
    }
    if (strcmp(name, "contem") == 0) {
        int quoted = 0, escaped = 0; char *comma = NULL;
        for (char *p = arg; *p; p++) {
            if (escaped) escaped = 0;
            else if (quoted && *p == '\\') escaped = 1;
            else if (*p == '"') quoted = !quoted;
            else if (*p == ',' && !quoted) { comma = p; break; }
        }
        if (!comma) fail(filename, line, "contem() exige texto e trecho");
        *comma = '\0'; char a[MAX_LINE], b[MAX_LINE];
        parse_string_arg(filename, line, trim(arg), a, sizeof a);
        parse_string_arg(filename, line, trim(comma + 1), b, sizeof b);
        snprintf(ctype, ctype_cap, "bool");
        if (snprintf(cexpr, cexpr_cap, "(strstr(%s, %s) != NULL)", a, b) >= (int)cexpr_cap) fail(filename, line, "expressão longa demais");
        return 1;
    }
    if (strcmp(name, "subtexto") == 0) {
        char *parts[MAX_BUILTIN_ARGS]; size_t argc = 0;
        int quoted = 0, escaped = 0, depth = 0; char *start = arg;
        for (char *p = arg;; p++) {
            char c = *p;
            if (escaped) escaped = 0;
            else if (quoted && c == '\\') escaped = 1;
            else if (c == '"') quoted = !quoted;
            else if (!quoted && (c == '(' || c == '[')) depth++;
            else if (!quoted && (c == ')' || c == ']')) depth--;
            if ((!c || (c == ',' && !quoted && depth == 0))) {
                if (argc >= MAX_BUILTIN_ARGS) fail(filename, line, "subtexto() tem argumentos demais");
                if (c) { *p = '\0'; }
                parts[argc++] = trim(start);
                if (!c) break;
                start = p + 1;
            }
            if (!c) break;
        }
        if (argc != 3) fail(filename, line, "subtexto() exige texto, início e comprimento");
        char a[MAX_LINE]; parse_string_arg(filename, line, parts[0], a, sizeof a);
        if (parse_expression(parts[1], filename, line) != TYPE_INT || parse_expression(parts[2], filename, line) != TYPE_INT) fail(filename, line, "índices de subtexto() precisam ser inteiros");
        snprintf(ctype, ctype_cap, "const char *");
        if (snprintf(cexpr, cexpr_cap, "cbr_substr(%s, %s, %s)", a, parts[1], parts[2]) >= (int)cexpr_cap) fail(filename, line, "expressão longa demais");
        return 1;
    }
    if (strcmp(name, "substituir") == 0) {
        char *parts[MAX_BUILTIN_ARGS]; size_t argc = 0;
        int quoted = 0, escaped = 0, depth = 0; char *start = arg;
        for (char *p = arg;; p++) {
            char c = *p;
            if (escaped) escaped = 0;
            else if (quoted && c == '\\') escaped = 1;
            else if (c == '"') quoted = !quoted;
            else if (!quoted && (c == '(' || c == '[')) depth++;
            else if (!quoted && (c == ')' || c == ']')) depth--;
            if ((!c || (c == ',' && !quoted && depth == 0))) {
                if (argc >= MAX_BUILTIN_ARGS) fail(filename, line, "substituir() tem argumentos demais");
                if (c) { *p = '\0'; }
                parts[argc++] = trim(start);
                if (!c) break;
                start = p + 1;
            }
            if (!c) break;
        }
        if (argc != 3) fail(filename, line, "substituir() exige texto, trecho antigo e trecho novo");
        char a[MAX_LINE], b[MAX_LINE], c[MAX_LINE];
        parse_string_arg(filename, line, parts[0], a, sizeof a);
        parse_string_arg(filename, line, parts[1], b, sizeof b);
        parse_string_arg(filename, line, parts[2], c, sizeof c);
        snprintf(ctype, ctype_cap, "const char *");
        if (snprintf(cexpr, cexpr_cap, "cbr_replace(%s, %s, %s)", a, b, c) >= (int)cexpr_cap) fail(filename, line, "expressão longa demais");
        return 1;
    }
    if (strcmp(name, "maiusculas") == 0 || strcmp(name, "minusculas") == 0) {
        char a[MAX_LINE]; parse_string_arg(filename, line, arg, a, sizeof a);
        snprintf(ctype, ctype_cap, "const char *");
        if (snprintf(cexpr, cexpr_cap, "%s(%s)", strcmp(name, "maiusculas") == 0 ? "cbr_upper" : "cbr_lower", a) >= (int)cexpr_cap) fail(filename, line, "expressão longa demais");
        return 1;
    }
    if (strcmp(name, "juntar") == 0) {
        int quoted = 0, escaped = 0; char *comma = NULL;
        for (char *p = arg; *p; p++) {
            if (escaped) escaped = 0;
            else if (quoted && *p == '\\') escaped = 1;
            else if (*p == '"') quoted = !quoted;
            else if (*p == ',' && !quoted) { comma = p; break; }
        }
        if (!comma) fail(filename, line, "juntar() exige dois argumentos de texto");
        *comma = '\0'; char a[MAX_LINE], b[MAX_LINE];
        parse_string_arg(filename, line, trim(arg), a, sizeof a);
        parse_string_arg(filename, line, trim(comma + 1), b, sizeof b);
        snprintf(ctype, ctype_cap, "const char *");
        if (snprintf(cexpr, cexpr_cap, "cbr_join(%s, %s)", a, b) >= (int)cexpr_cap) fail(filename, line, "expressão longa demais");
        return 1;
    }
    return 0;
}

/*
 * Biblioteca matemática da Cbr.
 * Estas chamadas são reconhecidas antes da análise de uma atribuição e viram
 * chamadas explícitas à biblioteca matemática padrão do C.
 */
typedef struct {
    const char *name;
    const char *c_name;
    int min_args;
    int max_args;
    int result_is_integer;
} NumericBuiltin;

static const NumericBuiltin numeric_builtins[] = {
    {"raiz", "sqrt", 1, 1, 0},
    {"absoluto", "fabs", 1, 1, 0},
    {"arredondar", "round", 1, 1, 0},
    {"piso", "floor", 1, 1, 0},
    {"teto", "ceil", 1, 1, 0},
    {"seno", "sin", 1, 1, 0},
    {"cosseno", "cos", 1, 1, 0},
    {"tangente", "tan", 1, 1, 0},
    {"logaritmo", "log", 1, 1, 0},
    {"log10", "log10", 1, 1, 0},
    {"exponencial", "exp", 1, 1, 0},
    {"maximo", "fmax", 2, 2, 0},
    {"minimo", "fmin", 2, 2, 0},
    {"potencia", "pow", 2, 2, 0},
    {"limitar", "fmin", 3, 3, 0}
};

static size_t split_call_arguments(const char *filename, int line, char *args,
                                   char **parts, size_t capacity) {
    size_t count = 0;
    char *start = args;
    int quoted = 0, escaped = 0, depth = 0;
    for (char *p = args;; p++) {
        char c = *p;
        if (escaped) escaped = 0;
        else if (quoted && c == '\\') escaped = 1;
        else if (c == '"') quoted = !quoted;
        else if (!quoted && (c == '(' || c == '[')) depth++;
        else if (!quoted && (c == ')' || c == ']')) depth--;
        if (depth < 0) fail(filename, line, "parênteses ou colchetes desequilibrados nos argumentos");
        if ((!c || (c == ',' && !quoted && depth == 0))) {
            if (count >= capacity) fail(filename, line, "argumentos demais na chamada");
            if (c) *p = '\0';
            parts[count++] = trim(start);
            if (!c) break;
            start = p + 1;
        }
        if (!c) break;
    }
    if (quoted || depth != 0) fail(filename, line, "aspas ou parênteses não foram fechados nos argumentos");
    return count;
}

static int numeric_builtin_match(const char *value, const NumericBuiltin **builtin,
                                 char *args, size_t args_capacity) {
    const char *open = strchr(value, '(');
    if (!open) return 0;
    const char *close = strrchr(open + 1, ')');
    if (!close || *trim((char *)close + 1) != '\0') return 0;
    size_t name_length = (size_t)(open - value);
    if (name_length == 0 || name_length >= MAX_NAME) return 0;
    char name[MAX_NAME];
    memcpy(name, value, name_length); name[name_length] = '\0';
    for (size_t i = 0; i < sizeof numeric_builtins / sizeof numeric_builtins[0]; i++) {
        if (strcmp(name, numeric_builtins[i].name) == 0) {
            size_t n = (size_t)(close - open - 1);
            if (n >= args_capacity) return 0;
            memcpy(args, open + 1, n); args[n] = '\0';
            *builtin = &numeric_builtins[i];
            return 1;
        }
    }
    return 0;
}

static int translate_numeric_builtin(const char *filename, int line, const char *value,
                                     char *ctype, size_t ctype_capacity,
                                     char *expression, size_t expression_capacity) {
    char args[MAX_LINE];
    const NumericBuiltin *builtin = NULL;
    if (!numeric_builtin_match(value, &builtin, args, sizeof args)) return 0;
    char *parts[MAX_BUILTIN_ARGS];
    size_t count = split_call_arguments(filename, line, args, parts, MAX_BUILTIN_ARGS);
    if ((int)count < builtin->min_args || (int)count > builtin->max_args)
        fail(filename, line, "quantidade incorreta de argumentos para função matemática");
    char converted[MAX_BUILTIN_ARGS][MAX_LINE];
    for (size_t i = 0; i < count; i++) {
        VarType t = parse_expression(parts[i], filename, line);
        if (t != TYPE_INT && t != TYPE_FLOAT)
            fail(filename, line, "função matemática exige argumentos numéricos");
        snprintf(converted[i], sizeof converted[i], "((double)(%s))", parts[i]);
        convert_power(converted[i]);
    }
    if (strcmp(builtin->name, "limitar") == 0) {
        snprintf(ctype, ctype_capacity, "double");
        if (snprintf(expression, expression_capacity, "fmax((%s), fmin((%s), (%s)))", converted[1], converted[2], converted[0]) >= (int)expression_capacity)
            fail(filename, line, "expressão matemática longa demais");
        return 1;
    }
    if (builtin->result_is_integer) snprintf(ctype, ctype_capacity, "int");
    else snprintf(ctype, ctype_capacity, "double");
    size_t used = (size_t)snprintf(expression, expression_capacity, "%s(", builtin->c_name);
    for (size_t i = 0; i < count; i++) {
        int wrote = snprintf(expression + used, expression_capacity - used,
                             "%s(%s)", i ? ", " : "", converted[i]);
        if (wrote < 0 || (size_t)wrote >= expression_capacity - used)
            fail(filename, line, "expressão matemática longa demais");
        used += (size_t)wrote;
    }
    if (used + 2 >= expression_capacity) fail(filename, line, "expressão matemática longa demais");
    expression[used++] = ')'; expression[used] = '\0';
    return 1;
}

/* Validadores reutilizados por futuras funções da biblioteca padrão. */
static int is_numeric_type(VarType type) {
    return type == TYPE_INT || type == TYPE_FLOAT;
}

static int is_scalar_type(VarType type) {
    return type == TYPE_STRING || type == TYPE_INT || type == TYPE_FLOAT || type == TYPE_BOOL;
}

static const char *c_type_name(VarType type) {
    switch (type) {
        case TYPE_STRING: return "const char *";
        case TYPE_INT: return "int";
        case TYPE_FLOAT: return "double";
        case TYPE_BOOL: return "bool";
        case TYPE_ARRAY: return "CbrValue";
    }
    return "void";
}

static const char *type_name_pt(VarType type) {
    switch (type) {
        case TYPE_STRING: return "texto";
        case TYPE_INT: return "inteiro";
        case TYPE_FLOAT: return "decimal";
        case TYPE_BOOL: return "booleano";
        case TYPE_ARRAY: return "array";
    }
    return "desconhecido";
}

static int same_numeric_family(VarType left, VarType right) {
    return is_numeric_type(left) && is_numeric_type(right);
}

static void ensure_numeric_argument(const char *filename, int line, const char *arg) {
    VarType type = parse_expression(arg, filename, line);
    if (!is_numeric_type(type)) {
        char message[256];
        snprintf(message, sizeof message, "esperado argumento numérico, recebido %s", type_name_pt(type));
        fail(filename, line, message);
    }
}

static int safe_copy_expression(const char *filename, int line, char *destination,
                                size_t capacity, const char *source) {
    int written = snprintf(destination, capacity, "%s", source);
    if (written < 0 || (size_t)written >= capacity) fail(filename, line, "expressão excede o limite permitido");
    return written;
}

static int is_reserved_builtin(const char *name) {
    static const char *names[] = {
        "juntar", "tamanho", "maiusculas", "minusculas", "contem",
        "subtexto", "substituir", "raiz", "absoluto", "arredondar",
        "piso", "teto", "seno", "cosseno", "tangente", "logaritmo",
        "log10", "exponencial", "maximo", "minimo", "potencia", "limitar"
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strcmp(name, names[i]) == 0) return 1;
    return 0;
}

static void emit_builtin_documentation(FILE *out) {
    fputs("/* Biblioteca padrão Cbr 1.0 Estável: texto, matemática, arrays e controle. */\n", out);
}

static void translate_assignment(const char *filename, int line, char *s, FILE *out) {
    /* Atribuição a elemento de array: valores podem trocar de tipo porque o array é heterogêneo. */
    char *first_eq = strchr(s, '=');
    if (first_eq && !(first_eq > s && (first_eq[-1] == '=' || first_eq[-1] == '!' || first_eq[-1] == '<' || first_eq[-1] == '>')) && first_eq[1] != '=') {
        char lhs[MAX_LINE];
        size_t lhs_len = (size_t)(first_eq - s);
        if (lhs_len < sizeof lhs) {
            memcpy(lhs, s, lhs_len); lhs[lhs_len] = '\0';
            char *left = trim(lhs);
            char *bracket = strchr(left, '[');
            char *close_bracket = bracket ? strchr(bracket + 1, ']') : NULL;
            if (bracket && close_bracket && trim(close_bracket + 1)[0] == '\0') {
                *bracket = '\0'; *close_bracket = '\0';
                char *array_name = trim(left); char *index_expr = trim(bracket + 1); char *rhs = trim(first_eq + 1);
                int ai = find_array(array_name);
                if (ai < 0) fail(filename, line, "atribuição por índice exige um array declarado");
                if (parse_expression(index_expr, filename, line) != TYPE_INT) fail(filename, line, "índice de array precisa ser inteiro");
                if (!*rhs) fail(filename, line, "valor de array ausente");
                if (is_string_literal(rhs)) fprintf(out, "    cbr_set(%s, %s_len, %s, cbr_string(%s));\n", array_name, array_name, index_expr, rhs);
                else if (strcmp(rhs, "true") == 0 || strcmp(rhs, "false") == 0) fprintf(out, "    cbr_set(%s, %s_len, %s, cbr_bool(%s));\n", array_name, array_name, index_expr, rhs);
                else {
                    VarType rt = parse_expression(rhs, filename, line);
                    convert_power(rhs);
                    if (rt == TYPE_INT) fprintf(out, "    cbr_set(%s, %s_len, %s, cbr_int(%s));\n", array_name, array_name, index_expr, rhs);
                    else if (rt == TYPE_FLOAT) fprintf(out, "    cbr_set(%s, %s_len, %s, cbr_float(%s));\n", array_name, array_name, index_expr, rhs);
                    else fail(filename, line, "valor não suportado para atribuição em array");
                }
                return;
            }
        }
    }
    char *eq = strchr(s, '=');
    if (!eq) fail(filename, line, "instrução não reconhecida");
    if ((eq > s && (eq[-1] == '=' || eq[-1] == '!' || eq[-1] == '<' || eq[-1] == '>')) || eq[1] == '=')
        fail(filename, line, "atribuição inválida: use apenas '=' para atribuir um valor");

    *eq = '\0';
    char *name = trim(s);
    char *value = trim(eq + 1);
    if (!*name || !*value) fail(filename, line, "atribuição incompleta: esperado nome = expressão");
    if (!valid_identifier(name)) fail(filename, line, "nome de variável inválido");

    int existing = find_var(name);
    char builtin_ctype[32], builtin_expr[MAX_LINE * 2];
    int numeric_builtin = translate_numeric_builtin(filename, line, value, builtin_ctype, sizeof builtin_ctype, builtin_expr, sizeof builtin_expr);
    if (numeric_builtin) {
        VarType nt = strcmp(builtin_ctype, "int") == 0 ? TYPE_INT : TYPE_FLOAT;
        if (existing >= 0) {
            if (vars[existing].type != nt) fail(filename, line, "tipo incompatível na reatribuição matemática");
            fprintf(out, "    %s = %s;\n", name, builtin_expr);
        } else {
            add_var(filename, line, name, nt);
            fprintf(out, "    %s %s = %s;\n", builtin_ctype, name, builtin_expr);
        }
        return;
    }
    int builtin = translate_text_builtin(filename, line, "tamanho", value, builtin_ctype, sizeof builtin_ctype, builtin_expr, sizeof builtin_expr);
    if (!builtin) builtin = translate_text_builtin(filename, line, "juntar", value, builtin_ctype, sizeof builtin_ctype, builtin_expr, sizeof builtin_expr);
    if (!builtin) builtin = translate_text_builtin(filename, line, "maiusculas", value, builtin_ctype, sizeof builtin_ctype, builtin_expr, sizeof builtin_expr);
    if (!builtin) builtin = translate_text_builtin(filename, line, "minusculas", value, builtin_ctype, sizeof builtin_ctype, builtin_expr, sizeof builtin_expr);
    if (!builtin) builtin = translate_text_builtin(filename, line, "contem", value, builtin_ctype, sizeof builtin_ctype, builtin_expr, sizeof builtin_expr);
    if (!builtin) builtin = translate_text_builtin(filename, line, "subtexto", value, builtin_ctype, sizeof builtin_ctype, builtin_expr, sizeof builtin_expr);
    if (!builtin) builtin = translate_text_builtin(filename, line, "substituir", value, builtin_ctype, sizeof builtin_ctype, builtin_expr, sizeof builtin_expr);
    if (builtin) {
        VarType bt = strcmp(builtin_ctype, "int") == 0 ? TYPE_INT : strcmp(builtin_ctype, "bool") == 0 ? TYPE_BOOL : TYPE_STRING;
        if (existing >= 0) {
            if (vars[existing].type != bt) fail(filename, line, "tipo incompatível na reatribuição");
            fprintf(out, "    %s = %s;\n", name, builtin_expr);
        } else {
            add_var(filename, line, name, bt);
            fprintf(out, "    %s %s = %s;\n", builtin_ctype, name, builtin_expr);
        }
        return;
    }
    if (array_literal(value)) {
        translate_array_assignment(filename, line, name, value, out);
        return;
    }
    VarType type;
    if (is_string_literal(value)) {
        type = TYPE_STRING;
    } else {
        type = parse_expression(value, filename, line);
    }

    if (existing >= 0) {
        if (vars[existing].type != type) fail(filename, line, "tipo incompatível na reatribuição; não é permitido mudar o tipo da variável");
        convert_power(value);
        fprintf(out, "    %s = %s;\n", name, value);
        return;
    }

    add_var(filename, line, name, type);
    convert_power(value);
    switch (type) {
        case TYPE_STRING: fprintf(out, "    const char *%s = %s;\n", name, value); break;
        case TYPE_INT: fprintf(out, "    int %s = %s;\n", name, value); break;
        case TYPE_FLOAT: fprintf(out, "    double %s = %s;\n", name, value); break;
        case TYPE_BOOL: fprintf(out, "    bool %s = %s;\n", name, value); break;
        case TYPE_ARRAY: fail(filename, line, "uso inválido de array"); break;
    }
}

typedef enum {
    BLOCK_IF,
    BLOCK_ELSE,
    BLOCK_WHILE,
    BLOCK_FOR,
    BLOCK_SWITCH
} BlockType;

typedef struct {
    BlockType type;
    size_t vars_before;
} Block;

#define MAX_BLOCKS 128

static void translate_control_condition(const char *filename, int line, char *s,
                                        const char *keyword, FILE *out) {
    char *p = trim(s + strlen(keyword));
    if (*p != '(') fail(filename, line, "esperado '(' depois da palavra-chave");
    p++;
    char *close = strrchr(p, ')');
    if (!close) fail(filename, line, "faltou ')' na condição");
    *close = '\0';
    char *tail = trim(close + 1);
    if (strcmp(tail, "{") != 0) fail(filename, line, "esperado '{' depois da condição");
    char *condition = trim(p);
    if (!*condition) fail(filename, line, "condição vazia");
    VarType type = parse_expression(condition, filename, line);
    if (type != TYPE_BOOL) fail(filename, line, "a condição precisa resultar em booleano; use uma comparação");
    fprintf(out, "    %s (%s) {\n", keyword, condition);
}

static int translate_assignment_to_text(const char *filename, int line, char *source,
                                        char *buffer, size_t size, int allow_new) {
    char copy[MAX_LINE];
    if (strlen(source) >= sizeof copy) fail(filename, line, "atribuição longa demais");
    strcpy(copy, source);

    char *eq = strchr(copy, '=');
    if (!eq || (eq > copy && (eq[-1] == '=' || eq[-1] == '!' || eq[-1] == '<' || eq[-1] == '>')) || eq[1] == '=')
        fail(filename, line, "atribuição inválida dentro do for");
    *eq = '\0';
    char *name = trim(copy);
    char *value = trim(eq + 1);
    if (!valid_identifier(name) || !*value) fail(filename, line, "atribuição inválida dentro do for");

    int existing = find_var(name);
    VarType type = is_string_literal(value) ? TYPE_STRING : parse_expression(value, filename, line);
    if (existing >= 0) {
        if (vars[existing].type != type) fail(filename, line, "tipo incompatível na reatribuição");
        if (snprintf(buffer, size, "%s = %s", name, value) >= (int)size)
            fail(filename, line, "expressão for longa demais");
    } else {
        if (!allow_new) fail(filename, line, "a variável do for precisa ser declarada antes do loop");
        add_var(filename, line, name, type);
        const char *ctype = type == TYPE_INT ? "int" : type == TYPE_FLOAT ? "double" : type == TYPE_BOOL ? "bool" : "const char *";
        if (snprintf(buffer, size, "%s %s = %s", ctype, name, value) >= (int)size)
            fail(filename, line, "expressão for longa demais");
    }
    return 1;
}

static void translate_for(const char *filename, int line, char *s, FILE *out) {
    char *p = trim(s + 3);

    /* Forma simplificada inspirada em Python: for i in range(3) { */
    if (isalpha((unsigned char)*p) || *p == '_') {
        char *name_end = p;
        while (isalnum((unsigned char)*name_end) || *name_end == '_') name_end++;
        char saved = *name_end;
        *name_end = '\0';
        char name[MAX_NAME];
        snprintf(name, sizeof name, "%s", p);
        *name_end = saved;
        char *rest = trim(name_end);
        if (strncmp(rest, "in", 2) == 0 && isspace((unsigned char)rest[2])) {
            rest = trim(rest + 2);
            if (strncmp(rest, "range", 5) != 0 || rest[5] != '(')
                fail(filename, line, "use: for nome in range(fim) { ou range(inicio, fim) {");
            char *args = rest + 6;
            char *close = strchr(args, ')');
            if (!close) fail(filename, line, "faltou ')' em range()");
            *close = '\0';
            if (strcmp(trim(close + 1), "{") != 0)
                fail(filename, line, "esperado '{' depois de range()");
            if (find_var(name) >= 0) fail(filename, line, "a variável do for já existe; use outro nome");

            char *comma = strchr(args, ',');
            char *start = (char *)"0";
            char *stop;
            if (comma) {
                *comma = '\0';
                start = trim(args);
                stop = trim(comma + 1);
            } else {
                stop = trim(args);
            }
            if (!*start || !*stop) fail(filename, line, "range() exige range(fim) ou range(inicio, fim)");
            if (parse_expression(start, filename, line) != TYPE_INT ||
                parse_expression(stop, filename, line) != TYPE_INT)
                fail(filename, line, "range() aceita apenas expressões inteiras");
            add_var(filename, line, name, TYPE_INT);
            fprintf(out, "    for (int %s = %s; %s < (%s); %s++) {\n", name, start, name, stop, name);
            return;
        }
        *name_end = saved;
    }

    /* Compatibilidade temporária com o for clássico de C. */
    if (*p != '(') fail(filename, line, "use: for nome in range(fim) { ou range(inicio, fim) {");
    p++;
    char *close = strrchr(p, ')');
    if (!close) fail(filename, line, "faltou ')' em for");
    *close = '\0';
    if (strcmp(trim(close + 1), "{") != 0) fail(filename, line, "esperado '{' depois de for");
    char *first = strchr(p, ';');
    char *second = first ? strchr(first + 1, ';') : NULL;
    if (!first || !second) fail(filename, line, "for clássico: use for (inicialização; condição; atualização) {");
    *first = '\0'; *second = '\0';
    char *init = trim(p), *condition = trim(first + 1), *update = trim(second + 1);
    if (!*init || !*condition || !*update) fail(filename, line, "as três partes do for são obrigatórias");
    char init_c[MAX_LINE], update_c[MAX_LINE];
    translate_assignment_to_text(filename, line, init, init_c, sizeof init_c, 1);
    if (parse_expression(condition, filename, line) != TYPE_BOOL)
        fail(filename, line, "a condição do for precisa resultar em booleano");
    translate_assignment_to_text(filename, line, update, update_c, sizeof update_c, 0);
    fprintf(out, "    for (%s; %s; %s) {\n", init_c, condition, update_c);
}

static int is_boolean_literal(const char *text);

/* Operações mutáveis em arrays de capacidade 256: adicionar e remover. */
static int translate_array_mutation(const char *filename, int line, char *statement, FILE *out) {
    char *open = strchr(statement, '(');
    char *close = strrchr(statement, ')');
    if (!open || !close || *trim(close + 1) != '\0') return 0;
    *open = '\0';
    char *name = trim(statement);
    if (strcmp(name, "adicionar") != 0 && strcmp(name, "remover") != 0 && strcmp(name, "limpar") != 0) {
        *open = '(';
        return 0;
    }
    *close = '\0';
    char args[MAX_LINE];
    if (strlen(open + 1) >= sizeof args) fail(filename, line, "argumentos de array muito longos");
    strcpy(args, open + 1);
    char *parts[MAX_BUILTIN_ARGS];
    size_t count = split_call_arguments(filename, line, args, parts, MAX_BUILTIN_ARGS);
    if (strcmp(name, "limpar") == 0) {
        if (count != 1 || !valid_identifier(parts[0])) fail(filename, line, "use: limpar(array)");
        if (find_array(parts[0]) < 0) fail(filename, line, "limpar() exige um array declarado");
        fprintf(out, "    %s_len = 0;\n", parts[0]);
        return 1;
    }
    if (strcmp(name, "adicionar") == 0) {
        if (count != 2 || !valid_identifier(parts[0])) fail(filename, line, "use: adicionar(array, valor)");
        if (find_array(parts[0]) < 0) fail(filename, line, "adicionar() exige um array declarado");
        char *value = parts[1];
        if (is_string_literal(value)) fprintf(out, "    cbr_append(%s, &%s_len, 256, cbr_string(%s));\n", parts[0], parts[0], value);
        else if (is_boolean_literal(value)) fprintf(out, "    cbr_append(%s, &%s_len, 256, cbr_bool(%s));\n", parts[0], parts[0], value);
        else {
            VarType type = parse_expression(value, filename, line);
            convert_power(value);
            if (type == TYPE_INT) fprintf(out, "    cbr_append(%s, &%s_len, 256, cbr_int(%s));\n", parts[0], parts[0], value);
            else if (type == TYPE_FLOAT) fprintf(out, "    cbr_append(%s, &%s_len, 256, cbr_float(%s));\n", parts[0], parts[0], value);
            else fail(filename, line, "adicionar() aceita texto, booleano ou número");
        }
        return 1;
    }
    if (count != 2 || !valid_identifier(parts[0])) fail(filename, line, "use: remover(array, indice)");
    if (find_array(parts[0]) < 0) fail(filename, line, "remover() exige um array declarado");
    if (parse_expression(parts[1], filename, line) != TYPE_INT) fail(filename, line, "índice de remover() precisa ser inteiro");
    fprintf(out, "    cbr_remove(%s, &%s_len, %s);\n", parts[0], parts[0], parts[1]);
    return 1;
}

static void translate_main_body(FILE *in, FILE *out, const char *filename) {
    char linebuf[MAX_LINE];
    int line_no = 0;
    int saw_end = 0;
    Block blocks[MAX_BLOCKS];
    size_t block_count = 0;
    int last_closed_if = 0;

    fputs("#include <stdio.h>\n#include <stdbool.h>\n#include <string.h>\n\nint main(void) {\n", out);

    while (fgets(linebuf, sizeof linebuf, in)) {
        line_no++;
        if (!strchr(linebuf, '\n') && !feof(in)) {
            fail(filename, line_no, "linha longa demais");
        }

        char *s = trim(linebuf);
        if (*s == '\0' || strncmp(s, "//", 2) == 0) continue;

        size_t n = strlen(s);
        if (n > 0 && s[n - 1] == ';') s[n - 1] = '\0';
        s = trim(s);

        if (strncmp(s, "#include", 8) == 0) continue;
        if (strcmp(s, "int main() {") == 0 || strcmp(s, "int main(void) {") == 0) continue;

        if (strcmp(s, "} else {") == 0) {
            if (block_count == 0 || blocks[block_count - 1].type != BLOCK_IF)
                fail(filename, line_no, "else precisa vir imediatamente depois de um if");
            Block b = blocks[--block_count];
            var_count = b.vars_before;
            if (block_count >= MAX_BLOCKS) fail(filename, line_no, "blocos aninhados demais");
            blocks[block_count++] = (Block){ BLOCK_ELSE, var_count };
            fputs("    } else {\n", out);
            last_closed_if = 0;
            continue;
        }

        if (strcmp(s, "}") == 0) {
            if (block_count > 0) {
                Block b = blocks[--block_count];
                fputs("    }\n", out);
                var_count = b.vars_before;
                last_closed_if = (b.type == BLOCK_IF);
            }
            continue;
        }

        if (strncmp(s, "else", 4) == 0 && (s[4] == '\0' || isspace((unsigned char)s[4]) || s[4] == '{')) {
            if (!last_closed_if) fail(filename, line_no, "else precisa vir imediatamente depois do fechamento de um if");
            if (strcmp(trim(s + 4), "{") != 0) fail(filename, line_no, "use else {");
            if (block_count >= MAX_BLOCKS) fail(filename, line_no, "blocos aninhados demais");
            blocks[block_count++] = (Block){ BLOCK_ELSE, var_count };
            fputs("    else {\n", out);
            last_closed_if = 0;
            continue;
        }

        if (strncmp(s, "switch", 6) == 0 && (s[6] == '(' || isspace((unsigned char)s[6]))) {
            if (block_count >= MAX_BLOCKS) fail(filename, line_no, "blocos aninhados demais");
            char *p = trim(s + 6);
            if (*p != '(') fail(filename, line_no, "use: switch (expressao) {");
            p++;
            char *close = strrchr(p, ')');
            if (!close) fail(filename, line_no, "faltou ')' no switch");
            *close = '\0';
            if (strcmp(trim(close + 1), "{") != 0) fail(filename, line_no, "esperado '{' depois do switch");
            char *expression = trim(p);
            VarType switch_type = parse_expression(expression, filename, line_no);
            if (switch_type != TYPE_INT && switch_type != TYPE_BOOL) fail(filename, line_no, "switch aceita uma expressão inteira ou booleana");
            fprintf(out, "    switch (%s) {\n", expression);
            blocks[block_count++] = (Block){ BLOCK_SWITCH, var_count };
            last_closed_if = 0;
            continue;
        }
        if (strncmp(s, "case", 4) == 0 && (s[4] == ' ' || s[4] == '\t')) {
            int in_switch = 0;
            for (size_t i = block_count; i > 0; i--) {
                if (blocks[i - 1].type == BLOCK_SWITCH) { in_switch = 1; break; }
                if (blocks[i - 1].type == BLOCK_WHILE || blocks[i - 1].type == BLOCK_FOR || blocks[i - 1].type == BLOCK_IF || blocks[i - 1].type == BLOCK_ELSE) continue;
            }
            if (!in_switch) fail(filename, line_no, "case só pode aparecer dentro de switch");
            char *label = trim(s + 4);
            char *colon = strchr(label, ':');
            if (!colon || *trim(colon + 1) != '\0') fail(filename, line_no, "use: case valor:");
            *colon = '\0'; label = trim(label);
            if (!is_integer_literal(label)) fail(filename, line_no, "case aceita apenas valores inteiros literais");
            fprintf(out, "    case %s:\n", label);
            last_closed_if = 0;
            continue;
        }
        if (strncmp(s, "default", 7) == 0) {
            if (s[7] != ':') fail(filename, line_no, "use: default:");
            if (*trim(s + 8) != '\0') fail(filename, line_no, "texto inesperado depois de default:");
            int in_switch = 0;
            for (size_t i = block_count; i > 0; i--) if (blocks[i - 1].type == BLOCK_SWITCH) { in_switch = 1; break; }
            if (!in_switch) fail(filename, line_no, "default só pode aparecer dentro de switch");
            fputs("    default:\n", out);
            last_closed_if = 0;
            continue;
        }
        if (strncmp(s, "if", 2) == 0 && (s[2] == '(' || isspace((unsigned char)s[2]))) {
            if (block_count >= MAX_BLOCKS) fail(filename, line_no, "blocos aninhados demais");
            blocks[block_count++] = (Block){ BLOCK_IF, var_count };
            translate_control_condition(filename, line_no, s, "if", out);
            last_closed_if = 0;
            continue;
        }
        if (strncmp(s, "while", 5) == 0 && (s[5] == '(' || isspace((unsigned char)s[5]))) {
            if (block_count >= MAX_BLOCKS) fail(filename, line_no, "blocos aninhados demais");
            blocks[block_count++] = (Block){ BLOCK_WHILE, var_count };
            translate_control_condition(filename, line_no, s, "while", out);
            last_closed_if = 0;
            continue;
        }
        if (strncmp(s, "for", 3) == 0 && (s[3] == '(' || isspace((unsigned char)s[3]))) {
            if (block_count >= MAX_BLOCKS) fail(filename, line_no, "blocos aninhados demais");
            size_t vars_before = var_count;
            translate_for(filename, line_no, s, out);
            blocks[block_count++] = (Block){ BLOCK_FOR, vars_before };
            last_closed_if = 0;
            continue;
        }
        if (strcmp(s, "break") == 0 || strcmp(s, "continue") == 0) {
            int inside_loop = 0, inside_breakable = 0;
            for (size_t i = block_count; i > 0; i--) {
                BlockType type = blocks[i - 1].type;
                if (type == BLOCK_WHILE || type == BLOCK_FOR) { inside_loop = 1; inside_breakable = 1; break; }
                if (type == BLOCK_SWITCH && strcmp(s, "break") == 0) { inside_breakable = 1; break; }
            }
            if (strcmp(s, "continue") == 0 && !inside_loop) fail(filename, line_no, "continue só pode ser usado dentro de loops");
            if (strcmp(s, "break") == 0 && !inside_breakable) fail(filename, line_no, "break só pode ser usado dentro de loops ou switch");
            fprintf(out, "    %s;\n", s);
            last_closed_if = 0;
            continue;
        }

        if (strncmp(s, "printf", 6) == 0 && (s[6] == '(' || isspace((unsigned char)s[6]))) {
            translate_printf(filename, line_no, s, out);
            last_closed_if = 0;
            continue;
        }

        if (strncmp(s, "scan", 4) == 0 && (s[4] == '(' || isspace((unsigned char)s[4]))) {
            translate_scan(filename, line_no, s, out);
            last_closed_if = 0;
            continue;
        }

        if (strncmp(s, "end", 3) == 0 && s[3] == '(') {
            if (block_count != 0) fail(filename, line_no, "end() não pode aparecer dentro de um bloco aberto");
            char *p = trim(s + 3);
            if (*p++ != '(') fail(filename, line_no, "end inválido");
            p = trim(p);
            if (*p != '0' && *p != '1' && *p != '2' && *p != '3' && *p != '4' && *p != '5' && *p != '6' && *p != '7' && *p != '8' && *p != '9' && *p != '-')
                fail(filename, line_no, "end() exige um código inteiro");
            char *close = strchr(p, ')');
            if (!close) fail(filename, line_no, "faltou ')' em end()");
            *close = '\0';
            char *tail = trim(close + 1);
            if (*tail != '\0') fail(filename, line_no, "texto inesperado depois de end()");
            char *code = trim(p);
            if (!is_integer_literal(code)) fail(filename, line_no, "código de saída inválido");
            fprintf(out, "    return %s;\n", code);
            saw_end = 1;
            last_closed_if = 0;
            continue;
        }

        if (strncmp(s, "adicionar(", 10) == 0 || strncmp(s, "remover(", 8) == 0 || strncmp(s, "limpar(", 7) == 0) {
            if (!translate_array_mutation(filename, line_no, s, out)) fail(filename, line_no, "operação de array inválida");
            last_closed_if = 0;
            continue;
        }

        if (strchr(s, '=')) {
            translate_assignment(filename, line_no, s, out);
            last_closed_if = 0;
            continue;
        }
        fail(filename, line_no, "instrução não reconhecida nesta versão");
    }

    if (block_count != 0) fail(filename, line_no, "faltou '}' para fechar um bloco");
    if (!saw_end) fputs("    return 0;\n", out);
    fputs("}\n", out);
}

static void parse_function_header(const char *filename, int line, char *s, Function *fn) {
    char *p = trim(s + strlen("function"));
    char *open = strchr(p, '(');
    char *close = open ? strchr(open + 1, ')') : NULL;
    if (!open || !close || strcmp(trim(close + 1), "{") != 0)
        fail(filename, line, "use: function nome(parametro1, parametro2) {");
    *open = '\0'; *close = '\0';
    char *name = trim(p);
    char params_text[MAX_LINE];
    if (strlen(open + 1) >= sizeof params_text) fail(filename, line, "lista de parâmetros muito longa");
    strcpy(params_text, open + 1);
    if (!valid_identifier(name)) fail(filename, line, "nome de função inválido");
    if (find_function(name) >= 0) fail(filename, line, "função já declarada");
    if (function_count >= MAX_FUNCTIONS) fail(filename, line, "limite de funções excedido");
    memset(fn, 0, sizeof *fn);
    snprintf(fn->name, sizeof fn->name, "%s", name);
    fn->source_line = line;
    char *param_parts[8];
    char *trimmed_params = trim(params_text);
    size_t count = *trimmed_params ? split_call_arguments(filename, line, trimmed_params, param_parts, 8) : 0;
    if (count > 8) fail(filename, line, "função aceita no máximo oito parâmetros");
    for (size_t i = 0; i < count; i++) {
        if (!valid_identifier(param_parts[i])) fail(filename, line, "nome de parâmetro inválido");
        for (size_t j = 0; j < i; j++) if (strcmp(param_parts[i], param_parts[j]) == 0) fail(filename, line, "parâmetro repetido na função");
        snprintf(fn->params[i], sizeof fn->params[i], "%s", param_parts[i]);
    }
    fn->param_count = count;
}

/* Lê a fonte uma vez, recolhe funções e emite-as antes de main(). */
static void translate_file(FILE *in, FILE *out, const char *filename) {
    char **lines = calloc(MAX_SOURCE_LINES, sizeof *lines);
    if (!lines) { fprintf(stderr, "memória insuficiente\n"); exit(EXIT_FAILURE); }
    size_t count = 0;
    char buf[MAX_LINE];
    while (fgets(buf, sizeof buf, in)) {
        if (count >= MAX_SOURCE_LINES) fail(filename, (int)count + 1, "arquivo grande demais");
        lines[count] = malloc(strlen(buf) + 1);
        if (!lines[count]) { fprintf(stderr, "memória insuficiente\n"); exit(EXIT_FAILURE); }
        strcpy(lines[count], buf); count++;
    }

    /* Primeira passagem: registrar cabeçalhos para permitir chamadas após a definição. */
    for (size_t i = 0; i < count; i++) {
        char tmp[MAX_LINE]; snprintf(tmp, sizeof tmp, "%s", lines[i]); trim(tmp);
        if (strncmp(tmp, "function", 8) == 0 && isspace((unsigned char)tmp[8])) {
            Function fn; parse_function_header(filename, (int)i + 1, tmp, &fn);
            functions[function_count++] = fn;
        }
    }

    /* Recolher corpos de função; por ora, corpo aceita atribuições numéricas, printf e return. */
    for (size_t i = 0; i < count; i++) {
        char tmp[MAX_LINE]; snprintf(tmp, sizeof tmp, "%s", lines[i]); trim(tmp);
        if (!(strncmp(tmp, "function", 8) == 0 && isspace((unsigned char)tmp[8]))) continue;
        int fi = -1;
        char *name_start = trim(tmp + 8); char *op = strchr(name_start, '(');
        if (op) { *op = '\0'; fi = find_function(trim(name_start)); }
        if (fi < 0) fail(filename, (int)i + 1, "cabeçalho de função inválido");
        Function *fn = &functions[fi];
        int closed = 0;
        while (++i < count) {
            char bodyline[MAX_LINE]; snprintf(bodyline, sizeof bodyline, "%s", trim(lines[i]));
            if (strcmp(bodyline, "}") == 0) { closed = 1; break; }
            if (fn->body_lines >= MAX_FUNCTION_LINES) fail(filename, (int)i + 1, "função grande demais");
            snprintf(fn->body[fn->body_lines++], MAX_LINE, "%s", bodyline);
        }
        if (!closed) fail(filename, fn->source_line, "faltou '}' para fechar a função");
    }

    fputs("#include <stdio.h>\n#include <stdbool.h>\n#include <string.h>\n#include <stdlib.h>\n#include <ctype.h>\n#include <math.h>\n#include <stdint.h>\n\n", out);
    emit_builtin_documentation(out);
    fputs("typedef enum { CBR_INT, CBR_FLOAT, CBR_BOOL, CBR_STRING } CbrType;\n", out);
    fputs("typedef struct { CbrType type; union { long long i; double f; bool b; const char *s; } value; } CbrValue;\n", out);
    fputs("static inline CbrValue cbr_int(long long x) { CbrValue v; v.type=CBR_INT; v.value.i=x; return v; }\n", out);
    fputs("static inline CbrValue cbr_float(double x) { CbrValue v; v.type=CBR_FLOAT; v.value.f=x; return v; }\n", out);
    fputs("static inline CbrValue cbr_bool(bool x) { CbrValue v; v.type=CBR_BOOL; v.value.b=x; return v; }\n", out);
    fputs("static inline CbrValue cbr_string(const char *x) { CbrValue v; v.type=CBR_STRING; v.value.s=x; return v; }\n", out);
    fputs("static inline CbrValue cbr_at(const CbrValue *a, size_t n, long long i) { if(i<0 || (size_t)i>=n) { fprintf(stderr, \"Cbr: índice de array fora do limite\\n\"); exit(1); } return a[i]; }\n", out);
    fputs("static inline void cbr_set(CbrValue *a, size_t n, long long i, CbrValue v) { if(i<0 || (size_t)i>=n) { fprintf(stderr, \"Cbr: índice de array fora do limite\\n\"); exit(1); } a[i]=v; }\n", out);
    fputs("static inline void cbr_append(CbrValue *a, size_t *n, size_t capacity, CbrValue v) { if(*n>=capacity) { fprintf(stderr, \"Cbr: capacidade máxima do array atingida\\n\"); exit(1); } a[(*n)++]=v; }\n", out);
    fputs("static inline void cbr_remove(CbrValue *a, size_t *n, long long i) { if(i<0 || (size_t)i>=*n) { fprintf(stderr, \"Cbr: índice de remoção fora do limite\\n\"); exit(1); } for(size_t j=(size_t)i;j+1<*n;j++) a[j]=a[j+1]; (*n)--; }\n", out);
    fputs("static inline long long cbr_as_int(CbrValue v) { if(v.type!=CBR_INT) { fprintf(stderr, \"Cbr: elemento não é int\\n\"); exit(1); } return v.value.i; }\n", out);
    fputs("static inline double cbr_as_float(CbrValue v) { if(v.type==CBR_INT) return (double)v.value.i; if(v.type!=CBR_FLOAT) { fprintf(stderr, \"Cbr: elemento não é float\\n\"); exit(1); } return v.value.f; }\n", out);
    fputs("static inline bool cbr_as_bool(CbrValue v) { if(v.type!=CBR_BOOL) { fprintf(stderr, \"Cbr: elemento não é bool\\n\"); exit(1); } return v.value.b; }\n", out);
    fputs("static inline const char *cbr_as_string(CbrValue v) { if(v.type!=CBR_STRING) { fprintf(stderr, \"Cbr: elemento não é str\\n\"); exit(1); } return v.value.s; }\n\n", out);
    fputs("static inline size_t cbr_length(const char *s) { return strlen(s ? s : \"\"); }\n", out);
    fputs("static inline const char *cbr_join(const char *a, const char *b) { size_t x=strlen(a), y=strlen(b); char *r=(char*)malloc(x+y+1); if(!r){perror(\"malloc\"); exit(1);} memcpy(r,a,x); memcpy(r+x,b,y+1); return r; }\n", out);
    fputs("static inline const char *cbr_substr(const char *s, long long start, long long count) { size_t n=strlen(s?s:\"\"); if(start<0) start=0; if((size_t)start>n) start=(long long)n; if(count<0) count=0; size_t available=n-(size_t)start; size_t take=(size_t)count<available?(size_t)count:available; char *r=(char*)malloc(take+1); if(!r){perror(\"malloc\"); exit(1);} memcpy(r,s+start,take); r[take]=0; return r; }\n", out);
    fputs("static inline const char *cbr_replace(const char *s, const char *old, const char *replacement) { if(!s||!old||!replacement) return s?s:\"\"; size_t oldn=strlen(old), newn=strlen(replacement); if(oldn==0) return s; size_t count=0; const char *p=s; while((p=strstr(p,old))!=NULL){count++;p+=oldn;} size_t srcn=strlen(s); if(newn>oldn && count>(SIZE_MAX-srcn-1)/(newn-oldn)){fprintf(stderr,\"Cbr: texto substituído grande demais\\n\");exit(1);} size_t outn=newn>=oldn?srcn+count*(newn-oldn):srcn-count*(oldn-newn); char *r=(char*)malloc(outn+1); if(!r){perror(\"malloc\");exit(1);} const char *cur=s; char *dst=r; while((p=strstr(cur,old))!=NULL){size_t n=(size_t)(p-cur);memcpy(dst,cur,n);dst+=n;memcpy(dst,replacement,newn);dst+=newn;cur=p+oldn;} strcpy(dst,cur); return r; }\n", out);
    fputs("static inline const char *cbr_case(const char *s, int upper) { size_t n=strlen(s); char *r=(char*)malloc(n+1); if(!r){perror(\"malloc\"); exit(1);} for(size_t i=0;i<n;i++) r[i]=(char)(upper?toupper((unsigned char)s[i]):tolower((unsigned char)s[i])); r[n]=0; return r; }\n", out);
    fputs("#define cbr_upper(s) cbr_case((s), 1)\n#define cbr_lower(s) cbr_case((s), 0)\n\n", out);
    /* Protótipos */
    for (size_t i = 0; i < function_count; i++) {
        fprintf(out, "static double %s(", functions[i].name);
        for (size_t j = 0; j < functions[i].param_count; j++)
            fprintf(out, "%sdouble %s", j ? ", " : "", functions[i].params[j]);
        fputs(");\n", out);
    }
    fputc('\n', out);

    for (size_t i = 0; i < function_count; i++) {
        Function *fn = &functions[i];
        var_count = 0;
        fprintf(out, "static double %s(", fn->name);
        for (size_t j = 0; j < fn->param_count; j++) {
            add_var(filename, fn->source_line, fn->params[j], TYPE_FLOAT);
            fprintf(out, "%sdouble %s", j ? ", " : "", fn->params[j]);
        }
        fputs(") {\n", out);
        int has_return = 0;
        for (int j = 0; j < fn->body_lines; j++) {
            char stmt[MAX_LINE]; snprintf(stmt, sizeof stmt, "%s", fn->body[j]);
            int ln = fn->source_line + j + 1;
            if (!*stmt || strncmp(stmt, "//", 2) == 0) continue;
            size_t n = strlen(stmt); if (n && stmt[n-1] == ';') stmt[n-1] = '\0';
            char *t = trim(stmt);
            if (strncmp(t, "return", 6) == 0 && (t[6] == '\0' || isspace((unsigned char)t[6]))) {
                char *expr = trim(t + 6);
                if (!*expr) fail(filename, ln, "return exige uma expressão");
                VarType rt = parse_expression(expr, filename, ln);
                if (rt != TYPE_INT && rt != TYPE_FLOAT) fail(filename, ln, "return de função 0.6 exige valor numérico");
                convert_power(expr);
                fprintf(out, "    return (double)(%s);\n", expr); has_return = 1;
            } else if (strchr(t, '=')) {
                translate_assignment(filename, ln, t, out);
            } else if (strncmp(t, "printf", 6) == 0) {
                translate_printf(filename, ln, t, out);
            } else {
                fail(filename, ln, "função 0.6: use atribuição numérica, printf ou return");
            }
        }
        if (!has_return) fail(filename, fn->source_line, "função precisa ter return");
        fputs("}\n\n", out);
    }

    /* Reconstruir apenas as linhas fora das definições para o tradutor original. */
    FILE *main_src = tmpfile();
    if (!main_src) { perror("tmpfile"); exit(EXIT_FAILURE); }
    int inside = 0;
    for (size_t i = 0; i < count; i++) {
        char tmp[MAX_LINE]; snprintf(tmp, sizeof tmp, "%s", lines[i]); trim(tmp);
        if (!inside && strncmp(tmp, "function", 8) == 0 && isspace((unsigned char)tmp[8])) {
            inside = 1;
            fputc('\n', main_src);
            continue;
        }
        if (inside) {
            if (strcmp(tmp, "}") == 0) inside = 0;
            fputc('\n', main_src);
            continue;
        }
        fputs(lines[i], main_src);
    }
    rewind(main_src);
    /* Isolar o escopo global do programa dos parâmetros temporários das funções. */
    var_count = 0;
    array_count = 0;
    /* translate_main_body historically emits headers and main wrapper; strip duplicate headers. */
    translate_main_body(main_src, out, filename);
    fclose(main_src);
    for (size_t i = 0; i < count; i++) free(lines[i]);
    free(lines);
}


/* Utilidades internas de diagnóstico e análise. */
static int starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static int ends_with(const char *text, const char *suffix) {
    size_t text_len = strlen(text), suffix_len = strlen(suffix);
    return suffix_len <= text_len && memcmp(text + text_len - suffix_len, suffix, suffix_len) == 0;
}

static int count_char_outside_quotes(const char *text, char target) {
    int quoted = 0, escaped = 0, count = 0;
    for (const char *p = text; *p; p++) {
        if (escaped) escaped = 0;
        else if (quoted && *p == '\\') escaped = 1;
        else if (*p == '"') quoted = !quoted;
        else if (!quoted && *p == target) count++;
    }
    return count;
}

static int balanced_delimiters(const char *text, char open_char, char close_char) {
    int quoted = 0, escaped = 0, depth = 0;
    for (const char *p = text; *p; p++) {
        if (escaped) escaped = 0;
        else if (quoted && *p == '\\') escaped = 1;
        else if (*p == '"') quoted = !quoted;
        else if (!quoted && *p == open_char) depth++;
        else if (!quoted && *p == close_char && --depth < 0) return 0;
    }
    return !quoted && depth == 0;
}

static int is_blank_line(const char *text) {
    for (; *text; text++) if (!isspace((unsigned char)*text)) return 0;
    return 1;
}

static int is_comment_line(const char *text) {
    while (isspace((unsigned char)*text)) text++;
    return starts_with(text, "//");
}

static size_t count_registered_variables(void) {
    return var_count;
}

static size_t count_registered_arrays(void) {
    return array_count;
}

static size_t count_registered_functions(void) {
    return function_count;
}

static void reset_variable_scope(void) {
    var_count = 0;
}

static void reset_array_registry(void) {
    array_count = 0;
}

static int name_is_declared(const char *name) {
    return find_var(name) >= 0;
}

static int name_is_array(const char *name) {
    return find_array(name) >= 0;
}

static int name_is_function(const char *name) {
    return find_function(name) >= 0;
}

static size_t array_length_by_name(const char *filename, int line, const char *name) {
    int index = find_array(name);
    if (index < 0) fail(filename, line, "array não declarado");
    return arrays[index].length;
}

static VarType variable_type_by_name(const char *filename, int line, const char *name) {
    int index = find_var(name);
    if (index < 0) fail(filename, line, "variável não declarada");
    return vars[index].type;
}

static void validate_array_limit(const char *filename, int line, size_t length) {
    if (length == 0) fail(filename, line, "array vazio não é suportado nesta versão");
    if (length > 256) fail(filename, line, "array excede o limite de 256 elementos");
}

static int is_boolean_literal(const char *text) {
    return strcmp(text, "true") == 0 || strcmp(text, "false") == 0;
}

static int is_numeric_literal_start(const char *text) {
    return isdigit((unsigned char)*text) || (*text == '.' && isdigit((unsigned char)text[1]));
}

static const char *translator_version(void) {
    return "Cbr 1.0 Estável";
}

static void print_usage(FILE *stream, const char *program) {
    fprintf(stream, "Uso: %s arquivo.cbr [arquivo_gerado.c]\n", program);
    fprintf(stream, "Tradutor %s - gera C a partir de Cbr.\n", translator_version());
}

static void report_open_error(const char *path) {
    fprintf(stderr, "Não foi possível abrir '%s': %s\n", path, strerror(errno));
}

static void report_output_error(const char *path) {
    fprintf(stderr, "Não foi possível criar '%s': %s\n", path, strerror(errno));
}

static int write_all(FILE *stream, const char *text) {
    size_t length = strlen(text);
    return fwrite(text, 1, length, stream) == length;
}

static int read_line_checked(FILE *stream, char *buffer, size_t capacity) {
    if (!fgets(buffer, (int)capacity, stream)) return 0;
    size_t length = strlen(buffer);
    if (length && buffer[length - 1] != '\n' && !feof(stream)) return -1;
    return 1;
}

static void explain_supported_types(FILE *stream) {
    fputs("Tipos: str, int, float, bool e arrays mistos.\n", stream);
}

static void explain_control_flow(FILE *stream) {
    fputs("Controle: if/else, while, for range, break e continue.\n", stream);
}

static void explain_standard_library(FILE *stream) {
    fputs("Texto: juntar, tamanho, maiusculas, minusculas, contem, subtexto, substituir.\n", stream);
    fputs("Matemática: raiz, absoluto, arredondar, piso, teto, seno, cosseno, tangente, logaritmo, log10, exponencial, maximo, minimo, potencia, limitar.\n", stream);
}

static void explain_input_output(FILE *stream) {
    fputs("E/S: printf com interpolação e scan com tipo declarado pela variável.\n", stream);
}

static void explain_known_limits(FILE *stream) {
    fputs("Limites: funções de usuário aceitam até oito parâmetros numéricos; arrays têm capacidade fixa de 256 elementos.\n", stream);
    fputs("Ainda não há import, dicionários, try/catch ou depurador interativo.\n", stream);
}

static void print_language_help(FILE *stream) {
    explain_supported_types(stream);
    explain_control_flow(stream);
    explain_standard_library(stream);
    explain_input_output(stream);
    explain_known_limits(stream);
}

static int cbr_source_has_extension(const char *path) {
    return ends_with(path, ".cbr");
}

static int c_output_has_extension(const char *path) {
    return ends_with(path, ".c") || ends_with(path, ".h");
}

static void validate_cli_paths(const char *input, const char *output) {
    if (!*input || !*output) {
        fprintf(stderr, "Caminho de arquivo vazio.\\n");
        exit(EXIT_FAILURE);
    }
    if (strcmp(input, output) == 0) {
        fprintf(stderr, "Entrada e saída não podem ser o mesmo arquivo.\\n");
        exit(EXIT_FAILURE);
    }
}

static int file_is_readable(FILE *stream) {
    return stream != NULL && !ferror(stream);
}

static int file_is_writable(FILE *stream) {
    return stream != NULL && !ferror(stream);
}

static void print_success_message(const char *output) {
    printf("Cbr: tradução concluída -> %s\n", output);
}

static void print_failure_context(const char *filename, int line, const char *message) {
    fprintf(stderr, "%s:%d: erro: %s\\n", filename, line, message);
}

static void explain_array_rules(FILE *stream) {
    fputs("Arrays: [10, \\\"texto\\\", true, 3.14], índice iniciado em zero.\\n", stream);
    fputs("Atribuição: dados[0] = 42. Limites de índice são verificados em execução.\\n", stream);
}

static void explain_function_rules(FILE *stream) {
    fputs("Funções: function somar(a, b) { return a + b }.\\n", stream);
    fputs("A versão beta aceita até oito parâmetros numéricos e retorno numérico.\\n", stream);
}

static void explain_expression_rules(FILE *stream) {
    fputs("Expressões: +, -, *, /, %, **, comparações, &&, || e !.\\n", stream);
    fputs("Parênteses podem ser usados para controlar a precedência.\\n", stream);
}

static void print_full_help(FILE *stream) {
    print_language_help(stream);
    explain_array_rules(stream);
    explain_function_rules(stream);
    explain_expression_rules(stream);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(stdout, argv[0]);
        print_full_help(stdout);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("Cbr 1.0 Estável");
        return EXIT_SUCCESS;
    }
    if (argc < 2 || argc > 3) {
        print_usage(stderr, argv[0]);
        if (argc == 2 && strcmp(argv[1], "--help") == 0) print_full_help(stdout);
        return EXIT_FAILURE;
    }

    const char *input_name = argv[1];
    const char *output_name = argc == 3 ? argv[2] : "programa_gerado.c";
    validate_cli_paths(input_name, output_name);
    (void)cbr_source_has_extension;
    (void)c_output_has_extension;
    (void)file_is_readable;
    (void)file_is_writable;
    (void)write_all;
    (void)read_line_checked;
    (void)is_blank_line;
    (void)is_comment_line;
    (void)count_char_outside_quotes;
    (void)balanced_delimiters;
    (void)count_registered_variables;
    (void)count_registered_arrays;
    (void)count_registered_functions;
    (void)reset_variable_scope;
    (void)reset_array_registry;
    (void)name_is_declared;
    (void)name_is_array;
    (void)name_is_function;
    (void)array_length_by_name;
    (void)variable_type_by_name;
    (void)validate_array_limit;
    (void)is_boolean_literal;
    (void)is_numeric_literal_start;
    (void)is_reserved_builtin;
    (void)c_type_name;
    (void)is_scalar_type;
    (void)same_numeric_family;
    (void)ensure_numeric_argument;
    (void)safe_copy_expression;
    (void)print_failure_context;
    (void)report_open_error;
    (void)report_output_error;
    (void)print_success_message;

    FILE *in = fopen(input_name, "r");
    if (!in) {
        report_open_error(input_name);
        return EXIT_FAILURE;
    }

    FILE *out = fopen(output_name, "w");
    if (!out) {
        report_output_error(output_name);
        fclose(in);
        return EXIT_FAILURE;
    }

    translate_file(in, out, input_name);

    if (fclose(in) != 0 || fclose(out) != 0) {
        fprintf(stderr, "Erro ao fechar arquivos.\n");
        return EXIT_FAILURE;
    }

    print_success_message(output_name);
    return EXIT_SUCCESS;
}

/* -------------------------------------------------------------------------
 * Catálogo da biblioteca padrão Cbr 1.0 Estável
 * -------------------------------------------------------------------------
 * Texto:
 *   juntar(a, b)                  concatena dois textos;
 *   tamanho(texto)                conta bytes UTF-8, não caracteres Unicode;
 *   maiusculas(texto)             converte letras ASCII para maiúsculas;
 *   minusculas(texto)             converte letras ASCII para minúsculas;
 *   contem(texto, trecho)         verifica se um trecho existe;
 *   subtexto(texto, inicio, n)    extrai uma faixa de bytes;
 *   substituir(texto, a, b)       substitui todas as ocorrências de a por b.
 * Matemática:
 *   raiz(x), absoluto(x), arredondar(x), piso(x), teto(x);
 *   seno(x), cosseno(x), tangente(x), logaritmo(x), log10(x), exponencial(x);
 *   maximo(a,b), minimo(a,b), potencia(a,b), limitar(x,min,max).
 * Arrays:
 *   arrays mistos de capacidade 256, leitura tipada por interpolação,
 *   tamanho(array), adicionar(array, valor), remover(array, índice), limpar(array), alteração de elementos por índice e verificação de limites
 *   em tempo de execução.
 * Controle:
 *   if/else, while, for range, for clássico, break, continue e end(código).
 * Funções:
 *   funções de usuário com até oito parâmetros numéricos e retorno numérico.
 * Entrada/saída:
 *   printf com interpolação tipada e scan para int, float, bool e str.
 * Segurança e diagnósticos:
 *   identificadores validados, tipos verificados, limite de tamanho de fonte,
 *   limite de variáveis, limite de arrays, limite de funções e linhas de erro.
 *
 * Esta lista também serve como referência de manutenção para que as funções
 * adicionadas não fiquem sem documentação dentro do código-fonte.
 * ------------------------------------------------------------------------- */

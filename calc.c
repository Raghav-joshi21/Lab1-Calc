// Raghav Joshi 251ADB004
// https://github.com/Raghav-joshi21/calc-lab-home.git
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: https://chatgpt.com/share/6adfba14-39d4-83ed-8011-1f56524ba02a, K&R, The C Programming Language 2nd ed, section 4.3,chapter 7, or man 3 fopen, https://en.wikipedia.org/wiki/Shunting_yard_algorithm, Theodore Norvell, Parsing Expressions by Recursive Descent (online) details in comments where you used them>

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define OWNER_FIRST "raghav"
#define OWNER_LAST "joshi"
#define OWNER_ID "251ADB004" 

enum {
    TK_NUMBER,
    TK_OP,    // + - * / or ** here we reported them as single letter 'p'
    TK_OPEN,
    TK_CLOSE,
    TK_LINEEND,  // the'\n' that end the file or code  which cant be appeared in an expression at all  
    TK_JUNK      
};
typedef struct {
    int kind;
    size_t pos;    
    char op;       
    int is_real;   
    long long whole;
    double real;
} Token; // this is one piece of expression which handed back by pull_token() so our file never turned into list of tokens
//the scanner holds the whole files and one offset 
typedef struct {
    char *text;    // writable on purpose,and it see read_number()
    size_t size;
    size_t at;
} Scanner; //scanner holds the whole file and one offset into it and how postions are tracked 'at' is a offset from start of the file

static int is_digit(char c) { return c>= '0' && c <= '9'; }
//holds out one token a"\n is reported but not consumed 
static void read_number(Scanner *sc, Token *t)
{
    char *s = sc->text;
    size_t start = sc->at;
    // Accumulate in unsigned signed overflow is undefined behaviour in C while unsigned simply wraps.
    unsigned long long acc = 0; 
    while (sc->at < sc->size && is_digit(s[sc->at])) {
        acc = acc * 10+(unsigned long long)(s[sc->at] - '0');
        sc->at++;
    } 
    int looks_real =sc-> at+1<sc->size && s[sc->at] == '.'&& is_digit(s[sc->at + 1]);
    if (!looks_real) {
        t->kind = TK_NUMBER;
        t->is_real = 0;
        t->whole = (long long)acc;
        return;
    }
    sc->at++; 
    while (sc->at < sc->size && is_digit(s[sc->at])) sc->at++;
    // strtod needs a string that ends in '\0', but this number sits in the middle of the file buffer rather than copying the digits out 
    char keep = s[sc->at];
    s[sc->at] = '\0';
    t->real = strtod(s + start, NULL);
    s[sc->at] = keep;
    t->kind = TK_NUMBER;
    t->is_real = 1;
}
// Hands out one token. A '\n' is reported but NOT consumed, because the line driver needs to see it to know where the next line begins.

//AI(chatgpt): prompt: i forgot to add this line and ask this to chatgpt with error getting this error tried fixing but stuck
static Token pull_token(Scanner *sc)
{
    Token t;
    t.kind = TK_JUNK;
    t.op = 0;
    t.is_real = 0;
    t.whole = 0;
    t.real = 0.0;
    char *s = sc->text;
    while (sc->at < sc->size && (s[sc->at] == ' ' || s[sc->at] == '\t')) sc->at++;
    t.pos = sc->at + 1;
    // End of the line or end of a last line that has no '\n' In the second case the position works out as (file length+1) on its own
    if (sc->at >= sc->size || s[sc->at] == '\n') {
        t.kind = TK_LINEEND;
        return t;
    }
    char c = s[sc->at];
    if (is_digit(c)) {
        read_number(sc, &t);
        return t;
    }
    sc->at++;
    switch (c) {
    case '(':t.kind = TK_OPEN; break;
    case ')':t.kind = TK_CLOSE; break;
    case '+':case '-': case '/':
        t.kind=TK_OP;
        t.op=c;
        break;
    case '*':
        t.kind = TK_OP; //we have only 2 stars that touch are a powerful "2 * * 3" stays two separate
        if (sc->at < sc->size && s[sc->at] == '*') {
            sc->at++;
            t.op = 'p';
        } else {
        t.op = '*';
        }
        break;
    default:
        // Everything else # in the middle of a line included.
        t.kind = TK_JUNK;
        break;
    }
    return t;
}
// a value is an integer or a double never both Keeping the two apart instead of storing everything as double is what makes 7 / 2 give 3 while 7 / 2.0 gives 3.5 and it keeps big integers exact.
typedef struct {
    int is_real;
    long long whole;
    double real;
    size_t pos;
} Value;

static double to_double(Value v) { return v.is_real ? v.real : (double)v.whole; }


// Precedence
// '(' is kept on the operator stack as a marker and must never win a precedencecomparison, so it gets 0.
static int precedence(char op)
{
if (op=='p') return 3;
    if (op == '*'||op == '/') return 2;
    if (op == '+' ||op == '-') return 1;
    return 0;
}
static int binds_rightwards(char op) { return op == 'p'; }
// Instead of one function per precedence level, this is the two-stack method Why the precedence comes out right: an operator may only be applied once we know that nothing to its right binds tighter.
typedef struct {
    Value *val;
    size_t nval, val_cap;
    struct { char op; size_t pos; } *ops;
    size_t nops, ops_cap;
    size_t err; // 1-based position of the first error 0 while fine
} Engine;
//an operator cannot be applied until you know that nothing to its right binds more tightly When the + arrives you do not know that yet So operators have to wait somewhere, and that somewhere is a second stack.
static void out_of_memory(void)
{
    fprintf(stderr, "calc: out of memory\n");
    exit(1);
} 

// The stacks are grown by doubling and are reused for every line of every input file so a long run does not keep allocating.
static void push_value(Engine *e, Value v)
{
    if (e->nval == e->val_cap) {
        e->val_cap= e->val_cap ? e->val_cap * 2 : 32;
        Value *grown =realloc(e->val, e->val_cap * sizeof *grown);
        if (!grown) out_of_memory();
        e->val =grown;
    }
    e->val[e->nval++] = v;
}
static void push_op(Engine *e, char op, size_t pos)
{
    if (e->nops == e->ops_cap) {
        e->ops_cap = e->ops_cap ? e->ops_cap * 2 : 32;
        void *grown = realloc(e->ops, e->ops_cap * sizeof *e->ops);
        if (!grown) out_of_memory();
        e->ops = grown;
    }
    e->ops[e->nops].op = op;
    e->ops[e->nops].pos = pos;
    e->nops++;
}
// A float result has to be a real number
static int real_result(Engine *e, double d, size_t op_pos, Value *out)
{
    if (!isfinite(d)) {
        e->err = op_pos;
        return 0;
    }
    out->is_real = 1;
    out->real = d;
    out->whole = 0;
    return 1;
}

// x ** y for two integers. Squaring and halving the exponent costs O(log y)
// multiplications so even 1 ** 1000000000 returns at once instead of looping a billion times starting from 1 also gives x ** 0 == 1 for free 0 ** 0 is also included.
static long long int_power(long long base, long long exp)
{
    unsigned long long result = 1;
    unsigned long long b = (unsigned long long)base;
    unsigned long long n = (unsigned long long)exp;
    while (n) {
        if (n & 1) result *= b;
        b *= b;
     n >>= 1;
    }
    return (long long)result;
}

//AI(chatgpt) : prompt: what approch should i follow for apply_top 
static int apply_top(Engine *e) //applies the operator on the top of the stack to the top two values
{
    char op = e->ops[e->nops - 1].op;
    size_t op_pos = e->ops[e->nops - 1].pos;
    e->nops--;
    Value b = e->val[--e->nval];
    Value a = e->val[--e->nval];
    Value r;
    r.is_real = 0;
    r.whole = 0;
    r.real = 0.0;
    r.pos = a.pos; //the result keep the ;eft operand position so it stand for whole sub sub-expression
    int in_double = a.is_real || b.is_real;
    if (op == 'p') {
        if (!in_double) {
            if (b.whole < 0) {
                 e->err = b.pos;
                return 0;
            }// An integer can'not hold 2 ** -1. The exponent is the operand at fault so the error sits at the first character of the exponent
            r.whole = int_power(a.whole, b.whole);
        } else if (!real_result(e, pow(to_double(a), to_double(b)), op_pos, &r)) {
            return 0;
        }
        e->val[e->nval++] = r;
        return 1;
    }//

    if (op == '/') {
        // Dividing by zero is reported at the start of the right operand
        int divisor_is_zero = b.is_real ? (b.real == 0.0) : (b.whole == 0);
        if (divisor_is_zero) {
            e->err = b.pos;
            return 0;
        }
        if (!in_double) {
            r.whole = (b.whole == -1) ? (long long)(0ULL - (unsigned long long)a.whole): a.whole / b.whole;
            e->val[e->nval++] = r;
            return 1;
        }
    }
    if (!in_double) {
        unsigned long long x = (unsigned long long)a.whole;
        unsigned long long y = (unsigned long long)b.whole;
        if (op == '+')      r.whole = (long long)(x + y);
        else if (op == '-') r.whole = (long long)(x - y);
        else                r.whole = (long long)(x * y);
        e->val[e->nval++] = r;
        return 1;
    }
    double x = to_double(a), y = to_double(b), d;
    if (op == '+')      d = x + y;
    else if (op == '-') d = x - y;
    else if (op == '*') d = x * y;
    else                d = x / y;
    if (!real_result(e, d, op_pos, &r)) return 0;
    e->val[e->nval++] = r;
    return 1;
}
//Does the actual arithmetic if both operands are integers it computes in long long otherwise it converts both to double and computes there with real_result rejecting an infinite or NaN answer.

static int apply_until_open(Engine *e)
{
    while (e->nops && e->ops[e->nops - 1].op != '(') {
        if (!apply_top(e)) return 0;
    }
    return 1;
}// Applies every pending operator down to the nearest '(' marker or down to the bottom of the stack when there is none Each of those is reported at the token the parser could not use
static int eval_line(Scanner *sc, Engine *e, Value *out)
{
    e->nval = 0;
    e->nops = 0;
    int expect_value = 1;

    for (;;) {
        Token t = pull_token(sc);

        if (expect_value) {
            if (t.kind == TK_NUMBER) {
                Value v;
                v.is_real = t.is_real;
                v.whole = t.whole;
                v.real = t.real;
                v.pos = t.pos;
                if (v.is_real && !isfinite(v.real)) {
                    e->err = t.pos;
                    return 0;
                }
                push_value(e, v);
                expect_value = 0;
            } else if (t.kind == TK_OPEN) {
                push_op(e, '(', t.pos);
            } else {
                // No reduction is attempted first: a value is still missing so the pending operator on top could not be applied anyway
                e->err = t.pos;
                return 0;
            }
            continue;
        }
        if (t.kind == TK_OP) {
            int p = precedence(t.op);
            while (e->nops && e->ops[e->nops - 1].op != '(') {
                int q = precedence(e->ops[e->nops - 1].op);
                if (q > p || (q == p && !binds_rightwards(t.op))) {
                    if (!apply_top(e)) return 0;
                } else {
                    break;
                }
            }
            push_op(e, t.op, t.pos);
            expect_value = 1;
        } else if (t.kind == TK_CLOSE) {
            if (!apply_until_open(e)) return 0;
            if (e->nops == 0) {
                e->err = t.pos;
                return 0;
            }
            size_t open_pos = e->ops[--e->nops].pos;
            // The group now counts as starting at its '(' rather than at it's first number so "1 / (2 - 2)" blames the '(' as the right operand.
            e->val[e->nval - 1].pos = open_pos;
        } else if (t.kind == TK_LINEEND) {
            while (e->nops) {
                if (e->ops[e->nops - 1].op == '(') {
                    // A '(' is still open at the end of the line 
                    e->err = t.pos;
                    return 0;
                }
                if (!apply_top(e)) return 0;
            }
            *out = e->val[0];
            return 1;
        } else {
            // A finished value followed by something that cannot continue the expression: junk a second number or a '(' 
            if (!apply_until_open(e)) return 0;
            e->err=t.pos;
            return 0;
        }
    }
}

// Writes one result line per expression straight to `out` and returns 0 If anything fails returns the 1-based position of the first error and the caller throws away whatever was written

static size_t evaluate_text(char *text, size_t size, Engine *e, FILE *out)
{
    Scanner sc;
    sc.text=text;
    sc.size=size;
    sc.at =0;
    e->err =0;
    while (sc.at < size) {
        //it looks into the past lines and spaces to classify the line without consuming them
        size_t look = sc.at;
        while (look < size && (text[look] == ' ' || text[look] == '\t')) look++;
        if (look >= size) break;  
        if (text[look] == '\n') {   //produces no output
            sc.at = look + 1;
            continue;
        }
        if (text[look] == '#') {
            // faster than stepping character by character, and because we only never gets here: it reaches the scanner and becomes TK_JUNK
            char *nl = memchr(text + look, '\n', size - look);
            sc.at = nl ? (size_t)(nl - text) + 1 : size;
            continue;
        }
        sc.at = look;
        Value v;
        if (!eval_line(&sc, e, &v)) return e->err;

        char line[40];
        int n = v.is_real ? snprintf(line, sizeof line, "%.15g\n", v.real) : snprintf(line, sizeof line, "%lld\n", v.whole);
        fwrite(line, 1, (size_t)n, out);
        if (sc.at < size) sc.at++;     
    }
    return 0;
}//It walks the file line by line skipping blank and comment lines, hands every other line to eval_line and writes each result straight into the output file as it goes. It returns 0 if the whole file was fine

//AI(ChatGPT) : prompt   "i worte this to read files now should i proceed with mkdir as lab says that we have parent folder"
static char *slurp(const char *path, size_t *size_out)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "calc: cannot read '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "calc: '%s' is not a regular file\n", path);
        return NULL;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "calc: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    size_t cap = (size_t)st.st_size + 2;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) out_of_memory();
    for (;;) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *grown = realloc(buf, cap);
            if (!grown) out_of_memory();
            buf = grown;
        }
        size_t got = fread(buf + len, 1, cap - len - 1, fp);
        if (got == 0) break;
        len += got;
    }
    if (ferror(fp)) {
        fprintf(stderr, "calc: error reading '%s'\n", path);
        fclose(fp);
        free(buf);
        return NULL;
    }
    fclose(fp);
    buf[len] = '\0';   // the spare byte used by read_number()
    *size_out = len;
    return buf;
}//this reads the whole file into one buffer with one spare byte at the end the size come to us from stat and then everything afterwards workson this one buffer we used offset so that no part of code can be copied again 

static char *copy_part(const char *s, size_t n)
{
    char *d = malloc(n + 1);
    if (!d) out_of_memory();
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}
// Joins up to four pieces NULL pieces are treated as empty paths are built on the heap so that a long folder name cannot silently get cut off which is what
static char *join(const char *a, const char *b, const char *c, const char *d)
{
    size_t na = a ? strlen(a) : 0, nb = b ? strlen(b) : 0;
    size_t nc = c ? strlen(c) : 0, nd = d ? strlen(d) : 0;
    char *s = malloc(na + nb + nc + nd + 1);
    if (!s) out_of_memory();
    char *p = s;
    if (a) { memcpy(p, a, na); p += na; }
    if (b) { memcpy(p, b, nb); p += nb; }
    if (c) { memcpy(p, c, nc); p += nc; }
    if (d) { memcpy(p, d, nd); p += nd; }
    *p = '\0';
    return s;
}
static int ends_in_txt(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && strcmp(name + n - 4, ".txt") == 0;
}
// "labs/task1.txt" gives "task1", and with want_dir set "labs/" gives "labs".
static char *stem_of(const char *path, int want_dir)
{
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') end--;   // ignore trailing slashes
    size_t start = end;
    while (start > 0 && path[start - 1] != '/') start--;

    char *name = copy_part(path + start, end - start);
    if (!want_dir && ends_in_txt(name)) name[strlen(name) - 4] = '\0';
    return name;
}
static int make_folder(const char *path)
{
    if (mkdir(path, 0777) == 0) return 1;
    if (errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
        fprintf(stderr, "calc: '%s' is not a folder\n", path);
        return 0;
    }
    fprintf(stderr, "calc: cannot create folder '%s': %s\n", path, strerror(errno));
    return 0;
} // The lab guarantees the parent folder exists so one mkdir is enough and output folder that is already there evaluates one input file into <outdir>/<stem>_<first>_<last>_<id>.txt.
// Returns 1 when the file was handled, 0 only on a real failure; an ERROR:<pos>

static int handle_file(const char *in_path, const char *outdir, Engine *e)
{
    size_t size = 0;
    char *text = slurp(in_path, &size);
    if (!text) return 0;              
    if (!make_folder(outdir)) {
        free(text);
        return 0;
    }
    char *stem = stem_of(in_path, 0);
    char *path = join(outdir, "/", stem,"_" OWNER_FIRST "_" OWNER_LAST "_" OWNER_ID ".txt");
    free(stem);
    int ok = 1;
    FILE *out = fopen(path, "wb");
    if (!out) {
        fprintf(stderr, "calc: cannot write '%s': %s\n", path, strerror(errno));
        ok = 0;
    } else {
        setvbuf(out, NULL, _IOFBF, 1 << 16);

        size_t err = evaluate_text(text, size, e, out);//results will direct go straight into the file instead of being collected in memory
       
        if (err) {
            if (fclose(out) != 0) ok = 0;
            out = fopen(path, "wb");
            if (!out) {
                fprintf(stderr, "calc: cannot write '%s': %s\n", path, strerror(errno));
                ok = 0;
            } else {
                fprintf(out, "ERROR:%zu\n", err);
            }
        }//if we have error anyewhere that means the has only this line so everything will be dropped 
        if (out) {
            if (ferror(out)) ok = 0;
            if (fclose(out) != 0) ok = 0;
            if (!ok) fprintf(stderr, "calc: error writing '%s'\n", path);
        }
    }
    free(path);
    free(text);
    return ok;
}
 //if anything fails in the file it only give one error line and no partial results 
static int by_name(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
// Evaluates every *.txt file directly inside `dir`. Subfolders are not followed.
static int handle_dir(const char *dir, const char *outdir, Engine *e)
{
    DIR *dp = opendir(dir);
    if (!dp) {
        fprintf(stderr, "calc: cannot open folder '%s': %s\n", dir, strerror(errno));
        return 0;
    }
    // The names are collected and the folder is closed before anything is written if the output folder happen to be the input folder our own result files must not turn up in the listing we are still walking.
    char **names = NULL;
    size_t count = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (!ends_in_txt(de->d_name)) continue;
        char *full = join(dir, "/", de->d_name, NULL);
        struct stat st;
        int regular = stat(full, &st) == 0 && S_ISREG(st.st_mode);  // we skipped a folder named x.txt 
        free(full);
        if (!regular) continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 16;
            char **grown = realloc(names, cap * sizeof *grown);
            if (!grown) out_of_memory();
            names = grown;
        }
        names[count++] = join(de->d_name, NULL, NULL, NULL);
    }
    closedir(dp);

    // readdir hands names out in whatever order the filesystem likes
    if (count > 1) qsort(names, count, sizeof *names, by_name);
    int ok = make_folder(outdir);
    for (size_t i = 0; i < count; i++) {
        if (ok){ char *full = join(dir, "/", names[i], NULL);
            if (!handle_file(full, outdir, e)) ok = 0;
            free(full);
        }
        free(names[i]);
    }
    free(names);
    return ok;
}
static int show_usage(void)
{
    fprintf(stderr, "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
    return 2;
}
int main(int argc, char **argv)
{
    const char *dir = NULL;
    const char *outdir = NULL;
    const char *input = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int takes_dir = strcmp(a, "-d") == 0 || strcmp(a, "--dir") == 0;
        int takes_out = strcmp(a, "-o") == 0 || strcmp(a, "--output-dir") == 0;
        if (takes_dir || takes_out) {
            if (i + 1 >= argc) {
                fprintf(stderr, "calc: '%s' needs a folder after it\n", a);
                return show_usage();
            }
            if (takes_dir) dir = argv[++i];
            else outdir = argv[++i];
        } else if (a[0] == '-') {
            fprintf(stderr, "calc: unknown option '%s'\n", a);
            return show_usage();
        } else if (input) {
            fprintf(stderr, "calc: only one input file can be given\n");
            return show_usage();
        } else {
            input = a;
        }
    }
    // we need Exactly one source of input is needed a file or a folder
    if ((dir == NULL) == (input == NULL)) {
        fprintf(stderr, dir ? "calc: give an input file or -d FOLDER, not both\n"
                            : "calc: nothing to do, no input file or -d FOLDER given\n");
        return show_usage();
    }
    // Without -o the output folder is <input_base>_<name>_<studentid> here.
    char *fallback = NULL;
    if (!outdir) {
        char *stem = stem_of(dir ? dir : input, dir != NULL);
        fallback = join(stem, "_" OWNER_FIRST "_" OWNER_ID, NULL, NULL);
        free(stem);
        outdir = fallback;
    }

    Engine e;
    memset(&e, 0, sizeof e);

    int ok = dir ? handle_dir(dir, outdir, &e) : handle_file(input, outdir, &e);

    free(e.val);
    free(e.ops);
    free(fallback);
    return ok ? 0 : 1;
}
/**
 * gcov lines checker
 * (author murata.muu@gmail.com)
 * 2009.12.22 new
 * 2010.01.28 deleteのみの差分ソースは無視
 * 2010.01.29 diffall fmt
 * 2010.04.05 ファイル名Index:のみでdiffの実体がない場合は対象外
 * 2010.05.31 .gcov.diff を作成しない
 *            svn diff フォーマットに対応
 *            diffファイルフォーマット自動選択
 *            default diffファイル名を追加
 *            gcov timestamp チェック
 * 2011.02.10 c1 branch coverageに対応
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>

#define DEFAULT_DIFF_FILENAME "diff.txt"
#define LINEBUFSZ 1024
#define FILENAMESZ 256
#define GCOV_COMMAND "/usr/bin/gcov"

enum _diff_fmt {
    UNKNOWN_FMT, /* 20100531 */
    CVS_FMT,
    DIFF_FMT,
    SVN_FMT,     /* 20100531 */
};

enum _gcov_level { /* 20110210 */
    UNKNOWN_LEVEL,
    C0_LINE_LEVEL,
    C1_BRANCH_LEVEL,
};

/**
 * structure define
 */
struct _line_data {
    int start;
    int end;
    struct _line_data *next;
};
typedef struct _line_data LINE_DATA;

struct _diff_data {
    char src[FILENAMESZ];
    LINE_DATA *line;
    struct _diff_data *next;
};
typedef struct _diff_data DIFF_DATA;

struct _gcov_line_buf {
    char *top;
    unsigned long max;
    unsigned long pos;
    unsigned long refpos;
};
typedef struct _gcov_line_buf GCOV_LINE_BUF; /* 20100531 */

struct _gcov_branch_data {
    int s_pos;
    int e_pos;
    struct _gcov_branch_data *next;
};
typedef struct _gcov_branch_data GCOV_BRANCH_DATA; /* 20110210 */

struct _gcov_line_data {
    int s_pos;
    int e_pos;
    GCOV_BRANCH_DATA *branch;
    int branch_pass;
    int branch_notpass;
    struct _gcov_line_data *next;
};
typedef struct _gcov_line_data GCOV_LINE_DATA; /* 20110210 */

struct _gcov_data {
    char gcov[FILENAMESZ];
    GCOV_LINE_BUF linebuf;
    GCOV_LINE_DATA *line; /* 20110210 */
    double line_parcent;
    double branch_parcent; /* 20110210 */
    int line_pass;
    int line_notpass;
    int branch_pass; /* 20110210 */
    int branch_notpass;
    struct _gcov_data *next;
};
typedef struct _gcov_data GCOV_DATA;

struct _read_buf {
    char prev[LINEBUFSZ];
    char crnt[LINEBUFSZ];
    char next[LINEBUFSZ];
};
typedef struct _read_buf READ_BUF;

struct _option {
    int diff_fmt;
    char *file;
    int level; /* 20110210 */
};
typedef struct _option OPTION;

/**
 * local function
 */
void create_diff_data(OPTION *opt, DIFF_DATA **top);
void parse_diff_src(char *p, char *name, int fmt);
void create_line_data(FILE *fp, READ_BUF *readbuf, LINE_DATA **top, int fmt);
void parse_diff_lineno(char *line, int *start, int *end);
void free_diff_data(DIFF_DATA *p);
void free_line_data(LINE_DATA *p);
void debug_print_diff_data(DIFF_DATA *diff);
int readline(char *buf, READ_BUF *p, FILE *fp);
void cut_LF(char *p);
int is_start_diff_section(READ_BUF *p, int fmt);
int is_end_diff_section(READ_BUF *p, int fmt);
int is_diff_summary_line(READ_BUF *p, int fmt);
void create_gcov_data(DIFF_DATA *diff, GCOV_DATA **top);
void free_gcov_data(GCOV_DATA *p);
void free_gcov_line_data(GCOV_LINE_DATA *p);
void free_gcov_branch_data(GCOV_BRANCH_DATA *p);
void calc_gcov(GCOV_DATA *p);
void parcent(GCOV_DATA *p);
void print_gcov(GCOV_DATA *p, int level);
void print_notpass_line(GCOV_DATA *p, int level);
int get_option(int argc, char **argv, OPTION *opt);
void debug_print_option(OPTION *opt);
void print_usage(char *cmd_name);
int get_diff_format(char *filename);
void create_line_data_for_svn(FILE *fp, READ_BUF *readbuf, LINE_DATA **top);
int is_svndiff_summary(char *line);
int is_svndiff_start(READ_BUF *p);
int is_svndiff_end(READ_BUF *p);
int get_svndiff_baseline(char *line);
unsigned long gcov_line_data_copy(GCOV_LINE_BUF *p, char *data, unsigned long sz, unsigned long *s_pos, unsigned long *e_pos);
int gcov_line_gets(GCOV_LINE_BUF *p, char *out, unsigned long outsz);
int gcov_line_get_by_pos(GCOV_LINE_BUF *p, char *out, unsigned long outsz, unsigned long s_pos, unsigned long e_pos);
void gcov_line_refreset(GCOV_LINE_BUF *p);
int need_gcov_update(DIFF_DATA *diff);
int gcov_update(int level);

/**
 * main
 */
int main(int argc, char **argv)
{
    OPTION opt;
    DIFF_DATA *diff;
    GCOV_DATA *gcov;

    memset(&opt, 0, sizeof(opt));
    if (get_option(argc, argv, &opt) != 0) {
        print_usage(argv[0]);
        return -1;
    }
    /* debug_print_option(&opt); */

    if (opt.diff_fmt == UNKNOWN_FMT) {
        if ((opt.diff_fmt = get_diff_format(opt.file)) == UNKNOWN_FMT) { /* 20100531 */
            print_usage(argv[0]);
            return -1;
        }
    }

    diff = NULL;
    create_diff_data(&opt, &diff);
    if (diff == NULL) return -1;
    /* debug_print_diff_data(diff); */

    if (need_gcov_update(diff)) {
        if (gcov_update(opt.level) == 0) {
            free_diff_data(diff);
            return 0;
        }
    }

    gcov = NULL;
    create_gcov_data(diff, &gcov);
    if (gcov == NULL) return -1;
    calc_gcov(gcov);
    print_gcov(gcov, opt.level);
    free_gcov_data(gcov);

    free_diff_data(diff);

    return 0;
}

/**
 * create diff data
 */
void create_diff_data(OPTION *opt, DIFF_DATA **top)
{
    FILE *fp;
    char linebuf[LINEBUFSZ];
    READ_BUF readbuf;
    DIFF_DATA *p, *p_prev;

    if ((fp = fopen(opt->file, "r")) == NULL) return;

    memset(&readbuf, 0, sizeof(readbuf));

    while(1) {
        memset(linebuf, 0, sizeof(linebuf));
        if (readline(linebuf, &readbuf, fp) == -1) {fclose(fp); return;} /* eof */

        if (is_start_diff_section(&readbuf, opt->diff_fmt)) {
            if ((p = (DIFF_DATA *)malloc(sizeof(DIFF_DATA))) == NULL) continue;

            memset(p, 0, sizeof(DIFF_DATA));
            parse_diff_src(linebuf, p->src, opt->diff_fmt);

            if (opt->diff_fmt == SVN_FMT) {
                create_line_data_for_svn(fp, &readbuf, &p->line);
            } else {
                create_line_data(fp, &readbuf, &p->line, opt->diff_fmt);
            }
            if (p->line == NULL) { free(p); continue; } /* diff is only 'd' */

            if (*top == NULL) {
                *top = p;
                p_prev = p;
            } else {
                p_prev->next = p;
                p_prev = p;
            }
        }
    }
}

/**
 * parse diff src name
 * ex Index: aaa.c -> aaa.c
 */
void parse_diff_src(char *p, char *name, int fmt)
{
    if (fmt == CVS_FMT || fmt == SVN_FMT) {
        if (p = strstr(p, "Index:")) {
            p = p + strlen("Index:");
            p++; /* space */
            strcpy(name, p);
        }
    } else {
        strcpy(name, p);
    }
}

/**
 * create line data
 */
void create_line_data(FILE *fp, READ_BUF *readbuf, LINE_DATA **top, int fmt)
{
    char linebuf[LINEBUFSZ];
    LINE_DATA *p, *p_prev;

    while(1) {
        if (is_end_diff_section(readbuf, fmt)) return; /* add murata 20100405 */

        memset(linebuf, 0, sizeof(linebuf));
        if (readline(linebuf, readbuf, fp) == -1) return; /* eof */

        if (isdigit(linebuf[0])) {
            if ((p = (LINE_DATA *)malloc(sizeof(LINE_DATA))) == NULL) continue;

            memset(p, 0, sizeof(LINE_DATA));
            parse_diff_lineno(linebuf, &p->start, &p->end);
            if (p->start == 0 && p->end == 0) { free(p); continue; } /* 'd' */

            if (*top == NULL) {
                *top = p;
                p_prev = p;
            } else {
                p_prev->next = p;
                p_prev = p;
            }
        }
        if (is_end_diff_section(readbuf, fmt)) return;
    }
}

/**
 * parse diff line
 * ex1. 30a31,32 -> start = 31, end = 32
 * ex2. 22,30d22 -> start = 22, end = 22
 */
void parse_diff_lineno(char *line, int *start, int *end)
{
    int i;
    char *start_p, *end_p;

    for (i = 0; i < strlen(line); i++) {
        if (line[i] == 'a' || line[i] == 'c') {
            start_p = line+i+1;
            end_p = strchr(start_p, ',');
            if (end_p != NULL) {
                end_p++;
            } else {
                end_p =start_p;
            }
            *start = atoi(start_p);
	        *end   = atoi(end_p);
            break;
        }
    }
    return;
}

/**
 * diff data memory free
 */
void free_diff_data(DIFF_DATA *p)
{
    DIFF_DATA *p_next;

    for (; p; p = p_next) {
        free_line_data(p->line);
        p_next = p->next;
        free(p);
    }
}

/**
 * line data memory free
 */
void free_line_data(LINE_DATA *p)
{
    LINE_DATA *p_next;

    for (; p; p = p_next) {
        p_next = p->next;
        free(p);
    }
}

/**
 * debug print diff data (unused)
 */
void debug_print_diff_data(DIFF_DATA *diff)
{
    LINE_DATA *line;

    for (; diff; diff = diff->next) {
        printf("src[%s]\n", diff->src);
        line = diff->line;
        for (; line; line = line->next)
            printf("start[%d] end[%d]\n", line->start, line->end);
    }
}

/**
 * file read line , next line
 * 0: ok, -1: eof
 */
int readline(char *buf, READ_BUF *p, FILE *fp)
{
    /* crnt -> prev copy */
    memcpy(p->prev, p->crnt, sizeof(p->crnt));
    memset(p->crnt, 0, sizeof(p->crnt));

    /* next -> crnt copy */
    memcpy(p->crnt, p->next, sizeof(p->next));
    memset(p->next, 0, sizeof(p->next));

    if (p->crnt[0] == '\0') {
        if (fgets(p->crnt, sizeof(p->crnt)-1, fp) == NULL) {
            return -1; /* eof */
        }
        cut_LF(p->crnt);
    }
    if (p->next[0] == '\0') {
        if (fgets(p->next, sizeof(p->next)-1, fp) == NULL) {
            /* next readline is eof */
        }
        cut_LF(p->next);
    }
    memcpy(buf, p->crnt, strlen(p->crnt));
    return 0;
}

/**
 * cut LF
 */
void cut_LF(char *p)
{
    if (p[strlen(p)-1] == '\n') {
        p[strlen(p)-1] = '\0';
    }
}
/**
 * check diff start section
 * 1: start
 * 0: not start
 */
int is_start_diff_section(READ_BUF *p, int fmt)
{
   if (fmt == CVS_FMT || fmt == SVN_FMT) {
       if (p->crnt[0] == 'I')
           if (strstr(p->crnt, "Index:") != NULL) return 1;
   } else {
       if (isalpha(p->crnt[0]) && isdigit(p->next[0])) return 1;
   }
   return 0;
}

/**
 * check diff end section
 * 1: end
 * 0: not end
 */
int is_end_diff_section(READ_BUF *p, int fmt)
{
   if (fmt == CVS_FMT || fmt == SVN_FMT) {
       if (p->next[0] == 'I')
           if (strstr(p->next, "Index:") != NULL) return 1;
   } else {
       if (isalpha(p->next[0])) return 1;
   }
   return 0;
}

/*************** diff list -> gcov list **************/

/**
 * create gcov file with diff merge, and create gcov data list
 */
void create_gcov_data(DIFF_DATA *diff, GCOV_DATA **top)
{
    FILE *fp;
    char linebuf[LINEBUFSZ];
    READ_BUF readbuf; /* 20110210 */
    char *c1, *c2;
    char linestr[32];
    int lineno;
    LINE_DATA *line;
    GCOV_DATA *p, *p_prev;
    GCOV_LINE_DATA *pl, *pl_prev;
    GCOV_BRANCH_DATA *pb, *pb_prev;
    unsigned long s_pos, e_pos;

    for (; diff; diff = diff->next) {

        if ((p = (GCOV_DATA *)malloc(sizeof(GCOV_DATA))) == NULL) break;
        memset(p, 0, sizeof(GCOV_DATA));

        strcat(p->gcov, diff->src);
        strcat(p->gcov, ".gcov");

        if ((fp = fopen(p->gcov, "r")) == NULL) { free(p); continue; }
        memset(&readbuf, 0, sizeof(readbuf)); /* 20110210 */

        line = diff->line;
        for (; line; line = line->next) {
            while(1) {
                memset(linebuf, 0, sizeof(linebuf));
                if (readline(linebuf, &readbuf, fp) == -1) break; /* 20110210 */ /* eof */

                c1 = strchr(linebuf, ':');
                if (c1 == NULL) continue;
                c1++;
                c2 = strchr(c1, ':');
                memset(linestr, 0, sizeof(linestr));
                memcpy(linestr, c1, c2-c1);
                lineno = atoi(linestr);

                if (lineno < line->start) continue;

                if (gcov_line_data_copy(&p->linebuf, linebuf, strlen(linebuf), &s_pos, &e_pos) == 0) break;

                /* -- add 20110210 */
                if ((pl = (GCOV_LINE_DATA *)malloc(sizeof(GCOV_LINE_DATA))) == NULL) break;
                memset(pl, 0, sizeof(GCOV_LINE_DATA));
                pl->s_pos = s_pos;
                pl->e_pos = e_pos;

                while (readbuf.next[0] != ' ') {
                    memset(linebuf, 0, sizeof(linebuf));
                    if (readline(linebuf, &readbuf, fp) == -1) break; /* eof */

                    if (linebuf[0] == 'b' && strstr(linebuf, "branch")) {
                        if (gcov_line_data_copy(&p->linebuf, linebuf, strlen(linebuf), &s_pos, &e_pos) == 0) break;
                        if ((pb = (GCOV_BRANCH_DATA *)malloc(sizeof(GCOV_BRANCH_DATA))) == NULL) break;
                        memset(pb, 0, sizeof(GCOV_BRANCH_DATA));
                        pb->s_pos = s_pos;
                        pb->e_pos = e_pos;
                        if (pl->branch == NULL) {
                            pl->branch = pb;
                            pb_prev = pb;
                        } else {
                            pb_prev->next = pb;
                            pb_prev = pb;
                        }
                    }
                }
                if (p->line == NULL) {
                    p->line = pl;
                    pl_prev = pl;
                } else {
                    pl_prev->next = pl;
                    pl_prev = pl;
                }
                /* -- add 20110210 */

                if (lineno+1 > line->end) break;
            }
        }
        fclose(fp);

        if (*top == NULL) {
            *top = p;
            p_prev = p;
        } else {
            p_prev->next = p;
            p_prev = p;
        }
    }
}

/**
 * gcov data memory free
 */
void free_gcov_data(GCOV_DATA *p)
{
    GCOV_DATA *p_next;

    for(; p; p = p_next) {
        if (p->linebuf.top) free(p->linebuf.top);
        free_gcov_line_data(p->line); /* 20110210 */
        p_next = p->next;
        free(p);
    }
}

/**
 * gcov line data memory free
 */
void free_gcov_line_data(GCOV_LINE_DATA *p)
{
    GCOV_LINE_DATA *p_next;

    for(; p; p = p_next) {
        free_gcov_branch_data(p->branch);
        p_next = p->next;
        free(p);
    }
}

/**
 * gcov branch data memory free
 */
void free_gcov_branch_data(GCOV_BRANCH_DATA *p)
{
    GCOV_BRANCH_DATA *p_next;

    for(; p; p = p_next) {
        p_next = p->next;
        free(p);
    }
}

/*************** calc and print (with diff merge gcov file) **************/
/**
 * calc gocv data
 */
void calc_gcov(GCOV_DATA *p)
{
    for (; p; p = p->next) parcent(p);
}

/**
 * get gcov line executed parcent
 */
void parcent(GCOV_DATA *p)
{
    char linebuf[LINEBUFSZ];
    char *ptr;
    GCOV_LINE_DATA *pl;
    GCOV_BRANCH_DATA *pb;

    for(pl = p->line; pl; pl = pl->next) {
        memset(linebuf, 0, sizeof(linebuf));
        if (gcov_line_get_by_pos(&p->linebuf, linebuf, sizeof(linebuf)-1, pl->s_pos, pl->e_pos) == 0) break; /* 20100531 */

        if ((ptr = strchr(linebuf, ':')) == NULL) continue;

        ptr--;
        if (isdigit(*ptr)) p->line_pass++;
        else if (*ptr == '#') p->line_notpass++;

        /* -- add 20110210 */
        for(pb = pl->branch; pb; pb = pb->next) {
            memset(linebuf, 0, sizeof(linebuf));
            if (gcov_line_get_by_pos(&p->linebuf, linebuf, sizeof(linebuf)-1, pb->s_pos, pb->e_pos) == 0) break;
            if (strstr(linebuf, " 0%") || strstr(linebuf, "never")) {
                pl->branch_notpass++;
                p->branch_notpass++;
            } else {
                pl->branch_pass++;
                p->branch_pass++;
            }
        }
        /* -- add 20110210 */
    }

    if ((p->line_pass + p->line_notpass) != 0)
        p->line_parcent = (double)p->line_pass / (p->line_pass + p->line_notpass) * 100;
    if ((p->branch_pass + p->branch_notpass) != 0) { /* 20110210 */
        p->branch_parcent = (double)p->branch_pass / (p->branch_pass + p->branch_notpass) * 100;
    } else {
        p->branch_parcent = 100.0;
    }
}

/**
 * print gcov
 */
void print_gcov(GCOV_DATA *p, int level)
{
    int line_pass, line_notpass, branch_pass, branch_notpass;
    line_pass = line_notpass = branch_pass = branch_notpass = 0;
    GCOV_DATA *p_bk = p;

    printf("***************************\n");
    printf("***** coverage result *****\n");
    printf("***************************\n");
    for(; p; p = p->next) { /* 20100531 */
        if (p->line_pass == 0 && p->line_notpass == 0) continue; /* 解析エラー */
        printf("%s Lines executed:%02.2f%% (%d/%d)\n", p->gcov, p->line_parcent, p->line_pass, (p->line_pass + p->line_notpass));
        if (level == C1_BRANCH_LEVEL) /* 20110210 */
            printf("%s Branches executed:%02.2f%% (%d/%d)\n", p->gcov, p->branch_parcent, p->branch_pass, (p->branch_pass + p->branch_notpass));
        print_notpass_line(p, level);
    }

    printf("*******************\n");
    printf("***** summary *****\n");
    printf("*******************\n");
    p = p_bk;
    for(; p; p = p->next) {
        if (p->line_pass == 0 && p->line_notpass == 0) continue; /* 解析エラー */
        line_pass += p->line_pass;
        line_notpass += p->line_notpass;
        branch_pass += p->branch_pass; /* 20110210 */
        branch_notpass += p->branch_notpass;

        printf("%s Lines executed:%02.2f%% (%d/%d)\n", p->gcov, p->line_parcent, p->line_pass, (p->line_pass + p->line_notpass));
        if (level == C1_BRANCH_LEVEL) /* 20110210 */
            printf("%s Branches executed:%02.2f%% (%d/%d)\n", p->gcov, p->branch_parcent, p->branch_pass, (p->branch_pass + p->branch_notpass));
    }
    if ((line_pass+line_notpass) > 0) {
        printf("Total Lines executed:%02.2f%% (%d/%d)\n", (double)line_pass / (line_pass+line_notpass) * 100, line_pass, (line_pass+line_notpass));
    } else {
        printf("Total Lines executed:100.00%% (%d/%d)\n", line_pass, (line_pass+line_notpass));
    }
    if (level == C1_BRANCH_LEVEL) { /* 20110210 */
        if ((branch_pass+branch_notpass) > 0) {
            printf("Total Branches executed:%02.2f%% (%d/%d)\n", (double)branch_pass / (branch_pass+branch_notpass) * 100, branch_pass, (branch_pass+branch_notpass));
        } else {
            printf("Total Branches executed:100.00%% (%d/%d)\n", branch_pass, (branch_pass+branch_notpass));
        }
    }
}

/**
 * print gcov not pass line
 */
void print_notpass_line(GCOV_DATA *p, int level)
{
    char linebuf[LINEBUFSZ];
    char *ptr;
    GCOV_LINE_DATA *pl;
    GCOV_BRANCH_DATA *pb;

    if (p->linebuf.top == NULL) return;

    for(pl = p->line; pl; pl = pl->next) {
        memset(linebuf, 0, sizeof(linebuf));
        if (gcov_line_get_by_pos(&p->linebuf, linebuf, sizeof(linebuf)-1, pl->s_pos, pl->e_pos) == 0) break;
        if (strrchr(linebuf, '\n') != NULL) *strrchr(linebuf, '\n') = 0;

        if ((ptr = strchr(linebuf, ':')) == NULL) continue;
        ptr--;
        if (level == C0_LINE_LEVEL) {
            if (*ptr == '#') printf("%s\n", linebuf);
        } else {
            if (*ptr == '#' || pl->branch_notpass > 0) printf("%s\n", linebuf);
            if (pl->branch_notpass > 0) {
                for (pb = pl->branch; pb; pb = pb->next) {
                    memset(linebuf, 0, sizeof(linebuf));
                    if (gcov_line_get_by_pos(&p->linebuf, linebuf, sizeof(linebuf)-1, pb->s_pos, pb->e_pos) == 0) break;
                    if (strrchr(linebuf, '\n') != NULL) *strrchr(linebuf, '\n') = 0;
                    printf("%s\n", linebuf);
                }
            }
        }
    }
}

/**
 * get command line option parameter
 * 0: ok
 * -1: err
 */
int get_option(int argc, char **argv, OPTION *opt)
{
    int i;

    opt->diff_fmt = UNKNOWN_FMT;
    opt->file = (char *)DEFAULT_DIFF_FILENAME; /* 20100531 */
    opt->level = C0_LINE_LEVEL;
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-c0") || !strcmp(argv[i], "-C0")) {
                opt->level = C0_LINE_LEVEL;
            } else if (!strcmp(argv[i], "-c1") || !strcmp(argv[i], "-C1")) {
                opt->level = C1_BRANCH_LEVEL;
            } else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--cvsdiff")) {
                opt->diff_fmt = CVS_FMT;
            } else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--diffall")) {
                opt->diff_fmt = DIFF_FMT;
            } else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--svndiff")) {
                opt->diff_fmt = SVN_FMT;
            } else {
                opt->file = argv[i];
            }
        }
    }
    return 0;
}

/**
 * debug print command line option
 */
void debug_print_option(OPTION *opt)
{
    printf("fmt[%d] file[%s] level[%d]\n", opt->diff_fmt, opt->file, opt->level);
}

/**
 * print out command usage
 */
void print_usage(char *cmd_name)
{
    const char *msg =
        "Usage: %s [-c0 | -c1] [-c cvs_diff | -d diffall | -s svn_diff] (default diff filename -> %s\n";
    printf(msg, cmd_name, DEFAULT_DIFF_FILENAME);
}

/**
 * analyze diff format
 * (add 20100531)
 */
int get_diff_format(char *filename)
{
    FILE *fp;
    char linebuf[LINEBUFSZ];
    READ_BUF readbuf;
    unsigned long svn, cvs, diffall;
    int diff_fmt = UNKNOWN_FMT;

    svn = cvs = diffall = 0;

    if ((fp = fopen(filename, "r")) == NULL) return diff_fmt;

    memset(&readbuf, 0, sizeof(readbuf));

    while(1) {
        memset(linebuf, 0, sizeof(linebuf));
        if (readline(linebuf, &readbuf, fp) == -1) {fclose(fp); break;} /* eof */

        if (linebuf[0] == '@' && linebuf[1] == '@') {
            svn++;
        }
        if (strstr(linebuf, "RCS file")) {
            cvs++;
        }
        if (strstr(linebuf, "Target=")) {
            diffall++;
        }
    }

    if (diffall > 0)   diff_fmt = DIFF_FMT;
    if (cvs > diffall) diff_fmt = CVS_FMT;
    if (svn > cvs)     diff_fmt = SVN_FMT;
    return diff_fmt;
}

/****** svn diff format create line data ******/
/**
 * create line data (for svn diff file)
 * (add 20100531)
 */
void create_line_data_for_svn(FILE *fp, READ_BUF *readbuf, LINE_DATA **top)
{
    char  linebuf[LINEBUFSZ];
    LINE_DATA *p, *p_prev;
    int base, crnt, start, end;

    base = crnt = start = end = 0;

    while(1) {
        if (is_end_diff_section(readbuf, SVN_FMT)) return;

        memset(linebuf, 0, sizeof(linebuf));
        if (readline(linebuf, readbuf, fp) == -1) return; /* eof */

        if (linebuf[0] == ' ' || linebuf[0] == '+') crnt++;

        if (is_svndiff_summary(linebuf)) {
            crnt = -1;
            base = get_svndiff_baseline(linebuf);
        }

        if (base > 0) {
            if (is_svndiff_start(readbuf)) {
                start = base + crnt;
            }
            if (is_svndiff_end(readbuf)) {
                end = base + crnt;
            }
        }

        if (start > 0 && end > 0) {
            if ((p = (LINE_DATA *)malloc(sizeof(LINE_DATA))) == NULL) {
                start = end = 0;
                continue;
            }
            memset(p, 0, sizeof(LINE_DATA));
            p->start = start;
            p->end = end;
            if (*top == NULL) {
                *top = p;
                p_prev = p;
            } else {
                p_prev->next = p;
                p_prev = p;
            }
            start = end = 0;
        }
    }
}

/**
 * check svn diff summary line (add 20100531)
 * 1: summary line
 * 0: not summary line
 */
int is_svndiff_summary(char *line)
{
    if (line[0] == '@' && line[1] == '@') return 1;
    return 0;
}

/**
 * check svn diff start section (add 20100531)
 * 1: start
 * 0: not start
 *
 * ex)
 * | aaaaa....
 * |-bbbbb....
 * |+ccccc.... <- start line
 * |+ddddd....
 * | edddd....
 */
int is_svndiff_start(READ_BUF *p)
{
    if (p->prev[0] != '+' && p->crnt[0] == '+') return 1;
    return 0;
}

/**
 * check svn diff end section (add 20100531)
 * 1: end
 * 0: not end
 *
 * ex)
 * | aaaaa....
 * |-bbbbb....
 * |+ccccc....
 * |+ddddd.... <- end line
 * | edddd....
 */
int is_svndiff_end(READ_BUF *p)
{
    if (p->crnt[0] == '+' && p->next[0] != '+') return 1;
    return 0;
}

/**
 * svn diff summary line analyze (add 20100531)
 * 0: error
 * >0: base line
 *
 * ex)
 * @@ -130,7 +130,7 @@
 *            ^^^
 */
int get_svndiff_baseline(char *line)
{
    char *p1, *p2;
    char tmp[16];

    if ((p1 = strchr(line, '+')) == NULL) return 0;
    p1++;
    if ((p2 = strchr(p1, ',')) == NULL) return 0;

    memset(tmp, 0, sizeof(tmp));
    memcpy(tmp, p1, p2-p1);
    return atoi(tmp);
}

/******* GCOV_LINE_BUF accessor *******/
/**
 * GCOV_LINE create & data copy (add 20100531)
 * 0: error
 * >0: copied size
 * s_pos : copy start buffer position (output param)
 * e_pos : copy end buffer position (output param)
 */
unsigned long gcov_line_data_copy(GCOV_LINE_BUF *p, char *data, unsigned long sz, unsigned long *s_pos, unsigned long *e_pos)
{
    if (p->top == NULL) {
        if ((p->top = (char *)malloc(LINEBUFSZ * 10)) == NULL) return 0;
        p->max = LINEBUFSZ * 10;
        p->pos = 0;
    }

    if ( (p->pos + sz) > (p->max - 1) ) {
        if ((p->top = (char *)realloc(p->top, p->max + (LINEBUFSZ * 10))) == NULL) return 0;
        p->max = p->max + (LINEBUFSZ * 10);
    }
    *s_pos = p->pos;      /* 20110210 */
    *e_pos = p->pos + sz; /* 20110210 */
    memcpy(p->top + p->pos, data, sz);
    p->pos += sz;
    p->top[p->pos] = 0; /* null terminate */

    return sz;
}

/**
 * GCOV_LINE gets (add 20100531)
 * 0: error
 * >0: read size
 * attention: \n is not read
 */
int gcov_line_gets(GCOV_LINE_BUF *p, char *out, unsigned long outsz)
{
    int pos = 0;

    if (p->top == NULL) return 0;

    for (pos = 0; pos < outsz; ) {
        if (p->refpos >= p->pos) break;
        if (p->top[p->refpos] == '\n') {
            p->refpos++;
            break;
        }

        out[pos] = p->top[p->refpos];
        pos++;
        p->refpos++;
    }
    return pos;
}

/**
 * GCOV_LINE get by position (add 20110210)
 * 0: error
 * >0: read size
 * attention: \n is read
 */
int gcov_line_get_by_pos(GCOV_LINE_BUF *p, char *out, unsigned long outsz, unsigned long s_pos, unsigned long e_pos)
{
    int pos = 0;
    int refpos = s_pos;

    if (p->top == NULL) return 0;

    for (pos = 0; pos < outsz; ) {
        if (refpos >= p->pos) break;
        if (refpos == e_pos) break;

        out[pos] = p->top[refpos];
        pos++;
        refpos++;
    }
    return pos;
}

/**
 * GCOV_LINE_BUF refpos reset (add 20100531)
 */
void gcov_line_refreset(GCOV_LINE_BUF *p)
{
    p->refpos = 0;
}

/***** check gcov file timestamp *****/
/**
 * check gcov timestamp
 * 0: no need
 * 1: need update gcov file
 */
int need_gcov_update(DIFF_DATA *diff)
{
    char base[FILENAMESZ];
    char gcov[FILENAMESZ];
    char gcda[FILENAMESZ];
    struct stat gcov_stat;
    struct stat gcda_stat;
    int need_update = 0;

    for (; diff; diff = diff->next) {
        if (strlen(diff->src) == 0) continue;
        if (strrchr(diff->src, '.'))
            if (*(strrchr(diff->src, '.') + 1) == 'h') continue; /* header file */

        memset(base, 0, sizeof(base));
        memset(gcov, 0, sizeof(gcov));
        memset(gcda, 0, sizeof(gcda));
        strncpy(base, diff->src, sizeof(base)-1);
        if (strrchr(base, '.')) *strrchr(base, '.') = 0;
        sprintf(gcov, "./%s.gcov", diff->src);
        sprintf(gcda, "./%s.gcda", base);

        memset(&gcda_stat, 0, sizeof(gcda_stat));
        memset(&gcov_stat, 0, sizeof(gcov_stat));
        if (stat(gcda, &gcda_stat) < 0) continue;
        if (stat(gcov, &gcov_stat) < 0) {
            printf("!!! %s is none !!!\n",gcov);
            need_update = 1;
            continue;
        }

        if (gcov_stat.st_mtime < gcda_stat.st_mtime) {
            printf("!!! %s needs update !!!\n", gcov);
            need_update = 1;
        }
    }

    return need_update;
}

/**
 * gcov update
 * 0: proc cancel
 * 1: proc continue
 */
int gcov_update(int level)
{
    char input[256];
    char command[256];
    char *opt = (char *)"";

    if (level == C1_BRANCH_LEVEL) { /* 20110210 */
        opt = (char *)"-b";
    }
    memset(command, 0, sizeof(command));
    sprintf(command, "%s %s -f *.gcno", GCOV_COMMAND, opt);

    printf("create %s gcov\?[y/n/q]", level == C0_LINE_LEVEL ? "C0" : "C1");
    memset(input, 0, sizeof(input));
    fgets(input, sizeof(input)-1, stdin);
    if (input[0] == 'y' || input[0] == 'Y') {
        printf("create gcov ...\n");
        system(command);
        return 1;
    } else if (input[0] == 'n' || input[0] == 'N') {
        return 1;
    } else {
        return 0;
    }
}

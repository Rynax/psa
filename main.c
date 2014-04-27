#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dirent.h>
#include "list.h"

//linux路径最大长度通常为4096
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

//linux文件名最大长度通常为255
#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#ifdef DEBUG
#define DBG(...) fprintf(stdout, __VA_ARGS__)
#else
#define DBG(...) 
#endif

#define scpy(x) strcpy(xmalloc(strlen(x)+1),(x))
#define LETTER(x) (((x) >= 'A' && (x) <= 'Z') || ((x) >= 'a' && (x) <= 'z') || ((x) >= '0' && (x) <= '9') || (x) == '_')

// linux颜色表
// 00 OFF, 01 高亮显示, 04 下划线, 05 闪烁, 07 反白显示, 08 不可见
// 前景色/背景色参照linux_color.gif
const char *color_list[] = {
//    0     1      2        3        4       5
//                ( )       :       ->      func
    "\e[", "m", "01;31", "01;31", "01;32",  "00",
//     6        7        8        9        10       11
//   lfile    lline    rfile    rline    default    tag
    "00;37", "00;36", "00;37", "00;36", "\e[00m", "00;33"};

const char *ignore_list[] = {
    "if", "switch", "while", "for", "return", "exit",
    "printf", "sprintf", "snprintf", "fprintf",
    "memset", "strcpy", "strncpy", "memcpy", NULL};

enum FSM_C {
    C_INDENT = 0,   //去掉行首空白或空行
    C_SKIP,         //跳过当前行
    C_HOME,         //根据行首设置状态
    C_SLASH,        //'/' 注释
    C_POUND,        //'#' 预编译
    C_FIND_OP,      //查找'('
    C_FILE_END
};

typedef struct dir_tree DIR_TREE_ST;
typedef struct func_list FUNC_LIST_ST;
typedef struct in_list IN_LIST_ST;
typedef struct file_list FILE_LIST_ST;
typedef struct out_list OUT_LIST_ST;

struct dir_tree {       //待解析的全部文件和目录, 将只包含.c和.h文件
    BTD_NODE tree;      //left同级, right子级
    char *name;         //文件或目录名, 含路径, 目录名最后不带'/'
    char valid;         //此目录或文件是否有效, 重复将被标记为无效
};

struct func_list {      //解析过的函数
    BTD_NODE func;
    char *name;         //函数名
    FILE_LIST_ST *file; //函数所在文件
    int count;          //-1-函数调用, 0-函数声明, 1-函数定义
    size_t line;        //函数所在行
};

struct in_list {        //某文件包含的.h文件(名称列表)
    DL_NODE(struct in_list) in;
    char *name;
};

struct file_list {      //解析过的文件
    DL_NODE(FILE_LIST_ST) file;
    FUNC_LIST_ST *func;
    IN_LIST_ST *in;
    char *name;         //文件名
};

struct out_list {       //整理过的函数
    BTD_NODE func;
    char *name;         //函数名
    FILE_LIST_ST *file; //函数所在文件
    FILE_LIST_ST *dfile;//函数定义所在文件
    size_t line;        //函数所在行
    size_t dline;       //函数定义所在行
};

static void usage(int);
static void* xmalloc(size_t);
static int xstrcmp(const char *, const char *);
static DIR_TREE_ST* new_tree_list(const char *);
static FILE_LIST_ST* new_file_list(const char *);
static FUNC_LIST_ST* new_func_list(const char *);
static IN_LIST_ST* new_in_list(const char *);
static OUT_LIST_ST* new_out_list(const char *);
static void erase_tree_list(BTD_NODE *);
static void erase_func_list(BTD_NODE *);
static void erase_out_list(BTD_NODE *);
static void erase_in_list(void *);
static void erase_file_list(void *);
static ssize_t get_hfile_name(char *, size_t, const char *, const char *);
static ssize_t get_func_name(char *, size_t, const char *);
static void parse_dir(DIR_TREE_ST *);
static void parse_dir_list(DIR_TREE_ST *, FILE_LIST_ST **);
static void parse_file(FILE_LIST_ST **, const char *, off_t);
static OUT_LIST_ST* link_func(FILE_LIST_ST *, const char *);
static void build_func_tree(FILE_LIST_ST *, FUNC_LIST_ST *, OUT_LIST_ST *);

char *func_name = "main";   //全局选项: 主函数名
char *tfunc_name = NULL;    //全局选项: 标记函数名
int tag_func = 0;           //全局选项: 标记函数
int head_file = 1;          //全局选项: 自动解析头文件
int macros = 1;             //全局选项: 解析带参宏定义
int ex_func = 2;            //全局选项: 函数名过滤, 0-不过滤, 1-按列表过滤, 2-全过滤
int verbose = 2;            //全局选项: 输出信息过滤
#define RET(...)  if(verbose >= 1) fprintf(stdout, __VA_ARGS__)
#define ERR(...)  if(verbose >= 2) fprintf(stderr, __VA_ARGS__)
#define WARN(...) if(verbose >= 3) fprintf(stdout, __VA_ARGS__)
#define INFO(...) if(verbose >= 4) fprintf(stdout, __VA_ARGS__)

void out_list(BTD_NODE* n) {
    OUT_LIST_ST *t = (OUT_LIST_ST*)n;
    int i = 5;
    
    if(tag_func && !strcmp(t->name, tfunc_name)) i = 11;
    RET("%s%s%s%s%s %s%s%s(%s%s%s%s%s%s%s%s%s:%s%s%s%s%d%s",
    color_list[0], color_list[i], color_list[1], t->name, color_list[10],
    color_list[0], color_list[2], color_list[1], color_list[10],
    color_list[0], color_list[6], color_list[1], t->file->name, color_list[10],
    color_list[0], color_list[3], color_list[1], color_list[10],
    color_list[0], color_list[7], color_list[1], (int)(t->line), color_list[10]);
    // RET("%s \e[01;31;40m(\033[00m%s:%d", t->name, t->file->name, (int)(t->line));
    if(t->dfile) { RET(" %s%s%s->%s %s%s%s%s%s%s%s%s:%s%s%s%s%d%s%s%s%s)%s\n",
    color_list[0], color_list[4], color_list[1], color_list[10],
    color_list[0], color_list[8], color_list[1], t->dfile->name, color_list[10],
    color_list[0], color_list[3], color_list[1], color_list[10],
    color_list[0], color_list[9], color_list[1], (int)(t->dline), color_list[10],
    color_list[0], color_list[2], color_list[1], color_list[10]);}
    // if(t->dfile) { RET(" -> %s:%d)\n", t->dfile->name, (int)(t->dline));}
    else { RET("%s%s%s)%s\n", color_list[0], color_list[2], color_list[1], color_list[10]);}
    // else { RET(")\n");}
    return;
}

#ifdef DEBUG
void out(BTD_NODE* n) {
    FUNC_LIST_ST *t = (FUNC_LIST_ST*)n;
    RET("%s (%s:%d)\n", t->name, t->file->name, (int)(t->line));
    return;
}
#endif

int main(int argc, char **argv) {
    DIR_TREE_ST *dir_list = NULL, *t = NULL;    //需要解析的目录结构
    FILE_LIST_ST *file_list = NULL;  //所有解析过的文件列表
    OUT_LIST_ST *output_list = NULL;
    int arg_flag = 1, i = 0;

    for(i = 1; i < argc; i++) {
        if(arg_flag && argv[i][0] == '-') {
            if(!argv[i][1]) {
                ERR("Missing option to '-' symbol.\n");
                usage(1);
            }
            if(argv[i][1] != '-' && argv[i][2]) {
                ERR("Invalid argument -\"%s\".\n", argv[i]+1);
                usage(1);
            }
            switch(argv[i][1]) {
                case 'i':
                    head_file = 0;
                    break;
                case 'm':
                    macros = 0;
                    break;
                case 'f':
                    i++;
                    if(!argv[i] || argv[i][0] == '-') {
                        ERR("Missing argument to -f option.\n");
                        usage(1);
                    }
                    func_name = argv[i];
                    break;
                case 't':
                    i++;
                    if(!argv[i] || argv[i][0] == '-') {
                        ERR("Missing argument to -b option.\n");
                        usage(1);
                    }
                    tag_func = 1;
                    tfunc_name = argv[i];
                    break;
                case 'v':
                    i++;
                    if(!argv[i] || argv[i][0] == '-') {
                        ERR("Missing argument to -v option.\n");
                        usage(1);
                    }
                    if((verbose = atoi(argv[i])) <= 0) {
                        ERR("Invalid argument \"%s\" to -v option.\n", argv[i]);
                        usage(1);
                    }
                    break;
                case 'e':
                    i++;
                    if(!argv[i] || argv[i][0] == '-') {
                        ERR("Missing argument to -v option.\n");
                        usage(1);
                    }
                    if((ex_func = atoi(argv[i])) < 0) {
                        ERR("Invalid argument \"%s\" to -v option.\n", argv[i]);
                        usage(1);
                    }
                    break;
                case 'h':
                case 'H':
                    usage(0);
                case '-':
                    if(!argv[i][2]) {
                        arg_flag = 0;
                        break;
                    }
                    if (!strcmp("--help",argv[i])) usage(0);
                    else {
                        ERR("Invalid argument --\"%s\".\n", argv[i]+2);
                        usage(1);
                    }
                default:
                    ERR("Invalid argument -'%c'.\n", argv[i][1]);
                    usage(1);
            }
        }else {
            t = new_tree_list(argv[i]);
            if(t->name[strlen(argv[i])-1] == '/' && strlen(argv[i]) > 1) t->name[strlen(argv[i])-1] = '\0';
            if(dir_list) { BTD_INSERT_AFTER(dir_list, t, tree, left);}
            else dir_list = t;
            t = NULL;
        }
    }
    if(!dir_list) {
        dir_list = new_tree_list(NULL);
    }

    parse_dir_list(dir_list, &file_list);
    if(!file_list) {
        RET("Not valid file.\n");
        BTD_DESTROY(dir_list, tree, erase_tree_list);
        return 0;
    }

    output_list = link_func(file_list, func_name);
    if(!output_list) {
        RET("Not found function \"%s\".\n", func_name);
        BTD_DESTROY(dir_list, tree, erase_tree_list);
        DLC_DESTROY(file_list, file, erase_file_list);
        return 0;
    }

    BTD_DUMP(output_list, func, out_list);

    BTD_DESTROY(dir_list, tree, erase_tree_list);
    DLC_DESTROY(file_list, file, erase_file_list);
    BTD_DESTROY(output_list, func, erase_out_list);

    return 0;
}

static void usage(int n) {
    switch(n) {
        case 0:
            RET("Usage: ./psa [-h -H --help] [-i] [-m] [-v num] [-e num] [-f func] [-t func] [names].\n");
            RET("Options:\n");
            RET("        -i           程序默认自动解析被包含的头文件, 使用这个选项关闭它\n");
            RET("        -m           程序默认解析带参宏定义, 使用这个选项关闭它\n");
            RET("        -v num       输出信息过滤\n");
            RET("            1        仅输出结果(指定-h时输出此help信息)\n");
            RET("            2(默认)  输出错误信息\n");
            RET("            3        输出警告信息(如不支持的文件等)\n");
            RET("            4        输出普通信息\n");
            RET("        -e num       函数名称过滤\n");
            RET("            0        不过滤(显示所有函数, 这甚至包括if, for, while, ...)\n");
            RET("            1        按照列表过滤(内部的静态列表虽然很简短, 至少过滤了一些)\n");
            RET("            2(默认)  只列出已定义或声明的函数\n");
            RET("        -f func      指定起始函数名称(默认为main)\n");
            RET("        -t func      用单独的颜色标记指定的函数\n");
            RET("        names        指定文件或目录, 可以多个(默认为当前目录)\n");
            break;
        case 1:
        case 2:
        default:
            ERR("Usage: ./psa [-h -H --help] [-i] [-m] [-v num] [-e num] [-f func] [-t func] [names].\n");
            break;
    }
    exit(n);
}

static void* xmalloc(size_t size) {
    void *value = malloc(size);
    if(value == NULL) {
        ERR("Virtual memory exhausted.\n");
        exit(1);
    }
    return value;
}

static int xstrcmp(const char *s1, const char *s2) {
    for( ; *s1 == *s2; ++s1, ++s2)
        if(*s1 == '\0') return 0;
    if(*s2 == '\0') {
        if(!LETTER(*s1)) return 0;
    }
    return (*(unsigned char*)s1 - *(unsigned char*)s2);
}

static DIR_TREE_ST* new_tree_list(const char *name) {
    DIR_TREE_ST *t = NULL;
    t = (DIR_TREE_ST *)xmalloc(sizeof(DIR_TREE_ST));
    if(name) t->name = scpy(name);
    else t->name = NULL;
    t->valid = 1;
    BTD_IINIT(t, tree);
    return t;
}

static FILE_LIST_ST* new_file_list(const char *name) {
    FILE_LIST_ST *t = NULL;
    t = (FILE_LIST_ST *)xmalloc(sizeof(FILE_LIST_ST));
    if(name) t->name = scpy(name);
    else t->name = NULL;
    t->func = NULL;
    t->in = NULL;
    DLC_IINIT(t, file);
    return t;
}

static FUNC_LIST_ST* new_func_list(const char *name) {
    FUNC_LIST_ST *t = NULL;
    t = (FUNC_LIST_ST *)xmalloc(sizeof(FUNC_LIST_ST));
    if(name) t->name = scpy(name);
    else t->name = NULL;
    t->file = NULL;
    t->count = 0;
    t->line = 0;
    BTD_IINIT(t, func);
    return t;
}

static IN_LIST_ST* new_in_list(const char *name) {
    IN_LIST_ST *t = NULL;
    t = (IN_LIST_ST *)xmalloc(sizeof(IN_LIST_ST));
    if(name) t->name = scpy(name);
    else t->name = NULL;
    DL_IINIT(t, in);
    return t;
}

static OUT_LIST_ST* new_out_list(const char *name) {
    OUT_LIST_ST *t = NULL;
    t = (OUT_LIST_ST *)xmalloc(sizeof(OUT_LIST_ST));
    if(name) t->name = scpy(name);
    else t->name = NULL;
    t->file = NULL;
    t->dfile = NULL;
    t->line = 0;
    t->dline = 0;
    BTD_IINIT(t, func);
    return t;
}

static void erase_tree_list(BTD_NODE *n) {
    DIR_TREE_ST *t = (DIR_TREE_ST*)n;
    if(t->name) free(t->name);
    free(t);
    return;
}

static void erase_func_list(BTD_NODE *n) {
    FUNC_LIST_ST *t = (FUNC_LIST_ST*)n;
    if(t->name) free(t->name);
    free(t);
    return;
}

static void erase_out_list(BTD_NODE *n) {
    OUT_LIST_ST *t = (OUT_LIST_ST*)n;
    if(t->name) free(t->name);
    free(t);
    return;
}

static void erase_in_list(void *n) {
    IN_LIST_ST *t = (IN_LIST_ST *)n;
    if(t->name) free(t->name);
    free(t);
    return;
}

static void erase_file_list(void *n) {
    FILE_LIST_ST *t = (FILE_LIST_ST *)n;
    if(t->func) { BTD_DESTROY(t->func, func, erase_func_list);}
    if(t->in) { DL_DESTROY(t->in, in, erase_in_list);}
    if(t->name) free(t->name);
    free(t);
    return;
}

static void parse_dir_list(DIR_TREE_ST *dir_list, FILE_LIST_ST **file_list) { //目录主解析函数
    struct stat st;
    char *name = ".";
    int len = 0;

    if(dir_list->name) name = dir_list->name;
    if(stat(name, &st) == -1) {
        WARN("Invalid file or directory \"%s\".\n", name);
        dir_list->valid = 0;
    }else {
        len = strlen(name);
        switch(st.st_mode & S_IFMT) {
            case S_IFREG:
                if(len <= 2 ||
                        (strcmp((name + len - 2), ".c") &&
                         strcmp((name + len - 2), ".h"))) {
                    WARN("Unsupported file \"%s\".\n", name);
                    dir_list->valid = 0;
                }else {
                    if(st.st_size > 0) parse_file(file_list, name, st.st_size);
                }
                break;
            case S_IFDIR:
                dir_list->valid = 0;
                parse_dir(dir_list);
                break;
            default:
                WARN("Unsupported file \"%s\".\n", name);
                dir_list->valid = 0;
                break;
        }
    }

    if(dir_list->tree.right.next) parse_dir_list((DIR_TREE_ST*)(dir_list->tree.right.next), file_list);
    if(dir_list->tree.left.next) parse_dir_list((DIR_TREE_ST*)(dir_list->tree.left.next), file_list);
    return;
}

static void parse_dir(DIR_TREE_ST *dir_list) {  //只解析一层目录, 递归工作由主解析函数完成
    DIR *dp = NULL;
    struct dirent *ent = NULL;
    DIR_TREE_ST *t = NULL;
    char buf[PATH_MAX+1] = {0}, *b = buf, *name = ".";

    if(dir_list->name) name = dir_list->name;
    if(!(dp = opendir(name))) {
        WARN("Invalid directory \"%s\".\n", name);
        return;
    }

    while((ent = readdir(dp))) {
        if (!strcmp("..", ent->d_name) || !strcmp(".", ent->d_name)) continue;
        if(dir_list->name) snprintf(b, PATH_MAX+1, "%s/%s", dir_list->name, ent->d_name);
        else b = ent->d_name;
        t = new_tree_list(b);
        if(dir_list->tree.right.next) __BTD_INSERT_AFTER(dir_list->tree.right.next, &(t->tree), left);
        else BTD_INSERT_AFTER(dir_list, t, tree, right);
    }

    closedir(dp);
    return;
}

//返回<=0失败, >0成功
static ssize_t get_hfile_name(char *buf, size_t size, const char *file_name, const char *rpi) {
    ssize_t i = 0, j = 0, k = 0;

    for(i = strlen(file_name); i >= 0 && file_name[i] != '/'; i--) ;
    if(i >= 0) {
        for(j = 0; !strncmp(rpi+j, "../", 3); j += 3) {
            for(i--; i >= 0 && file_name[i] != '/'; i--) ;
            if(i < 0) {j += 3; break;}
        }
    }

    i++;
    strncpy(buf, file_name, i);

    for(k = 0; k < size; k++) {
        if(rpi[j+k] == '\0') return -1;
        if(rpi[j+k] == '"') return j+k;
        buf[i+k] = rpi[j+k];
    }
    memset(buf, 0, size);
    return -1;
}

//返回0失败, <0成功
static ssize_t get_func_name(char *buf, size_t size, const char *rpi) {
    ssize_t i = -1, j = 0, k = 0;

    for(i = -1; rpi[i] == ' ' || rpi[i] == '\t' || rpi[i] == '\r' || rpi[i] == '\n' || rpi[i] == '\\'; i--) ;
    k = i;

    for(j = 0; ; i--, j++) {
        if(!LETTER(rpi[i])) break;
    }

    i++;
    if(i > k) return 0;

    strncpy(buf, &rpi[i], j);

    return i;
}

static void parse_file(FILE_LIST_ST **file_list, const char *file_name, off_t file_size) {
    char dir_buf[PATH_MAX+1] = {0}, *rp = NULL;
    void *file_map = NULL;
    size_t fs = file_size+1, i = 0, count = 0, line_count = 1;
    ssize_t ret = 0;
    int fd = 0, c_stat = C_INDENT, flag = 0;
    FILE_LIST_ST *t = NULL;
    IN_LIST_ST *ht = NULL;
    FUNC_LIST_ST *ft = NULL, *fts = NULL, *tft = NULL, *tfts = NULL;
    struct stat st;

    if((fd = open(file_name, O_RDONLY)) == -1) {
        WARN("Invalid file \"%s\".\n", file_name);
        return;
    }

    if((file_map = mmap(NULL, fs, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
        WARN("Mmap file \"%s\" failed.\n", file_name);
        close(fd);
        return;
    }
    rp = (char*)file_map;

    t = new_file_list(file_name);

    while(c_stat != C_FILE_END) {
        switch(c_stat) {
            case C_INDENT:
                for( ; rp[i] == ' ' || rp[i] == '\t' || rp[i] == '\\' || rp[i] == '\r' || (rp[i] == '\n' && line_count++); i++) ;
                if(rp[i] == '\0') c_stat = C_FILE_END;
                else c_stat = C_HOME;
                break;
            case C_SKIP:
                for( ; rp[i] != '\0' && rp[i] != '\n'; i++) ;
                if(rp[i] == '\0') c_stat = C_FILE_END;    //处理无效文件
                else {      //处理'\'
                    if(rp[i-1] == '\r' && rp[i-2] == '\\') c_stat = C_SKIP;
                    else if(rp[i-1] == '\\') c_stat = C_SKIP;
                    else c_stat = C_INDENT;
                    i++;
                    line_count++;
                }
                break;
            case C_HOME:
                switch(rp[i]) {
                    case '/':   c_stat = C_SLASH;     break;
                    case '#':   c_stat = C_POUND;     break;
                    case '(':   c_stat = C_FIND_OP;   break;
                    case '*':   c_stat = C_FIND_OP;   break;
                    default:
                                if(LETTER(rp[i])) c_stat = C_FIND_OP;
                                else c_stat = C_SKIP;
                                break;
                }
                break;
            case C_SLASH:
                i++;
                if(rp[i] == '*') {
                    for( ; rp[i] != '\0' && (rp[i] != '*' || rp[i+1] != '/'); i++)
                        if(rp[i] == '\n') line_count++;
                    if(rp[i] == '\0') c_stat = C_FILE_END;    //处理无效文件
                    else { i += 2; c_stat = C_INDENT;}
                }else {
                    c_stat = C_SKIP;
                }
                break;
            case C_POUND:
                for(i++; ; i++) {
                    if(rp[i] == ' ' || rp[i] == '\t') continue;
                    if(rp[i] == '\\') {
                        if(rp[i+1] == '\n') { i++; line_count++;}
                        else if(rp[i+1] == '\r' && rp[i+2] == '\n') { i += 2; line_count++;}
                        continue;
                    }
                    break;
                }
                if(rp[i] == '\0') c_stat = C_FILE_END;    //处理无效文件
                else if(rp[i] == '\n') { i++; c_stat = C_INDENT; line_count++;}
                else if(!xstrcmp(&rp[i], "if")) {
                    for(i += 2; rp[i] == ' ' || rp[i] == '\t' || rp[i] == '('; i++) ;
                    if(rp[i] == '0') {
                        for(count = 1, i++; count; i++) {   //寻找')'
                            if(rp[i] == '\n') line_count++;
                            if(rp[i] == '\0') { c_stat = C_FILE_END; break;}  //处理无效文件
                            if(rp[i] == '#') {
                                for(i++; rp[i] == ' ' || rp[i] == '\t'; i++) ;
                                if(!strncmp(&rp[i], "if", 2)) { i++; count++;}
                                if(!xstrcmp(&rp[i], "endif")) { i += 4; count--;}
                            }
                        }
                        if(rp[i] == '\0') { c_stat = C_FILE_END;}  //处理无效文件
                        else {
                            c_stat = C_SKIP;
                        }
                    }else c_stat = C_SKIP;
                }
                if(head_file && !strncmp(&rp[i], "include", 7)) {
                    for(i += 7; rp[i] == ' ' || rp[i] == '\t'; i++) ;
                    if(rp[i] == '\\') {
                        if(rp[i+1] == '\n') { i += 2; c_stat = C_INDENT; line_count++;}
                        else if(rp[i+1] == '\r' && rp[i+2] == '\n') { i += 3; c_stat = C_INDENT; line_count++;}
                        else { i++; c_stat = C_SKIP;}
                    }else if(rp[i] == '"') {
                        i++;
                        ret = get_hfile_name(dir_buf, PATH_MAX+1, file_name, &rp[i]);
                        if(ret > 0) {
                            ht = new_in_list(dir_buf);
                            if(t->in) { DL_INSERT_AFTER(t->in, ht, in);}
                            else t->in = ht;
                            ht = NULL;
                            memset(dir_buf, 0, PATH_MAX+1);
                            i = i+ret+1;
                        }else if(ret == 0) i++;
                        c_stat = C_SKIP;
                    }
                    break;
                }
                if(macros && !strncmp(&rp[i], "define", 6)) {
                    for(i += 6; rp[i] == ' ' || rp[i] == '\t'; i++) ;
                    for( ; rp[i] != '\0' && rp[i] != '\n' && rp[i] != '(' && rp[i] != ' ' && rp[i] != '\t'; i++) {
                        if(rp[i] == '\\') {
                            if(rp[i+1] == '\n') { i++; line_count++;}
                            else if(rp[i+1] == '\r' && rp[i+2] == '\n') { i += 2; line_count++;}
                        }
                    }
                    if(rp[i] == '\0') c_stat = C_FILE_END;    //处理无效文件
                    else if(rp[i] == '\n') { i++; c_stat = C_INDENT; line_count++;}
                    else if(rp[i] == ' ' || rp[i] == '\t') { i++; c_stat = C_SKIP;}
                    else {  //找到'('
                        ret = get_func_name(dir_buf, PATH_MAX+1, &rp[i]);
                        if(!ret) { c_stat = C_SKIP; break;}
                        ft = new_func_list(dir_buf);
                        ft->count = 1;
                        ft->file = t;
                        ft->line = line_count;
                        if(tft) { BTD_INSERT_AFTER(tft, ft, func, left);}
                        else { t->func = ft;}
                        tft = ft;
                        memset(dir_buf, 0, PATH_MAX+1);
                        for(count = 1, i++; count; i++) {   //寻找')'
                            if(rp[i] == '\0') { c_stat = C_FILE_END; break;}  //处理无效文件
                            if(rp[i] == '\\') {
                                if(rp[i+1] == '\n') { i++; line_count++; continue;}
                                else if(rp[i+1] == '\r' && rp[i+2] == '\n') { i += 2; line_count++; continue;}
                            }
                            if(rp[i] == '\n') { i++; line_count++; c_stat = C_INDENT; break;}
                            if(rp[i] == '\'' && (rp[i+1] == '(' || rp[i+1] == ')')) { i++; continue;}
                            if(rp[i] == '(') count++;
                            if(rp[i] == ')') count--;
                        }
                        while(c_stat == C_POUND) {
                            for( ; rp[i] != '\0' && rp[i] != '\n' && rp[i] != '('; i++) {
                                if(rp[i] == '\\') {
                                    if(rp[i+1] == '\n') { i++; line_count++;}
                                    else if(rp[i+1] == '\r' && rp[i+2] == '\n') { i += 2; line_count++;}
                                }
                            }
                            if(rp[i] == '\0') c_stat = C_FILE_END;    //处理无效文件
                            else if(rp[i] == '\n') { i++; c_stat = C_INDENT; line_count++;}
                            else {  //找到'('
                                ret = get_func_name(dir_buf, PATH_MAX+1, &rp[i]);
                                if(!ret) { i++; continue;}
                                fts = new_func_list(dir_buf);
                                fts->count = -1;
                                fts->file = t;
                                fts->line = line_count;
                                if(tfts) { BTD_INSERT_AFTER(tfts, fts, func, left);}
                                else { BTD_INSERT_AFTER(ft, fts, func, right);}
                                tfts = fts;
                                fts = NULL;
                                memset(dir_buf, 0, PATH_MAX+1);
                                i++;
                            }
                        }
                        ft = NULL;
                        tfts = NULL;
                    }
                    break;
                }
                c_stat = C_SKIP;
                break;
            case C_FIND_OP:
                for( ; rp[i] != '\0' && rp[i] != '\n' && rp[i] != '{' && rp[i] != '('; i++) ;
                if(rp[i] == '\0') c_stat = C_FILE_END;    //处理无效文件
                else if(rp[i] == '\n') { i++; c_stat = C_INDENT; line_count++;}
                else if(rp[i] == '{') {
                    for(count = 1, i++; count && rp[i] != '\0'; i++) {   //跳过全局结构体
                        if(rp[i] == '\n') line_count++;
                        if(rp[i] == '\'' && (rp[i+1] == '{' || rp[i+1] == '}')) { i++; continue;}
                        else if(rp[i] == '{') count++;
                        else if(rp[i] == '}') count--;
                    }
                    if(rp[i] == '\0') c_stat = C_FILE_END;
                    else c_stat = C_INDENT;
                }else {     //找到'('
                    ret = get_func_name(dir_buf, PATH_MAX+1, &rp[i]);
                    if(!ret) { c_stat = C_SKIP; break;}
                    ft = new_func_list(dir_buf);
                    ft->count = 0;
                    ft->file = t;
                    ft->line = line_count;
                    if(tft) { BTD_INSERT_AFTER(tft, ft, func, left);}
                    else { t->func = ft;}
                    tft = ft;
                    memset(dir_buf, 0, PATH_MAX+1);
                    for(count = 1, i++; count && rp[i] != '\0'; i++) {   //寻找')'
                        if(rp[i] == '\n') { line_count++; continue;}
                        if(rp[i] == '\'' && (rp[i+1] == '(' || rp[i+1] == ')')) { i++; continue;}
                        if(rp[i] == '(') count++;
                        if(rp[i] == ')') count--;
                    }
                    for( ; rp[i] != '\0' && rp[i] != ';' && rp[i] != ',' && rp[i] != '{'; i++) {
                        if(rp[i] == '\n') line_count++;
                    }
                    if(rp[i] == '\0') c_stat = C_FILE_END;    //处理无效文件
                    else if(rp[i] == ';' || rp[i] == ',') { i++; c_stat = C_INDENT;}   //函数声明
                    else {                     //函数定义
                        ft->count = 1;
                        count = 1;
                        i++;
                        while(c_stat == C_FIND_OP) {
                            for(; count && rp[i] != '\0' && rp[i] != '('; i++) {
                                if(rp[i] == '\n') { line_count++; continue;}
                                if(rp[i] == '/') {
                                    if(rp[i+1] == '/') {
                                        for(i += 2; rp[i] != '\0' && rp[i] != '\n'; i++) ;
                                        if(rp[i] == '\0') break;    //处理无效文件
                                        else line_count++;
                                    }else if(rp[i+1] == '*') {
                                        for(i += 2; rp[i] != '\0' && (rp[i] != '*' || rp[i+1] != '/'); i++)
                                            if(rp[i] == '\n') line_count++;
                                        if(rp[i] == '\0') break;    //处理无效文件
                                        else i++;
                                    }
                                    continue;
                                }
                                if(rp[i] == '\'' && (rp[i+1] == '{' || rp[i+1] == '}')) { i++; continue;}
                                if(rp[i] == '"' && rp[i-1] != '\\' && rp[i-1] != '\'') {
                                    for(i++; rp[i] != '\0' && rp[i] != '"'; i++) {
                                        if(rp[i] == '\n') line_count++;
                                    }
                                    if(rp[i] == '\0') break;    //处理无效文件
                                    else continue;
                                }
                                if(rp[i] == '{') count++;
                                if(rp[i] == '}') count--;
                            }
                            if(rp[i] == '\0') c_stat = C_FILE_END;    //处理无效文件
                            else if(!count) c_stat = C_INDENT;
                            else {  //找到'('
                                ret = get_func_name(dir_buf, PATH_MAX+1, &rp[i]);
                                if(!ret) { i++; continue;}
                                fts = new_func_list(dir_buf);
                                fts->count = -1;
                                fts->file = t;
                                fts->line = line_count;
                                if(tfts) { BTD_INSERT_AFTER(tfts, fts, func, left);}
                                else { BTD_INSERT_AFTER(ft, fts, func, right);}
                                tfts = fts;
                                fts = NULL;
                                memset(dir_buf, 0, PATH_MAX+1);
                                i++;
                            }
                        }
                        ft = NULL;
                        tfts = NULL;
                    }
                }
                break;
            default:
                ERR("C_FSM c_stat \"%d\" error.\n", c_stat);
                c_stat = C_FILE_END;
                break;
        }

    }

    munmap(file_map, fs);
    close(fd);
    if(*file_list) { DL_INSERT_BEFORE(*file_list, t, file);}
    else *file_list = t;
#ifdef DEBUG
    if(t->func) { BTD_DUMP(t->func, func, out);}
#endif
    for(ht = t->in; ht; ht = ht->in.next) {
        t = *file_list;
        do {
            if(!strcmp(ht->name, t->name)) { flag = 1; break;}
            t = t->file.next;
        }while(t != *file_list);
        if(flag) {flag = 0; continue;}
        if(stat(ht->name, &st) == -1) {
            WARN("Invalid head file \"%s\".\n", ht->name);
            continue;
        }
        parse_file(file_list, ht->name, st.st_size);
    }

    return;
}

static OUT_LIST_ST* link_func(FILE_LIST_ST *file_list, const char *func_name) {
    OUT_LIST_ST *output = NULL, *ot = NULL, *otpre = NULL;
    FILE_LIST_ST *t = NULL;
    FUNC_LIST_ST *ft = NULL;
    BTD_NODE *bt = NULL;

    t = file_list;
    do {
        if(t->func) {
            for(bt = &(t->func->func); bt; bt = bt->left.next) {
                ft = (FUNC_LIST_ST*)bt;
                if(ft->count > 0 && !strcmp(ft->name, func_name)) {
                    ot = new_out_list(func_name);
                    ot->file = ft->file;
                    ot->dfile = ft->file;
                    ot->line = ft->line;
                    ot->dline = ft->line;
                    build_func_tree(file_list, ft, ot);
                    if(!output) {
                        output = ot;
                        otpre = ot;
                    }else {
                        BTD_INSERT_AFTER(otpre, ot, func, left);
                        otpre = ot;
                    }
                }
            }
        }
        t = t->file.next;
    }while(t != file_list);

    return output;
}

static void build_func_tree(FILE_LIST_ST *file_list, FUNC_LIST_ST *call_list, OUT_LIST_ST *ot) {
    int i = 0;
    FILE_LIST_ST *t = NULL;
    FUNC_LIST_ST *lt = NULL, *rt = NULL, *ltbak = NULL;
    OUT_LIST_ST *new = NULL, *temp = NULL;
    BTD_NODE *brt = NULL, *blt = NULL;

    if(!call_list) return;

    for(brt = call_list->func.right.next; brt; brt = brt->left.next) {
        rt = (FUNC_LIST_ST*)brt;
        t = file_list;
        do {
            if(t->func) {
                for(blt = &(t->func->func); blt; blt = blt->left.next) {
                    lt = (FUNC_LIST_ST*)blt;
                    if(!strcmp(rt->name, lt->name)) {
                        if(lt->count == 0) { if(!ltbak) ltbak = lt; continue;}
                        break;
                    }
                }
                if(blt) break;
            }
            t = t->file.next;
        }while(t != file_list);
        if(!blt) lt = ltbak;
        ltbak = NULL;

        if(ex_func == 2 && !lt) continue;
        else if(ex_func == 1) {
            for(i = 0; ignore_list[i] && strcmp(rt->name, ignore_list[i]); i++) ;
            if(ignore_list[i]) continue;
        }

        new = new_out_list(rt->name);
        new->file = rt->file;
        if(lt) new->dfile = lt->file;
        new->line = rt->line;
        if(lt) new->dline = lt->line;

        if(!ot->func.right.next) {
            BTD_INSERT_AFTER(ot, new, func, right);
        }else {
            BTD_INSERT_AFTER(temp, new, func, left);
        }
        
        temp = new;

        if(strcmp(call_list->name, rt->name))
            build_func_tree(t, lt, new);
    }

    return;
}
/*
static void back_link(const char *bfunc_name, OUT_LIST_ST *func_list) {
    OUT_LIST_ST *t = NULL;
    int bfunc_count = 0;
    int i = 0;

    for(i = 0; i < func_count; i++) {
        if(!strcmp(func_list[i].name, bfunc_name)) {
            t = new_out_list(bfunc_name);

        }
    }

}

static OUT_LIST_ST* back_link(FILE_LIST_ST *file_list, const char *bfunc_name) {
    OUT_LIST_ST *output = NULL, *ot = NULL, *otpre = NULL;
    FILE_LIST_ST *t = NULL;
    FUNC_LIST_ST *ft = NULL, *rt = NULL;
    BTD_NODE *bt = NULL, *brt = NULL;

    t = file_list;
    do {
        if(t && t->func) {
            for(bt = &(t->func->func); bt; bt = bt->left.next) {
                ft = (FUNC_LIST_ST*)bt;
                for(brt = ft->func.right.next; brt; brt = brt->left.next) {
                    rt = (FUNC_LIST_ST*)brt;
                    // if(!strcmp(rt->name, bfunc_name)) {
                    ot = new_out_list(func_name);
                    ot->file = ft->file;
                    ot->dfile = ft->file;
                    ot->line = ft->line;
                    ot->dline = ft->line;
                    build_func_tree(file_list, ft, ot);
                    if(!output) {
                        output = ot;
                        otpre = ot;
                    }else {
                        BTD_INSERT_AFTER(otpre, ot, func, left);
                        otpre = ot;
                    }
                }
            }
        }
        t = t->file.next;
    }while(t != file_list);

    return output;
}
*/
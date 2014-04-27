#ifndef LIST_H
#define LIST_H

#include <stdio.h>

#define LEVEL_MAX 256
#define INDENT " "
#define DEBUG_DUMP(...) printf(__VA_ARGS__)

/**********************************************
链表
**********************************************/

//双链表DOUBLE LIST
//双链表节点
#define DL_NODE(type) \
struct { \
    type *prev, *next; \
}

//双链表主动初始化
#define DL_IINIT(node, field) \
do { \
    (node)->field.prev = NULL; \
    (node)->field.next = NULL; \
} while (0)
//双链表被动初始化
#define DL_PINIT { NULL, NULL }
//双循环链表主动初始化
#define DLC_IINIT(node, field) \
do { \
    (node)->field.prev = (node); \
    (node)->field.next = (node); \
} while (0)
//双循环链表被动初始化
#define DLC_PINIT(node) { (node), (node) }
/*
struct typexxx {
    int anything;
    DL_NODE(struct typexxx) fieldxxx;
} nodexxx = { 0, DLC_PINIT(&nodexxx) };
*/

//双链表前插
#define DL_INSERT_BEFORE(list_node, node, node_field) \
do { \
    (node)->node_field.next = (list_node); \
    (node)->node_field.prev = (list_node)->node_field.prev; \
    if((list_node)->node_field.prev) \
        (list_node)->node_field.prev->node_field.next = (node); \
    (list_node)->node_field.prev = (node); \
} while (0)
//双链表后插
#define DL_INSERT_AFTER(list_node, node, node_field) \
do { \
    (node)->node_field.prev = (list_node); \
    (node)->node_field.next = (list_node)->node_field.next; \
    if((list_node)->node_field.next) \
        (list_node)->node_field.next->node_field.prev = (node); \
    (list_node)->node_field.next = (node); \
} while (0)

//双链表头插, head_field与node_field相同时请使用AFTER代替
#define DL_INSERT_HEAD(head, head_field, node, node_field) \
do { \
    (node)->node_field.prev = (head); \
    (node)->node_field.next = (head)->head_field.next; \
    if((head)->head_field.next) { \
        (head)->head_field.next->node_field.prev = (node); \
    } \
    (head)->head_field.next = (node); \
} while (0)
//双循环链表头插, head_field与node_field相同时请使用AFTER代替
#define DLC_INSERT_HEAD(head, head_field, node, node_field) \
do { \
    (node)->node_field.prev = (head); \
    (node)->node_field.next = (head)->head_field.next; \
    if((head)->head_field.next == (head)) { \
        (head)->head_field.prev = (node); \
    } else { \
        (head)->head_field.next->node_field.prev = (node); \
    } \
    (head)->head_field.next = (node); \
} while (0)
//双链表尾插
#define DL_INSERT_LAST(head, head_field, node, node_field) \
do { \
    typeof(head) __t = (head); \
    for( ; __t->head_field.next; __t = __t->head_field.next) ; \
    (node)->node_field.prev = __t; \
    (node)->node_field.next = NULL; \
    __t->head_field.next = (node); \
} while (0)
//双循环链表尾插, head_field与node_field相同时请使用BEFORE代替
#define DLC_INSERT_LAST(head, head_field, node, node_field) \
do { \
    (node)->node_field.prev = (head)->head_field.prev; \
    (node)->node_field.next = (head); \
    if((head)->head_field.prev == (head)) { \
        (head)->head_field.next = (node); \
    } else { \
        (head)->head_field.prev->node_field.next = (node); \
    } \
    (head)->head_field.prev = (node); \
} while (0)

//双链表节点卸载
//仅改动链表指针, 节点仍可以继续访问链表, 复用节点时需要重新主动初始化
#define DL_DELETE(node, node_field) \
do { \
    if((node)->node_field.next) \
        (node)->node_field.next->node_field.prev = (node)->node_field.prev; \
    if((node)->node_field.prev) \
        (node)->node_field.prev->node_field.next = (node)->node_field.next; \
} while (0)

//erase_func: void (*erase_func)(void*)函数指针, 参数为节点指针(强转成void*), 通常用于释放资源
static inline void __ERASE_FUNC(void* __n, void (*erase_func)(void*)) {
    if(erase_func) erase_func(__n);
}
//双链表销毁, 从当前节点(包括)向后销毁
#define DL_DESTROY(node, node_field, erase_func) \
do { \
    typeof((node)) __n = (node), __t = (node)->node_field.next; \
    for( ; __n && ({__t = __n->node_field.next; 1;}); __n = __t) { \
        DL_DELETE(__n, node_field); \
        DL_IINIT(__n, node_field); \
        __ERASE_FUNC((void*)__n, erase_func); \
    } \
} while (0)
//双循环链表销毁
#define DLC_DESTROY(node, node_field, erase_func) \
do { \
    (node)->node_field.prev->node_field.next = NULL; \
    (node)->node_field.prev = NULL; \
    DL_DESTROY(node, node_field, erase_func); \
} while (0)
//双链表销毁, 销毁各种整个链表, 无论节点在哪个位置
#define DL_DESTROY__(node, node_field, erase_func) \
do { \
    typeof((node)) __n = (node), __t = (node)->node_field.prev; \
    for( ; __t && __n != __t ? 1: ({DL_IINIT(__n, node_field); 0;}); __n = __t, __t = __n->node_field.prev) { \
        DL_DELETE(__n, node_field); \
        DL_IINIT(__n, node_field); \
        __ERASE_FUNC((void*)__n, erase_func); \
    } \
    for( ; __n && ({__t = __n->node_field.next; 1;}); __n = __t) { \
        DL_DELETE(__n, node_field); \
        DL_IINIT(__n, node_field); \
        __ERASE_FUNC((void*)__n, erase_func); \
    } \
} while (0)

//output_func: void (*output_func)(void*)函数指针, 参数为节点指针(强转成void*), 用于输出节点内容, 建议不要输出'\n'
static inline void __OUTPUT_FUNC(void* __n, void (*output_func)(void*)) {
    if(output_func) output_func(__n);
}
//双链表DUMP, 从当前节点(包括)向后输出, 输出到end节点(不包括), 非循环链表node不可与end相同
#define DL_DUMP__(node, node_field, end, output_func) \
do { \
    typeof((node)) __n = (node), __t = (node)->node_field.next; \
    if(__n->node_field.prev) DEBUG_DUMP("↔"); \
    else DEBUG_DUMP("x"); \
    DEBUG_DUMP("|"); \
    __OUTPUT_FUNC((void*)__n, output_func); \
    DEBUG_DUMP("|"); \
    for(__n = __t; __n != (end) && ({__t = __n->node_field.next; 1;}); __n = __t) { \
        DEBUG_DUMP("↔"); \
        DEBUG_DUMP("|"); \
        __OUTPUT_FUNC((void*)__n, output_func); \
        DEBUG_DUMP("|"); \
    } \
    if(end) DEBUG_DUMP("↔"); \
    else DEBUG_DUMP("x"); \
} while (0)
//双链表DUMP, 从当前节点(包括)向后输出
#define DL_DUMP(node, node_field, output_func) \
do { \
    DL_DUMP__(node, node_field, NULL, output_func); \
    DEBUG_DUMP("\n"); \
} while (0)
//双循环链表DUMP, 从当前节点(包括)向后输出
#define DLC_DUMP(node, node_field, output_func) \
do { \
    DL_DUMP__(node, node_field, node, output_func); \
    DEBUG_DUMP("\n"); \
} while (0)
//双链表DUMP, 输出整个链表, 并用{}标记当前节点
#define DL_DUMP_(node, node_field, output_func) \
do { \
    typeof((node)) __p = (node); \
    for( ; __p->node_field.prev; __p = __p->node_field.prev) ; \
    if(__p != (node)) DL_DUMP__(__p, node_field, node, output_func); \
    else DEBUG_DUMP("x"); \
    __p = (node)->node_field.next; \
    DEBUG_DUMP("{"); \
    __OUTPUT_FUNC((void*)(node), output_func); \
    DEBUG_DUMP("}"); \
    if(__p) DL_DUMP__(__p, node_field, NULL, output_func); \
    else DEBUG_DUMP("x"); \
    DEBUG_DUMP("\n"); \
} while (0)


/**********************************************
二叉树BINARY TREE
**********************************************/

//双链表二叉树
typedef struct btd_node_st {
    DL_NODE(struct btd_node_st) left;
    DL_NODE(struct btd_node_st) right;
} BTD_NODE;

//二叉树主动初始化
#define __BTD_IINIT(node) \
do { \
    DL_IINIT((node), left); \
    DL_IINIT((node), right); \
} while (0)
#define BTD_IINIT(node, field) __BTD_IINIT(&((node)->field))
//二叉树被动初始化
#define BTD_PINIT { DL_PINIT, DL_PINIT }
/*
struct typexxx {
    int anything;
    BTD_NODE fieldxxx;
} nodexxx = { 0, BTD_PINIT };
BTD_IINIT(&nodexxx, fieldxxx);
展开后:
struct typexxx {
    int anything;
    struct btd_node_st {
        struct {
            struct btd_node_st *prev;
            struct btd_node_st *next;
        } left;
        struct {
            struct btd_node_st *prev;
            struct btd_node_st *next;
        } right;
    } fieldxxx;
} nodexxx = { 0, { { NULL, NULL }, { NULL, NULL } } };
do {
    do {
        (&((&nodexxx)->fieldxxx))->left.prev = NULL;
        (&((&nodexxx)->fieldxxx))->left.next = NULL;
    } while (0);
    do {
        (&((&nodexxx)->fieldxxx))->right.prev = NULL;
        (&((&nodexxx)->fieldxxx))->right.next = NULL;
    } while (0);
} while (0);
*/

//二叉树后插
//sub_field: 要插入的方向(left/right)
#define __BTD_INSERT_AFTER(bt_node, node, sub_field) \
do { \
    (node)->sub_field.prev = (bt_node); \
    (node)->sub_field.next = (bt_node)->sub_field.next; \
    if((bt_node)->sub_field.next) \
        (bt_node)->sub_field.next->sub_field.prev = (node); \
    (bt_node)->sub_field.next = (node); \
} while (0)
#define BTD_INSERT_AFTER(bt_node, node, node_field, sub_field) \
    __BTD_INSERT_AFTER(&((bt_node)->node_field), &((node)->node_field), sub_field)
//二叉树尾插
#define __BTD_INSERT_LAST(bt_node, node, sub_field) \
do { \
    BTD_NODE *__t = (bt_node); \
    for( ; __t->sub_field.next; __t = __t->sub_field.next) ; \
    (node)->sub_field.prev = __t; \
    (node)->sub_field.next = NULL; \
    __t->sub_field.next = (node); \
} while (0)
#define BTD_INSERT_LAST(bt_node, node, node_field, sub_field) \
    __BTD_INSERT_LAST(&((bt_node)->node_field), &((node)->node_field), sub_field)

//二叉树节点卸载, 此节点的父节点将成为叶子节点, 此节点将成为新树的根
#define __BTD_DELETE(node) \
do { \
    if((node)->left.prev) { \
        (node)->left.prev->left.next = NULL; \
        (node)->left.prev = NULL; \
    } \
    if((node)->right.prev) {\
        (node)->right.prev->right.next = NULL; \
        (node)->right.prev = NULL; \
    } \
} while (0)
#define BTD_DELETE(node, node_field) __BTD_DELETE(&((node)->node_field))

//erase_func: void (*erase_func)(BTD_NODE*)函数指针, 用于释放资源
//注意: 函数指针的参数为BTD_NODE*类型, 即&((node)->node_field)的类型
//      在函数指针中必须使用GET_NODE宏找回node地址
static inline void __BTD_ERASE_FUNC(BTD_NODE *__n, void (*erase_func)(BTD_NODE*)) {
    if(__n->left.next) __BTD_ERASE_FUNC(__n->left.next, erase_func);
    if(__n->right.next) __BTD_ERASE_FUNC(__n->right.next, erase_func);
    __BTD_DELETE(__n);
    if(erase_func) erase_func(__n);
}
//二叉树销毁, 从当前节点(包括)向后销毁
#define BTD_DESTROY(node, node_field, erase_func) \
do { \
    __BTD_ERASE_FUNC(&((node)->node_field), erase_func); \
} while (0)
//二叉树销毁, 销毁整个二叉树, 无论节点在哪个位置
#define BTD_DESTROY__(node, node_field, erase_func) \
do { \
    BTD_NODE *__n = &((node)->node_field); \
    while((__n->left.prev)?({__n=__n->left.prev;}):0 || (__n->right.prev)?({__n=__n->right.prev;}):0) ; \
    __BTD_ERASE_FUNC(__n, erase_func); \
} while (0)

//level: 当前节点所处层级, 通常为0
//output_func: void (*output_func)(BTD_NODE*)函数指针, 用于输出节点内容, 建议输出'\n'
//注意: 函数指针的参数为BTD_NODE*类型, 即&((node)->node_field)的类型
//      在函数指针中必须使用GET_NODE宏找回node地址
static inline void __BTD_DUMP_FUNC(BTD_NODE *__n, void (*output_func)(BTD_NODE*), unsigned int level, unsigned char *mark) {
    unsigned int i = 1;

    for(; i <= level; i++) {
        DEBUG_DUMP(INDENT);
        if(i != level) {
            if(mark[i]) DEBUG_DUMP("┃ ");
            else DEBUG_DUMP("   ");
        }
    }
        
    if(level > 0) {
        if(__n->left.next) {
            DEBUG_DUMP("┣ ");
            mark[level] = 1;
        }else {
            DEBUG_DUMP("┗ ");
            mark[level] = 0;
        }
    }
    
    if(output_func) output_func(__n);

    if(__n->right.next)
        __BTD_DUMP_FUNC(__n->right.next, output_func, level+1, mark);
    
    if(__n->left.next)
        __BTD_DUMP_FUNC(__n->left.next, output_func, level, mark);
}
//二叉树DUMP, 从当前节点(包括)向下输出
#define BTD_DUMP(node, node_field, output_func) \
do { \
    unsigned char mark[LEVEL_MAX] = {0}; \
    __BTD_DUMP_FUNC(&((node)->node_field), output_func, 0, mark); \
} while (0)

//获取struct type *指针
//btd_addr即函数指针的传入参数, type即例子中struct typexxx, field即fieldxxx
#define GET_NODE(btd_addr, type, field) \
({ const typeof(((type *)0)->field) *__mptr = (btd_addr); \
   (type *)((char *)__mptr - ((size_t) &((type *)0)->field)); })


#endif

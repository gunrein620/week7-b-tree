#include "btree.h"

#include <stdlib.h>
#include <string.h>

#define BTREE_ORDER 64

/* 노드 배열 크기는 BTREE_ORDER 로 잡아 "삽입 후 오버플로 발생 시 분할"
 * 패턴을 허용한다. 정상 상태의 최대 키 수는 BTREE_ORDER - 1. */
typedef struct BTreeNode {
    int is_leaf;
    int num_keys;
    int32_t keys[BTREE_ORDER];
    int64_t values[BTREE_ORDER];                 /* leaf 전용 */
    struct BTreeNode *children[BTREE_ORDER + 1]; /* internal 전용 */
    struct BTreeNode *next;                       /* leaf 연결 리스트 */
} BTreeNode;

struct BTree {
    BTreeNode *root;
    size_t size;
};

typedef struct {
    int did_split;
    int was_duplicate;
    int32_t sep_key;
    BTreeNode *right;
} InsertResult;

static BTreeNode *node_create(int is_leaf) {
    BTreeNode *n = (BTreeNode *)calloc(1, sizeof(BTreeNode));
    if (n == NULL) {
        return NULL;
    }
    n->is_leaf = is_leaf;
    return n;
}

static void node_free_recursive(BTreeNode *n) {
    int i;

    if (n == NULL) {
        return;
    }
    if (!n->is_leaf) {
        for (i = 0; i <= n->num_keys; ++i) {
            node_free_recursive(n->children[i]);
        }
    }
    free(n);
}

/* 첫 번째 keys[i] >= key 의 인덱스를 이진 탐색으로 찾는다. */
static int node_lower_bound(const BTreeNode *n, int32_t key) {
    int lo = 0;
    int hi = n->num_keys;

    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (n->keys[mid] < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static InsertResult split_leaf(BTreeNode *leaf) {
    InsertResult r;
    BTreeNode *right;
    int mid;
    int i;

    mid = BTREE_ORDER / 2;
    right = node_create(1);
    right->num_keys = leaf->num_keys - mid;
    for (i = 0; i < right->num_keys; ++i) {
        right->keys[i] = leaf->keys[mid + i];
        right->values[i] = leaf->values[mid + i];
    }
    leaf->num_keys = mid;

    /* leaf 연결 리스트 유지: left → right → (기존 left->next) */
    right->next = leaf->next;
    leaf->next = right;

    r.did_split = 1;
    r.was_duplicate = 0;
    /* B+ 트리: 분할 시 오른쪽 leaf 의 첫 키를 부모로 "복사 업" */
    r.sep_key = right->keys[0];
    r.right = right;
    return r;
}

static InsertResult split_internal(BTreeNode *node) {
    InsertResult r;
    BTreeNode *right;
    int mid;
    int i;

    mid = BTREE_ORDER / 2;
    right = node_create(0);
    right->num_keys = node->num_keys - mid - 1;
    for (i = 0; i < right->num_keys; ++i) {
        right->keys[i] = node->keys[mid + 1 + i];
    }
    for (i = 0; i <= right->num_keys; ++i) {
        right->children[i] = node->children[mid + 1 + i];
    }
    /* internal 분할 시 mid 키는 위로 "이동"한다 (복사 아님). */
    r.did_split = 1;
    r.was_duplicate = 0;
    r.sep_key = node->keys[mid];
    r.right = right;
    node->num_keys = mid;
    return r;
}

static InsertResult insert_into(BTreeNode *node, int32_t key, int64_t value) {
    InsertResult r;
    int pos;
    int i;

    r.did_split = 0;
    r.was_duplicate = 0;
    r.sep_key = 0;
    r.right = NULL;

    pos = node_lower_bound(node, key);

    if (node->is_leaf) {
        if (pos < node->num_keys && node->keys[pos] == key) {
            r.was_duplicate = 1;
            return r;
        }
        for (i = node->num_keys; i > pos; --i) {
            node->keys[i] = node->keys[i - 1];
            node->values[i] = node->values[i - 1];
        }
        node->keys[pos] = key;
        node->values[pos] = value;
        node->num_keys++;
        if (node->num_keys >= BTREE_ORDER) {
            return split_leaf(node);
        }
        return r;
    }

    /* internal: 하강할 자식 결정.
     * 구분키가 "오른쪽 서브트리의 최소 키"라서, key == keys[pos] 인 경우에도
     * 오른쪽 자식(pos+1)으로 내려가야 그 키를 가진 leaf 를 만난다. */
    {
        int child_idx = pos;
        InsertResult sub;

        if (pos < node->num_keys && node->keys[pos] == key) {
            child_idx = pos + 1;
        }
        sub = insert_into(node->children[child_idx], key, value);

        if (sub.was_duplicate) {
            r.was_duplicate = 1;
            return r;
        }
        if (!sub.did_split) {
            return r;
        }

        /* 자식이 분할됐으면 sep_key/right 를 현재 노드에 삽입 */
        {
            int ins_pos = node_lower_bound(node, sub.sep_key);
            for (i = node->num_keys; i > ins_pos; --i) {
                node->keys[i] = node->keys[i - 1];
            }
            for (i = node->num_keys + 1; i > ins_pos + 1; --i) {
                node->children[i] = node->children[i - 1];
            }
            node->keys[ins_pos] = sub.sep_key;
            node->children[ins_pos + 1] = sub.right;
            node->num_keys++;
            if (node->num_keys >= BTREE_ORDER) {
                return split_internal(node);
            }
        }
        return r;
    }
}

BTree *btree_create(void) {
    BTree *t = (BTree *)calloc(1, sizeof(BTree));
    if (t == NULL) {
        return NULL;
    }
    t->root = node_create(1);
    if (t->root == NULL) {
        free(t);
        return NULL;
    }
    t->size = 0;
    return t;
}

void btree_free(BTree *tree) {
    if (tree == NULL) {
        return;
    }
    node_free_recursive(tree->root);
    free(tree);
}

int btree_insert(BTree *tree, int32_t key, int64_t offset) {
    InsertResult r;

    if (tree == NULL || tree->root == NULL) {
        return -2;
    }
    r = insert_into(tree->root, key, offset);
    if (r.was_duplicate) {
        return -1;
    }
    if (r.did_split) {
        BTreeNode *new_root = node_create(0);
        if (new_root == NULL) {
            return -2;
        }
        new_root->num_keys = 1;
        new_root->keys[0] = r.sep_key;
        new_root->children[0] = tree->root;
        new_root->children[1] = r.right;
        tree->root = new_root;
    }
    tree->size++;
    return 0;
}

int btree_find(BTree *tree, int32_t key, int64_t *out_offset) {
    BTreeNode *n;
    int pos;

    if (tree == NULL || tree->root == NULL) {
        return 0;
    }
    n = tree->root;
    while (!n->is_leaf) {
        int child_idx;
        pos = node_lower_bound(n, key);
        child_idx = pos;
        if (pos < n->num_keys && n->keys[pos] == key) {
            child_idx = pos + 1;
        }
        n = n->children[child_idx];
    }
    pos = node_lower_bound(n, key);
    if (pos < n->num_keys && n->keys[pos] == key) {
        if (out_offset != NULL) {
            *out_offset = n->values[pos];
        }
        return 1;
    }
    return 0;
}

int32_t btree_max_key(BTree *tree) {
    BTreeNode *n;

    if (tree == NULL || tree->root == NULL || tree->size == 0) {
        return 0;
    }
    n = tree->root;
    while (!n->is_leaf) {
        n = n->children[n->num_keys];
    }
    if (n->num_keys == 0) {
        return 0;
    }
    return n->keys[n->num_keys - 1];
}

size_t btree_size(BTree *tree) {
    return tree ? tree->size : 0;
}

int btree_visit_in_order(BTree *tree, BTreeVisitFn visitor, void *ctx) {
    BTreeNode *node;
    int i;

    if (tree == NULL || tree->root == NULL || visitor == NULL) {
        return 0;
    }

    node = tree->root;
    while (!node->is_leaf) {
        node = node->children[0];
    }

    while (node != NULL) {
        for (i = 0; i < node->num_keys; ++i) {
            if (!visitor(node->keys[i], node->values[i], ctx)) {
                return 0;
            }
        }
        node = node->next;
    }

    return 1;
}

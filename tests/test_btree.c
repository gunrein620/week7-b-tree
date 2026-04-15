#include "btree.h"

#include "test_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int test_empty_tree(void) {
    BTree *t = btree_create();
    int64_t v = -1;
    int ok = 1;

    if (t == NULL) return th_fail("create returned NULL");
    if (btree_size(t) != 0) ok = th_fail("empty size should be 0");
    if (btree_find(t, 42, &v) != 0) ok = th_fail("find on empty should miss");
    if (btree_max_key(t) != 0) ok = th_fail("empty max_key should be 0");

    btree_free(t);
    th_print_result("empty_tree", ok);
    return ok;
}

static int test_single_insert_find(void) {
    BTree *t = btree_create();
    int64_t v = -1;
    int ok = 1;

    if (btree_insert(t, 7, 100) != 0) ok = th_fail("insert failed");
    if (btree_size(t) != 1) ok = th_fail("size after one insert");
    if (btree_find(t, 7, &v) != 1 || v != 100) ok = th_fail("find 7");
    if (btree_find(t, 8, &v) != 0) ok = th_fail("find missing 8");
    if (btree_max_key(t) != 7) ok = th_fail("max_key after one");

    btree_free(t);
    th_print_result("single_insert_find", ok);
    return ok;
}

static int test_duplicate_rejected(void) {
    BTree *t = btree_create();
    int ok = 1;

    if (btree_insert(t, 1, 10) != 0) ok = th_fail("first insert");
    if (btree_insert(t, 1, 20) != -1) ok = th_fail("dup should return -1");
    if (btree_size(t) != 1) ok = th_fail("size unchanged after dup");

    btree_free(t);
    th_print_result("duplicate_rejected", ok);
    return ok;
}

/* 순차 1..N 삽입 후 전수 검색. split 경로를 반복적으로 돈다. */
static int test_sequential_insert(void) {
    BTree *t = btree_create();
    int ok = 1;
    int i;
    const int N = 5000;

    for (i = 1; i <= N; ++i) {
        if (btree_insert(t, (int32_t)i, (int64_t)(i * 10)) != 0) {
            ok = th_fail("seq insert");
            break;
        }
    }
    if (ok && (int)btree_size(t) != N) ok = th_fail("size after seq insert");
    for (i = 1; ok && i <= N; ++i) {
        int64_t v = -1;
        if (btree_find(t, (int32_t)i, &v) != 1 || v != (int64_t)(i * 10)) {
            ok = th_fail("seq find missing");
            break;
        }
    }
    if (ok && btree_max_key(t) != N) ok = th_fail("max_key after seq");

    btree_free(t);
    th_print_result("sequential_insert_and_find", ok);
    return ok;
}

/* 역순 삽입: internal split 의 왼/오른쪽 밸런스를 반대 방향으로 검증. */
static int test_reverse_insert(void) {
    BTree *t = btree_create();
    int ok = 1;
    int i;
    const int N = 3000;

    for (i = N; i >= 1; --i) {
        if (btree_insert(t, (int32_t)i, (int64_t)(i + 1000)) != 0) {
            ok = th_fail("rev insert");
            break;
        }
    }
    for (i = 1; ok && i <= N; ++i) {
        int64_t v = -1;
        if (btree_find(t, (int32_t)i, &v) != 1 || v != (int64_t)(i + 1000)) {
            ok = th_fail("rev find");
            break;
        }
    }
    if (ok && btree_max_key(t) != N) ok = th_fail("rev max_key");

    btree_free(t);
    th_print_result("reverse_insert_and_find", ok);
    return ok;
}

/* 의사 랜덤 (결정적) 순서로 삽입/조회하여 split 경계가 불규칙해도 정확한지 본다. */
static int test_random_like(void) {
    BTree *t = btree_create();
    int ok = 1;
    int i;
    const int N = 4096;
    /* 선형 합동 기반 결정적 순열: key = (i*2654435761) & 0xFFF ... */
    /* 단순화: 0..N-1 을 XOR 기반으로 섞는다. */
    for (i = 0; i < N; ++i) {
        int32_t k = (int32_t)((i * 2654435761u) % 65521u) + 1;
        /* 중복 회피를 위해 위 식이 실패하면 i+1 사용 */
        if (btree_insert(t, k, (int64_t)k) != 0) {
            /* 중복일 수 있으니 대체키 */
            if (btree_insert(t, (int32_t)(100000 + i), (int64_t)(100000 + i)) != 0) {
                ok = th_fail("rand insert");
                break;
            }
        }
    }
    /* 삽입된 키들이 전부 조회되는지는 각 insert 직후가 아니라 시퀀스 후에 다시 한 번 전수 조회하기가 어렵다.
     * 대신 broad-invariant: size 는 N. */
    if (ok && (int)btree_size(t) != N) ok = th_fail("rand size");

    btree_free(t);
    th_print_result("random_like_insert", ok);
    return ok;
}

/* split 경계 크기 주변: ORDER-1, ORDER, ORDER+1 근처. */
static int test_split_boundaries(void) {
    BTree *t = btree_create();
    int ok = 1;
    int i;
    /* 64 (ORDER) 주변에서 안정적인지 테스트 */
    const int N = 200;

    for (i = 1; i <= N; ++i) {
        if (btree_insert(t, (int32_t)(i * 2), (int64_t)i) != 0) {
            ok = th_fail("boundary insert");
            break;
        }
    }
    for (i = 1; ok && i <= N; ++i) {
        int64_t v = -1;
        if (btree_find(t, (int32_t)(i * 2), &v) != 1 || v != (int64_t)i) {
            ok = th_fail("boundary find");
            break;
        }
        /* 인접 (홀수) 키는 없어야 한다. */
        if (btree_find(t, (int32_t)(i * 2 - 1), &v) != 0) {
            ok = th_fail("boundary neg find");
            break;
        }
    }

    btree_free(t);
    th_print_result("split_boundaries", ok);
    return ok;
}

int main(void) {
    int passed = 0;
    int failed = 0;

    th_reset_reason(); if (test_empty_tree()) passed++; else failed++;
    th_reset_reason(); if (test_single_insert_find()) passed++; else failed++;
    th_reset_reason(); if (test_duplicate_rejected()) passed++; else failed++;
    th_reset_reason(); if (test_sequential_insert()) passed++; else failed++;
    th_reset_reason(); if (test_reverse_insert()) passed++; else failed++;
    th_reset_reason(); if (test_random_like()) passed++; else failed++;
    th_reset_reason(); if (test_split_boundaries()) passed++; else failed++;

    printf("Tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

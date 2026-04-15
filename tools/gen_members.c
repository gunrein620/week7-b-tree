/*
 * tools/gen_members  —  week7 B+ tree 벤치마크용 members 더미 데이터 생성기
 *
 * 두 가지 모드를 지원한다:
 *   --mode tbl  (기본)  : 파이프 구분 .tbl 파일을 직접 작성한다.
 *                         SQL 파싱/중복검사 오버헤드를 우회하여 100만 행을
 *                         수 초 안에 준비할 수 있다. 엔진은 다음 SELECT 시
 *                         IndexManager 가 lazy build 로 인덱스를 구축한다.
 *   --mode sql           : INSERT 문 N 개를 찍는다. executor → btree_insert
 *                         전체 경로를 실제로 통과시키는 correctness 용.
 *
 * 사용 예:
 *   ./tools/gen_members 1000000                       # data/members.tbl 덮어쓰기
 *   ./tools/gen_members 1000000 --out /tmp/m.tbl      # 경로 지정
 *   ./tools/gen_members 10000 --mode sql --out s.sql  # SQL 모드
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *GRADES[] = {"vip", "gold", "silver", "normal"};
static const int GRADES_LEN = 4;
static const char *CLASSES[] = {"beginner", "basic", "intermediate", "advanced", "expert"};
static const int CLASSES_LEN = 5;

static void write_header_tbl(FILE *f) {
    fputs("id|name|grade|class|age\n", f);
}

static void write_row_tbl(FILE *f, int id) {
    fprintf(f,
            "%d|name_%07d|%s|%s|%d\n",
            id,
            id,
            GRADES[id % GRADES_LEN],
            CLASSES[id % CLASSES_LEN],
            20 + (id % 60));
}

static void write_row_sql(FILE *f, int id) {
    fprintf(f,
            "INSERT INTO members (id, name, grade, class, age) VALUES "
            "(%d, 'name_%07d', '%s', '%s', %d);\n",
            id,
            id,
            GRADES[id % GRADES_LEN],
            CLASSES[id % CLASSES_LEN],
            20 + (id % 60));
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s <count> [--mode tbl|sql] [--out path]\n"
            "  default --mode tbl, default --out data/members.tbl (tbl) or stdout (sql)\n",
            prog);
}

int main(int argc, char **argv) {
    long count;
    const char *mode = "tbl";
    const char *out_path = NULL;
    FILE *f;
    long i;
    char *end_ptr;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    count = strtol(argv[1], &end_ptr, 10);
    if (*end_ptr != '\0' || count <= 0) {
        usage(argv[0]);
        return 1;
    }

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (strcmp(mode, "tbl") == 0) {
        if (out_path == NULL) {
            out_path = "data/members.tbl";
        }
        f = fopen(out_path, "w");
        if (f == NULL) {
            fprintf(stderr, "failed to open %s\n", out_path);
            return 1;
        }
        write_header_tbl(f);
        for (i = 1; i <= count; ++i) {
            write_row_tbl(f, (int)i);
        }
        fclose(f);
        fprintf(stderr, "wrote %ld rows to %s\n", count, out_path);
        return 0;
    }

    if (strcmp(mode, "sql") == 0) {
        if (out_path == NULL) {
            f = stdout;
        } else {
            f = fopen(out_path, "w");
            if (f == NULL) {
                fprintf(stderr, "failed to open %s\n", out_path);
                return 1;
            }
        }
        for (i = 1; i <= count; ++i) {
            write_row_sql(f, (int)i);
        }
        if (f != stdout) {
            fclose(f);
            fprintf(stderr, "wrote %ld INSERT statements to %s\n", count, out_path);
        }
        return 0;
    }

    usage(argv[0]);
    return 1;
}

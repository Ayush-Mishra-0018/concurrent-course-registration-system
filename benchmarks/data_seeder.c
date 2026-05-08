/*
 * data_seeder.c - Populates test data directly via file_ops for benchmarking.
 *
 * Usage:
 *   ./data_seeder --seed      Create 250 test students + 5 test courses
 *   ./data_seeder --cleanup   Remove seeded test data
 *   ./data_seeder --status    Show seeded record counts
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "../utils/file_ops.h"

/* Test data ranges */
#define SEED_STUDENT_ID_START  9001
#define SEED_STUDENT_ID_END    9250
#define SEED_STUDENT_COUNT     250
#define SEED_COURSE_ID_START   101
#define SEED_COURSE_COUNT      5
#define SEED_PASSWORD          "test1234"
#define SEED_SEATS             30

static const char *COURSE_NAMES[SEED_COURSE_COUNT] = {
    "Operating Systems",
    "Data Structures",
    "Computer Networks",
    "Database Systems",
    "Algorithms"
};
static const char *COURSE_CODES[SEED_COURSE_COUNT] = {
    "CS501", "CS502", "CS503", "CS504", "CS505"
};

/* Seed test data */
static void do_seed(void) {
    printf("[seeder] Seeding %d test students (IDs %d-%d)...\n",
           SEED_STUDENT_COUNT, SEED_STUDENT_ID_START, SEED_STUDENT_ID_END);

    int created = 0, reactivated = 0;
    for (int id = SEED_STUDENT_ID_START; id <= SEED_STUDENT_ID_END; id++) {
        Student s;
        if (get_student_by_id(id, &s) == SUCCESS) {
            /* Re-activate and reset course list for a clean run */
            s.status = ACTIVE;
            s.course_count = 0;
            memset(s.enrolled_courses, 0, sizeof(s.enrolled_courses));
            update_student(s);
            reactivated++;
            continue;
        }
        memset(&s, 0, sizeof(s));
        s.id     = id;
        s.status = ACTIVE;
        s.course_count = 0;
        snprintf(s.name,     sizeof(s.name),     "TestStudent%d", id);
        snprintf(s.password, sizeof(s.password), "%s", SEED_PASSWORD);
        if (add_student(s) == SUCCESS) created++;
        else fprintf(stderr, "[seeder] WARNING: failed to add student %d\n", id);
    }
    printf("[seeder] Students: %d created, %d reactivated/reset\n", created, reactivated);

    printf("[seeder] Seeding %d test courses (IDs %d-%d)...\n",
           SEED_COURSE_COUNT,
           SEED_COURSE_ID_START,
           SEED_COURSE_ID_START + SEED_COURSE_COUNT - 1);

    int courses_created = 0, courses_reset = 0;
    for (int i = 0; i < SEED_COURSE_COUNT; i++) {
        Course c;
        int cid = SEED_COURSE_ID_START + i;
        if (get_course_by_id(cid, &c) == SUCCESS) {
            /* Reset seat count for clean re-run */
            c.available_seats = SEED_SEATS;
            c.status = ACTIVE;
            update_course(c);
            courses_reset++;
            continue;
        }
        memset(&c, 0, sizeof(c));
        c.id              = cid;
        c.faculty_id      = 1;
        c.max_seats       = SEED_SEATS;
        c.available_seats = SEED_SEATS;
        c.status          = ACTIVE;
        strncpy(c.name, COURSE_NAMES[i], sizeof(c.name) - 1);
        strncpy(c.code, COURSE_CODES[i], sizeof(c.code) - 1);
        if (add_course(c) == SUCCESS) courses_created++;
        else fprintf(stderr, "[seeder] WARNING: failed to add course %d\n", cid);
    }
    printf("[seeder] Courses: %d created, %d seat-reset\n", courses_created, courses_reset);

    /* Truncate enrollment file to clear stale records from previous runs */
    int efd = open(ENROLLMENT_FILE, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (efd >= 0) { close(efd); printf("[seeder] Enrollment file cleared\n"); }

    printf("[seeder] Seed complete. Use --cleanup to remove data.\n");
}

/* Cleanup seeded data */
static void do_cleanup(void) {
    printf("[seeder] Removing seeded students (IDs %d-%d)...\n",
           SEED_STUDENT_ID_START, SEED_STUDENT_ID_END);

    int removed = 0;
    for (int id = SEED_STUDENT_ID_START; id <= SEED_STUDENT_ID_END; id++) {
        if (deactivate_student(id) == SUCCESS) removed++;
    }
    printf("[seeder] Deactivated %d students\n", removed);

    printf("[seeder] Removing seeded courses (IDs %d-%d)...\n",
           SEED_COURSE_ID_START,
           SEED_COURSE_ID_START + SEED_COURSE_COUNT - 1);
    removed = 0;
    for (int i = 0; i < SEED_COURSE_COUNT; i++) {
        if (remove_course(SEED_COURSE_ID_START + i) == SUCCESS) removed++;
    }
    printf("[seeder] Removed %d courses\n", removed);
}

/* Status check */
static void do_status(void) {
    printf("[seeder] Checking seeded data status...\n");
    int found = 0;
    for (int id = SEED_STUDENT_ID_START; id <= SEED_STUDENT_ID_END; id++) {
        Student s;
        if (get_student_by_id(id, &s) == SUCCESS && s.status == ACTIVE) found++;
    }
    printf("[seeder] Active test students : %d / %d\n", found, SEED_STUDENT_COUNT);

    found = 0;
    for (int i = 0; i < SEED_COURSE_COUNT; i++) {
        Course c;
        if (get_course_by_id(SEED_COURSE_ID_START + i, &c) == SUCCESS) found++;
    }
    printf("[seeder] Test courses found   : %d / %d\n", found, SEED_COURSE_COUNT);
}

int main(int argc, char *argv[]) {
    init_data_files();

    if (argc < 2) {
        fprintf(stderr,
            "Usage: %s [--seed | --cleanup | --status]\n"
            "  --seed     Create %d test students + %d courses\n"
            "  --cleanup  Deactivate seeded students & courses\n"
            "  --status   Check how many seeded records exist\n",
            argv[0], SEED_STUDENT_COUNT, SEED_COURSE_COUNT);
        return 1;
    }

    if      (strcmp(argv[1], "--seed")    == 0) do_seed();
    else if (strcmp(argv[1], "--cleanup") == 0) do_cleanup();
    else if (strcmp(argv[1], "--status")  == 0) do_status();
    else { fprintf(stderr, "Unknown flag: %s\n", argv[1]); return 1; }

    return 0;
}

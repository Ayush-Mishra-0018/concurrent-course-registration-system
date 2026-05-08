#include "file_ops.h"
#include "../metrics/lock_stats.h"
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

/* enroll_student - race-condition-free implementation.
 *
 * Uses g_course_mutex + g_enroll_mutex (pthread mutexes) for intra-process
 * thread safety. Linux fcntl locks do not block threads within the same
 * process, so we use mutexes as the primary synchronization and retain
 * fcntl locks for inter-process correctness.
 *
 * Lock order: g_course_mutex -> g_enroll_mutex (consistent to prevent deadlock)
 */
int enroll_student(int student_id, int course_id) {
    /* Verify student exists (read-only, no lock needed here) */
    Student student;
    int result = get_student_by_id(student_id, &student);
    if (result != SUCCESS) return result;

    /* Acquire course mutex first */
    pthread_mutex_lock(&g_course_mutex);

    int course_fd = open(COURSE_FILE, O_RDWR);
    if (course_fd < 0) {
        pthread_mutex_unlock(&g_course_mutex);
        return FAILURE;
    }

    if (lock_file(course_fd, WRITE_LOCK) < 0) {
        close(course_fd);
        pthread_mutex_unlock(&g_course_mutex);
        return FAILURE;
    }

    /* Re-read course under mutex+lock - authoritative seat count */
    Course course;
    int found = 0;
    off_t course_offset = 0;
    Course ctmp;
    lseek(course_fd, 0, SEEK_SET);
    while (1) {
        course_offset = lseek(course_fd, 0, SEEK_CUR);
        if (read(course_fd, &ctmp, sizeof(Course)) <= 0) break;
        if (ctmp.id == course_id) { course = ctmp; found = 1; break; }
    }

    if (!found || course.status != ACTIVE) {
        unlock_file(course_fd);
        close(course_fd);
        pthread_mutex_unlock(&g_course_mutex);
        return COURSE_NOT_FOUND;
    }

    /* Authoritative seat check - under mutex + lock */
    if (course.available_seats <= 0) {
        unlock_file(course_fd);
        close(course_fd);
        pthread_mutex_unlock(&g_course_mutex);
        return COURSE_FULL;
    }

    /* Now acquire enrollment mutex */
    pthread_mutex_lock(&g_enroll_mutex);

    int enroll_fd = open(ENROLLMENT_FILE, O_RDWR | O_CREAT, 0644);
    if (enroll_fd < 0) {
        pthread_mutex_unlock(&g_enroll_mutex);
        unlock_file(course_fd);
        close(course_fd);
        pthread_mutex_unlock(&g_course_mutex);
        return FAILURE;
    }

    if (lock_file(enroll_fd, WRITE_LOCK) < 0) {
        close(enroll_fd);
        pthread_mutex_unlock(&g_enroll_mutex);
        unlock_file(course_fd);
        close(course_fd);
        pthread_mutex_unlock(&g_course_mutex);
        return FAILURE;
    }

    Enrollment temp;
    int exists = 0;
    int next_id = 1;

    lseek(enroll_fd, 0, SEEK_SET);
    while (read(enroll_fd, &temp, sizeof(Enrollment)) > 0) {
        if (temp.id >= next_id) next_id = temp.id + 1;
        if (temp.student_id == student_id && temp.course_id == course_id) {
            exists = 1;
            break;
        }
    }

    if (exists) {
        unlock_file(enroll_fd);
        close(enroll_fd);
        pthread_mutex_unlock(&g_enroll_mutex);
        unlock_file(course_fd);
        close(course_fd);
        pthread_mutex_unlock(&g_course_mutex);
        return ALREADY_ENROLLED;
    }

    /* Write enrollment record */
    Enrollment enrollment;
    enrollment.id              = next_id;
    enrollment.student_id      = student_id;
    enrollment.course_id       = course_id;
    enrollment.enrollment_date = time(NULL);

    lseek(enroll_fd, 0, SEEK_END);
    if (write(enroll_fd, &enrollment, sizeof(Enrollment)) <= 0) {
        unlock_file(enroll_fd);
        close(enroll_fd);
        pthread_mutex_unlock(&g_enroll_mutex);
        unlock_file(course_fd);
        close(course_fd);
        pthread_mutex_unlock(&g_course_mutex);
        return FAILURE;
    }

    unlock_file(enroll_fd);
    close(enroll_fd);
    pthread_mutex_unlock(&g_enroll_mutex);

    /* Decrement seat count while still holding course mutex + lock */
    course.available_seats--;
    lseek(course_fd, course_offset, SEEK_SET);
    write(course_fd, &course, sizeof(Course));
    fsync(course_fd);

    unlock_file(course_fd);
    close(course_fd);
    pthread_mutex_unlock(&g_course_mutex);

    /* Update student enrolled course list (best-effort, no mutex needed for student) */
    for (int i = 0; i < student.course_count; i++) {
        if (student.enrolled_courses[i] == course_id) return SUCCESS;
    }
    if (student.course_count < 10) {
        pthread_mutex_lock(&g_student_mutex);
        /* Re-read student to get latest course_count (another thread may have updated) */
        get_student_by_id(student_id, &student);
        if (student.course_count < 10) {
            student.enrolled_courses[student.course_count++] = course_id;
            update_student(student);
        }
        pthread_mutex_unlock(&g_student_mutex);
    }

    return SUCCESS;
}

int unenroll_student(int student_id, int course_id) {
    Student student;
    int result = get_student_by_id(student_id, &student);
    if (result != SUCCESS) return result;

    Course course;
    result = get_course_by_id(course_id, &course);
    if (result != SUCCESS) return result;

    pthread_mutex_lock(&g_enroll_mutex);

    int fd = open(ENROLLMENT_FILE, O_RDWR);
    if (fd < 0) {
        pthread_mutex_unlock(&g_enroll_mutex);
        return FAILURE;
    }

    if (lock_file(fd, WRITE_LOCK) < 0) {
        close(fd);
        pthread_mutex_unlock(&g_enroll_mutex);
        return FAILURE;
    }

    Enrollment temp;
    int found = 0;
    off_t offset;

    while (1) {
        offset = lseek(fd, 0, SEEK_CUR);
        if (read(fd, &temp, sizeof(Enrollment)) <= 0) break;
        if (temp.student_id == student_id && temp.course_id == course_id) {
            temp.student_id = -1;
            lseek(fd, offset, SEEK_SET);
            write(fd, &temp, sizeof(Enrollment));
            found = 1;
            break;
        }
    }

    unlock_file(fd);
    close(fd);
    pthread_mutex_unlock(&g_enroll_mutex);

    if (!found) return NOT_ENROLLED;

    pthread_mutex_lock(&g_course_mutex);
    course.available_seats++;
    update_course(course);
    pthread_mutex_unlock(&g_course_mutex);

    pthread_mutex_lock(&g_student_mutex);
    get_student_by_id(student_id, &student);
    for (int i = 0; i < student.course_count; i++) {
        if (student.enrolled_courses[i] == course_id) {
            for (int j = i; j < student.course_count - 1; j++)
                student.enrolled_courses[j] = student.enrolled_courses[j + 1];
            student.course_count--;
            break;
        }
    }
    update_student(student);
    pthread_mutex_unlock(&g_student_mutex);

    return SUCCESS;
}

int get_student_enrollments(int student_id, Enrollment *enrollments, int *count) {
    int fd = open(ENROLLMENT_FILE, O_RDONLY);
    if (fd < 0) return FAILURE;

    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }

    Enrollment temp;
    *count = 0;

    while (read(fd, &temp, sizeof(Enrollment)) > 0 && *count < 100) {
        if (temp.student_id == student_id) {
            enrollments[*count] = temp;
            (*count)++;
        }
    }

    unlock_file(fd);
    close(fd);
    return SUCCESS;
}

int get_course_enrollments(int course_id, Enrollment *enrollments, int *count) {
    int fd = open(ENROLLMENT_FILE, O_RDONLY);
    if (fd < 0) return FAILURE;

    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }

    Enrollment temp;
    *count = 0;

    while (read(fd, &temp, sizeof(Enrollment)) > 0 && *count < 100) {
        if (temp.course_id == course_id && temp.student_id > 0) {
            enrollments[*count] = temp;
            (*count)++;
        }
    }

    unlock_file(fd);
    close(fd);
    return SUCCESS;
}
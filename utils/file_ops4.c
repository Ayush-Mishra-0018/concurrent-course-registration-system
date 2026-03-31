#include "file_ops.h"
#include <time.h>

// Enrollment file operations
int enroll_student(int student_id, int course_id) {
    // Check if student exists
    Student student;
    int result = get_student_by_id(student_id, &student);
    if (result != SUCCESS) return result;
    
    // Check if course exists and has available seats
    Course course;
    result = get_course_by_id(course_id, &course);
    if (result != SUCCESS) return result;
    
    if (course.available_seats <= 0) return COURSE_FULL;
    
    // Check if student is already enrolled in this course
    int fd = open(ENROLLMENT_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, WRITE_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Enrollment temp;
    int exists = 0;
    int next_id = 1;
    
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &temp, sizeof(Enrollment)) > 0) {
        if (temp.id >= next_id) next_id = temp.id + 1;
        if (temp.student_id == student_id && temp.course_id == course_id) {
            exists = 1;
            break;
        }
    }
    
    if (exists) {
        unlock_file(fd);
        close(fd);
        return ALREADY_ENROLLED;
    }
    
    // Create new enrollment
    Enrollment enrollment;
    enrollment.id = next_id;
    enrollment.student_id = student_id;
    enrollment.course_id = course_id;
    enrollment.enrollment_date = time(NULL);
    
    // Write enrollment record
    lseek(fd, 0, SEEK_END);
    write(fd, &enrollment, sizeof(Enrollment));
    
    unlock_file(fd);
    close(fd);
    
    // Update course available seats
    course.available_seats--;
    update_course(course);
    
    // Update student's enrolled courses
    for (int i = 0; i < student.course_count; i++) {
        if (student.enrolled_courses[i] == course_id) {
            // Already in list (shouldn't happen, but just in case)
            return SUCCESS;
        }
    }
    
    if (student.course_count < 10) {
        student.enrolled_courses[student.course_count++] = course_id;
        update_student(student);
    }
    
    return SUCCESS;
}

int unenroll_student(int student_id, int course_id) {
    // Check if student exists
    Student student;
    int result = get_student_by_id(student_id, &student);
    if (result != SUCCESS) return result;
    
    // Check if course exists
    Course course;
    result = get_course_by_id(course_id, &course);
    if (result != SUCCESS) return result;
    
    // Check if student is enrolled in this course
    int fd = open(ENROLLMENT_FILE, O_RDWR);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, WRITE_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Enrollment temp;
    int found = 0;
    off_t offset;
    
    while (1) {
        offset = lseek(fd, 0, SEEK_CUR);
        if (read(fd, &temp, sizeof(Enrollment)) <= 0) break;
        
        if (temp.student_id == student_id && temp.course_id == course_id) {
            // Mark this record by setting student_id to -1 (soft delete)
            temp.student_id = -1;
            lseek(fd, offset, SEEK_SET);
            write(fd, &temp, sizeof(Enrollment));
            found = 1;
            break;
        }
    }
    
    unlock_file(fd);
    close(fd);
    
    if (!found) return NOT_ENROLLED;
    
    // Update course available seats
    course.available_seats++;
    update_course(course);
    
    // Update student's enrolled courses
    int i;
    for (i = 0; i < student.course_count; i++) {
        if (student.enrolled_courses[i] == course_id) {
            // Remove this course from the list by shifting elements
            for (int j = i; j < student.course_count - 1; j++) {
                student.enrolled_courses[j] = student.enrolled_courses[j + 1];
            }
            student.course_count--;
            break;
        }
    }
    
    update_student(student);
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
#include "file_ops.h"

// Course file operations
int add_course(Course course) {
    int fd = open(COURSE_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, WRITE_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    // Check if course ID or code already exists (only for active courses)
    Course temp;
    int exists = 0;
    
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &temp, sizeof(Course)) > 0) {
        // Only check active courses
        if (temp.status == ACTIVE && (temp.id == course.id || strcmp(temp.code, course.code) == 0)) {
            exists = 1;
            break;
        }
    }
    
    if (exists) {
        unlock_file(fd);
        close(fd);
        return COURSE_EXISTS;
    }
    
    // Explicitly set status to ACTIVE
    course.status = ACTIVE;
    
    // Go to end of file
    lseek(fd, 0, SEEK_END);
    
    // Write course record
    write(fd, &course, sizeof(Course));
    
    unlock_file(fd);
    close(fd);
    return SUCCESS;
}

int get_course_by_id(int id, Course *course) {
    int fd = open(COURSE_FILE, O_RDONLY);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Course temp;
    int found = 0;
    
    while (read(fd, &temp, sizeof(Course)) > 0) {
        if (temp.id == id) {
            *course = temp;
            found = 1;
            break;
        }
    }
    
    unlock_file(fd);
    close(fd);
    return found ? SUCCESS : COURSE_NOT_FOUND;
}

int update_course(Course course) {
    int fd = open(COURSE_FILE, O_RDWR);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, WRITE_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Course temp;
    int found = 0;
    off_t offset;
    
    while (1) {
        offset = lseek(fd, 0, SEEK_CUR);
        if (read(fd, &temp, sizeof(Course)) <= 0) break;
        
        if (temp.id == course.id) {
            lseek(fd, offset, SEEK_SET);
            write(fd, &course, sizeof(Course));
            found = 1;
            break;
        }
    }
    
    unlock_file(fd);
    close(fd);
    return found ? SUCCESS : COURSE_NOT_FOUND;
}

int remove_course(int id) {
    Course course;
    int result = get_course_by_id(id, &course);
    
    if (result != SUCCESS) return result;
    
    course.status = INACTIVE;
    result = update_course(course);
    
    // Clean the database to ensure inactive courses are properly purged
    purge_inactive_courses();
    
    return result;
}

// Function to purge inactive courses
int purge_inactive_courses() {
    int fd_old = open(COURSE_FILE, O_RDONLY);
    if (fd_old < 0) return FAILURE;
    
    // Create a temp file for rewriting
    char temp_file[100];
    sprintf(temp_file, "%s.tmp", COURSE_FILE);
    int fd_new = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    
    if (fd_new < 0) {
        close(fd_old);
        return FAILURE;
    }
    
    // Lock both files
    if (lock_file(fd_old, READ_LOCK) < 0 || lock_file(fd_new, WRITE_LOCK) < 0) {
        close(fd_old);
        close(fd_new);
        unlink(temp_file);
        return FAILURE;
    }
    
    // Read from old file and write active courses to new file
    Course course;
    while (read(fd_old, &course, sizeof(Course)) > 0) {
        if (course.status == ACTIVE) {
            write(fd_new, &course, sizeof(Course));
        }
    }
    
    // Unlock and close both files
    unlock_file(fd_old);
    unlock_file(fd_new);
    close(fd_old);
    close(fd_new);
    
    // Replace the old file with the new one
    if (rename(temp_file, COURSE_FILE) < 0) {
        // If rename fails, remove the temp file
        unlink(temp_file);
        return FAILURE;
    }
    
    return SUCCESS;
}

int get_all_courses(Course *course_list, int *count) {
    int fd = open(COURSE_FILE, O_RDONLY);
    if (fd < 0) {
        *count = 0;
        return FAILURE;
    }
    
    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        *count = 0;
        return FAILURE;
    }
    
    Course temp;
    *count = 0;
    
    while (read(fd, &temp, sizeof(Course)) > 0 && *count < 100) {
        if (temp.status == ACTIVE) {
            course_list[*count] = temp;
            (*count)++;
        }
    }
    
    unlock_file(fd);
    close(fd);
    return SUCCESS;
}

int get_courses_by_faculty(int faculty_id, Course *course_list, int *count) {
    int fd = open(COURSE_FILE, O_RDONLY);
    if (fd < 0) {
        *count = 0;
        return FAILURE;
    }
    
    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        *count = 0;
        return FAILURE;
    }
    
    Course temp;
    *count = 0;
    
    while (read(fd, &temp, sizeof(Course)) > 0 && *count < 100) {
        if (temp.faculty_id == faculty_id && temp.status == ACTIVE) {
            course_list[*count] = temp;
            (*count)++;
        }
    }
    
    unlock_file(fd);
    close(fd);
    return SUCCESS;
} 
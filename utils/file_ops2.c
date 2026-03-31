#include "file_ops.h"

// Student file operations
int add_student(Student student) {
    int fd = open(STUDENT_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, WRITE_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    // Check if student ID already exists
    Student temp;
    int exists = 0;
    
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &temp, sizeof(Student)) > 0) {
        if (temp.id == student.id) {
            exists = 1;
            break;
        }
    }
    
    if (exists) {
        unlock_file(fd);
        close(fd);
        return USER_EXISTS;
    }
    
    // Go to end of file
    lseek(fd, 0, SEEK_END);
    
    // Write student record
    write(fd, &student, sizeof(Student));
    
    unlock_file(fd);
    close(fd);
    return SUCCESS;
}

int get_student_by_id(int id, Student *student) {
    int fd = open(STUDENT_FILE, O_RDONLY);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Student temp;
    int found = 0;
    
    while (read(fd, &temp, sizeof(Student)) > 0) {
        if (temp.id == id) {
            *student = temp;
            found = 1;
            break;
        }
    }
    
    unlock_file(fd);
    close(fd);
    return found ? SUCCESS : USER_NOT_FOUND;
}

int update_student(Student student) {
    int fd = open(STUDENT_FILE, O_RDWR);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, WRITE_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Student temp;
    int found = 0;
    off_t offset;
    
    while (1) {
        offset = lseek(fd, 0, SEEK_CUR);
        if (read(fd, &temp, sizeof(Student)) <= 0) break;
        
        if (temp.id == student.id) {
            lseek(fd, offset, SEEK_SET);
            write(fd, &student, sizeof(Student));
            found = 1;
            break;
        }
    }
    
    unlock_file(fd);
    close(fd);
    return found ? SUCCESS : USER_NOT_FOUND;
}

int authenticate_student(int id, char *password) {
    int fd = open(STUDENT_FILE, O_RDONLY);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Student temp;
    int result = USER_NOT_FOUND;
    
    while (read(fd, &temp, sizeof(Student)) > 0) {
        if (temp.id == id) {
            if (strcmp(temp.password, password) == 0) {
                if (temp.status == ACTIVE) {
                    result = SUCCESS;
                } else {
                    result = USER_NOT_FOUND; // Inactive
                }
            } else {
                result = WRONG_PASSWORD;
            }
            break;
        }
    }
    
    unlock_file(fd);
    close(fd);
    return result;
}

int get_all_students(Student *student_list, int *count) {
    int fd = open(STUDENT_FILE, O_RDONLY);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Student temp;
    *count = 0;
    
    while (read(fd, &temp, sizeof(Student)) > 0 && *count < 100) {
        student_list[*count] = temp;
        (*count)++;
    }
    
    unlock_file(fd);
    close(fd);
    return SUCCESS;
}

int activate_student(int id) {
    Student student;
    int result = get_student_by_id(id, &student);
    
    if (result != SUCCESS) return result;
    
    student.status = ACTIVE;
    return update_student(student);
}

int deactivate_student(int id) {
    Student student;
    int result = get_student_by_id(id, &student);
    
    if (result != SUCCESS) return result;
    
    student.status = INACTIVE;
    return update_student(student);
} 
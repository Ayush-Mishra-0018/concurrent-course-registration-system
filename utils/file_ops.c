#include "file_ops.h"
#include <sys/file.h>
#include <errno.h>
#include <sys/stat.h>

// Forward declaration of get_all_admins function
int get_all_admins(Admin *admin_list, int *count);

// Initialize data files and directories
void init_data_files() {
    // Create data directory if it doesn't exist
    mkdir("data", 0777);
    
    // Create empty files if they don't exist
    FILE *fd;
    
    fd = fopen(ADMIN_FILE, "a+");
    if (fd) fclose(fd);
    
    fd = fopen(FACULTY_FILE, "a+");
    if (fd) fclose(fd);
    
    fd = fopen(STUDENT_FILE, "a+");
    if (fd) fclose(fd);
    
    fd = fopen(COURSE_FILE, "a+");
    if (fd) fclose(fd);
    
    fd = fopen(ENROLLMENT_FILE, "a+");
    if (fd) fclose(fd);
}

// File locking operations
int lock_file(int fd, int lock_type) {
    struct flock lock;
    lock.l_type = (lock_type == READ_LOCK) ? F_RDLCK : F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0; // Lock entire file
    
    return fcntl(fd, F_SETLKW, &lock); // Wait until lock is acquired
}

int unlock_file(int fd) {
    struct flock lock;
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0; // Unlock entire file
    
    return fcntl(fd, F_SETLK, &lock);
}

// Admin file operations
int add_admin(Admin admin) {
    int fd = open(ADMIN_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, WRITE_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    // Go to end of file
    lseek(fd, 0, SEEK_END);
    
    // Write admin record
    write(fd, &admin, sizeof(Admin));
    
    unlock_file(fd);
    close(fd);
    return SUCCESS;
}

int get_admin_by_id(int id, Admin *admin) {
    int fd = open(ADMIN_FILE, O_RDONLY);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Admin temp;
    int found = 0;
    
    while (read(fd, &temp, sizeof(Admin)) > 0) {
        if (temp.id == id) {
            *admin = temp;
            found = 1;
            break;
        }
    }
    
    unlock_file(fd);
    close(fd);
    return found ? SUCCESS : USER_NOT_FOUND;
}

int authenticate_admin(char *username, char *password) {
    int fd = open(ADMIN_FILE, O_RDONLY);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Admin temp;
    int admin_id = -1;
    
    while (read(fd, &temp, sizeof(Admin)) > 0) {
        if (strcmp(temp.username, username) == 0) {
            if (strcmp(temp.password, password) == 0 && temp.status == ACTIVE) {
                admin_id = temp.id;
            } else {
                admin_id = WRONG_PASSWORD;
            }
            break;
        }
    }
    
    unlock_file(fd);
    close(fd);
    return (admin_id > 0) ? admin_id : (admin_id == -1) ? USER_NOT_FOUND : WRONG_PASSWORD;
}

int get_all_admins(Admin *admin_list, int *count) {
    int fd = open(ADMIN_FILE, O_RDONLY);
    if (fd < 0) {
        *count = 0;
        return FAILURE;
    }
    
    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        *count = 0;
        return FAILURE;
    }
    
    Admin temp;
    *count = 0;
    
    while (read(fd, &temp, sizeof(Admin)) > 0 && *count < 100) {
        admin_list[*count] = temp;
        (*count)++;
    }
    
    unlock_file(fd);
    close(fd);
    return SUCCESS;
}

// Faculty file operations
int add_faculty(Faculty faculty) {
    int fd = open(FACULTY_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, WRITE_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    // Check if faculty ID already exists
    Faculty temp;
    int exists = 0;
    
    lseek(fd, 0, SEEK_SET);
    while (read(fd, &temp, sizeof(Faculty)) > 0) {
        if (temp.id == faculty.id) {
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
    
    // Write faculty record
    write(fd, &faculty, sizeof(Faculty));
    
    unlock_file(fd);
    close(fd);
    return SUCCESS;
}

int get_faculty_by_id(int id, Faculty *faculty) {
    int fd = open(FACULTY_FILE, O_RDONLY);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Faculty temp;
    int found = 0;
    
    while (read(fd, &temp, sizeof(Faculty)) > 0) {
        if (temp.id == id) {
            *faculty = temp;
            found = 1;
            break;
        }
    }
    
    unlock_file(fd);
    close(fd);
    return found ? SUCCESS : USER_NOT_FOUND;
}

int update_faculty(Faculty faculty) {
    int fd = open(FACULTY_FILE, O_RDWR);
    if (fd < 0) {
        perror("Error opening faculty file");
        return FAILURE;
    }
    
    if (lock_file(fd, WRITE_LOCK) < 0) {
        perror("Error locking faculty file");
        close(fd);
        return FAILURE;
    }
    
    Faculty temp;
    int found = 0;
    off_t offset;
    
    lseek(fd, 0, SEEK_SET); // Ensure we start from the beginning
    
    while (1) {
        offset = lseek(fd, 0, SEEK_CUR); // Save current position
        int bytes_read = read(fd, &temp, sizeof(Faculty));
        
        if (bytes_read <= 0) {
            break; // End of file or error
        }
        
        if (temp.id == faculty.id) {
            // Go back to the start of this record
            if (lseek(fd, offset, SEEK_SET) < 0) {
                perror("Error seeking in faculty file");
                unlock_file(fd);
                close(fd);
                return FAILURE;
            }
            
            // Write the updated faculty record
            int bytes_written = write(fd, &faculty, sizeof(Faculty));
            if (bytes_written != sizeof(Faculty)) {
                perror("Error writing faculty record");
                unlock_file(fd);
                close(fd);
                return FAILURE;
            }
            
            found = 1;
            break;
        }
    }
    
    // Flush changes to disk
    fsync(fd);
    
    unlock_file(fd);
    close(fd);
    
    return found ? SUCCESS : USER_NOT_FOUND;
}

int authenticate_faculty(int id, char *password) {
    int fd = open(FACULTY_FILE, O_RDONLY);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Faculty temp;
    int result = USER_NOT_FOUND;
    
    while (read(fd, &temp, sizeof(Faculty)) > 0) {
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

int get_all_faculty(Faculty *faculty_list, int *count) {
    int fd = open(FACULTY_FILE, O_RDONLY);
    if (fd < 0) return FAILURE;
    
    if (lock_file(fd, READ_LOCK) < 0) {
        close(fd);
        return FAILURE;
    }
    
    Faculty temp;
    *count = 0;
    
    while (read(fd, &temp, sizeof(Faculty)) > 0 && *count < 100) {
        faculty_list[*count] = temp;
        (*count)++;
    }
    
    unlock_file(fd);
    close(fd);
    return SUCCESS;
} 
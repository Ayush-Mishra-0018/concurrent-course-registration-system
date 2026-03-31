#ifndef CONSTANTS_H
#define CONSTANTS_H

// Server configuration
#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

// User types
#define ADMIN 1
#define FACULTY 2
#define STUDENT 3

// Status codes
#define SUCCESS 1
#define FAILURE 0
#define USER_EXISTS -1
#define USER_NOT_FOUND -2
#define WRONG_PASSWORD -3
#define COURSE_EXISTS -4
#define COURSE_NOT_FOUND -5
#define COURSE_FULL -6
#define ALREADY_ENROLLED -7
#define NOT_ENROLLED -8

// Paths for data files
#define ADMIN_FILE "data/admin.dat"
#define FACULTY_FILE "data/faculty.dat"
#define STUDENT_FILE "data/student.dat"
#define COURSE_FILE "data/courses.dat"
#define ENROLLMENT_FILE "data/enrollments.dat"

// Locking
#define READ_LOCK 0
#define WRITE_LOCK 1

// Status
#define ACTIVE 1
#define INACTIVE 0

#endif // CONSTANTS_H 
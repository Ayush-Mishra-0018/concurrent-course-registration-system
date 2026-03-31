#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <time.h>
#include "constants.h"

// Platform-specific includes
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
#endif

// Common user structure
typedef struct {
    int id;
    char name[50];
    char password[50];
    int type;
    int status; // Active or Inactive
} User;

// Admin structure
typedef struct {
    int id;
    char username[50];
    char password[50];
    int status;
} Admin;

// Faculty structure
typedef struct {
    int id;
    char name[50];
    char password[50];
    char department[50];
    int status;
} Faculty;

// Student structure
typedef struct {
    int id;
    char name[50];
    char password[50];
    int status;
    int enrolled_courses[10]; // Array to hold course IDs
    int course_count;
} Student;

// Course structure
typedef struct {
    int id;
    char name[100];
    char code[20];
    int faculty_id;
    int max_seats;
    int available_seats;
    int status; // Active or Inactive
} Course;

// Enrollment structure
typedef struct {
    int id;
    int student_id;
    int course_id;
    time_t enrollment_date;
} Enrollment;

// Structure to pass client data to handler thread
typedef struct {
    int socket_fd;
    struct sockaddr_in client_addr;
} ClientInfo;

#endif // STRUCTURES_H 
#ifndef STUDENT_CONTROLLER_H
#define STUDENT_CONTROLLER_H

#include "../utils/file_ops.h"

// Student login function
int student_login(int client_socket);

// Student menu display and handler
void student_menu(int client_socket, int student_id);

// Student functions
int view_all_courses(int client_socket);
int enroll_new_course(int client_socket, int student_id);
int drop_course_handler(int client_socket, int student_id);
int view_enrolled_courses(int client_socket, int student_id);
int change_student_password(int client_socket, int student_id);

#endif // STUDENT_CONTROLLER_H 
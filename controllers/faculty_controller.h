#ifndef FACULTY_CONTROLLER_H
#define FACULTY_CONTROLLER_H

#include "../utils/file_ops.h"

// Faculty login function
int faculty_login(int client_socket);

// Faculty menu display and handler
void faculty_menu(int client_socket, int faculty_id);

// Faculty functions
int view_offering_courses(int client_socket, int faculty_id);
int add_new_course(int client_socket, int faculty_id);
int remove_course_handler(int client_socket, int faculty_id);
int update_course_details(int client_socket, int faculty_id);
int change_faculty_password(int client_socket, int faculty_id);

#endif // FACULTY_CONTROLLER_H 
#ifndef ADMIN_CONTROLLER_H
#define ADMIN_CONTROLLER_H

#include "../utils/file_ops.h"

// Admin login function
int admin_login(int client_socket);

// Admin menu display and handler
void admin_menu(int client_socket, int admin_id);

// Admin functions
int add_student_handler(int client_socket);
int view_student_details(int client_socket);
int add_faculty_handler(int client_socket);
int view_faculty_details(int client_socket);
int activate_student_handler(int client_socket);
int block_student_handler(int client_socket);
int modify_student_details(int client_socket);
int modify_faculty_details(int client_socket);

#endif // ADMIN_CONTROLLER_H 
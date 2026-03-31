#include "admin_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../common_utils.h"

// Utility function to send message to client
void send_message(int client_socket, char *message) {
    write(client_socket, message, strlen(message));
}

// Utility function to receive input from client
int receive_input(int client_socket, char *buffer, int buffer_size) {
    memset(buffer, 0, buffer_size);
    return read(client_socket, buffer, buffer_size - 1);
}

// Admin login function
int admin_login(int client_socket) {
    char username[50];
    char password[50];
    char buffer[BUFFER_SIZE];
    
    // Prompt for username
    send_message(client_socket, "Enter Admin Username: ");
    receive_input(client_socket, username, sizeof(username));
    username[strcspn(username, "\n")] = 0; // Remove newline
    
    // Prompt for password
    send_message(client_socket, "Enter Password: ");
    receive_input(client_socket, password, sizeof(password));
    password[strcspn(password, "\n")] = 0; // Remove newline
    
    int admin_id = authenticate_admin(username, password);
    
    if (admin_id > 0) {
        sprintf(buffer, "Login successful! Welcome, %s\n", username);
        send_message(client_socket, buffer);
        return admin_id;
    } else if (admin_id == WRONG_PASSWORD) {
        send_message(client_socket, "Wrong password! Login failed.\n");
    } else {
        send_message(client_socket, "Admin not found! Login failed.\n");
    }
    
    return -1;
}

// Admin menu display and handler
void admin_menu(int client_socket, int admin_id) {
    int choice;
    char buffer[BUFFER_SIZE];
    
    while (1) {
        // Display menu
        send_message(client_socket, "\n....... Welcome to Admin Menu .......\n");
        send_message(client_socket, "1. Add Student\n");
        send_message(client_socket, "2. View Student Details\n");
        send_message(client_socket, "3. Add Faculty\n");
        send_message(client_socket, "4. View Faculty Details\n");
        send_message(client_socket, "5. Activate Student\n");
        send_message(client_socket, "6. Block Student\n");
        send_message(client_socket, "7. Modify Student Details\n");
        send_message(client_socket, "8. Modify Faculty Details\n");
        send_message(client_socket, "9. Logout and Exit\n");
        send_message(client_socket, "Enter Your Choice: ");
        
        // Get user choice
        receive_input(client_socket, buffer, sizeof(buffer));
        choice = atoi(buffer);
        
        switch (choice) {
            case 1:
                add_student_handler(client_socket);
                break;
            case 2:
                view_student_details(client_socket);
                break;
            case 3:
                add_faculty_handler(client_socket);
                break;
            case 4:
                view_faculty_details(client_socket);
                break;
            case 5:
                activate_student_handler(client_socket);
                break;
            case 6:
                block_student_handler(client_socket);
                break;
            case 7:
                modify_student_details(client_socket);
                break;
            case 8:
                modify_faculty_details(client_socket);
                break;
            case 9:
                send_message(client_socket, "Logging out...\n");
                return;
            default:
                send_message(client_socket, "Invalid choice. Please try again.\n");
        }
    }
}

// Admin function implementations
int add_student_handler(int client_socket) {
    Student student;
    char buffer[BUFFER_SIZE];
    
    // Get highest student ID
    Student students[100];
    int count;
    get_all_students(students, &count);
    
    int max_id = 0;
    for (int i = 0; i < count; i++) {
        if (students[i].id > max_id) max_id = students[i].id;
    }
    
    student.id = max_id + 1;
    
    // Get student details
    send_message(client_socket, "\nEnter Student Name: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    strcpy(student.name, buffer);
    
    send_message(client_socket, "Enter Password for Student: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    strcpy(student.password, buffer);
    
    student.status = ACTIVE;
    student.course_count = 0;
    
    // Add student to database
    int result = add_student(student);
    
    if (result == SUCCESS) {
        sprintf(buffer, "Student added successfully! ID: %d\n", student.id);
        send_message(client_socket, buffer);
    } else if (result == USER_EXISTS) {
        send_message(client_socket, "Error: Student with this ID already exists!\n");
    } else {
        send_message(client_socket, "Error: Failed to add student!\n");
    }
    
    return result;
}

int view_student_details(int client_socket) {
    Student students[100];
    int count;
    char buffer[BUFFER_SIZE];
    
    int result = get_all_students(students, &count);
    
    if (result != SUCCESS) {
        send_message(client_socket, "Error: Failed to retrieve student data!\n");
        return FAILURE;
    }
    
    if (count == 0) {
        send_message(client_socket, "No students found in the system.\n");
        return SUCCESS;
    }
    
    send_message(client_socket, "\n---------- Student Details ----------\n");
    send_message(client_socket, "ID\tName\t\tStatus\n");
    send_message(client_socket, "-------------------------------------\n");
    
    for (int i = 0; i < count; i++) {
        sprintf(buffer, "%d\t%s\t\t%s\n", 
                students[i].id, 
                students[i].name, 
                students[i].status == ACTIVE ? "Active" : "Blocked");
        send_message(client_socket, buffer);
    }
    
    return SUCCESS;
}

int add_faculty_handler(int client_socket) {
    Faculty faculty;
    char buffer[BUFFER_SIZE];
    
    // Get highest faculty ID
    Faculty faculties[100];
    int count;
    get_all_faculty(faculties, &count);
    
    int max_id = 0;
    for (int i = 0; i < count; i++) {
        if (faculties[i].id > max_id) max_id = faculties[i].id;
    }
    
    faculty.id = max_id + 1;
    
    // Get faculty details
    send_message(client_socket, "\nEnter Faculty Name: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    strcpy(faculty.name, buffer);
    
    send_message(client_socket, "Enter Department: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    strcpy(faculty.department, buffer);
    
    send_message(client_socket, "Enter Password for Faculty: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    strcpy(faculty.password, buffer);
    
    faculty.status = ACTIVE;
    
    // Add faculty to database
    int result = add_faculty(faculty);
    
    if (result == SUCCESS) {
        sprintf(buffer, "Faculty added successfully! ID: %d\n", faculty.id);
        send_message(client_socket, buffer);
    } else if (result == USER_EXISTS) {
        send_message(client_socket, "Error: Faculty with this ID already exists!\n");
    } else {
        send_message(client_socket, "Error: Failed to add faculty!\n");
    }
    
    return result;
} 
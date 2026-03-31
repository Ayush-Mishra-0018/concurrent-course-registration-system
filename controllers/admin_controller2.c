#include "admin_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../common_utils.h"

// Declare external functions from admin_controller.c
// Not needed as we're now including common_utils.h

int view_faculty_details(int client_socket) {
    Faculty faculties[100];
    int count;
    char buffer[BUFFER_SIZE];
    
    int result = get_all_faculty(faculties, &count);
    
    if (result != SUCCESS) {
        send_message(client_socket, "Error: Failed to retrieve faculty data!\n");
        return FAILURE;
    }
    
    if (count == 0) {
        send_message(client_socket, "No faculty found in the system.\n");
        return SUCCESS;
    }
    
    send_message(client_socket, "\n---------- Faculty Details ----------\n");
    send_message(client_socket, "ID\tName\t\tDepartment\t\tStatus\n");
    send_message(client_socket, "-----------------------------------------\n");
    
    for (int i = 0; i < count; i++) {
        sprintf(buffer, "%d\t%s\t\t%s\t\t%s\n", 
                faculties[i].id, 
                faculties[i].name, 
                faculties[i].department,
                faculties[i].status == ACTIVE ? "Active" : "Inactive");
        send_message(client_socket, buffer);
    }
    
    return SUCCESS;
}

int activate_student_handler(int client_socket) {
    char buffer[BUFFER_SIZE];
    
    // Display students first
    view_student_details(client_socket);
    
    // Get student ID
    send_message(client_socket, "\nEnter Student ID to Activate: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0; // Remove newline
    int student_id = atoi(buffer);
    
    // Activate student
    int result = activate_student(student_id);
    
    if (result == SUCCESS) {
        send_message(client_socket, "Student activated successfully!\n");
    } else if (result == USER_NOT_FOUND) {
        send_message(client_socket, "Error: Student with this ID not found!\n");
    } else {
        send_message(client_socket, "Error: Failed to activate student!\n");
    }
    
    return result;
}

int block_student_handler(int client_socket) {
    char buffer[BUFFER_SIZE];
    
    // Display students first
    view_student_details(client_socket);
    
    // Get student ID
    send_message(client_socket, "\nEnter Student ID to Block: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0; // Remove newline
    int student_id = atoi(buffer);
    
    // Deactivate student
    int result = deactivate_student(student_id);
    
    if (result == SUCCESS) {
        send_message(client_socket, "Student blocked successfully!\n");
    } else if (result == USER_NOT_FOUND) {
        send_message(client_socket, "Error: Student with this ID not found!\n");
    } else {
        send_message(client_socket, "Error: Failed to block student!\n");
    }
    
    return result;
}

int modify_student_details(int client_socket) {
    char buffer[BUFFER_SIZE];
    Student student;
    
    // Display students first
    view_student_details(client_socket);
    
    // Get student ID
    send_message(client_socket, "\nEnter Student ID to Modify: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0; // Remove newline
    int student_id = atoi(buffer);
    
    // Get student details
    int result = get_student_by_id(student_id, &student);
    
    if (result != SUCCESS) {
        send_message(client_socket, "Error: Student with this ID not found!\n");
        return USER_NOT_FOUND;
    }
    
    // Display current details
    sprintf(buffer, "\nCurrent Details:\nName: %s\n", student.name);
    send_message(client_socket, buffer);
    
    // Get new details
    send_message(client_socket, "\nEnter New Student Name (or press Enter to keep current): ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    
    if (strlen(buffer) > 0) {
        strcpy(student.name, buffer);
    }
    
    send_message(client_socket, "Enter New Password (or press Enter to keep current): ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    
    if (strlen(buffer) > 0) {
        strcpy(student.password, buffer);
    }
    
    // Update student
    result = update_student(student);
    
    if (result == SUCCESS) {
        send_message(client_socket, "Student details updated successfully!\n");
    } else {
        send_message(client_socket, "Error: Failed to update student details!\n");
    }
    
    return result;
}

int modify_faculty_details(int client_socket) {
    char buffer[BUFFER_SIZE];
    Faculty faculty;
    
    // Display faculty first
    view_faculty_details(client_socket);
    
    // Get faculty ID
    send_message(client_socket, "\nEnter Faculty ID to Modify: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0; // Remove newline
    int faculty_id = atoi(buffer);
    
    // Get faculty details
    int result = get_faculty_by_id(faculty_id, &faculty);
    
    if (result != SUCCESS) {
        send_message(client_socket, "Error: Faculty with this ID not found!\n");
        return USER_NOT_FOUND;
    }
    
    // Display current details
    sprintf(buffer, "\nCurrent Details:\nName: %s\nDepartment: %s\n", 
            faculty.name, faculty.department);
    send_message(client_socket, buffer);
    
    // Get new details
    send_message(client_socket, "\nEnter New Faculty Name (or press Enter to keep current): ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    
    if (strlen(buffer) > 0) {
        strcpy(faculty.name, buffer);
    }
    
    send_message(client_socket, "Enter New Department (or press Enter to keep current): ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    
    if (strlen(buffer) > 0) {
        strcpy(faculty.department, buffer);
    }
    
    send_message(client_socket, "Enter New Password (or press Enter to keep current): ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    
    if (strlen(buffer) > 0) {
        strcpy(faculty.password, buffer);
    }
    
    // Update faculty
    result = update_faculty(faculty);
    
    if (result == SUCCESS) {
        send_message(client_socket, "Faculty details updated successfully!\n");
    } else {
        send_message(client_socket, "Error: Failed to update faculty details!\n");
    }
    
    return result;
} 
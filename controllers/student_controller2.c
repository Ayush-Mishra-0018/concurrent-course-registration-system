#include "student_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Utility functions
extern void send_message(int client_socket, char *message);
extern int receive_input(int client_socket, char *buffer, int buffer_size);

// Drop course handler
int drop_course_handler(int client_socket, int student_id) {
    char buffer[BUFFER_SIZE];
    
    // Display enrolled courses
    view_enrolled_courses(client_socket, student_id);
    
    // Get course ID to drop
    send_message(client_socket, "\nEnter Course ID to Drop: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    int course_id = atoi(buffer);
    
    // Unenroll student from course
    int result = unenroll_student(student_id, course_id);
    
    if (result == SUCCESS) {
        send_message(client_socket, "Dropped course successfully!\n");
    } else if (result == COURSE_NOT_FOUND) {
        send_message(client_socket, "Error: Course with this ID not found!\n");
    } else if (result == NOT_ENROLLED) {
        send_message(client_socket, "Error: Not enrolled in this course!\n");
    } else {
        send_message(client_socket, "Error: Failed to drop course!\n");
    }
    
    return result;
}

// View enrolled courses
int view_enrolled_courses(int client_socket, int student_id) {
    Student student;
    char buffer[BUFFER_SIZE];
    
    // Get student details
    int result = get_student_by_id(student_id, &student);
    
    if (result != SUCCESS) {
        send_message(client_socket, "Error: Student not found!\n");
        return USER_NOT_FOUND;
    }
    
    if (student.course_count == 0) {
        send_message(client_socket, "You are not enrolled in any courses yet.\n");
        return SUCCESS;
    }
    
    send_message(client_socket, "\n---------- Your Enrolled Courses ----------\n");
    send_message(client_socket, "ID\tCode\tName\n");
    send_message(client_socket, "-------------------------------------\n");
    
    for (int i = 0; i < student.course_count; i++) {
        Course course;
        if (get_course_by_id(student.enrolled_courses[i], &course) == SUCCESS) {
            sprintf(buffer, "%d\t%s\t%s\n", 
                    course.id, 
                    course.code, 
                    course.name);
            send_message(client_socket, buffer);
            
            // Find faculty information
            Faculty faculty;
            if (get_faculty_by_id(course.faculty_id, &faculty) == SUCCESS) {
                sprintf(buffer, "  Faculty: %s, Department: %s\n", 
                        faculty.name, faculty.department);
                send_message(client_socket, buffer);
            }
            
            send_message(client_socket, "-------------------------------------\n");
        }
    }
    
    return SUCCESS;
}

// Change student password
int change_student_password(int client_socket, int student_id) {
    char buffer[BUFFER_SIZE];
    Student student;
    
    // Get student details
    int result = get_student_by_id(student_id, &student);
    
    if (result != SUCCESS) {
        send_message(client_socket, "Error: Student not found!\n");
        return USER_NOT_FOUND;
    }
    
    // Verify current password
    send_message(client_socket, "\nEnter Current Password: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    
    // Check if current password is correct
    if (strcmp(buffer, student.password) != 0) {
        send_message(client_socket, "Error: Current password is incorrect!\n");
        return WRONG_PASSWORD;
    }
    
    // Get new password
    send_message(client_socket, "Enter New Password: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    
    // Validate new password isn't empty
    if (strlen(buffer) == 0) {
        send_message(client_socket, "Error: Password cannot be empty!\n");
        return FAILURE;
    }
    
    // Store new password
    char new_password[50];
    strcpy(new_password, buffer);
    
    // Confirm new password
    send_message(client_socket, "Confirm New Password: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    
    if (strcmp(buffer, new_password) != 0) {
        send_message(client_socket, "Error: Passwords do not match!\n");
        return FAILURE;
    }
    
    // Update password in student record
    strcpy(student.password, new_password);
    
    // Update student in file
    result = update_student(student);
    
    if (result == SUCCESS) {
        send_message(client_socket, "Password changed successfully!\n");
    } else {
        send_message(client_socket, "Error: Failed to change password!\n");
    }
    
    return result;
} 
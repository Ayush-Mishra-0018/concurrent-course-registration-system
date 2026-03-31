#include "student_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../common_utils.h"

// Student login function
int student_login(int client_socket) {
    char id_str[20];
    char password[50];
    char buffer[BUFFER_SIZE];
    
    // Prompt for student ID
    send_message(client_socket, "Enter Student ID: ");
    receive_input(client_socket, id_str, sizeof(id_str));
    id_str[strcspn(id_str, "\n")] = 0; // Remove newline
    int student_id = atoi(id_str);
    
    // Prompt for password
    send_message(client_socket, "Enter Password: ");
    receive_input(client_socket, password, sizeof(password));
    password[strcspn(password, "\n")] = 0; // Remove newline
    
    int result = authenticate_student(student_id, password);
    
    if (result == SUCCESS) {
        Student student;
        get_student_by_id(student_id, &student);
        sprintf(buffer, "Login successful! Welcome, %s\n", student.name);
        send_message(client_socket, buffer);
        return student_id;
    } else if (result == WRONG_PASSWORD) {
        send_message(client_socket, "Wrong password! Login failed.\n");
    } else {
        send_message(client_socket, "Student not found or inactive! Login failed.\n");
    }
    
    return -1;
}

// Student menu display and handler
void student_menu(int client_socket, int student_id) {
    int choice;
    char buffer[BUFFER_SIZE];
    
    while (1) {
        // Display menu
        send_message(client_socket, "\n...... Welcome to Student Menu .......\n");
        send_message(client_socket, "1. View All Courses\n");
        send_message(client_socket, "2. Enroll (pick) New Course\n");
        send_message(client_socket, "3. Drop Course\n");
        send_message(client_socket, "4. View Enrolled Course Details\n");
        send_message(client_socket, "5. Change Password\n");
        send_message(client_socket, "6. Logout and Exit\n");
        send_message(client_socket, "Enter Your Choice: ");
        
        // Get user choice
        receive_input(client_socket, buffer, sizeof(buffer));
        choice = atoi(buffer);
        
        switch (choice) {
            case 1:
                view_all_courses(client_socket);
                break;
            case 2:
                enroll_new_course(client_socket, student_id);
                break;
            case 3:
                drop_course_handler(client_socket, student_id);
                break;
            case 4:
                view_enrolled_courses(client_socket, student_id);
                break;
            case 5:
                change_student_password(client_socket, student_id);
                break;
            case 6:
                send_message(client_socket, "Logging out...\n");
                return;
            default:
                send_message(client_socket, "Invalid choice. Please try again.\n");
        }
    }
}

// Student function implementations
int view_all_courses(int client_socket) {
    Course courses[100];
    int count;
    char buffer[BUFFER_SIZE];
    
    int result = get_all_courses(courses, &count);
    
    if (result != SUCCESS) {
        send_message(client_socket, "Error: Failed to retrieve course data!\n");
        return FAILURE;
    }
    
    if (count == 0) {
        send_message(client_socket, "No courses available at the moment.\n");
        return SUCCESS;
    }
    
    send_message(client_socket, "\n---------- Available Courses ----------\n");
    send_message(client_socket, "ID\tCode\tName\t\tAvailable Seats\n");
    send_message(client_socket, "----------------------------------------\n");
    
    for (int i = 0; i < count; i++) {
        sprintf(buffer, "%d\t%s\t%s\t\t%d/%d\n", 
                courses[i].id, 
                courses[i].code, 
                courses[i].name,
                courses[i].available_seats,
                courses[i].max_seats);
        send_message(client_socket, buffer);
    }
    
    return SUCCESS;
}

int enroll_new_course(int client_socket, int student_id) {
    char buffer[BUFFER_SIZE];
    
    // Display available courses first
    view_all_courses(client_socket);
    
    // Display enrolled courses
    Student student;
    get_student_by_id(student_id, &student);
    
    send_message(client_socket, "\n---------- Your Enrolled Courses ----------\n");
    for (int i = 0; i < student.course_count; i++) {
        Course course;
        get_course_by_id(student.enrolled_courses[i], &course);
        sprintf(buffer, "%d: %s - %s\n", course.id, course.code, course.name);
        send_message(client_socket, buffer);
    }
    
    // Get course ID to enroll
    send_message(client_socket, "\nEnter Course ID to Enroll: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    int course_id = atoi(buffer);
    
    // Enroll student in course
    int result = enroll_student(student_id, course_id);
    
    if (result == SUCCESS) {
        send_message(client_socket, "Enrolled in course successfully!\n");
    } else if (result == COURSE_NOT_FOUND) {
        send_message(client_socket, "Error: Course with this ID not found!\n");
    } else if (result == COURSE_FULL) {
        send_message(client_socket, "Error: Course is full, no seats available!\n");
    } else if (result == ALREADY_ENROLLED) {
        send_message(client_socket, "Error: Already enrolled in this course!\n");
    } else {
        send_message(client_socket, "Error: Failed to enroll in course!\n");
    }
    
    return result;
} 
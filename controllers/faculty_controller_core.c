#include "faculty_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../common_utils.h"

// Faculty login function
int faculty_login(int client_socket) {
    char id_str[20];
    char password[50];
    char buffer[BUFFER_SIZE];
    
    // Prompt for faculty ID
    send_message(client_socket, "Enter Faculty ID: ");
    receive_input(client_socket, id_str, sizeof(id_str));
    id_str[strcspn(id_str, "\n")] = 0; // Remove newline
    int faculty_id = atoi(id_str);
    
    // Prompt for password
    send_message(client_socket, "Enter Password: ");
    receive_input(client_socket, password, sizeof(password));
    password[strcspn(password, "\n")] = 0; // Remove newline
    
    int result = authenticate_faculty(faculty_id, password);
    
    if (result == SUCCESS) {
        Faculty faculty;
        get_faculty_by_id(faculty_id, &faculty);
        sprintf(buffer, "Login successful! Welcome, %s\n", faculty.name);
        send_message(client_socket, buffer);
        return faculty_id;
    } else if (result == WRONG_PASSWORD) {
        send_message(client_socket, "Wrong password! Login failed.\n");
    } else {
        send_message(client_socket, "Faculty not found or inactive! Login failed.\n");
    }
    
    return -1;
}

// Faculty menu display and handler
void faculty_menu(int client_socket, int faculty_id) {
    int choice;
    char buffer[BUFFER_SIZE];
    
    while (1) {
        // Display menu
        send_message(client_socket, "\n....... Welcome to Faculty Menu .......\n");
        send_message(client_socket, "1. View Offering Courses\n");
        send_message(client_socket, "2. Add New Course\n");
        send_message(client_socket, "3. Remove Course from Catalog\n");
        send_message(client_socket, "4. Update Course Details\n");
        send_message(client_socket, "5. Change Password\n");
        send_message(client_socket, "6. Logout and Exit\n");
        send_message(client_socket, "Enter Your Choice: ");
        
        // Get user choice
        receive_input(client_socket, buffer, sizeof(buffer));
        buffer[strcspn(buffer, "\n")] = 0; // Remove newline
        choice = atoi(buffer);
        
        switch (choice) {
            case 1:
                view_offering_courses(client_socket, faculty_id);
                break;
            case 2:
                add_new_course(client_socket, faculty_id);
                break;
            case 3:
                remove_course_handler(client_socket, faculty_id);
                break;
            case 4:
                update_course_details(client_socket, faculty_id);
                break;
            case 5:
                change_faculty_password(client_socket, faculty_id);
                break;
            case 6:
                send_message(client_socket, "Logging out...\n");
                return;
            default:
                send_message(client_socket, "Invalid choice. Please try again.\n");
        }
    }
}

// Faculty function implementations
int view_offering_courses(int client_socket, int faculty_id) {
    Course courses[100];
    int count;
    char buffer[BUFFER_SIZE];
    
    int result = get_courses_by_faculty(faculty_id, courses, &count);
    
    if (result != SUCCESS) {
        send_message(client_socket, "Error: Failed to retrieve course data!\n");
        return FAILURE;
    }
    
    if (count == 0) {
        send_message(client_socket, "You are not offering any courses yet.\n");
        return SUCCESS;
    }
    
    send_message(client_socket, "\n---------- Your Courses ----------\n");
    send_message(client_socket, "ID\tCode\tName\t\tAvailable Seats\n");
    send_message(client_socket, "-------------------------------------\n");
    
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

int add_new_course(int client_socket, int faculty_id) {
    Course course;
    char buffer[BUFFER_SIZE];
    
    // Get highest course ID
    Course courses[100];
    int count;
    get_all_courses(courses, &count);
    
    int max_id = 0;
    for (int i = 0; i < count; i++) {
        if (courses[i].id > max_id) max_id = courses[i].id;
    }
    
    course.id = max_id + 1;
    course.faculty_id = faculty_id;
    
    // Get course details
    send_message(client_socket, "\nEnter Course Name: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    strcpy(course.name, buffer);
    
    send_message(client_socket, "Enter Course Code: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    strcpy(course.code, buffer);
    
    send_message(client_socket, "Enter Maximum Seats: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    course.max_seats = atoi(buffer);
    course.available_seats = course.max_seats;
    
    course.status = ACTIVE;
    
    // Add course to database
    int result = add_course(course);
    
    if (result == SUCCESS) {
        sprintf(buffer, "Course added successfully! ID: %d\n", course.id);
        send_message(client_socket, buffer);
    } else if (result == COURSE_EXISTS) {
        send_message(client_socket, "Error: Course with this code already exists!\n");
    } else {
        send_message(client_socket, "Error: Failed to add course!\n");
    }
    
    return result;
} 
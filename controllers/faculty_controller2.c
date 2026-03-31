#include "faculty_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Utility functions
extern void send_message(int client_socket, char *message);
extern int receive_input(int client_socket, char *buffer, int buffer_size);

// Remove course handler
int remove_course_handler(int client_socket, int faculty_id) {
    char buffer[BUFFER_SIZE];
    
    // Display faculty courses first
    view_offering_courses(client_socket, faculty_id);
    
    // Get course ID
    send_message(client_socket, "\nEnter Course ID to Remove: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    int course_id = atoi(buffer);
    
    // Verify the course belongs to this faculty
    Course course;
    int result = get_course_by_id(course_id, &course);
    
    if (result != SUCCESS) {
        send_message(client_socket, "Error: Course with this ID not found!\n");
        return COURSE_NOT_FOUND;
    }
    
    if (course.faculty_id != faculty_id) {
        send_message(client_socket, "Error: You can only remove your own courses!\n");
        return FAILURE;
    }
    
    // Remove course
    result = remove_course(course_id);
    
    if (result == SUCCESS) {
        send_message(client_socket, "Course removed successfully!\n");
    } else {
        send_message(client_socket, "Error: Failed to remove course!\n");
    }
    
    return result;
}

// Update course details
int update_course_details(int client_socket, int faculty_id) {
    char buffer[BUFFER_SIZE];
    Course course;
    
    // Display faculty courses first
    view_offering_courses(client_socket, faculty_id);
    
    // Get course ID
    send_message(client_socket, "\nEnter Course ID to Update: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    int course_id = atoi(buffer);
    
    // Get course details and verify ownership
    int result = get_course_by_id(course_id, &course);
    
    if (result != SUCCESS) {
        send_message(client_socket, "Error: Course with this ID not found!\n");
        return COURSE_NOT_FOUND;
    }
    
    if (course.faculty_id != faculty_id) {
        send_message(client_socket, "Error: You can only update your own courses!\n");
        return FAILURE;
    }
    
    // Display current details
    sprintf(buffer, "\nCurrent Details:\nName: %s\nCode: %s\nMax Seats: %d\n", 
            course.name, course.code, course.max_seats);
    send_message(client_socket, buffer);
    
    // Get new details
    send_message(client_socket, "\nEnter New Course Name (or press Enter to keep current): ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    
    if (strlen(buffer) > 0) {
        strcpy(course.name, buffer);
    }
    
    // We don't allow changing course code as it might be referenced elsewhere
    
    send_message(client_socket, "Enter New Maximum Seats (or press Enter to keep current): ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    
    if (strlen(buffer) > 0) {
        int new_max_seats = atoi(buffer);
        
        // Ensure new max seats is not less than already enrolled students
        if (new_max_seats < (course.max_seats - course.available_seats)) {
            send_message(client_socket, "Error: New maximum seats cannot be less than already enrolled students!\n");
            return FAILURE;
        }
        
        // Update available seats
        int enrolled = course.max_seats - course.available_seats;
        course.max_seats = new_max_seats;
        course.available_seats = new_max_seats - enrolled;
    }
    
    // Update course
    result = update_course(course);
    
    if (result == SUCCESS) {
        send_message(client_socket, "Course details updated successfully!\n");
    } else {
        send_message(client_socket, "Error: Failed to update course details!\n");
    }
    
    return result;
}

// Change faculty password
int change_faculty_password(int client_socket, int faculty_id) {
    char buffer[BUFFER_SIZE];
    Faculty faculty;
    
    // Get faculty details
    int result = get_faculty_by_id(faculty_id, &faculty);
    
    if (result != SUCCESS) {
        send_message(client_socket, "Error: Faculty not found!\n");
        return USER_NOT_FOUND;
    }
    
    // Verify current password
    send_message(client_socket, "\nEnter Current Password: ");
    receive_input(client_socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\n")] = 0;
    
    // Check if current password is correct
    if (strcmp(buffer, faculty.password) != 0) {
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
    
    // Update password in faculty record
    strcpy(faculty.password, new_password);
    
    // Update faculty in file
    result = update_faculty(faculty);
    
    if (result == SUCCESS) {
        send_message(client_socket, "Password changed successfully!\n");
    } else {
        send_message(client_socket, "Error: Failed to change password!\n");
    }
    
    return result;
} 
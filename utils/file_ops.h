#ifndef FILE_OPS_H
#define FILE_OPS_H

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include "../include/constants.h"
#include "../include/structures.h"

// File locking operations
int lock_file(int fd, int lock_type);
int unlock_file(int fd);

// Admin file operations
int add_admin(Admin admin);
int get_admin_by_id(int id, Admin *admin);
int authenticate_admin(char *username, char *password);

// Faculty file operations
int add_faculty(Faculty faculty);
int get_faculty_by_id(int id, Faculty *faculty);
int update_faculty(Faculty faculty);
int authenticate_faculty(int id, char *password);
int get_all_faculty(Faculty *faculty_list, int *count);

// Student file operations
int add_student(Student student);
int get_student_by_id(int id, Student *student);
int update_student(Student student);
int authenticate_student(int id, char *password);
int get_all_students(Student *student_list, int *count);
int activate_student(int id);
int deactivate_student(int id);

// Course file operations
int add_course(Course course);
int get_course_by_id(int id, Course *course);
int update_course(Course course);
int remove_course(int id);
int get_all_courses(Course *course_list, int *count);
int get_courses_by_faculty(int faculty_id, Course *course_list, int *count);
int purge_inactive_courses();

// Enrollment file operations
int enroll_student(int student_id, int course_id);
int unenroll_student(int student_id, int course_id);
int get_student_enrollments(int student_id, Enrollment *enrollments, int *count);
int get_course_enrollments(int course_id, Enrollment *enrollments, int *count);

// Initialize data directory and files
void init_data_files();

#endif // FILE_OPS_H 
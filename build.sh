#!/bin/bash

# Create directory structure if it doesn't exist
mkdir -p data

# Clean any previous builds
echo "Cleaning previous builds..."
rm -f server client *.o controllers/*.o utils/*.o
rm -f data/*.dat.tmp

# Compile the project
echo "Compiling Academia Course Registration Portal..."

# Compile the server
echo "Compiling server..."

gcc -Wall -pthread -o server server.c common_utils.c \
    utils/file_ops_core.c utils/file_ops_student.c utils/file_ops_course.c utils/file_ops_enrollment.c \
    controllers/admin_controller_core.c controllers/admin_controller_ops.c \
    controllers/faculty_controller_core.c controllers/faculty_controller_ops.c \
    controllers/student_controller_core.c controllers/student_controller_ops.c

# Compile the client
echo "Compiling client..."
gcc -Wall -o client client.c common_utils.c

# Check if compilation was successful
if [ -f "server" ] && [ -f "client" ]; then
    echo "Compilation successful!"
    echo "To run the server: ./server"
    echo "To run the client: ./client"
    echo "Default admin credentials: username=admin, password=admin123"
else
    echo "Compilation failed!"
fi 
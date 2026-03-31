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
    utils/file_ops.c utils/file_ops2.c utils/file_ops3.c utils/file_ops4.c \
    controllers/admin_controller_fixed.c controllers/admin_controller2.c \
    controllers/faculty_controller_fixed.c controllers/faculty_controller2_fixed.c \
    controllers/student_controller_fixed.c controllers/student_controller2_fixed.c \
    -pthread

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
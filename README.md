# Concurrent Course Registration System (C, Sockets, OS Concepts)

Name – Ayush Mishra  
Roll Number – IMT2023129  
Batch – IMT2023  

---

## Overview

This is a multi-client course registration system built using C. It uses socket programming and multithreading to handle multiple users at the same time.

The system simulates an academic portal with three roles: Admin, Faculty, and Student. Data is stored in files and protected using file locking to avoid conflicts when multiple users access the system concurrently.

---

## Core Components

### server.c

This is the main server-side program.

- Handles multiple client connections using threads  
- Uses socket programming for communication  
- Implements role-based logic (Admin, Faculty, Student)  
- Stores data in files and uses file locking for consistency  
- Processes client requests and sends responses  

---

### client.c

This is the client-side terminal application.

- Connects to the server using TCP  
- Allows user login  
- Displays options based on user role  
- Sends requests to server and displays responses  

---

## Features by Role

### Admin

- Add, update, or remove faculty and student accounts  
- View all courses and system data  

### Faculty

- Add, update, or remove courses  
- View enrolled students  
- Update profile and change password  

### Student

- View available courses  
- Enroll in or drop courses  
- Update profile and change password  

---

## System Architecture

- Server listens on a fixed port and accepts connections  
- Each client connects using TCP sockets  
- Each connection is handled using a separate thread  
- Data is stored in files (`student.dat`, `faculty.dat`, `courses.dat`, etc.)  
- File locking is used to prevent concurrent modification issues  

---

## Project Structure

    .
    ├── server.c
    ├── client.c
    ├── common_utils.c/h
    ├── controllers/
    │   ├── admin_controller*
    │   ├── faculty_controller*
    │   └── student_controller*
    ├── utils/
    │   └── file_ops*
    ├── include/
    │   ├── constants.h
    │   └── structures.h
    ├── data/
    ├── Makefile / build scripts

---

## Tech Stack

- C  
- POSIX Sockets  
- pthreads  
- File I/O + fcntl locking  
- Linux / WSL / MinGW  

---

## How to Run

### Linux / WSL

    ./build.sh
    ./server
    ./client

### Windows (MinGW)

    build.bat
    server.exe
    client.exe

---

## Default Credentials

- Username: admin  
- Password: admin123  

---

## Current Limitations

- Uses thread-per-client model (not scalable for high load)  
- File-based storage limits performance  
- No stress testing or benchmarking  
- No crash recovery or fault tolerance  

---

## Future Improvements

- Thread pool / event-driven concurrency  
- Performance metrics (latency, throughput)  
- Stress testing under high load  
- Record-level locking  
- Logging and monitoring  
- Failure recovery mechanisms  

---

## Author

Ayush Mishra  
IMT2023129  
IIIT Bangalore
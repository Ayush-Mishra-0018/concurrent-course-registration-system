# OS Mini Project – Course Registration Portal (Academia)  
Name – Ayush Mishra  
Roll Number – IMT2023129  
Batch – IMT2023  
Date of Submission – 12th May, 2025  

---

**server.c**  
This is the main server-side program for the course registration system. It handles connections from multiple clients at once using threads. It uses socket programming and file locking to manage data like users and courses. It supports three roles: admin, faculty, and student. Each user sends requests to the server, and the server handles the logic based on the request.

The server stores all data in files and uses file-level locking to prevent data corruption when multiple users are accessing or updating data at the same time. Authentication is handled through passwords. Based on the user type, different actions are allowed.

---

**client.c**  
This is the client-side terminal program. A user runs this to connect to the server. Once connected, the user logs in and is shown options based on their role. For example, students can enroll in courses, faculty can manage courses, and admins can manage users.

The client sends a request string to the server, waits for the response, and then shows the result on the terminal. All actions like login, enrolling, updating profile, etc., happen through the client talking to the server.

---

**Features by Role**

Admin can:
- Add/update/remove faculty and student accounts.
- View all courses and system stats.

Faculty can:
- Add, update, or remove courses they own.
- See list of students enrolled in their courses.
- Update their personal info or change password.

Student can:
- See list of available courses.
- Enroll in or drop courses.
- Update their profile or change password.

---

**How the system works**

- The server listens on a port and accepts client connections.
- Each client connects using TCP.
- Once connected, they authenticate and perform operations.
- Data is saved in files like `faculty.dat`, `student.dat`, `courses.dat`, etc.
- The server uses locks to prevent multiple users from modifying the same file at the same time.

---

## File Structure

- `include/` - Header files
- `utils/` - Utility functions (file operations, etc.)
- `controllers/` - Controller logic for different user types
- `client.c` - Client application
- `server.c` - Server application
- `data/` - Directory where data files are stored (created on first run)

**How to Compile and Run**

## Running the Project

For Linux/WSL: ./build.sh
For Windows:

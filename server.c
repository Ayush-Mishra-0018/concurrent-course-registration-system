#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Platform-specific includes
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #define sleep(x) Sleep(x * 1000)
    #define usleep(x) Sleep(x / 1000)
    #define pthread_t HANDLE
    #define pthread_create(thread, attr, start_routine, arg) ((*thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start_routine, arg, 0, NULL)) ? 0 : -1)
    #define pthread_detach(thread) CloseHandle(thread)
    #define SIGINT SIGTERM
#else
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <pthread.h>
    #include <signal.h>
#endif

#include "include/constants.h"
#include "controllers/admin_controller.h"
#include "controllers/faculty_controller.h"
#include "controllers/student_controller.h"
#include "utils/file_ops.h"
#include "common_utils.h"

// Global variable to keep track of server socket
int server_socket;

// Utility functions for controllers are in common_utils.h/c

// Function to handle client connections
#ifdef _WIN32
DWORD WINAPI handle_client(LPVOID arg) {
#else
void *handle_client(void *arg) {
#endif
    ClientInfo *client_info = (ClientInfo *)arg;
    int client_socket = client_info->socket_fd;
    
    // Send welcome message
    char welcome_message[BUFFER_SIZE];
    sprintf(welcome_message, "\n....................Welcome Back to Academia :: Course Registration....................\n");
    send_message(client_socket, welcome_message);
    
    // Prompt for login type
    send_message(client_socket, "Login Type\n");
    send_message(client_socket, "Enter Your Choice { 1.Admin, 2.Professor, 3.Student }: ");
    
    // Get user type
    char buffer[BUFFER_SIZE];
    if (receive_input(client_socket, buffer, sizeof(buffer)) <= 0) {
        printf("Error receiving user type from client\n");
        close(client_socket);
        free(client_info);
        return NULL;
    }
    
    buffer[strcspn(buffer, "\n")] = 0; // Remove newline
    int user_type = atoi(buffer);
    
    // Handle different user types
    int user_id = -1;
    
    switch (user_type) {
        case ADMIN:
            user_id = admin_login(client_socket);
            if (user_id > 0) {
                admin_menu(client_socket, user_id);
            }
            break;
        case FACULTY:
            user_id = faculty_login(client_socket);
            if (user_id > 0) {
                faculty_menu(client_socket, user_id);
            }
            break;
        case STUDENT:
            user_id = student_login(client_socket);
            if (user_id > 0) {
                student_menu(client_socket, user_id);
            }
            break;
        default:
            send_message(client_socket, "Invalid user type! Please try again.\n");
    }
    
    // Send exit message
    send_message(client_socket, "EXIT\n");
    
    // Close client socket and free memory
#ifdef _WIN32
    closesocket(client_socket);
#else
    close(client_socket);
#endif
    free(client_info);
    
    return NULL;
}

// Signal handler for graceful shutdown
void handle_signal(int sig) {
    if (sig == SIGINT) {
        printf("\nShutting down server...\n");
#ifdef _WIN32
        closesocket(server_socket);
        WSACleanup();
#else
        close(server_socket);
#endif
        exit(EXIT_SUCCESS);
    }
}

int main() {
    struct sockaddr_in server_addr;
    
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        perror("Failed to initialize Winsock");
        exit(EXIT_FAILURE);
    }
    
    // Set console handler for Ctrl+C
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)handle_signal, TRUE);
#else
    // Register signal handler
    signal(SIGINT, handle_signal);
#endif
    
    // Initialize data files
    init_data_files();
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options to reuse address
    int opt = 1;
#ifdef _WIN32
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
#else
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
#endif
        perror("Error setting socket options");
        exit(EXIT_FAILURE);
    }
    
    // Initialize server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server started on port %d\n", PORT);
    printf("Press Ctrl+C to stop the server\n");
    
    // Accept and handle client connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        // Accept connection
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
#ifdef _WIN32
        printf("Client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
#else
        printf("Client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
#endif
        
        // Create client info structure
        ClientInfo *client_info = (ClientInfo *)malloc(sizeof(ClientInfo));
        if (client_info == NULL) {
            perror("Failed to allocate memory for client info");
#ifdef _WIN32
            closesocket(client_socket);
#else
            close(client_socket);
#endif
            continue;
        }
        
        client_info->socket_fd = client_socket;
        client_info->client_addr = client_addr;
        
        // Create thread to handle client
#ifdef _WIN32
        HANDLE thread_id;
#else
        pthread_t thread_id;
#endif
        if (pthread_create(&thread_id, NULL, handle_client, (void *)client_info) < 0) {
            perror("Thread creation failed");
#ifdef _WIN32
            closesocket(client_socket);
#else
            close(client_socket);
#endif
            free(client_info);
            continue;
        }
        
        // Detach thread
        pthread_detach(thread_id);
    }
    
    // Close server socket
#ifdef _WIN32
    closesocket(server_socket);
    WSACleanup();
#else
    close(server_socket);
#endif
    
    return 0;
} 
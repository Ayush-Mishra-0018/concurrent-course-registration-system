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
#else
    #include <arpa/inet.h>
    #include <sys/socket.h>
#endif

#include "include/constants.h"
#include "common_utils.h"

void display_welcome_message() {
    // printf("\n....................Welcome Back to Academia :: Course Registration....................\n");
}

// Helper function to read complete response
int read_complete_response(int socket, char *buffer, int buffer_size) {
    int total_bytes = 0;
    int bytes_received;
    buffer[0] = '\0';
    
    do {
        bytes_received = recv(socket, buffer + total_bytes, buffer_size - total_bytes - 1, 0);
        if (bytes_received <= 0) {
            return -1; // Error or connection closed
        }
        
        total_bytes += bytes_received;
        buffer[total_bytes] = '\0';
        
        // If we get a prompt or have waited long enough, break out
        if (strstr(buffer, "Enter Your Choice: ") != NULL || 
            strstr(buffer, "Enter ") != NULL ||
            strstr(buffer, "Confirm ") != NULL) {
            break;
        }
        
        // Small delay to see if more data is coming
        usleep(100000);
    } while (total_bytes < buffer_size - 1);
    
   
    if (strstr(buffer, "Enter Your Choice:") != NULL) {
        //correct no need to do anything
    } else if (strstr(buffer, "Enter ") != NULL) {
        //correct no need to do anything
    } else if (strstr(buffer, "Confirm ") != NULL) {
        printf("(Debug: Received confirmation prompt)\n");
    }
    
    return total_bytes;
}

void handle_connection(int client_socket) {
    char buffer[BUFFER_SIZE * 4]; 
    
    // Receive and display initial welcome message
    int bytes_received = read_complete_response(client_socket, buffer, sizeof(buffer));
    if (bytes_received <= 0) {
        printf("Connection error or server disconnected.\n");
        return;
    }
    
    printf("%s", buffer);
    
    
    while (1) {
        // Read user input
        char input[BUFFER_SIZE];
        fgets(input, BUFFER_SIZE, stdin);
        
        // Send input to server 
        if (send(client_socket, input, strlen(input), 0) <= 0) {
            printf("Error sending data to server.\n");
            break;
        }
        
        // Sleep a bit to give server time to process
        usleep(200000); 
        
        // Receive response from server
        bytes_received = read_complete_response(client_socket, buffer, sizeof(buffer));
        
        if (bytes_received <= 0) {
            printf("Connection error or server disconnected.\n");
            break;
        }
        
        // Check if server sent exit message
        if (strcmp(buffer, "EXIT\n") == 0) {
            printf("Logged out. Goodbye!\n");
            break;
        }
        
        // Display server response
        printf("%s", buffer);
    }
}

int main() {
    int client_socket;
    struct sockaddr_in server_addr;
    
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        perror("Failed to initialize Winsock");
        exit(EXIT_FAILURE);
    }
#endif
    
    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }
    
    // Initialize server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Local server
    
    display_welcome_message();
    
    // Connect to server
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(client_socket);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to server.\n");
    
    // Handle connection
    handle_connection(client_socket);
    
    // Close socket
#ifdef _WIN32
    closesocket(client_socket);
    WSACleanup();
#else
    close(client_socket);
#endif
    
    return 0;
} 
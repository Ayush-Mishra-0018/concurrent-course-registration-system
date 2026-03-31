#include "common_utils.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

// Utility function to send message to client
void send_message(int client_socket, char *message) {
    int message_len = strlen(message);
    int bytes_sent = 0;
    int max_attempts = 3;
    int attempts = 0;
    
    while (bytes_sent < message_len && attempts < max_attempts) {
        int sent = write(client_socket, message + bytes_sent, message_len - bytes_sent);
        if (sent <= 0) {
            //if it is non blocking
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full, wait a bit and retry

                struct timespec delay;
                delay.tv_sec = 0;
                delay.tv_nsec = 50000000; // 50ms
                nanosleep(&delay, NULL);
                attempts++;
                continue;
            }
            // Other error occurred
            break;
        }
        
        bytes_sent += sent;
        attempts = 0; // Reset attempts counter if we made progress
    }
    
    // Small delay for system to process
    struct timespec delay;
    delay.tv_sec = 0;
    delay.tv_nsec = 20000000;
    nanosleep(&delay, NULL);
}

// Utility function to receive input from client
int receive_input(int client_socket, char *buffer, int buffer_size) {
    memset(buffer, 0, buffer_size);
    int bytes_read = 0;
    int max_attempts = 3;
    int attempts = 0;
    
    while (bytes_read == 0 && attempts < max_attempts) {
        bytes_read = read(client_socket, buffer, buffer_size - 1);
        
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available yet, wait a bit and retry
                struct timespec delay;
                delay.tv_sec = 0;
                delay.tv_nsec = 50000000; 
                nanosleep(&delay, NULL);
                attempts++;
                continue;
            }
            // Other error occurred
            return -1;
        } else if (bytes_read == 0) {
    
            return 0;
        }
        
   
        buffer[bytes_read] = '\0';
        
        // Remove CR/LF characters from the end if present
        int i = bytes_read - 1;
        while (i >= 0 && (buffer[i] == '\r' || buffer[i] == '\n')) {
            buffer[i] = '\0';
            i--;
        }
    }
    
    return bytes_read;
} 
#ifndef COMMON_UTILS_H
#define COMMON_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "include/constants.h"

// Utility functions for sending/receiving messages
void send_message(int client_socket, char *message);
int receive_input(int client_socket, char *buffer, int buffer_size);

#endif // COMMON_UTILS_H 
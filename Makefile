CC = gcc
CFLAGS = -Wall -pthread
LDFLAGS = -pthread

SERVER_SRCS = server.c \
              common_utils.c \
              utils/file_ops.c \
              utils/file_ops2.c \
              utils/file_ops3.c \
              utils/file_ops4.c \
              controllers/admin_controller_fixed.c \
              controllers/admin_controller2.c \
              controllers/faculty_controller_fixed.c \
              controllers/faculty_controller2_fixed.c \
              controllers/student_controller_fixed.c \
              controllers/student_controller2_fixed.c

CLIENT_SRCS = client.c \
              common_utils.c

SERVER_OBJS = $(SERVER_SRCS:.c=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

all: clean dirs server client

dirs:
	mkdir -p data

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f server client $(SERVER_OBJS) $(CLIENT_OBJS)
	rm -rf data

debug: CFLAGS += -g
debug: clean all

.PHONY: all clean dirs debug 
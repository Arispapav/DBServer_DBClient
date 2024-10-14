#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdint.h>
#include "msg.h"

#define BUF_SIZE 256

void Usage(char *progname);
int LookupName(char *name, unsigned short port, struct sockaddr_storage *ret_addr, size_t *ret_addrlen);
int Connect(const struct sockaddr_storage *addr, const size_t addrlen, int *ret_fd);

// Function to receive a message from a socket.
// Returns 1 on successful read, 0 if the connection is closed by the peer, and -1 on error.
int recv_msg(int socket_fd, struct msg *message) {
    ssize_t total_read = 0;
    size_t to_read = sizeof(*message);
    char *buf = (char *)message;
    // Loop until the whole message is read
    while (total_read < to_read) {
        ssize_t bytes_read = read(socket_fd, buf + total_read, to_read - total_read);
        // Handle read errors
        if (bytes_read == -1) {
            if (errno == EINTR) continue; // Continue if interrupted by signal
            perror("read");
            return -1; // Return error on read failure
        } else if (bytes_read == 0) {
            printf("Connection closed by peer.\n");
            return 0; // Return 0 on graceful close
        }
        total_read += bytes_read;
    }
    return 1; // Return 1 on successful read
}

// Function to display program usage and exit.
void Usage(char *progname)
{
    printf("usage: %s hostname port\n", progname);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    // Verify command line arguments
    if (argc != 3) {
        Usage(argv[0]);
    }

    unsigned short port = 0;
    // Parse port number from command line
    if (sscanf(argv[2], "%hu", &port) != 1) {
        Usage(argv[0]);
    }

    struct sockaddr_storage addr;
    size_t addrlen;
    // Resolve hostname and port into a sockaddr_storage structure
    if (!LookupName(argv[1], port, &addr, &addrlen)) {
        Usage(argv[0]);
    }

    int socket_fd;
    // Establish connection to the server
    if (!Connect(&addr, addrlen, &socket_fd)) {
        Usage(argv[0]);
    }

    int choice;
    struct msg message;
    int succeed;

    while (1) {
        printf("Enter your choice (1 to put, 2 to get, 0 to quit): ");
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Please enter an integer.\n");
            while (getchar() != '\n'); // clear the buffer
            continue;
        }

        switch (choice) {
        case 1: // PUT
            message.type = PUT;
            printf("Enter the name: ");
            scanf(" %127[^\n]", message.rd.name); // Read string with spaces
            printf("Enter the id: ");
            scanf("%u", &message.rd.id);
            memset(message.rd.pad, 0, sizeof(message.rd.pad)); // Clear padding
            write(socket_fd, &message, sizeof(message));
            succeed = recv_msg(socket_fd, &message);
            if (succeed == 1 && message.type == SUCCESS) {
                printf("Put success.\n");
            } else {
                printf("Put failed.\n");
            }
            break;
        case 2: // GET
            message.type = GET;
            printf("Enter the id: ");
            scanf("%u", &message.rd.id);
            memset(message.rd.name, 0, sizeof(message.rd.name)); // Clear name
            memset(message.rd.pad, 0, sizeof(message.rd.pad));   // Clear padding
            write(socket_fd, &message, sizeof(message));
            succeed = recv_msg(socket_fd, &message);
            if (succeed == 1 && message.type == SUCCESS) {
                printf("Name: %s\nID: %u\n", message.rd.name, message.rd.id);
            } else {
                printf("Get failed.\n");
            }
            break;
        case 0: // QUIT
            close(socket_fd);
            return EXIT_SUCCESS;
        default:
            printf("Invalid choice.\n");
            continue;
        }
    }

    return EXIT_SUCCESS;
}


int LookupName(char *name, unsigned short port, struct sockaddr_storage *ret_addr, size_t *ret_addrlen)
{
    struct addrinfo hints, *results;
    int retval;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((retval = getaddrinfo(name, NULL, &hints, &results)) != 0) {
        printf("getaddrinfo failed: %s\n", gai_strerror(retval));
        return 0;
    }

    if (results->ai_family == AF_INET) {
        struct sockaddr_in *v4addr = (struct sockaddr_in *)results->ai_addr;
        v4addr->sin_port = htons(port);
    } else if (results->ai_family == AF_INET6) {
        struct sockaddr_in6 *v6addr = (struct sockaddr_in6 *)results->ai_addr;
        v6addr->sin6_port = htons(port);
    } else {
        printf("getaddrinfo failed to provide an IPv4 or IPv6 address\n");
        freeaddrinfo(results);
        return 0;
    }

    memcpy(ret_addr, results->ai_addr, results->ai_addrlen);
    *ret_addrlen = results->ai_addrlen;
    freeaddrinfo(results);
    return 1;
}

int Connect(const struct sockaddr_storage *addr, const size_t addrlen, int *ret_fd)
{
    int socket_fd = socket(addr->ss_family, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        printf("socket() failed: %s\n", strerror(errno));
        return 0;
    }

    if (connect(socket_fd, (const struct sockaddr *)addr, addrlen) == -1) {
        printf("connect() failed: %s\n", strerror(errno));
        return 0;
    }

    *ret_fd = socket_fd;
    return 1;
}

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>
#include "msg.h" // Include the message structure header
#define DB_FILE "database.dat" // Database file name
void Usage(char *progname);
void PrintOut(int fd, struct sockaddr *addr, size_t addrlen);
void PrintReverseDNS(struct sockaddr *addr, size_t addrlen);
void PrintServerSide(int client_fd, int sock_family);

int Listen(char *portnum, int *sock_family);
void *HandleClient(void *arg);
int check_duplicate(int client_fd, struct record *new_record);
void send_msg(int client_fd, uint8_t type, struct record *rd);
void handle_put(int, struct record*);
void handle_get(int, uint32_t id);

int main(int argc, char **argv) {
    if (argc != 2) {
        Usage(argv[0]);
    }
    

    int sock_family;
    int listen_fd = Listen(argv[1], &sock_family);
    if (listen_fd <= 0) {
        printf("Couldn't bind to any addresses.\n");
        return EXIT_FAILURE;
    }

    while (1) {
        struct sockaddr_storage caddr;
        socklen_t caddr_len = sizeof(caddr);
        int client_fd = accept(listen_fd, (struct sockaddr *)(&caddr), &caddr_len);
        if (client_fd < 0) {
            if ((errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK))
                continue;
            printf("Failure on accept: %s\n", strerror(errno));
            break;
        }

       
        // Create a new thread for each client
        pthread_t tid;
        int *pclient = malloc(sizeof(int));
        *pclient = client_fd;
        pthread_create(&tid, NULL, HandleClient, pclient);
        pthread_detach(tid); // The thread will clean up itself
    }

    close(listen_fd);
    return EXIT_SUCCESS;
}

void Usage(char *progname) {
    printf("usage: %s port\n", progname);
    exit(EXIT_FAILURE);
}

// Handles the PUT request to add a new record to the database.
// Attempts to write the record into the database file.
// Notifies the client of success or failure via a message.
void handle_put(int client_fd, struct record *new_record) {
    // Opens the database file for appending, creating it if it doesn't exist.
    int fd = open(DB_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        printf("Failed to open database file: %s\n", strerror(errno));
        send_msg(client_fd, FAIL, NULL);
        return;
    }

    // Check if the record already exists to prevent duplicates.
    if (check_duplicate(client_fd, new_record) == 1) {
        // Writes the new record to the database file.
        if (write(fd, new_record, sizeof(struct record)) == sizeof(struct record)) {
            send_msg(client_fd, SUCCESS, NULL);
        } else {
            printf("Failed to write to database file: %s\n", strerror(errno));
            send_msg(client_fd, FAIL, NULL);
        }
    } else {
        send_msg(client_fd, FAIL, NULL);
    }
    close(fd);
}


// Checks for duplicate records in the database.
// Returns 1 if no duplicate is found, -1 if a duplicate exists or an error occurs.
int check_duplicate(int client_fd, struct record *new_record) {
    int fd = open(DB_FILE, O_RDONLY);
    if (fd == -1) {
        printf("Failed to open database file: %s\n", strerror(errno));
        send_msg(client_fd, FAIL, NULL);
        return -1;
    }

    struct record temp;
    // Reads the database file record by record.
    while (read(fd, &temp, sizeof(struct record)) == sizeof(struct record)) {
        if (temp.id == new_record->id) {
            close(fd);
            return -1; // Duplicate found.
        }
    }
    close(fd);
    return 1; // No duplicates found.
}


// Handles the GET request to retrieve a record based on its ID.
// Searches for the record in the database file and sends it to the client.
void handle_get(int client_fd, uint32_t id) {
    int fd = open(DB_FILE, O_RDONLY);
    if (fd == -1) {
        printf("Failed to open database file: %s\n", strerror(errno));
        send_msg(client_fd, FAIL, NULL);
        return;
    }

    struct record temp;
    // Reads the database file record by record.
    while (read(fd, &temp, sizeof(struct record)) == sizeof(struct record)) {
        if (temp.id == id) {
            printf("Match found: ID=%u, Name=%s\n", temp.id, temp.name);
            send_msg(client_fd, SUCCESS, &temp);
            close(fd);
            return;
        }
    }

    printf("No matching record found for ID: %u\n", id);
    send_msg(client_fd, FAIL, NULL);
    close(fd);
}


// Sends a message to the client with an optional record data.
// Handles message structuring and network communication.
void send_msg(int client_fd, uint8_t type, struct record *rd) {
    struct msg response;
    memset(&response, 0, sizeof(response)); // Initializes the message structure.
    response.type = type;

    if (rd && type == SUCCESS) {
        response.rd = *rd; // Copies record data if successful and data is provided.
    }

    ssize_t total_written = 0;
    size_t to_write = sizeof(response);
    char *buf = (char *)&response;
    // Writes the response message to the client socket.
    while (total_written < to_write) {
        ssize_t bytes_written = write(client_fd, buf + total_written, to_write - total_written);
        if (bytes_written == -1) {
            if (errno == EINTR) continue; // Ignores interruption errors.
            perror("write");
            break; // Exits loop on write error.
        }
        total_written += bytes_written;
    }
    printf("Sent message: Type=%d, ID=%u\n", type, rd ? rd->id : 0);
}

// Main function to handle client requests in a multi-threaded server environment.
// Reads messages from the client, determines the request type, and calls the appropriate handler.
void *HandleClient(void *arg) {
    int client_fd = *(int *)arg;
    free(arg); // Frees the dynamically allocated client file descriptor.

    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    getpeername(client_fd, (struct sockaddr *)&addr, &addrlen); // Retrieves client's address info.

    printf("\nNew client connection\n");
    PrintOut(client_fd, (struct sockaddr *)&addr, addrlen);
    PrintReverseDNS((struct sockaddr *)&addr, addrlen);
    PrintServerSide(client_fd, addr.ss_family);

    while (1) {
        struct msg message;
        printf("reading message\n");
        ssize_t res = read(client_fd, &message, sizeof(message));
        printf("message read\n");
        if (res == 0) {
            printf("[The client disconnected.]\n");
            break;
        }
        if (res == -1) {
            if ((errno == EAGAIN) || (errno == EINTR)) {
                continue; // Handles transient errors gracefully.
            }
            printf("Error on client socket: %s\n", strerror(errno));
            break;
        }

        switch (message.type) {
            case PUT:
                handle_put(client_fd, &message.rd);
                break;
            case GET:
                handle_get(client_fd, message.rd.id);
                break;
            default:
                printf("Unknown request type: %d\n", message.type);
        }
    }

    close(client_fd); // Closes the client socket on session end.
    return NULL; // Returns NULL to signify thread completion.
}


void PrintOut(int fd, struct sockaddr *addr, size_t addrlen) {
  printf("Socket [%d] is bound to:\n", fd);
  if (addr->sa_family == AF_INET) {
    // Print out the IPV4 address and port
    char astring[INET_ADDRSTRLEN];
    struct sockaddr_in *in4 = (struct sockaddr_in *)(addr);
    inet_ntop(AF_INET, &(in4->sin_addr), astring, INET_ADDRSTRLEN);
    printf(" IPv4 address %s", astring);
    printf(" and port %d\n", ntohs(in4->sin_port));
  } 
  else if (addr->sa_family == AF_INET6) {
    // Print out the IPV6 address and port
    char astring[INET6_ADDRSTRLEN];
    struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)(addr);
    inet_ntop(AF_INET6, &(in6->sin6_addr), astring, INET6_ADDRSTRLEN);
    printf("IPv6 address %s", astring);
    printf(" and port %d\n", ntohs(in6->sin6_port));
  } 
  else {
    printf(" ???? address and port ???? \n");
  }
}

void PrintReverseDNS(struct sockaddr *addr, size_t addrlen) {
  char hostname[1024];  // ought to be big enough.
  if (getnameinfo(addr, addrlen, hostname, 1024, NULL, 0, 0) != 0) {
    sprintf(hostname, "[reverse DNS failed]");
  }
  printf("DNS name: %s \n", hostname);
}

void PrintServerSide(int client_fd, int sock_family) {
  char hname[1024];
  hname[0] = '\0';

  printf("Server side interface is ");
  if (sock_family == AF_INET) {
    // The server is using an IPv4 address.
    struct sockaddr_in srvr;
    socklen_t srvrlen = sizeof(srvr);
    char addrbuf[INET_ADDRSTRLEN];
    getsockname(client_fd, (struct sockaddr *) &srvr, &srvrlen);
    inet_ntop(AF_INET, &srvr.sin_addr, addrbuf, INET_ADDRSTRLEN);
    printf("%s", addrbuf);
    // Get the server's dns name, or return it's IP address as
    // a substitute if the dns lookup fails.
    getnameinfo((const struct sockaddr *) &srvr,srvrlen, hname, 1024, NULL, 0, 0);
    printf(" [%s]\n", hname);
  } 
  else {
    // The server is using an IPv6 address.
    struct sockaddr_in6 srvr;
    socklen_t srvrlen = sizeof(srvr);
    char addrbuf[INET6_ADDRSTRLEN];
    getsockname(client_fd, (struct sockaddr *) &srvr, &srvrlen);
    inet_ntop(AF_INET6, &srvr.sin6_addr, addrbuf, INET6_ADDRSTRLEN);
    printf("%s", addrbuf);
    // Get the server's dns name, or return it's IP address as
    // a substitute if the dns lookup fails.
    getnameinfo((const struct sockaddr *) &srvr,
                srvrlen, hname, 1024, NULL, 0, 0);
    printf(" [%s]\n", hname);
  }
}

int Listen(char *portnum, int *sock_family) {
  // Populate the "hints" addrinfo structure for getaddrinfo().
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;  // IPv6 (also handles IPv4 clients)
  hints.ai_socktype = SOCK_STREAM;  // stream
  hints.ai_flags = AI_PASSIVE;      // use wildcard "in6addr_any" address
  hints.ai_protocol = IPPROTO_TCP;  // TCP protocol
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  struct addrinfo *result;
  int res = getaddrinfo(NULL, portnum, &hints, &result);
  if (res != 0) {
    printf("getaddrinfo failed: %s\n", gai_strerror(res));
    return -1;
  }

  int listen_fd = -1;
  for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
    listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (listen_fd == -1) {
      continue;
    }

    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
      PrintOut(listen_fd, rp->ai_addr, rp->ai_addrlen);
      *sock_family = rp->ai_family;
      break;
    }

    close(listen_fd);
    listen_fd = -1;
  }

  freeaddrinfo(result);
  if (listen_fd == -1) {
    return listen_fd;
  }

  if (listen(listen_fd, SOMAXCONN) != 0) {
    printf("Failed to mark socket as listening: %s\n", strerror(errno));
    close(listen_fd);
    return -1;
  }

  return listen_fd;
}


//
// Created by linayang on 10/16/23.
//
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int socket_run(char *argv[]);
static int socket_create(void);
static void socket_bind(int sockfd, const char *path);
static void start_listening(int server_fd, int backlog);
static int socket_accept_connection(int server_fd,
                                    struct sockaddr_un *client_addr);
static void handle_connection(int client_sockfd,
                              struct sockaddr_un *client_addr);
static void socket_close(int sockfd);

static void fifo_run(char *argv[]);
char *doesExist(const char *command, char *array);
int runCommand(const char *path, char *const *argument);
int executeCommand(char *arg);

#define GET_ENV_TYPE "PATH"
#define SIZE 2046
#define FAIL_VALUE 1
#define REQUIRED_VALUES 3
#define FIFO_VALUE "-fifo"
#define DOMAIN_VALUE "-domain"
#define PATH_VALUE 2

int main(int argc, char *argv[]) {
  if (argc != REQUIRED_VALUES) {
    perror("Command requires 2 arguments, the type of IPC and path");
    return EXIT_FAILURE;
  }
  if (argv[PATH_VALUE] == NULL) {
    perror("Please provide a path");
    return EXIT_FAILURE;
  }
  if (argv[1] == NULL) {
    perror("Please provide either -fifo or -domain");
    return EXIT_FAILURE;
  }

  if (strcmp(argv[1], FIFO_VALUE) == 0) {
    fifo_run(argv);
  } else if (strcmp(argv[1], DOMAIN_VALUE) == 0) {
    socket_run(argv);
  } else {
    perror("invalid IPC provided");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
static void fifo_run(char *argv[]) {
  int fd;
  uint8_t size;
  char word[UINT8_MAX + 1];
  // Create the FIFO if it doesn't exist
  mkfifo(argv[PATH_VALUE], S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

  // Open the FIFO for reading
  fd = open(argv[PATH_VALUE], O_RDONLY | O_CLOEXEC);

  if (fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }

  // Read and print words from the client
  while (read(fd, &size, sizeof(uint8_t)) > 0) {
    read(fd, word, size);
    word[size] = '\0';
    break;
  }

  close(fd);

  mkfifo(argv[PATH_VALUE], S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  fd = open(argv[PATH_VALUE], O_WRONLY); // NOLINT(android-cloexec-open)
  if (fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }
  if (dup2(fd, STDOUT_FILENO) == -1) {
    perror("Error redirecting stdout");
    close(fd);
    exit(EXIT_FAILURE);
  }
  executeCommand(word);

  if (fflush(stdout) != 0) {
    perror("Error flushing stdout");
    close(fd); // Close the file descriptor before exiting
    exit(EXIT_FAILURE);
  }
  close(fd);
}
static int socket_run(char *argv[]) {
  int sockfd;
  struct sockaddr_un client_addr;
  int client_sockfd;
  char *socket_path;
  socket_path = argv[PATH_VALUE];
  if (socket_path == NULL) {
    printf("Failed no socket path");
    return EXIT_FAILURE;
  }
  unlink(socket_path);
  sockfd = socket_create();
  socket_bind(sockfd, socket_path);
  start_listening(sockfd, SOMAXCONN);

  client_sockfd = socket_accept_connection(sockfd, &client_addr);

  handle_connection(client_sockfd, &client_addr);
  socket_close(client_sockfd);

  socket_close(sockfd);
  unlink(socket_path);
  return 0;
}
static int socket_create(void) {
  int sockfd;

#ifdef SOCK_CLOEXEC
  sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
  sockfd = socket(AF_UNIX, SOCK_STREAM, 0); // NOLINT(android-cloexec-socket)
#endif

  if (sockfd == -1) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  return sockfd;
}

static void socket_bind(int sockfd, const char *path) {
  struct sockaddr_un addr;

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
  addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind");
    exit(EXIT_FAILURE);
  }

  printf("Bound to domain socket: %s\n", path);
}

static void start_listening(int server_fd, int backlog) {
  if (listen(server_fd, backlog) == -1) {
    perror("listen failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  printf("Listening for incoming connections...\n");
}

static int socket_accept_connection(int server_fd,
                                    struct sockaddr_un *client_addr) {
  int client_fd;
  char client_host[NI_MAXHOST];
  socklen_t client_addr_len;

  errno = 0;
  client_addr_len = sizeof(*client_addr);
  client_fd =
      accept(server_fd, (struct sockaddr *)client_addr, &client_addr_len);

  if (client_fd == -1) {
    if (errno != EINTR) {
      perror("accept failed");
    }

    return -1;
  }

  if (getnameinfo((struct sockaddr *)client_addr, client_addr_len, client_host,
                  NI_MAXHOST, NULL, 0, 0) == 0) {
    printf("Accepted a new connection from %s\n", client_host);
  } else {
    printf("Unable to get client information\n");
  }

  return client_fd;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void handle_connection(int client_sockfd,
                              struct sockaddr_un *client_addr) {
  uint8_t size;
  char word[UINT8_MAX + 1];

  if (dup2(client_sockfd, STDOUT_FILENO) == -1) {
    perror("Error redirecting stdout");
  }
  if (fflush(stdout) != 0) {
    perror("Error flushing stdout");
  }
  read(client_sockfd, &size, sizeof(uint8_t));

  read(client_sockfd, word, size);
  word[size] = '\0';
  executeCommand(word);
}

#pragma GCC diagnostic pop

static void socket_close(int sockfd) {
  if (close(sockfd) == -1) {
    perror("Error closing socket");
    exit(EXIT_FAILURE);
  }
}

int executeCommand(char *arg) {
  char *cmdPtr;
  const char *delimiter = " ";
  char *command;
  char pathArray[SIZE];
  char *path;
  char *argument[SIZE];
  int success;
  size_t i;

  command = strtok_r(arg, delimiter, &cmdPtr); // tokenize argument

  if (command == NULL) { // checks if command has any values
    printf("Command and arguments cannot be empty\n");
    return FAIL_VALUE;
  }

  path = doesExist(command, pathArray); // grabs path if command is found

  if (path == NULL) { // check if path is NULL aka no command
    return FAIL_VALUE;
  }

  // create array of commands for execv

  for (i = 0; command != NULL && i < SIZE; i++) {
    argument[i] = command;
    command = strtok_r(NULL, " ", &cmdPtr);
  }
  if (i >= SIZE) {
    argument[SIZE - 1] = NULL;
  } else {
    argument[i] = NULL;
  }

  success = runCommand(path, (char *const *const)argument);
  if (success == FAIL_VALUE) {
    return FAIL_VALUE;
  }

  return 0;
}

/**
 * runCommand
 * @param path string/char pointer
 * @param argument string/char pointer
 * @return 0 or -1
 */
int runCommand(const char *path, char *const *const argument) {
  int status;
  pid_t pid;
  pid = fork();

  if (path == NULL) { // check if path is NULL aka no command
    return FAIL_VALUE;
  }

  if (pid == FAIL_VALUE) {
    perror("Error creating child process");
    return FAIL_VALUE;
  }

  if (pid == 0) {
    //      printf("Name: %s, Process: PID=%d, Parent PID=%d\n", "Child",
    //      getpid(),
    //             getppid());
    //    printf("Executing command:");
    if (access(path, X_OK) == 0) {
      printf("Path is ok");
      if (execv(path, argument) == -1) {
        perror("Failed to execute");
        fprintf(stderr, "Error message: %s\n", strerror(errno));
        return FAIL_VALUE;
      }
    } else {
      printf("path is not okay");
    }
  }

  if (waitpid(pid, &status, 0) == FAIL_VALUE) {
    perror("Error waiting for child process\n");
    return FAIL_VALUE;
  }

  if (pid != 0) {
    //    printf("\n\n");
    //    printf("Name: %s, Process: PID=%d, Parent PID=%d\n", "Parent",
    //    getpid(),
    //           getppid());
    //    printf("Child executed properly\n");
    return 0;
  }
  printf("Error running parent process\n");
  return FAIL_VALUE;
}

/**
 * doesExist
 * @param command string/char pointer
 * @param pathArray string/char pointer
 * @return char pointer
 */
char *doesExist(const char *command, char *pathArray) {
  char *path = getenv(GET_ENV_TYPE);
  char *pathToken;
  char *pathptr = path;
  pathToken = strtok_r(path, ":", &pathptr);

  for (; pathToken != NULL;) {
    snprintf(pathArray, SIZE, "%s/%s", pathToken, command);
    //    printf("%s", pathArray);
    if (access(pathArray, X_OK) == 0) {
      return pathArray;
    }
    pathArray[0] = '\0';
    pathToken = strtok_r(NULL, ":", &pathptr);
  }
  printf("%s command is not found in path\n", command);
  return NULL;
}

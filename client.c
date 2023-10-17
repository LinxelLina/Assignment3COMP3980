#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int socket_run(char *argv[]);
static int socket_create(void);
static void setup_socket_address(struct sockaddr_un *addr, const char *path);
static int connect_to_server(int sockfd, const struct sockaddr_un *addr);
static void socket_close(int sockfd);

static void fifo_run(char *argv[]);

#define LEN 1024
#define REQUIRED_VALUES 4
#define FIFO_VALUE "-fifo"
#define DOMAIN_VALUE "-domain"
#define ARG_VALUE 3
#define PATH_VALUE 2

int main(int argc, char *argv[]) {
  if (argc != REQUIRED_VALUES) {
    perror("Command requires 2 arguments, the type of IPC and path");
    return EXIT_FAILURE;
  }
  if (argv[1] == NULL) {
    perror("Please provide either -fifo or -domain");
    return EXIT_FAILURE;
  }
  if (argv[PATH_VALUE] == NULL) {
    perror("Please provide a path");
    return EXIT_FAILURE;
  }
  if (argv[ARG_VALUE] == NULL) {
    perror("Please provide an argument");
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
  char *word;
  size_t word_len;
  uint8_t size;
  char buffer[LEN];
  ssize_t bytes_read;

  fd = open(argv[PATH_VALUE], O_WRONLY | O_CLOEXEC);

  if (fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }

  word = argv[ARG_VALUE];

  word_len = strlen(word);

  if (word_len > UINT8_MAX) {
    fprintf(stderr, "Word exceeds maximum length\n");
    close(fd);
    exit(EXIT_FAILURE);
  }

  // Write the size of the word as uint8_t
  size = (uint8_t)word_len;
  write(fd, &size, sizeof(uint8_t));

  // Write the word
  write(fd, word, word_len);

  close(fd);

  fd = open(argv[PATH_VALUE], O_RDONLY | O_CLOEXEC);
  if (fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }

  while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
    write(STDOUT_FILENO, buffer,
          (size_t)bytes_read); // Display the data on the console
  }

  close(fd);
}
static int socket_run(char *argv[]) {
  struct sockaddr_un addr;
  int sockfd;
  char *word;
  size_t word_len;
  uint8_t size;
  char buffer[LEN];
  ssize_t bytes_read;
  char *socket_path;

  socket_path = argv[PATH_VALUE];
  setup_socket_address(&addr, socket_path);
  sockfd = socket_create();
  connect_to_server(sockfd, &addr);

  word = argv[ARG_VALUE];

  word_len = strlen(word);

  if (word_len > UINT8_MAX) {
    fprintf(stderr, "Word exceeds maximum length\n");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  // Write the size of the word as uint8_t
  size = (uint8_t)word_len;
  send(sockfd, &size, sizeof(uint8_t), 0);

  // Write the word
  write(sockfd, word, word_len);

  while ((bytes_read = read(sockfd, buffer, sizeof(buffer))) > 0) {
    write(STDOUT_FILENO, buffer,
          (size_t)bytes_read); // Display the data on the console
  }

  socket_close(sockfd);
  return 0;
}

static int socket_create(void) {
  int sockfd;

#ifdef SOCK_CLOEXEC
  sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
  sockfd = socket(AF_UNIX, SOCK_STREAM, 0); //
  NOLINT(android - cloexec - socket)
#endif

  if (sockfd == -1) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  return sockfd;
}

static void setup_socket_address(struct sockaddr_un *addr, const char *path) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  strncpy(addr->sun_path, path, sizeof(addr->sun_path) - 1);
  addr->sun_path[sizeof(addr->sun_path) - 1] = '\0';
}

static int connect_to_server(int sockfd, const struct sockaddr_un *addr) {
  if (connect(sockfd, (const struct sockaddr *)addr, sizeof(*addr)) == -1) {
    perror("Connection failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  return sockfd;
}

static void socket_close(int sockfd) {
  if (close(sockfd) == -1) {
    perror("Error closing socket");
    exit(EXIT_FAILURE);
  }
}

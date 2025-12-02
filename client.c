/*
 * rfs.c -- Remote File System Client
 *
 * adapted from:
 *   https://www.educative.io/answers/how-to-implement-tcp-sockets-in-c
 *
 * Extended for Question 1: WRITE command to send files to server

 */

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUFFER_SIZE 8196
#define MAX_PATH 512
#define DEFAULT_PORT 2000
#define DEFAULT_HOST "127.0.0.1"

// get file size
long get_file_size(const char* filename) {
  struct stat st;
  if (stat(filename, &st) == 0) {
    return st.st_size;
  }
  return -1;
}

// Print usage information
void print_usage(const char* prog) {
  printf("Usage: %s [-h host] [-p port] COMMAND arguments...\n\n", prog);
  printf("Commands:\n");
  printf("  WRITE local-file-path [remote-file-path]\n\n");
  printf("Options:\n");
  printf("  -h host    Server hostname or IP (default: %s)\n", DEFAULT_HOST);
  printf("  -p port    Server port (default: %d)\n\n", DEFAULT_PORT);
  printf("Examples:\n");
  printf("  %s WRITE data/localfoo.txt folder/foo.txt\n", prog);
  printf("  %s WRITE myfile.txt\n", prog);
  printf("  %s -h 192.168.1.100 -p 2000 WRITE test.txt remote/test.txt\n",
         prog);
}

/*
 * Execute WRITE command: send a local file to the server
 * Protocol:
 *   1. Client sends: "WRITE <remote_path>"
 *   2. Client receives: "READY"
 *   3. Client sends: "<file_size>"
 *   4. Client receives: "SIZE_OK"
 *   5. Client sends: <file_data>
 *   6. Client receives: "SUCCESS" or "ERROR"
 */
int do_write(int socket_desc, const char* local_path, const char* remote_path) {
  char buffer[BUFFER_SIZE];
  FILE* fp;
  long file_size;
  long bytes_sent = 0;
  int n;

  file_size = get_file_size(local_path);
  if (file_size < 0) {
    printf("Error: Cannot access local file '%s'\n", local_path);
    return -1;
  }

  fp = fopen(local_path, "rb");
  if (fp == NULL) {
    printf("Error: Cannot open local file '%s'\n", local_path);
    return -1;
  }

  printf("Sending file: %s (%ld bytes)\n", local_path, file_size);
  printf("Remote path: %s\n", remote_path);

  // Send WRITE command with remote path
  memset(buffer, 0, sizeof(buffer));
  snprintf(buffer, sizeof(buffer), "WRITE %s", remote_path);

  if (send(socket_desc, buffer, strlen(buffer), 0) < 0) {
    printf("Error: Unable to send command\n");
    fclose(fp);
    return -1;
  }

  memset(buffer, 0, sizeof(buffer));
  n = recv(socket_desc, buffer, sizeof(buffer), 0);
  if (n <= 0 || strstr(buffer, "READY") == NULL) {
    printf("Error: Server not ready - %s\n", buffer);
    fclose(fp);
    return -1;
  }

  memset(buffer, 0, sizeof(buffer));
  snprintf(buffer, sizeof(buffer), "%ld", file_size);

  if (send(socket_desc, buffer, strlen(buffer), 0) < 0) {
    printf("Error: Unable to send file size\n");
    fclose(fp);
    return -1;
  }

  memset(buffer, 0, sizeof(buffer));
  n = recv(socket_desc, buffer, sizeof(buffer), 0);
  if (n <= 0 || strstr(buffer, "SIZE_OK") == NULL) {
    printf("Error: Server rejected file size - %s\n", buffer);
    fclose(fp);
    return -1;
  }

  printf("Transferring...\n");
  while (bytes_sent < file_size) {
    size_t to_read = sizeof(buffer);
    if (file_size - bytes_sent < (long)to_read) {
      to_read = file_size - bytes_sent;
    }

    size_t bytes_read = fread(buffer, 1, to_read, fp);
    if (bytes_read <= 0) {
      printf("Error: Failed to read local file\n");
      fclose(fp);
      return -1;
    }

    int sent = send(socket_desc, buffer, bytes_read, 0);
    if (sent < 0) {
      printf("Error: Failed to send file data\n");
      fclose(fp);
      return -1;
    }

    bytes_sent += sent;

    int progress = (int)((bytes_sent * 100) / file_size);
    printf("\rProgress: %ld/%ld bytes (%d%%)", bytes_sent, file_size, progress);
    fflush(stdout);
  }
  printf("\n");

  fclose(fp);

  memset(buffer, 0, sizeof(buffer));
  n = recv(socket_desc, buffer, sizeof(buffer), 0);
  if (n > 0) {
    printf("Server response: %s\n", buffer);
    if (strstr(buffer, "SUCCESS") != NULL) {
      return 0;
    }
  }

  return -1;
}

int main(int argc, char* argv[]) {
  int socket_desc;
  struct sockaddr_in server_addr;
  char* host = DEFAULT_HOST;
  int port = DEFAULT_PORT;
  int opt;

  while ((opt = getopt(argc, argv, "h:p:")) != -1) {
    switch (opt) {
      case 'h':
        host = optarg;
        break;
      case 'p':
        port = atoi(optarg);
        break;
      default:
        print_usage(argv[0]);
        return 1;
    }
  }

  if (optind >= argc) {
    print_usage(argv[0]);
    return 1;
  }

  char* command = argv[optind];

  if (strcmp(command, "WRITE") == 0) {
    if (optind + 1 >= argc) {
      printf("Error: WRITE requires local-file-path\n\n");
      print_usage(argv[0]);
      return 1;
    }

    char* local_path = argv[optind + 1];
    char* remote_path;

    if (optind + 2 < argc) {
      remote_path = argv[optind + 2];
    } else {
      remote_path = local_path;
    }

    socket_desc = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_desc < 0) {
      printf("Unable to create socket\n");
      return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
      struct hostent* he = gethostbyname(host);
      if (he == NULL) {
        printf("Invalid address or hostname: %s\n", host);
        close(socket_desc);
        return -1;
      }
      memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    printf("Connecting to %s:%d...\n", host, port);

    if (connect(socket_desc, (struct sockaddr*)&server_addr,
                sizeof(server_addr)) < 0) {
      printf("Unable to connect to server\n");
      close(socket_desc);
      return -1;
    }

    printf("Connected to server successfully\n\n");

    int result = do_write(socket_desc, local_path, remote_path);

    close(socket_desc);

    return (result == 0) ? 0 : 1;

  } else {
    printf("Error: Unknown command '%s'\n\n", command);
    print_usage(argv[0]);
    return 1;
  }
}
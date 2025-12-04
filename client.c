/*
 * client.c -- Remote File System Client
 *
 * adapted from:
 *   https://www.educative.io/answers/how-to-implement-tcp-sockets-in-c
 *
 * Commands:
 *   WRITE - Upload file to server
 *   GET   - Download file from server
 *   RM    - Delete file/directory on server
 *   STOP  - Shutdown the server
 */

#include <arpa/inet.h>
#include <errno.h>
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

/**
 * Get the size of a file
 * @param filename - Path to the file
 * @return File size in bytes, or -1 on error
 */
long get_file_size(const char* filename) {
  struct stat st;
  if (stat(filename, &st) == 0) {
    return st.st_size;
  }
  return -1;
}

/**
 * Create directories recursively (like mkdir -p)
 * @param path - Directory path to create
 * @return 0 on success, -1 on failure
 */
int create_directories(const char* path) {
  char tmp[MAX_PATH];
  char* p = NULL;
  size_t len;

  snprintf(tmp, sizeof(tmp), "%s", path);
  len = strlen(tmp);

  if (tmp[len - 1] == '/') {
    tmp[len - 1] = '\0';
  }

  for (p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
      }
      *p = '/';
    }
  }

  if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
    return -1;
  }

  return 0;
}

/**
 * Extract directory path from a full file path
 * @param filepath - Full file path
 * @param dirpath - Output buffer for directory path
 */
void get_directory_path(const char* filepath, char* dirpath) {
  strncpy(dirpath, filepath, MAX_PATH - 1);
  dirpath[MAX_PATH - 1] = '\0';

  char* last_slash = strrchr(dirpath, '/');
  if (last_slash != NULL) {
    *last_slash = '\0';
  } else {
    dirpath[0] = '\0';
  }
}

/**
 * Print usage information
 * @param prog - Program name
 */
void print_usage(const char* prog) {
  printf("Usage: %s [-h host] [-p port] COMMAND arguments...\n\n", prog);
  printf("Commands:\n");
  printf("  WRITE local-file-path [remote-file-path]\n");
  printf("  GET   remote-file-path [local-file-path]\n");
  printf("  RM    remote-path\n");
  printf("  STOP  (shutdown server)\n\n");
  printf("Options:\n");
  printf("  -h host    Server hostname or IP (default: %s)\n", DEFAULT_HOST);
  printf("  -p port    Server port (default: %d)\n\n", DEFAULT_PORT);
  printf("Examples:\n");
  printf("  %s WRITE data/localfoo.txt folder/foo.txt\n", prog);
  printf("  %s GET folder/test.txt downloaded.txt\n", prog);
  printf("  %s RM folder/test.txt\n", prog);
  printf("  %s STOP\n", prog);
}

/**
 * Connect to the server
 * @param host - Server hostname or IP
 * @param port - Server port
 * @return Socket descriptor, or -1 on error
 */
int connect_to_server(const char* host, int port) {
  int socket_desc;
  struct sockaddr_in server_addr;

  // Create socket
  socket_desc = socket(AF_INET, SOCK_STREAM, 0);

  if (socket_desc < 0) {
    printf("Unable to create socket\n");
    return -1;
  }

  // Set port and IP
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);

  // Convert hostname/IP to address
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

  // Connect to server
  if (connect(socket_desc, (struct sockaddr*)&server_addr,
              sizeof(server_addr)) < 0) {
    printf("Unable to connect to server\n");
    close(socket_desc);
    return -1;
  }

  printf("Connected to server successfully\n\n");

  return socket_desc;
}

/**
 * Execute WRITE command - send a local file to the server
 * @param socket_desc - Socket descriptor
 * @param local_path - Path to local file
 * @param remote_path - Path on server
 * @return 0 on success, -1 on failure
 */
int do_write(int socket_desc, const char* local_path, const char* remote_path) {
  char buffer[BUFFER_SIZE];
  FILE* fp;
  long file_size;
  long bytes_sent = 0;
  int n;

  // Check if local file exists and get its size
  file_size = get_file_size(local_path);
  if (file_size < 0) {
    printf("Error: Cannot access local file '%s'\n", local_path);
    return -1;
  }

  // Open local file for reading
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

  // Wait for READY response
  memset(buffer, 0, sizeof(buffer));
  n = recv(socket_desc, buffer, sizeof(buffer), 0);
  if (n <= 0 || strstr(buffer, "READY") == NULL) {
    printf("Error: Server not ready - %s\n", buffer);
    fclose(fp);
    return -1;
  }

  // Send file size
  memset(buffer, 0, sizeof(buffer));
  snprintf(buffer, sizeof(buffer), "%ld", file_size);

  if (send(socket_desc, buffer, strlen(buffer), 0) < 0) {
    printf("Error: Unable to send file size\n");
    fclose(fp);
    return -1;
  }

  // Wait for SIZE_OK response
  memset(buffer, 0, sizeof(buffer));
  n = recv(socket_desc, buffer, sizeof(buffer), 0);
  if (n <= 0 || strstr(buffer, "SIZE_OK") == NULL) {
    printf("Error: Server rejected file size - %s\n", buffer);
    fclose(fp);
    return -1;
  }

  // Send file data
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

    // Show progress
    int progress = (int)((bytes_sent * 100) / file_size);
    printf("\rProgress: %ld/%ld bytes (%d%%)", bytes_sent, file_size, progress);
    fflush(stdout);
  }
  printf("\n");

  fclose(fp);

  // Receive final response
  memset(buffer, 0, sizeof(buffer));
  n = recv(socket_desc, buffer, sizeof(buffer), 0);
  if (n > 0) {
    printf("Server response: %s\n", buffer);
    if (strstr(buffer, "Success!") != NULL) {
      return 0;
    }
  }

  return -1;
}

// Execute GET command - retrieve a file from the server

int do_get(int socket_desc, const char* remote_path, const char* local_path) {
  char buffer[BUFFER_SIZE];
  char dir_path[MAX_PATH];
  FILE* fp;
  long file_size;
  long bytes_received = 0;
  int n;

  printf("Requesting file: %s\n", remote_path);
  printf("Local path: %s\n", local_path);

  // Send GET command with remote path
  memset(buffer, 0, sizeof(buffer));
  snprintf(buffer, sizeof(buffer), "GET %s", remote_path);

  if (send(socket_desc, buffer, strlen(buffer), 0) < 0) {
    printf("Error: Unable to send command\n");
    return -1;
  }

  // Receive SIZE response or error occured!
  memset(buffer, 0, sizeof(buffer));
  n = recv(socket_desc, buffer, sizeof(buffer), 0);
  if (n <= 0) {
    printf("Error: No response from server\n");
    return -1;
  }

  // Check for error
  if (strncmp(buffer, "error occured!", 5) == 0) {
    printf("Server error: %s\n", buffer);
    return -1;
  }

  // Parse file size from "SIZE <size>"
  if (strncmp(buffer, "SIZE", 4) != 0) {
    printf("Error: Unexpected response: %s\n", buffer);
    return -1;
  }

  file_size = atol(buffer + 5);
  printf("File size: %ld bytes\n", file_size);

  // Create local directory if needed
  get_directory_path(local_path, dir_path);
  if (strlen(dir_path) > 0) {
    if (create_directories(dir_path) != 0) {
      printf("Error: Cannot create local directory '%s'\n", dir_path);
      return -1;
    }
  }

  // Open local file for writing
  fp = fopen(local_path, "wb");
  if (fp == NULL) {
    printf("Error: Cannot create local file '%s'\n", local_path);
    return -1;
  }

  // Send READY signal
  strcpy(buffer, "READY");
  if (send(socket_desc, buffer, strlen(buffer), 0) < 0) {
    printf("Error: Unable to send READY\n");
    fclose(fp);
    return -1;
  }

  // Receive file data
  printf("Receiving...\n");
  while (bytes_received < file_size) {
    memset(buffer, 0, sizeof(buffer));
    n = recv(socket_desc, buffer, sizeof(buffer), 0);
    if (n <= 0) {
      printf("\nError: Connection lost during transfer\n");
      fclose(fp);
      return -1;
    }

    fwrite(buffer, 1, n, fp);
    bytes_received += n;

    // Show progress
    int progress = (int)((bytes_received * 100) / file_size);
    printf("\rProgress: %ld/%ld bytes (%d%%)", bytes_received, file_size,
           progress);
    fflush(stdout);
  }
  printf("\n");

  fclose(fp);

  printf("File saved successfully: %s\n", local_path);
  return 0;
}

/**
 * Execute RM command - delete a file or directory on the server
 * @param socket_desc - Socket descriptor
 * @param remote_path - Path to delete on server
 * @return 0 on success, -1 on failure
 */
int do_rm(int socket_desc, const char* remote_path) {
  char buffer[BUFFER_SIZE];
  int n;

  printf("Deleting: %s\n", remote_path);

  // Send RM command with remote path
  memset(buffer, 0, sizeof(buffer));
  snprintf(buffer, sizeof(buffer), "RM %s", remote_path);

  if (send(socket_desc, buffer, strlen(buffer), 0) < 0) {
    printf("Error: Unable to send command\n");
    return -1;
  }

  // Receive response
  memset(buffer, 0, sizeof(buffer));
  n = recv(socket_desc, buffer, sizeof(buffer), 0);
  if (n <= 0) {
    printf("Error: No response from server\n");
    return -1;
  }

  printf("Server response: %s\n", buffer);

  if (strstr(buffer, "Success!") != NULL) {
    return 0;
  }

  return -1;
}

/**
 * Execute STOP command - shutdown the server
 * @param socket_desc - Socket descriptor
 * @return 0 on success, -1 on failure
 */
int do_stop(int socket_desc) {
  char buffer[BUFFER_SIZE];
  int n;

  printf("Sending STOP command to server...\n");

  // Send STOP command
  strcpy(buffer, "STOP");
  if (send(socket_desc, buffer, strlen(buffer), 0) < 0) {
    printf("Error: Unable to send command\n");
    return -1;
  }

  // Receive response
  memset(buffer, 0, sizeof(buffer));
  n = recv(socket_desc, buffer, sizeof(buffer), 0);
  if (n > 0) {
    printf("Server response: %s\n", buffer);
  }

  return 0;
}

/**
 * Main function
 */
int main(int argc, char* argv[]) {
  int socket_desc;
  char* host = DEFAULT_HOST;
  int port = DEFAULT_PORT;
  int opt;

  // Parse command line options
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

  // Check for command
  if (optind >= argc) {
    print_usage(argv[0]);
    return 1;
  }

  char* command = argv[optind];

  // Handle WRITE command
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

    socket_desc = connect_to_server(host, port);
    if (socket_desc < 0) {
      return 1;
    }

    int result = do_write(socket_desc, local_path, remote_path);
    close(socket_desc);
    return (result == 0) ? 0 : 1;
  }
  // Handle GET command
  else if (strcmp(command, "GET") == 0) {
    if (optind + 1 >= argc) {
      printf("Error: GET requires remote-file-path\n\n");
      print_usage(argv[0]);
      return 1;
    }

    char* remote_path = argv[optind + 1];
    char* local_path;
    char default_local[MAX_PATH];

    if (optind + 2 < argc) {
      local_path = argv[optind + 2];
    } else {
      const char* filename = strrchr(remote_path, '/');
      if (filename != NULL) {
        filename++;
      } else {
        filename = remote_path;
      }
      strncpy(default_local, filename, MAX_PATH - 1);
      default_local[MAX_PATH - 1] = '\0';
      local_path = default_local;
    }

    socket_desc = connect_to_server(host, port);
    if (socket_desc < 0) {
      return 1;
    }

    int result = do_get(socket_desc, remote_path, local_path);
    close(socket_desc);
    return (result == 0) ? 0 : 1;
  }
  // Handle RM command
  else if (strcmp(command, "RM") == 0) {
    if (optind + 1 >= argc) {
      printf("Error: RM requires remote-path\n\n");
      print_usage(argv[0]);
      return 1;
    }

    char* remote_path = argv[optind + 1];

    socket_desc = connect_to_server(host, port);
    if (socket_desc < 0) {
      return 1;
    }

    int result = do_rm(socket_desc, remote_path);
    close(socket_desc);
    return (result == 0) ? 0 : 1;
  }
  // Handle STOP command
  else if (strcmp(command, "STOP") == 0) {
    socket_desc = connect_to_server(host, port);
    if (socket_desc < 0) {
      return 1;
    }

    int result = do_stop(socket_desc);
    close(socket_desc);
    return (result == 0) ? 0 : 1;
  }
  // Unknown command
  else {
    printf("Error: Unknown command '%s'\n\n", command);
    print_usage(argv[0]);
    return 1;
  }
}
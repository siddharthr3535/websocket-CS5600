/*
 * server.c -- TCP Socket Server (Multi-threaded File Server)
 *
 * adapted from:
 *   https://www.educative.io/answers/how-to-implement-tcp-sockets-in-c
 *
 * Extended for Question 1 & 2: WRITE and GET command support with concurrency
 * control
 */

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

pthread_mutex_t fs_mutex = PTHREAD_MUTEX_INITIALIZER;

// Structure to pass client information to handler thread
typedef struct {
  int client_sock;
  struct sockaddr_in client_addr;
} client_info_t;

// create directories recursively
int create_directories(const char* path) {
  char tmp[512];
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

// extract directory path from file path
void get_directory_path(const char* filepath, char* dirpath) {
  strncpy(dirpath, filepath, 512 - 1);
  dirpath[512 - 1] = '\0';

  char* last_slash = strrchr(dirpath, '/');
  if (last_slash != NULL) {
    *last_slash = '\0';
  } else {
    dirpath[0] = '\0';
  }
}

/*
 * Handle WRITE command from client
 * Protocol:
 *   1. Server receives: "WRITE <remote_path>"
 *   2. Server sends: "READY"
 *   3. Server receives: "<file_size>"
 *   4. Server sends: "SIZE_OK"
 *   5. Server receives: <file_data>
 *   6. Server sends: "SUCCESS" or "ERROR"
 */
int handle_write_command(int client_sock, const char* remote_path) {
  char buffer[8196];
  char full_path[512];
  char dir_path[512];
  FILE* fp;
  long file_size;
  long bytes_received = 0;
  int n;

  snprintf(full_path, sizeof(full_path), "%s/%s", "./server_root", remote_path);

  get_directory_path(full_path, dir_path);

  pthread_mutex_lock(&fs_mutex);
  if (strlen(dir_path) > 0) {
    if (create_directories(dir_path) != 0) {
      pthread_mutex_unlock(&fs_mutex);
      strcpy(buffer, "ERROR: Failed to create directory");
      send(client_sock, buffer, strlen(buffer), 0);
      return -1;
    }
  }
  pthread_mutex_unlock(&fs_mutex);

  strcpy(buffer, "READY");
  if (send(client_sock, buffer, strlen(buffer), 0) < 0) {
    return -1;
  }

  memset(buffer, 0, sizeof(buffer));
  n = recv(client_sock, buffer, sizeof(buffer), 0);
  if (n <= 0) {
    return -1;
  }
  file_size = atol(buffer);
  printf("  File size: %ld bytes\n", file_size);

  strcpy(buffer, "SIZE_OK");
  if (send(client_sock, buffer, strlen(buffer), 0) < 0) {
    return -1;
  }

  pthread_mutex_lock(&fs_mutex);

  fp = fopen(full_path, "wb");
  if (fp == NULL) {
    pthread_mutex_unlock(&fs_mutex);
    strcpy(buffer, "ERROR: Cannot create file");
    send(client_sock, buffer, strlen(buffer), 0);
    return -1;
  }

  while (bytes_received < file_size) {
    memset(buffer, 0, sizeof(buffer));
    n = recv(client_sock, buffer, sizeof(buffer), 0);
    if (n <= 0) {
      fclose(fp);
      pthread_mutex_unlock(&fs_mutex);
      return -1;
    }
    fwrite(buffer, 1, n, fp);
    bytes_received += n;
  }

  fclose(fp);
  pthread_mutex_unlock(&fs_mutex);

  printf("  File saved: %s\n", full_path);

  strcpy(buffer, "SUCCESS: File written successfully");
  send(client_sock, buffer, strlen(buffer), 0);

  return 0;
}

/*
 * Handle GET command from client
 * Protocol:
 *   1. Server receives: "GET <remote_path>"
 *   2. Server sends: "SIZE <file_size>" or "ERROR <message>"
 *   3. Client sends: "READY"
 *   4. Server sends: <file_data>
 */
int handle_get_command(int client_sock, const char* remote_path) {
  char buffer[8196];
  char full_path[512];
  FILE* fp;
  long file_size;
  long bytes_sent = 0;
  int n;
  struct stat st;

  snprintf(full_path, sizeof(full_path), "%s/%s", "./server_root", remote_path);

  pthread_mutex_lock(&fs_mutex);

  if (stat(full_path, &st) != 0) {
    pthread_mutex_unlock(&fs_mutex);
    snprintf(buffer, sizeof(buffer), "ERROR: File not found '%s'", remote_path);
    send(client_sock, buffer, strlen(buffer), 0);
    return -1;
  }

  if (S_ISDIR(st.st_mode)) {
    pthread_mutex_unlock(&fs_mutex);
    snprintf(buffer, sizeof(buffer), "ERROR: Path is a directory '%s'",
             remote_path);
    send(client_sock, buffer, strlen(buffer), 0);
    return -1;
  }

  file_size = st.st_size;

  fp = fopen(full_path, "rb");
  if (fp == NULL) {
    pthread_mutex_unlock(&fs_mutex);
    snprintf(buffer, sizeof(buffer), "ERROR: Cannot open file '%s'",
             remote_path);
    send(client_sock, buffer, strlen(buffer), 0);
    return -1;
  }

  pthread_mutex_unlock(&fs_mutex);

  printf("  File size: %ld bytes\n", file_size);

  snprintf(buffer, sizeof(buffer), "SIZE %ld", file_size);
  if (send(client_sock, buffer, strlen(buffer), 0) < 0) {
    fclose(fp);
    return -1;
  }

  memset(buffer, 0, sizeof(buffer));
  n = recv(client_sock, buffer, sizeof(buffer), 0);
  if (n <= 0 || strstr(buffer, "READY") == NULL) {
    fclose(fp);
    return -1;
  }

  pthread_mutex_lock(&fs_mutex);

  while (bytes_sent < file_size) {
    size_t to_read = sizeof(buffer);
    if (file_size - bytes_sent < (long)to_read) {
      to_read = file_size - bytes_sent;
    }

    size_t bytes_read = fread(buffer, 1, to_read, fp);
    if (bytes_read <= 0) {
      break;
    }

    int sent = send(client_sock, buffer, bytes_read, 0);
    if (sent < 0) {
      fclose(fp);
      pthread_mutex_unlock(&fs_mutex);
      return -1;
    }
    bytes_sent += sent;
  }

  fclose(fp);
  pthread_mutex_unlock(&fs_mutex);

  printf("  File sent: %s (%ld bytes)\n", full_path, bytes_sent);

  return 0;
}

// Client handler thread function
void* client_handler(void* arg) {
  client_info_t* info = (client_info_t*)arg;
  int client_sock = info->client_sock;
  char client_message[8196];
  char command[32];
  char remote_path[512];

  printf("Client connected at IP: %s and port: %i\n",
         inet_ntoa(info->client_addr.sin_addr),
         ntohs(info->client_addr.sin_port));

  memset(client_message, 0, sizeof(client_message));
  if (recv(client_sock, client_message, sizeof(client_message), 0) < 0) {
    printf("Couldn't receive command\n");
    close(client_sock);
    free(info);
    return NULL;
  }

  printf("Received: %s\n", client_message);

  memset(command, 0, sizeof(command));
  memset(remote_path, 0, sizeof(remote_path));

  if (sscanf(client_message, "%31s %511s", command, remote_path) < 1) {
    char* error_msg = "ERROR: Invalid command format";
    send(client_sock, error_msg, strlen(error_msg), 0);
    close(client_sock);
    free(info);
    return NULL;
  }

  if (strcmp(command, "WRITE") == 0) {
    if (strlen(remote_path) == 0) {
      char* error_msg = "ERROR: Missing remote path";
      send(client_sock, error_msg, strlen(error_msg), 0);
    } else {
      printf("Processing WRITE: %s\n", remote_path);
      handle_write_command(client_sock, remote_path);
    }
  } else if (strcmp(command, "GET") == 0) {
    if (strlen(remote_path) == 0) {
      char* error_msg = "ERROR: Missing remote path";
      send(client_sock, error_msg, strlen(error_msg), 0);
    } else {
      printf("Processing GET: %s\n", remote_path);
      handle_get_command(client_sock, remote_path);
    }
  } else {
    char error_msg[8196];
    snprintf(error_msg, sizeof(error_msg), "ERROR: Unknown command '%s'",
             command);
    send(client_sock, error_msg, strlen(error_msg), 0);
  }

  close(client_sock);
  printf("Client disconnected (IP: %s)\n",
         inet_ntoa(info->client_addr.sin_addr));
  free(info);

  return NULL;
}

int main(void) {
  int socket_desc;
  socklen_t client_size;
  struct sockaddr_in server_addr, client_addr;

  mkdir("./server_root", 0755);

  socket_desc = socket(AF_INET, SOCK_STREAM, 0);

  if (socket_desc < 0) {
    printf("Error while creating socket\n");
    return -1;
  }
  printf("Socket created successfully\n");

  int opt = 1;
  setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(2000);
  server_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(socket_desc, (struct sockaddr*)&server_addr, sizeof(server_addr)) <
      0) {
    printf("Couldn't bind to the port\n");
    close(socket_desc);
    return -1;
  }
  printf("Done with binding\n");

  if (listen(socket_desc, 10) < 0) {
    printf("Error while listening\n");
    close(socket_desc);
    return -1;
  }

  printf("File Server running on port %d\n", 2000);
  printf("Root directory: %s\n", "./server_root");
  printf("Waiting for connections...\n");

  while (1) {
    client_size = sizeof(client_addr);
    int client_sock =
        accept(socket_desc, (struct sockaddr*)&client_addr, &client_size);

    if (client_sock < 0) {
      printf("Can't accept connection\n");
      continue;
    }

    client_info_t* client_info = malloc(sizeof(client_info_t));
    if (client_info == NULL) {
      printf("Memory allocation failed\n");
      close(client_sock);
      continue;
    }

    client_info->client_sock = client_sock;
    client_info->client_addr = client_addr;

    pthread_t tid;
    if (pthread_create(&tid, NULL, client_handler, client_info) != 0) {
      printf("Failed to create thread\n");
      close(client_sock);
      free(client_info);
      continue;
    }

    pthread_detach(tid);
  }

  close(socket_desc);
  return 0;
}
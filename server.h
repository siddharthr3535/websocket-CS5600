#ifndef SERVER_H
#define SERVER_H

#include <netinet/in.h>
#include <pthread.h>

#define PORT 2000
#define BUFFER_SIZE 8196
#define MAX_PATH 512
#define ROOT_DIR "./server_root"
#define MAX_FILE_LOCKS 100

// Structure for per-file locking
typedef struct {
  char filepath[MAX_PATH];
  pthread_mutex_t mutex;
  int in_use;
} file_lock_t;

// Structure to pass client information to handler thread
typedef struct {
  int client_sock;
  struct sockaddr_in client_addr;
} client_info_t;

// Initialize file lock table

void init_file_locks(void);

/**
 * Get or create a mutex lock for a specific file
 * @param filepath - Full path to the file
 * @return Pointer to mutex, or NULL if lock table is full
 */
pthread_mutex_t* get_file_lock(const char* filepath);

/**
 * Release a file lock when file is deleted
 * @param filepath - Full path to the file
 */
void release_file_lock(const char* filepath);

// Create directories recursively

int create_directories(const char* path);

// Extract directory path from a full file path

void get_directory_path(const char* filepath, char* dirpath);

// Get the next available version number for a file

int get_next_version(const char* filepath);

// Save current file as a versioned backup before overwriting

int save_version(const char* filepath);

// Handle WRITE command from client

int handle_write_command(int client_sock, const char* remote_path);

// Handle GET command from client

int handle_get_command(int client_sock, const char* remote_path);

// Handle RM command from client

int handle_rm_command(int client_sock, const char* remote_path);

// Handle STOP command from client

void handle_stop_command(int client_sock);

// Thread function to handle a single client connection

void* client_handler(void* arg);

#endif
/**
 * Copyright (C) 2022 Carnegie Mellon University
 *
 * This file is part of the HTTP course project developed for
 * the Computer Networks course (15-441/641) taught at Carnegie
 * Mellon University.
 *
 * No part of the HTTP project may be copied and/or distributed
 * without the express permission of the 15-441/641 course staff.
 */
#include <parse_http.h>
#include <ports.h>
#include <test_error.h>
#include <pthread.h>

#define PARALLELISM 2
#define BUF_SIZE 8192
#define COMMON_FLAG 0
#define MIN(X, Y) (((X) > (Y)) ? (Y) : (X))

const char* resource_folder = "./www/";

vector_t *record_vector;

/* parallelism concurrency control */
int next_worker_id = 0;
vector_t *pending_work_vectors[PARALLELISM];
pthread_mutex_t record_vector_lock;
pthread_mutex_t next_worker_lock;
pthread_mutex_t work_vector_locks[PARALLELISM];
int sock_fds[PARALLELISM];
pthread_t thread_ids[PARALLELISM];

/**
 * @brief Fill in a request struct based on the resource requesting
 * @param resource_path file resource to request
 * @param server_fd server socker descriptor
 */
void send_request(const char *resource_path, int server_fd) {
    // method GET fixed
    Request *request = (Request *)malloc(sizeof(Request));
    memset(request->http_method, 0, sizeof(request->http_method));
    memcpy(request->http_method, GET, strlen(GET));

    // uri resource variable
    memset(request->http_uri, 0, sizeof(request->http_uri));
    memcpy(request->http_uri, resource_path, MIN(strlen(resource_path), sizeof(request->http_uri)));

    // hostname client IP address fixed
    // reference: https://www.geeksforgeeks.org/c-program-display-hostname-ip-address/
    memset(request->host, 0, sizeof(request->host));
    char host_buffer[256];
    gethostname(host_buffer, sizeof(host_buffer));
    struct hostent* host = gethostbyname(host_buffer);
    char *ip_buffer = inet_ntoa(*((struct in_addr *)host->h_addr_list[0]));
    memcpy(request->host, ip_buffer, MIN(sizeof(request->host), strlen(ip_buffer)));

    // serialize from the struct Request into char buffer
    char buf[BUF_SIZE];
    size_t request_size = 0;
    serialize_http_request(buf, &request_size, request);

    // send out the request
    robust_write(server_fd, buf, request_size);
}

/**
 * @brief store a buffer's content into a file
 * @param filename the file path
 * @param file_buf the file buffer
 */
void store_file(const char* filename, const char* file_buf, size_t file_size) {
    FILE * f_ptr = fopen(filename, "wb");
    if (f_ptr) {
        fwrite(file_buf, 1, file_size, f_ptr);
        fclose(f_ptr);
    } else {
        perror("Error in opening file");
    }
}

void process_dependency(char *dependency_csv_path) {
    FILE *fp;
    char *line = NULL;
    size_t len = 0;

    fp = fopen(dependency_csv_path, "r");
    while (getline(&line, &len, fp) != -1) {
        char *child_end = strstr(line, ",");
        if (*(child_end + 1) == '\n') {
            *child_end = '\0';
            add_record(record_vector, "dependency.csv", line);
            free(line);
            line = NULL;
            continue;
        }

        *child_end = '\0';
        char *parent_begin = child_end + 1;
        char *parent_end = strstr(parent_begin, "\n");
        *parent_end = '\0';
        /* add this dependency record into vector */
        add_record(record_vector, parent_begin, line);

        free(line);
        line = NULL;
    }
    free(line);
    fclose(fp);
}

/**
 * Check if all the works have been finished
 */
bool is_all_work_finished(void) {
    pthread_mutex_lock(&record_vector_lock);
    for (int i = 0; i < vec_size(record_vector); i++) {
        record_t *record = vec_get(record_vector, i);
        if (!record->finished) {
            pthread_mutex_unlock(&record_vector_lock);
            return false;
        }
    }
    pthread_mutex_unlock(&record_vector_lock);
    return true;
}

/**
 * Add more dependent work to pending work vector
 */
void add_more_work(char *finished_file, vector_t *work_vec, int thread_id) {
    pthread_mutex_lock(&work_vector_locks[thread_id]);
    for (int i = 0; i < vec_size(record_vector); i++) {
        record_t *record = vec_get(record_vector, i);
        if (strcmp(record->file, finished_file) == 0) {
            // finished as a child
            record->finished = true;
        }
        if (strcmp(record->parent_file, finished_file) == 0) {
            // trigger new task as a parent
            char * new_work_name = record->file;
            pending_work_t *new_work = make_pending_work(new_work_name);
            vec_push_back(work_vec, new_work);
        }
    }
    pthread_mutex_unlock(&work_vector_locks[thread_id]);
}

/**
 * Check if there is any new request to be made to server
 */
void pipeline_work_request(int tid, vector_t *work_vec, int server_fd) {
    pthread_mutex_lock(&work_vector_locks[tid]);
    for (int i = 0; i < vec_size(work_vec); i++) {
        pending_work_t *work = vec_get(work_vec, i);
        if (!work->requested) {
            send_request(work->file_name, server_fd);
            work->requested = true;
        }
    }
    pthread_mutex_unlock(&work_vector_locks[tid]);
}

/**
 * Threading worker
 */
void *thread(void* tid) {
    int thread_id = (int) tid;
    int server_fd = sock_fds[thread_id];
    char *response_buffer = NULL;
    int response_buffer_size = 0;
    char local_buf[BUF_SIZE];
    while (!is_all_work_finished()) {
        // 0. scan through pending work to see if there is any request to make
        pipeline_work_request(thread_id, pending_work_vectors[thread_id], server_fd);
        // 1. read
        memset(local_buf, 0, sizeof(local_buf));
        ssize_t ready = recv(server_fd, local_buf, BUF_SIZE, MSG_DONTWAIT);
        if (ready > 0) {
            if (response_buffer_size == 0) {
                response_buffer = (char *)malloc(ready * sizeof(char));
                memcpy(response_buffer, local_buf, ready);
                response_buffer_size += ready;
            } else {
                response_buffer = (char *)realloc(response_buffer, (ready + response_buffer_size) * sizeof(char));
                memcpy(response_buffer + response_buffer_size, local_buf, ready);
                response_buffer_size += ready;
            }
        }
        // 2. try to parse, another while loop
        // parse the response
        int content_size;
        int header_size;
        test_error_code_t result_code =
                parse_http_response(response_buffer, response_buffer_size, &content_size, &header_size);
        while (result_code == TEST_ERROR_NONE) {
            // content is not in the buffer yet, break
            if (response_buffer_size < content_size + header_size) {
                break;
            }
            // slide the response buffer
            char *new_buffer = (char *)malloc((response_buffer_size - header_size) * sizeof(char));
            memcpy(new_buffer, response_buffer + header_size, response_buffer_size - header_size);
            free(response_buffer);
            response_buffer = new_buffer;
            response_buffer_size -= header_size;
            // read the content into the content buf
            char * content_buf = malloc(sizeof(char) * content_size);
            memcpy(content_buf, response_buffer, content_size);
            // slide the response buffer
            if (content_size == response_buffer_size) {
                response_buffer_size = 0;
                free(response_buffer);
                response_buffer = NULL;
            } else {
                new_buffer = (char *)malloc((response_buffer_size - content_size) * sizeof(char));
                memcpy(new_buffer, response_buffer + content_size, response_buffer_size - content_size);
                free(response_buffer);
                response_buffer = new_buffer;
                response_buffer_size -= content_size;
            }
            // write the file onto disk;
            pthread_mutex_lock(&work_vector_locks[thread_id]);
            char * finished_filename = ((pending_work_t *)vec_get(pending_work_vectors[thread_id], 0))->file_name;
            pthread_mutex_unlock(&work_vector_locks[thread_id]);
            char store_buf[256] = "./www/";
            strcpy(store_buf + strlen("./www/"), finished_filename);
            store_file(store_buf, content_buf, content_size);
            free(content_buf);
            // add new task
            pthread_mutex_lock(&next_worker_lock);
            int next_worker = next_worker_id;
            next_worker_id = (next_worker_id + 1) % PARALLELISM;
            pthread_mutex_unlock(&next_worker_lock);
            add_more_work(finished_filename, pending_work_vectors[next_worker], next_worker); // trace dependency
            pthread_mutex_lock(&work_vector_locks[thread_id]);
            remove_pending_work(pending_work_vectors[thread_id]); // pop head of the pending work vector
            pthread_mutex_unlock(&work_vector_locks[thread_id]);
            // try to read the next file
            if (response_buffer_size == 0) {
                break;
            }
            result_code = parse_http_response(response_buffer, response_buffer_size, &content_size, &header_size);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
  /* Validate and parse args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <server-ip>\n", argv[0]);
    return EXIT_FAILURE;
  }

  /* initialize the vector for storing dependency record */
  record_vector = create_vector();
  for (int i = 0; i < PARALLELISM; i++) {
      pending_work_vectors[i] = create_vector();
  }

  /* clean and recreate the .www folder for resource stroage */
  recursive_delete_folder(resource_folder);
  mkdir(resource_folder, 0777);

  /* Set up a connection to the HTTP server */
  for (int i = 0; i < PARALLELISM; i++) {
      int http_sock;
      struct sockaddr_in http_server;
      if ((http_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
          return TEST_ERROR_HTTP_CONNECT_FAILED;
      }
      http_server.sin_family = AF_INET;
      http_server.sin_port = htons(HTTP_PORT);
      inet_pton(AF_INET, argv[1], &(http_server.sin_addr));

      fprintf(stderr, "Parsed IP address of the server: %X\n",
              htonl(http_server.sin_addr.s_addr));

      if (connect(http_sock, (struct sockaddr *) &http_server, sizeof(http_server)) <
          0) {
          return TEST_ERROR_HTTP_CONNECT_FAILED;
      }
      sock_fds[i] = http_sock;
  }

  int http_sock = sock_fds[0]; // pick the first one to do initial work

  /* CP1: Send out a HTTP request, waiting for the response */
  // make request here for dependency.csv
  send_request("dependency.csv", http_sock);

  // read the response from the server here
  bool header_obtained = false;
  char *response_buffer = NULL;
  int response_buffer_size = 0;
  int content_size;
  while (!header_obtained) {
      // read from the buffer
      char local_buf[BUF_SIZE];
      memset(local_buf, 0, sizeof(local_buf));
      ssize_t ready = recv(http_sock, local_buf, BUF_SIZE, COMMON_FLAG);
      if (ready > 0) {
          if (response_buffer_size == 0) {
              response_buffer = (char *)malloc(ready * sizeof(char));
              memcpy(response_buffer, local_buf, ready);
              response_buffer_size += ready;
          } else {
              response_buffer = (char *)realloc(response_buffer, (ready + response_buffer_size) * sizeof(char));
              memcpy(response_buffer + response_buffer_size, local_buf, ready);
              response_buffer_size += ready;
          }
      }
      // parse the response
      int header_size;
      test_error_code_t result_code =
              parse_http_response(response_buffer, response_buffer_size, &content_size, &header_size);
      if (result_code != TEST_ERROR_NONE) {
          continue;
      }
      header_obtained = true;
      // slide the response buffer
      if (header_size == response_buffer_size) {
          response_buffer_size = 0;
          free(response_buffer);
          response_buffer = NULL;
      } else {
          char *new_buffer = (char *)malloc((response_buffer_size - header_size) * sizeof(char));
          memcpy(new_buffer, response_buffer + header_size, response_buffer_size - header_size);
          free(response_buffer);
          response_buffer = new_buffer;
          response_buffer_size -= header_size;
      }
  }

    // poll in for enough size of the content
    char * content_buf = malloc(sizeof(char) * content_size + 1);
    if (response_buffer != NULL) {
        memcpy(content_buf, response_buffer, response_buffer_size);
        free(response_buffer);
    }
    robust_read(http_sock, content_buf + response_buffer_size, content_size - response_buffer_size);
    content_buf[content_size] = '\n';

    const char* dependency_csv_path = "./www/dependency.csv";
    store_file(dependency_csv_path, content_buf, content_size + 1);

    // make records based on dependency_csv_path;
    process_dependency(dependency_csv_path);

    pending_work_t *index_work = make_pending_work("index.html");
    vec_push_back(pending_work_vectors[1], index_work);

    /* spawn worker thread for parallelism */
    for (int i = 0; i < PARALLELISM; i++) {
        pthread_t pid;
        pthread_create(&pid, NULL, &thread, i);
        thread_ids[i] = pid;
    }

    /* harvest threads */
    for (int i = 0; i < PARALLELISM; i++) {
        pthread_join(thread_ids[i], NULL);
    }
    return 0;
}
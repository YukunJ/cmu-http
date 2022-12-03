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
#ifndef PARSE_HTTP_H
#define PARSE_HTTP_H

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "test_error.h"

#define SUCCESS 0
#define HTTP_SIZE 4096

/** the initial capacity of vector */
#define INITIAL_CAPACITY 4

/** the multiplicative factor when resizing the vector */
#define MUL_FACTOR 2

/** the default element type for vector is void *, so it could generically
 * handle any data type */
#define ELEMENT_TYPE void *

// ======================== vector ========================= //
/*
 * the dynamic expanding vector
 * NOT thread-safe be careful
 * use generic pointer "void *" as its element type, so it can hold any type
 * if you need to find an element in the vector,
 * it needs to be provided with a comparator function pointer
 */
typedef struct vector {
    ELEMENT_TYPE *data;
    int64_t size;
    int64_t capacity;
} vector_t;

/**
 * Create a new vector with the default initial capacity
 * @return pointer to dynamically allocated vector
 */
vector_t *create_vector();

/**
 * Release the dynamically-allocated memory for vector and its data
 * @param vec pointer to a vector
 */
void free_vector(vector_t *vec);

/**
 * Expand the vector by twice the size and copy over the data
 * local-only visible func
 * @param vec pointer to a vector
 */
static void vec_expand(vector_t *vec);

/**
 * Get the current size of the vector
 * @param vec pointer to a vector
 * @return size of the vector
 */
int64_t vec_size(vector_t *vec);

/**
 * Get the idx-th element in the vector
 * @param vec pointer to a vector
 * @param idx the index to be retrieved
 * @return the element (a pointer type), or NULL if the index is inappropriate
 */
ELEMENT_TYPE vec_get(vector_t *vec, int64_t idx);

/**
 * Insert an element at the back of the vector
 * if the size reaches capacity, do dynamic expanding on the fly
 * @attention once the element enters this vector, vector will control its
 * lifecycle including free its dynamically allocated memory in the end
 * @param vec pointer to a vector
 * @param e the element to be inserted
 */
void vec_push_back(vector_t *vec, ELEMENT_TYPE e);

/**
 * Reverse the order of elements in the vector
 * @param vec pointer to a vector
 */
void vec_reverse(vector_t *vec);

/**
 * Find the best element in the vector
 * according to the specific comparator function
 * this comparator should return True when the left hand side is better than rhs
 * @param vec pointer to a vector
 * @param comp a function pointer that return True if left hand side is better
 * than rhs
 * @return the index of best element in this vector, or -1 indicates problem
 */
int64_t vec_find_best(vector_t *vec, bool (*comp)(ELEMENT_TYPE, ELEMENT_TYPE));

/**
 * To Locate a specific element in the vector
 * require the input of a comparator
 * can be used together with the vec_remove_by_index function below
 * @param vec pointer to a vector
 * @param element_to_find the element to be find
 * @param comp a function pointer act as the equal comparator, return True if
 * two input is considered equal
 * @return index of that element if found, or -1
 */
int64_t vec_find(vector_t *vec, ELEMENT_TYPE element_to_find,
                 bool (*comp)(ELEMENT_TYPE, ELEMENT_TYPE));

/**
 * Remove the element specified at the index
 * @param vec pointer to a vector
 * @param idx the index of the element to be removed
 * @return true if such deletion is successfully
 */
bool vec_remove_by_index(vector_t *vec, int64_t idx);

/**
 * Pop out the first element of vector
 * @param vec pointer to a vector
 */
void vec_pop_front(vector_t *vec);

/**
 * Replace the vector's element at a specific index by a new one
 * @param vec pointer to a vector
 * @param idx the index of the element to be replaced
 * @param new_element pointer to a new element
 * @return true if such replacement is successfully
 */
bool vec_replace_by_index(vector_t *vec, int64_t idx,
                          ELEMENT_TYPE new_element);

/**
 * Clear out the data in vec and reset to default state
 * @param vec pointer to a vector
 */
void vec_clear(vector_t *vec);

/**
 * Helper function to print a vector with the provided printer function for each
 * element
 * @param vec pointer to a vector
 * @param element_printer function printer that prints out an individual element
 */
void vec_print(vector_t *vec, void(element_printer)(ELEMENT_TYPE));

/* HTTP Methods */
extern const char *HEAD, *GET, *POST;

/* Request Headers */
extern char *CONTENT_LENGTH_STR, *CONNECTION_STR, *CLOSE;

/* Response Headers */
extern char *CRLF, *CONNECTION, *CONNECTION_VAL, *CONNECTION_CLOSE, *SERVER,
    *SERVER_VAL, *DATE, *CONTENT_TYPE, *CONTENT_LENGTH, *ZERO, *LAST_MODIFIED,
    *HOST;

/* Responses */
extern char *HTTP_VER, *OK, *NOT_FOUND, *SERVICE_UNAVAILABLE, *BAD_REQUEST,
    *BAD_REQUEST_SHORT;
/* MIME TYPES */
extern char *HTML_EXT, *HTML_MIME, *CSS_EXT, *CSS_MIME, *PNG_EXT, *PNG_MIME,
    *JPG_EXT, *JPG_MIME, *GIF_EXT, *GIF_MIME, *OCTET_MIME;

// Header field
typedef struct {
  char header_name[4096];
  char header_value[4096];
} Request_header;

// HTTP Request Header
typedef struct {
  char http_version[50];  //!< HTTP version, should be 1.1 in this project
  char http_method[50];   //!< HTTP method, could be GET, HEAD, or POSt in this
                          //!< project
  char http_uri[4096];    //!< HTTP URI, could be /index.html, /index.css, etc.
  char host[40];          //!< Host name, should be the IP address,
  Request_header
      *headers;  //!< HTTP headers, could be Content-Length, Connection, etc.
  int header_count;           //!< Number of headers
  size_t allocated_headers;   //!< Number of headers allocated
  size_t status_header_size;  //!< Size of the status line and headers
  char *body;                 //!< HTTP body, could be the content of the file
  bool valid;                 //!< Whether the request is valid
} Request;

// functions decalred in parser.y
int yyparse();
void set_parsing_options(char *buf, size_t i, Request *request);
void yyrestart();

void trim_whitespace(char *str, size_t str_len);
void to_lower(char *str, size_t str_len);
int populate_header(char *msg, char *field, const size_t field_len, char *val,
                    const size_t val_len);

/**
 * @brief      Serialize a HTTP request from the Request struct to a buffer
 *
 * @param      buffer  The buffer (output)
 * @param      size    The size of the buffer (output)
 * @param      request The request (input)
 * @return     the error code
 */
test_error_code_t serialize_http_request(char *buffer, size_t *size,
                                         Request *request);

/**
 * @brief      Parse a HTTP request from a buffer to a Request struct
 *
 * @param      buffer  The buffer (input)
 * @param      size    The size of the buffer (input)
 * @param      request The request (output)
 * @return     the error code
 */
test_error_code_t parse_http_request(char *buffer, size_t size,
                                     Request *request, int *read_amount);


/**
 * @brief      Parse a HTTP response from a buffer
 *
 * @param      buffer  The buffer (input)
 * @param      size    The size of the buffer (input)
 * @param      content_size the size of the response content (output)
 * @param      header_size the size of the whole header to be slided (output)
 * @return     the error code
 */
test_error_code_t parse_http_response(char *buffer, size_t size, int *content_size, int *header_size);


/**
 * @brief      Serialize a HTTP response from the Request struct to a buffer
 *
 * @param      msg                  The message (output)
 * @param      len                  The length of the message (output)
 * @param      prepopulated_headers The prepopulated headers (input)
 * @param      content_type         The content type (input)
 * @param      content_length       The content length (input)
 * @param      last_modified        The last modified time (input)
 * @param      body_len             The HTTP body length (input)
 * @param      body                 The HTTP body (input)
 */
test_error_code_t serialize_http_response(char **msg, size_t *len,
                                          const char *prepopulated_headers,
                                          char *content_type,
                                          char *content_length,
                                          char *last_modified, size_t body_len,
                                          char *body, bool should_close);

#define INIT_POLL_ARRAY_CAPACITY 8

/**
 * @brief Get the real struct address pointer position.
 * It discriminates between IPv4 or IPv6 by inspecting the sa_family field.
 * @param sa pointer to the general struct sockaddr
 * @return void* pointer to struct sockaddr_in (IPv4) or sockaddr_in6 (IPv6)
 */
void *get_addr_in(const struct sockaddr *sa);

/**
 * @brief Build a client side socket.
 * Caller should close the socket descriptor after usage.
 * @param hostname the server's IP Address or Hostname to connect to
 * @param port the server's listening port to connect to
 * @param verbose if set True, intermediate logging will be made to stdout
 * @return the client socket descriptor, or -1 if any error happens
 */
int build_client(const char *host, const char *port, bool verbose);

/**
 * @brief Build a server slide socket.
 * It assume the server will listen on its own local machine's IP Address
 * Caller should close the socket descriptor after usage.
 * @param port the port server will be listening to
 * @param backlog how many pending connections to be accept()-ed the server
 * queue will hold
 * @param verbose if set True, intermediate logging will be made to stdout
 * @return int the listening socket descriptor, -1 if any error happens
 */
int build_server(const char *port, const int backlog, bool verbose);

/**
 * @brief Ensure to read in as many as len bytes of data into user provided
 * buffer. It will sit-wait until read in all the required many bytes.
 * @param fd the file descriptor to read from, typically socket
 * @param buf the buffer to place the data read from fd
 * @param len how many bytes to read from the fd
 * @attention user must ensure the buffer has at least len many space available
 * @return ssize_t how many bytes read, or -1 if any error happens
 */
ssize_t robust_read(int fd, void *buf, const size_t len);

/**
 * @brief Ensure to write out as many as len bytes of data from the user
 * provided buffer It will sit-wait until write out all the required many bytes
 * @param fd the file descriptor, typically socket
 * @param buf the buffer which contains data to be written into fd
 * @param len how many bytes to write into the fd
 * @attention user must ensure the buffer has at least len many space available
 * @return ssize_t how many bytes written, or -1 if any error happens
 */
ssize_t robust_write(int fd, const void *buf, const size_t len);

/**
   @brief poll() functionality support.
   notice it always use malloc() and free() regardless of compiling on C or C++.
   therefore, user must adhere to call init_poll_array() and
   release_poll_array(), instead of attempting to release the allocated space
   themselves
*/
typedef struct poll_array {
  struct pollfd *pfds;  // points to the array of struct pollfd for poll
  char **buffers;   // temporary storage for data polled out from a socket fd
  int *sizes;       // keep track of sizes of buffers
  nfds_t count;     // how many are there in the array pfds
  nfds_t capacity;  // the underlying allocated space for pfds
} poll_array_t;

/**
 * @brief Initialize an poll array with default capacity.
 * @return poll_array_t* pointer to the new allocated poll array struct
 */
poll_array_t *init_poll_array();

/**
 * @brief Add a new socket descriptor under poll monitoring.
 * User should ensure that no duplicate fd is added into the array.
 * This functionality doesn't check for duplicate inserts.
 * @param new_fd new socket descriptor to be added
 * @param array pointer to the allocated poll array struct
 * @param flag the bit flag for the descriptor to be monitored upon
 */
void add_to_poll_array(int new_fd, poll_array_t *array, short flag);

/**
 * @brief Remove an indexed socket descriptor from the poll array
 * User should ensure the index lies in between [0, array->count)
 * If the index out of bound, the program will exit with code 1
 * @param remove_idx the to-be-removed index from the poll array
 * @param array pointer to the allocated poll array struct
 */
void remove_from_poll_array(int remove_idx, poll_array_t *array);

/**
 * @brief Release the dynamical allocated space for poll array
 * @param array pointer to the allocated poll array
 */
void release_poll_array(poll_array_t *array);


/**
 * @brief recursively delete a folder and all contents inside it
 * @param dirname the path to the folder to be deleted
 * @reference: https://stackoverflow.com/questions/3833581/recursive-file-delete-in-c-on-linux
 * @return error code if any
 */
int recursive_delete_folder(const char* dirname);

/**
 * Record for a dependency task
 */
typedef struct {
    char *parent_file;
    char *file;
    bool finished;
} record_t;

typedef struct {
    char *file_name;
    bool requested;
} pending_work_t;

/**
 * Create a pending work on Heap
 */
pending_work_t *make_pending_work(char* work);

/**
 * Create a record on Heap
 */
record_t *make_record(char* parent, char* child);

/**
 * Generate a new dependency record and add into dynamic vector
 */
void add_record(vector_t *vec, char* parent, char* child);

/**
 * Remove the head of a vector
 */
void remove_record(vector_t *vec);

/**
 * Remove the head of a pending work vector
 */
void remove_pending_work(vector_t *vec);
#endif
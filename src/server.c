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
#include <assert.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include "parse_http.h"
#include "ports.h"

#define BUF_SIZE 8192
#define CONNECTION_TIMEOUT 50
#define COMMON_FLAG 0
#define LOGGING_BUF_SIZE 1024
#define FILE_BUF_SIZE (1024 * 1024)

/**
 * @brief helper function to check if
 * @param filename
 * @return
 */
bool check_file_existence(const char* filename){
    struct stat buffer;
    int exist = stat(filename, &buffer);
    if (exist == 0)
        return true;
    else
        return false;
}

/**
 * @brief load the file size and overwrite the size to tell outside world
 * @param filename the name of file to read (assume this file exists)
 * @param size the pointer to size_t indicating how many bytes are loaded from the file
 */
void load_filesize(const char *filename, size_t *size) {
    FILE *f = fopen(filename, "rb");
    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fclose(f);
    *size = fsize;
}

/**
 * @brief extract out the request uri's extension type
 * @param filename the filename to be examined
 * @param buf the buffer to be allocated and populated with extension
 * @param size pointer to how length of the extension
 */
void verify_extension(const char *filename, char **buf, size_t *size) {
    // TODO: free this buf
    size_t filename_len = strlen(filename);
    char *last_dot = strrchr(filename, '.');
    if (last_dot == NULL) {
        *size = strlen(OCTET_MIME);
        *buf = (char *)malloc(*size);
        memcpy(*buf, OCTET_MIME, *size);
    }
    size_t last_dot_idx = filename - last_dot;
    size_t extension_len = filename_len - last_dot_idx - 1;
    char extension[BUF_SIZE];
    memset(extension, 0, sizeof(extension));
    memcpy(extension, filename + last_dot_idx + 1, extension_len);
    if (strcmp(extension, HTML_EXT) == 0) {
        *size = strlen(HTML_MIME);
        *buf = (char *)malloc(*size);
        memcpy(*buf, HTML_MIME, *size);
    }
    if (strcmp(extension, CSS_EXT) == 0) {
        *size = strlen(CSS_MIME);
        *buf = (char *)malloc(*size);
        memcpy(*buf, CSS_MIME, *size);
    }
    if (strcmp(extension, PNG_EXT) == 0) {
        *size = strlen(PNG_MIME);
        *buf = (char *)malloc(*size);
        memcpy(*buf, PNG_MIME, *size);
    }
    if (strcmp(extension, JPG_EXT) == 0) {
        *size = strlen(JPG_MIME);
        *buf = (char *)malloc(*size);
        memcpy(*buf, JPG_MIME, *size);
    }
    if (strcmp(extension, GIF_EXT) == 0) {
        *size = strlen(GIF_MIME);
        *buf = (char *)malloc(*size);
        memcpy(*buf, GIF_MIME, *size);
    }
    // default unknown extension
    *size = strlen(OCTET_MIME);
    *buf = (char *)malloc(*size);
    memcpy(*buf, OCTET_MIME, *size);
}

/**
 * @brief Server serves a client's request
 * @param client_fd the client's socket descriptor
 * @param request pointer to a successfully parsed
 * @param server_dir the server's directory
 * @param read_buf the buffer containing request
 * @param read_amount how many bytes are there in the request
 */
void serve_request(int client_fd, Request *request, const char *server_dir, const char *read_buf, int read_amount,
                   bool should_close, char * file_buf) {
    // #TODO: add error checking before serving
    if (strcmp(request->http_method, GET) == 0) {
        // A GET request
        printf("Deal with a GET method\n");
        fflush(stdout);
        char *item_seek = request->http_uri;
        /* Logging */
        char whole_path[BUF_SIZE];
        memset(whole_path, 0, sizeof(whole_path));
        memcpy(whole_path, server_dir, strlen(server_dir));
        strncat(whole_path, item_seek, strlen(item_seek));
        size_t whole_path_len = strlen(whole_path);
        if (whole_path[whole_path_len-1] == '/') {
            // default route dir '/'to '/index.html'
            strncat(whole_path, "index.html", strlen("index.html"));
        }

        if (check_file_existence(whole_path)) {
            // the requested file do exist
            // load the file into memory
            size_t file_size;
            load_filesize(whole_path, &file_size);
            // check the extension type of the file
            char *extension;
            size_t extension_size;
            verify_extension(whole_path, &extension, &extension_size);

            char content_length[20] = "";
            snprintf(content_length, sizeof(content_length), "%zu", file_size);

            struct stat attr;
            stat(whole_path, &attr);
            char last_modified[100];
            struct tm gmt_time = *gmtime(&attr.st_mtime);
            strftime(last_modified, sizeof(last_modified), "%a, %d %b %Y %H:%M:%S %Z", &gmt_time);

            char *response;
            size_t response_len;

            serialize_http_response(&response, &response_len, OK, extension, content_length,
                                    last_modified, 0, NULL, should_close);
            // send the response header to the other end
            robust_write(client_fd, response, response_len);
            free(extension);
            free(response);
            // send the actual content of the file
            FILE *f = fopen(whole_path, "rb");
            size_t curr_read = 0;
            while (curr_read < file_size) {
                // memset(file_buf, 0, FILE_BUF_SIZE);
                size_t num_read = file_size - curr_read;
                if (num_read > FILE_BUF_SIZE) {
                    num_read = FILE_BUF_SIZE;
                }
                fread(file_buf, sizeof(char), num_read, f);
                curr_read += num_read;
                robust_write(client_fd, file_buf, num_read);
            }
            fclose(f);
        } else {
            // file not exist
        }

    } else if (strcmp(request->http_method, HEAD) == 0) {
        // A HEAD request
    } else if (strcmp(request->http_method, POST) == 0) {
        /**
        A POST request, echo back the whole request directly
        */
        robust_write(client_fd, read_buf, read_amount);
    }

}

int main(int argc, char *argv[]) {
    /* Validate and parse args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <www-folder>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *www_folder = argv[1];

    DIR *www_dir = opendir(www_folder);
    if (www_dir == NULL) {
        fprintf(stderr, "Unable to open www folder %s.\n", www_folder);
        return EXIT_FAILURE;
    }
    closedir(www_dir);

    printf("Successfully checked that the folder %s can be opened\n", www_folder);
    /* CP1: Set up sockets and read the buf */

    /* build the listening server descriptor */
    int max_pending_queue = 200; /* how many pending connection allowed to be placed in the socket queue */
    int listen_fd = build_server(HTTP_PORT_CHAR, max_pending_queue, true);
    if (listen_fd == -1) {
        fprintf(stderr, "Unable to build listening fd\n");
        return EXIT_FAILURE;
    }

    poll_array_t *poll_array = init_poll_array();
    add_to_poll_array(listen_fd, poll_array, POLLIN); // add the listening fd to be polled

    /* Logging */
    // int logging_fd = 0;
    // build_client("54.167.5.75", "3490", true);
    /* the main loop of HTTP server */
    int poll_wait = 3000; // in ms
    printf("About to begin main while loop\n");
    char * file_buf = (char *) malloc(sizeof(char) * FILE_BUF_SIZE);
    while (true) {
        int ready_count = poll(poll_array->pfds, poll_array->count, poll_wait);
        if (ready_count > 0) {
            // some socket fds are ready to be read
            // process backward, last deal with listen fd if available
            for (int i = poll_array->count - 1; i >= 0; i--) {
                if (poll_array->pfds[i].revents & POLLIN) {
                    // this socket fd is ready to be read
                    int ready_fd = poll_array->pfds[i].fd;
                    if (ready_fd == listen_fd) {
                        // accept new incoming connection
                        struct sockaddr_storage their_addr;
                        socklen_t sin_size = sizeof(their_addr);
                        int new_client_fd = accept(ready_fd, (struct sockaddr *)&their_addr, &sin_size);
                        printf("Get a new client connection fd=%d\n", new_client_fd);
                        add_to_poll_array(new_client_fd, poll_array, POLLIN);
                    } else {
                        // a client send new request to us
                        // read everything from the socket once, 8192 at most
                        printf("Get some messages from client connection fd=%d\n", poll_array->pfds[i].fd);
                        char local_buf[BUF_SIZE];
                        memset(local_buf, 0, sizeof(local_buf));
                        ssize_t ready = recv(ready_fd, local_buf, BUF_SIZE, COMMON_FLAG);
                        if (ready < 0) {
                            // something wrong happens
                            fprintf(stderr, "recv() return < 0 from client_fd\n");
                            return EXIT_FAILURE;
                        } else if (ready == 0) {
                            // client exit
                            printf("The client fd=%d has exited\n", poll_array->pfds[i].fd);
                            remove_from_poll_array(i, poll_array); // caution: pass in index
                            continue;
                        } else {
                            // glue together the buffers
                            if (poll_array->sizes[i] == 0) {
                                poll_array->buffers[i] = (char *)malloc(ready * sizeof(char));
                                memcpy(poll_array->buffers[i], local_buf, ready);
                                poll_array->sizes[i] += ready;
                            } else {
                                poll_array->buffers[i] = (char *)realloc(poll_array->buffers[i],
                                                                         (ready + poll_array->sizes[i]) * sizeof(char));
                                memcpy(poll_array->buffers[i] + poll_array->sizes[i], local_buf, ready);
                                poll_array->sizes[i] += ready;
                            }
                        }
                        // try to parse data to see if we have valid requests, and respond accordingly
                        // use while loop to handle multiple requests
                        printf("Try to parse the request\n");
                        Request request;
                        int read_amount;
                        test_error_code_t result_code = parse_http_request(poll_array->buffers[i], poll_array->sizes[i],
                                                                           &request, &read_amount);
                        while (poll_array->sizes[i] > 0 &&
                               (result_code == TEST_ERROR_NONE || result_code == TEST_ERROR_PARSE_FAILED)) {
                            // handle normal request here
                            if (result_code == TEST_ERROR_NONE) {
                                // first handle the body field:
                                bool should_close = false;
                                for (int request_counter = 0; request_counter < request.header_count; request_counter++) {
                                    if (strcasecmp(request.headers[request_counter].header_name, "Content-Length") == 0) {
                                        size_t content_len;
                                        sscanf(request.headers[request_counter].header_value, "%zu", &content_len);
                                        if (content_len + read_amount > poll_array->sizes[i]) {
                                            result_code = TEST_ERROR_PARSE_PARTIAL;
                                            break;
                                        }
                                        // the request is full
                                        // read from the buffer to update body
                                        request.body = (char *) malloc(sizeof(char) * content_len);
                                        memcpy(request.body, poll_array->buffers[i] + read_amount, content_len);
                                        // printf("request body: %s\n", request.body);
                                        read_amount += content_len;
                                    }
                                    if (strcasecmp(request.headers[request_counter].header_name, "Connection") == 0) {
                                        if (strcasecmp(request.headers[request_counter].header_value, CONNECTION_CLOSE) == 0) {
                                            should_close = true;
                                        }
                                    }
                                }
                                if (result_code == TEST_ERROR_PARSE_PARTIAL) {
                                    break;
                                }
                                printf("Parsed a full request, about to serve_request()\n");
                                serve_request(ready_fd, &request, www_folder, poll_array->buffers[i], read_amount, should_close, file_buf);
                                if (request.body != NULL) {
                                    free(request.body);
                                }
                                // if the request has 'Connection: close' in header
                                // should close the connection after service immediately
                                if (should_close) {
                                    // case in-sensitive search
                                    remove_from_poll_array(i, poll_array);
                                    break;
                                }
                            // handle malformed result
                            } else {
                                // TODO: handle malformed request
                            }
                            if (read_amount == poll_array->sizes[i]) {
                                printf("read everything from the buffer\n");
                                poll_array->sizes[i] = 0;
                                free(poll_array->buffers[i]);
                                poll_array->buffers[i] = NULL;
                            } else {
                                char *new_buffer = (char *)malloc((poll_array->sizes[i] - read_amount) * sizeof(char));
                                memcpy(new_buffer, poll_array->buffers[i] + read_amount,
                                       poll_array->sizes[i] - read_amount);
                                free(poll_array->buffers[i]);
                                poll_array->buffers[i] = new_buffer;
                                poll_array->sizes[i] -= read_amount;
                            }
                            if (poll_array->sizes[i] > 0) {
                                result_code = parse_http_request(poll_array->buffers[i], poll_array->sizes[i],
                                                                 &request, &read_amount);
                            }
                        }

                    }
                    if (--ready_count == 0) {
                        // all poll-ready fds are processed
                        break;
                    }
                }
            }
        }
    }
    free(file_buf);
    closedir(www_dir);
    return EXIT_SUCCESS;
}

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
#include "parse_http.h"

void trim_whitespace(char *str, size_t str_len) {
    size_t i = 0;

    while (isspace(str[i]) && str[i] != '\0') {
        i++;
    }
    // First non whitespace char
    size_t firstCharInd = i;

    i = str_len - 1;
    while (isspace(str[i])) {
        i--;
    }
    // First whitespace char
    size_t lastCharInd = i;

    if (firstCharInd == 0) {
        return;
    }

    size_t len = lastCharInd - firstCharInd + 1;
    for (int startInd = 0, curCharInd = firstCharInd; startInd < len; startInd++, curCharInd++) {
        str[startInd] = str[curCharInd];
    }
    str[len] = '\0';
}

void to_lower(char *str, size_t str_len) {
    for (int i = 0; i < str_len; i++) {
        str[i] = tolower(str[i]);
    }
}

/**
* Given a char buffer returns the parsed request headers
*/
test_error_code_t parse_http_request(char *buffer, size_t size, Request *request, int *read_amount) {
  //Differant states in the state machine
	enum {
		STATE_START = 0, STATE_CR, STATE_CRLF, STATE_CRLFCR, STATE_CRLFCRLF
	};

	int i = 0, state;
	size_t offset = 0;
	char ch;
	char buf[8192];
	memset(buf, 0, 8192);

	state = STATE_START;
	while (state != STATE_CRLFCRLF) {
		char expected = 0;

		if (i == size)
			break;

		ch = buffer[i++];
		buf[offset++] = ch;

		switch (state) {
		case STATE_START:
		case STATE_CRLF:
			expected = '\r';
			break;
		case STATE_CR:
		case STATE_CRLFCR:
			expected = '\n';
			break;
		default:
			state = STATE_START;
			continue;
		}

		if (ch == expected)
			state++;
		else
			state = STATE_START;

	}
    *read_amount = i;

    //Valid End State
	if (state == STATE_CRLFCRLF) {
        request->header_count = 0;
        request->status_header_size = 0;
        request->allocated_headers = 15;
        request->headers = (Request_header *) malloc(sizeof(Request_header) * request->allocated_headers);
        set_parsing_options(buf, i, request);

        yyrestart(NULL);
		if (yyparse() == SUCCESS) {
            request->valid = true;
            Request_header *header = &request->headers[request->header_count];
            trim_whitespace(header->header_name, strlen(header->header_name));
            to_lower(header->header_name, strlen(header->header_name));
            trim_whitespace(header->header_value, strlen(header->header_value));
            to_lower(header->header_value, strlen(header->header_value));
            return TEST_ERROR_NONE;
		}
        return TEST_ERROR_PARSE_FAILED;
	}
	return TEST_ERROR_PARSE_PARTIAL;
}

/**
 * Given a request returns the serialized char* buffer
 */
test_error_code_t serialize_http_request(char *buffer, size_t *size, Request *request) {
    memset(buffer, 0, HTTP_SIZE);
    char* p = buffer;
    if (strcmp(request->http_method, GET) != 0) {
        return TEST_ERROR_PARSE_FAILED;
    }
    memcpy(p, request->http_method, strlen(request->http_method));
    p[strlen(request->http_method)] = ' ';
    p += strlen(request->http_method) + 1;
    *size += strlen(request->http_method) + 1;
    
    memcpy(p, request->http_uri, strlen(request->http_uri));
    p[strlen(request->http_uri)] = ' ';
    p += strlen(request->http_uri) + 1;
    *size += strlen(request->http_uri) + 1;

    memcpy(p, HTTP_VER, strlen(HTTP_VER));
    p += strlen(HTTP_VER);
    *size += strlen(HTTP_VER);

    memcpy(p, CRLF, strlen(CRLF));
    p += strlen(CRLF);
    *size += strlen(CRLF);

    memcpy(p, HOST, strlen(HOST));
    p += strlen(HOST);
    *size += strlen(HOST);

    memcpy(p, request->host, strlen(request->host));
    p += strlen(request->host);
    *size += strlen(request->host);

    memcpy(p, CRLF, strlen(CRLF));
    p += strlen(CRLF);
    *size += strlen(CRLF);

    memcpy(p, CONNECTION, strlen(CONNECTION));
    p += strlen(CONNECTION);
    *size += strlen(CONNECTION);

    memcpy(p, CONNECTION_VAL, strlen(CONNECTION_VAL));
    p += strlen(CONNECTION_VAL);
    *size += strlen(CONNECTION_VAL);

    memcpy(p, CRLF, strlen(CRLF));
    p += strlen(CRLF);
    *size += strlen(CRLF);

    memcpy(p, CRLF, strlen(CRLF));
    p += strlen(CRLF);
    *size += strlen(CRLF);

    return TEST_ERROR_NONE;
}

/**
 * Given a char buffer returns the parsed request headers
 */
test_error_code_t serialize_http_response(char **msg, size_t *len, const char *prepopulated_headers, char *content_type,
                   char *content_length, char *last_modified, size_t body_len, char *body, bool should_close) {
    char date[4096];
    time_t now;
    time(&now);
    struct tm *now_tm = localtime(&now);
    strftime(date, 4096, "%a, %d %b %Y %H:%M:%S %Z", now_tm);

    size_t date_len = strlen(date);
    size_t content_type_len = 0;
    if (content_type != NULL) {
        content_type_len = strlen(content_type);
    }
    size_t content_length_len = 0;
    if (content_length != NULL) {
        content_length_len = strlen(content_length);
    }
    size_t last_modified_len = 0;
    if (last_modified != NULL) {
        last_modified_len = strlen(last_modified);
    }

    size_t prepopulated_len = strlen(prepopulated_headers);
    size_t msg_len;
    if (!should_close) {
        msg_len = prepopulated_len + strlen(HTTP_VER) + 1
                         + strlen(CONNECTION) + strlen(CONNECTION_VAL) + strlen(CRLF)
                         + strlen(SERVER) + strlen(SERVER_VAL) + strlen(CRLF)
                         + strlen(DATE) + date_len + strlen(CRLF);
    } else {
        msg_len = prepopulated_len + strlen(HTTP_VER) + 1
                         + strlen(CONNECTION) + strlen(CONNECTION_CLOSE) + strlen(CRLF)
                         + strlen(SERVER) + strlen(SERVER_VAL) + strlen(CRLF)
                         + strlen(DATE) + date_len + strlen(CRLF);
    }

    if (content_type != NULL) {
        msg_len += strlen(CONTENT_TYPE) + content_type_len + strlen(CRLF);
    }
    if (content_length != NULL) {
        msg_len += strlen(CONTENT_LENGTH) + content_length_len + strlen(CRLF);
    } else {
        msg_len += strlen(CONTENT_LENGTH) + strlen(ZERO) + strlen(CRLF);
    }
    if (last_modified != NULL) {
        msg_len += strlen(CONTENT_TYPE) + last_modified_len + strlen(CRLF);
    }
    msg_len += strlen(CRLF);
    msg_len += body_len;
    *len = msg_len;

    *msg = calloc(msg_len, sizeof(char));

    size_t cur_len = 0;
    // Prepopulated
    memcpy(*msg + cur_len, HTTP_VER, strlen(HTTP_VER));
    cur_len += strlen(HTTP_VER);

    memcpy(*msg + cur_len, " ", 1);
    cur_len += 1;

    memcpy(*msg + cur_len, prepopulated_headers, prepopulated_len);
    cur_len += prepopulated_len;

    // Standard headers
    if (!should_close) {
        cur_len += populate_header(*msg + cur_len, CONNECTION, strlen(CONNECTION), CONNECTION_VAL, strlen(CONNECTION_VAL));
    } else {
        cur_len += populate_header(*msg + cur_len, CONNECTION, strlen(CONNECTION), CONNECTION_CLOSE, strlen(CONNECTION_CLOSE));
    }
    cur_len += populate_header(*msg + cur_len, SERVER, strlen(SERVER), SERVER_VAL, strlen(SERVER_VAL));
    cur_len += populate_header(*msg + cur_len, DATE, strlen(DATE), date, date_len);
    if (content_type != NULL) {
        cur_len += populate_header(*msg + cur_len, CONTENT_TYPE, strlen(CONTENT_TYPE), content_type, content_type_len);
    }
    if (content_length != NULL) {
        cur_len += populate_header(*msg + cur_len, CONTENT_LENGTH, strlen(CONTENT_LENGTH), content_length,
                                   content_length_len);
    } else {
        cur_len += populate_header(*msg + cur_len, CONTENT_LENGTH, strlen(CONTENT_LENGTH), ZERO,
                                   strlen(ZERO));
    }
    if (last_modified != NULL) {
        cur_len += populate_header(*msg + cur_len, LAST_MODIFIED, strlen(CONTENT_TYPE), last_modified,
                                   last_modified_len);
    }
    memcpy(*msg + cur_len, CRLF, strlen(CRLF));
    cur_len += strlen(CRLF);

    if (body != NULL) {
        memcpy(*msg + cur_len, body, body_len);
        cur_len += body_len;
    }
    return TEST_ERROR_NONE;
}


int populate_header(char *msg, char *field, const size_t field_len, char *val, const size_t val_len) {
    memcpy(msg, field, field_len);
    memcpy(msg + field_len, val, val_len);
    memcpy(msg + field_len + val_len, CRLF, strlen(CRLF));
    return field_len + val_len + strlen(CRLF);
}

/**
 * @brief Get the real struct address pointer position.
 * It discriminates between IPv4 or IPv6 by inspecting the sa_family field.
 * @param sa pointer to the general struct sockaddr
 * @return void* pointer to struct sockaddr_in (IPv4) or sockaddr_in6 (IPv6)
 */

void *get_addr_in(const struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        // IPv4
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }
    // IPv4
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

/**
 * @brief Build a client side socket.
 * Caller should close the socket descriptor after usage.
 * @param hostname the server's IP Address or Hostname to connect to
 * @param port the server's listening port to connect to
 * @param verbose if set True, intermediate logging will be made to stdout
 * @return the client socket descriptor, or -1 if any error happens
 */
int build_client(const char *host, const char *port, bool verbose) {
    int error;
    int client_fd;
    struct addrinfo hints;
    struct addrinfo *server_info;
    struct addrinfo *walker;

    /* provide hints about what type of socket we want */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;      // either IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // want TCP Stream socket

    if ((error = getaddrinfo(host, port, &hints, &server_info)) != 0) {
        if (verbose) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        }
        return -1;
    }

    /* walk through the linked list to connect to the first succeeds */
    for (walker = server_info; walker != NULL; walker = walker->ai_next) {
        if ((client_fd = socket(walker->ai_family, walker->ai_socktype,
                                walker->ai_protocol)) == -1) {
            if (verbose) {
                perror("client: socket()");
            }
            continue;
        }

        if (connect(client_fd, walker->ai_addr, walker->ai_addrlen) == -1) {
            close(client_fd);
            if (verbose) {
                perror("client: connect()");
            }
            continue;
        }

        break;  // succeed in building a client socket
    }

    if (walker == NULL) {
        if (verbose) {
            perror("client: fail to connect to any");
        }
        freeaddrinfo(server_info);  // done with this linked list
        return -1;
    }

    if (verbose) {
        char server[INET6_ADDRSTRLEN];
        inet_ntop(walker->ai_family, get_addr_in(walker->ai_addr), server,
                  sizeof server);
        fprintf(stdout, "client: connected with address [%s] on port [%s]\n",
                server, port);
    }

    freeaddrinfo(server_info);  // done with this linked list

    return client_fd;
}

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
int build_server(const char *port, const int backlog, bool verbose) {
    int yes = 1;
    int error;
    int server_fd;
    struct addrinfo hints;
    struct addrinfo *server_info;
    struct addrinfo *walker;

    /* provide hints about what type of socket we want */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;      // either IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // want TCP Stream socket
    hints.ai_flags = AI_PASSIVE;      // use my own IP Address

    if ((error = getaddrinfo(NULL, port, &hints, &server_info)) != 0) {
        if (verbose) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        }
        return -1;
    }

    /* walk through the linked list to listen to the first succeeds */
    for (walker = server_info; walker != NULL; walker = walker->ai_next) {
        /* a successful workflow would be socket() -> setsockopt() -> bind() ->
         * listen() */
        if ((server_fd = socket(walker->ai_family, walker->ai_socktype,
                                walker->ai_protocol)) == -1) {
            if (verbose) {
                perror("server: socket()");
            }
            continue;
        }

        // allow re-usage of the same port
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) ==
            -1) {
            if (verbose) {
                perror("server: setsockopt()");
            }
            close(server_fd);
            continue;
        }

        if (bind(server_fd, walker->ai_addr, walker->ai_addrlen) == -1) {
            if (verbose) {
                perror("server: bind()");
            }
            close(server_fd);
            continue;
        }

        if (listen(server_fd, backlog) == -1) {
            if (verbose) {
                perror("server: listen()");
            }
            close(server_fd);
            continue;
        }

        break;  // succeed in building a server socket
    }

    if (walker == NULL) {
        if (verbose) {
            perror("server: fail to bind-listen to any");
        }
        freeaddrinfo(server_info);  // done with this linked list
        return -1;
    }

    if (verbose) {
        char server[INET6_ADDRSTRLEN];
        inet_ntop(walker->ai_family, get_addr_in(walker->ai_addr), server,
                  sizeof server);
        fprintf(stdout, "server: bind-listen with address [%s] on port [%s]\n",
                server, port);
    }

    freeaddrinfo(server_info);  // done with this linked list

    return server_fd;
}

/**
 * @brief Ensure to read in as many as len bytes of data into user provided
 * buffer. It will sit-wait until read in all the required many bytes.
 * @param fd the file descriptor to read from, typically socket
 * @param buf the buffer to place the data read from fd
 * @param len how many bytes to read from the fd
 * @attention user must ensure the buffer has at least len many space available
 * @return ssize_t how many bytes read, or -1 if any error happens
 */
ssize_t robust_read(int fd, void *buf, const size_t len) {
    static int flag = 0;  // most of cases
    ssize_t read;
    ssize_t total_read = len;
    ssize_t curr_read = 0;
    char *buf_next = (char *)buf;
    while (curr_read != total_read) {
        if ((read = recv(fd, buf_next, len, flag)) < 0) {
            return -1;
        } else if (read == 0) {
            // EOF
            return curr_read;
        }
        curr_read += read;
        buf_next += read;
    }
    return curr_read;
}

/**
 * @brief Ensure to write out as many as len bytes of data from the user
 * provided buffer It will sit-wait until write out all the required many bytes
 * @param fd the file descriptor, typically socket
 * @param buf the buffer which contains data to be written into fd
 * @param len how many bytes to write into the fd
 * @attention user must ensure the buffer has at least len many space available
 * @return ssize_t how many bytes written, or -1 if any error happens
 */
ssize_t robust_write(int fd, const void *buf, const size_t len) {
    static int flag = 0;  // most of cases
    ssize_t write;
    ssize_t total_write = len;
    ssize_t curr_write = 0;
    const char *buf_next = (const char *)buf;
    while (curr_write != total_write) {
        size_t write_size = len - curr_write;
        if ((write = send(fd, buf_next, write_size, flag)) <= 0) {
            if (errno != EINTR || errno != EAGAIN) {
                return -1;
            }
            write = 0;  // Interrupted by sig handler return, call write() again
        }
        buf_next += write;
        curr_write += write;
    }
    return curr_write;
}

/**
 * @brief Initialize an poll array with default capacity.
 * @return poll_array_t* pointer to the new allocated poll array struct
 */
poll_array_t *init_poll_array() {
    poll_array_t *new_poll_array = (poll_array_t *)malloc(sizeof(poll_array_t));
    new_poll_array->pfds =
            (struct pollfd *)malloc(INIT_POLL_ARRAY_CAPACITY * sizeof(struct pollfd));
    new_poll_array->buffers = (char **)malloc(INIT_POLL_ARRAY_CAPACITY * sizeof(char *));
    new_poll_array->sizes = (int *)malloc(INIT_POLL_ARRAY_CAPACITY * sizeof(int));
    new_poll_array->capacity = INIT_POLL_ARRAY_CAPACITY;
    new_poll_array->count = 0;
    return new_poll_array;
}

/**
 * @brief Add a new socket descriptor under poll monitoring.
 * User should ensure that no duplicate fd is added into the array.
 * This functionality doesn't check for duplicate inserts.
 * @param new_fd new socket descriptor to be added
 * @param array pointer to the allocated poll array struct
 * @param flag the bit flag for the descriptor to be monitored upon
 */
void add_to_poll_array(int new_fd, poll_array_t *array, short flag) {
    static int expand_factor = 2;
    if (array->count == array->capacity) {
        // allocated space expansion needed
        array->capacity *= expand_factor;
        array->pfds = (struct pollfd *)realloc(
                array->pfds, array->capacity * sizeof(struct pollfd));

        array->buffers = (char **)realloc(array->buffers, array->capacity * sizeof(char *));
        array->sizes = (int *)realloc(array->sizes, array->capacity * sizeof(int));
    }
    memset(&array->pfds[array->count], 0, sizeof(struct pollfd));
    array->pfds[array->count].fd = new_fd;
    array->pfds[array->count].events = flag;

    array->buffers[array->count] = NULL;
    array->sizes[array->count] = 0;

    array->count += 1;
}

/**
 * @brief Remove an indexed socket descriptor from the poll array
 * User should ensure the index lies in between [0, array->count)
 * If the index out of bound, the program will exit with code 1
 * @param remove_idx the to-be-removed index from the poll array
 * @param array pointer to the allocated poll array struct
 */
void remove_from_poll_array(int remove_idx, poll_array_t *array) {
    static int shrink_criteria = 4;
    static int shrink_factor = 2;
    if (remove_idx < 0 || remove_idx >= array->count) {
        perror("remove_from_poll_array(): index out of bound");
        exit(1);
    }
    // close the socket descriptor
    close(array->pfds[remove_idx].fd);
    // swap the last entry into the removed index
    array->pfds[remove_idx] = array->pfds[array->count - 1];

    if (array->buffers[remove_idx] != NULL) {
        free(array->buffers[remove_idx]);
    }
    array->buffers[remove_idx] = array->buffers[array->count - 1];
    array->sizes[remove_idx] = array->sizes[array->count - 1];

    array->count -= 1;
    if (array->count < array->capacity / shrink_criteria) {
        // allocated space shrinkage needed
        array->capacity /= shrink_factor;
        array->pfds = (struct pollfd *)realloc(
                array->pfds, array->capacity * sizeof(struct pollfd));
        array->buffers = (char **)realloc(array->buffers, array->capacity * sizeof(char *));
        array->sizes = (int *)realloc(array->sizes, array->capacity * sizeof(int));
    }
}

/**
 * @brief Release the dynamical allocated space for poll array
 * @param array pointer to the allocated poll array
 */
void release_poll_array(poll_array_t *array) {
    for (int i = array->count - 1; i >= 0; i--) {
        remove_from_poll_array(i, array);
    }
    free(array->pfds);
    free(array);
}
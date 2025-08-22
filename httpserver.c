// Asgn 2: A simple HTTP server.
// By: Eugene Chou
//     Andrew Quinn
//     Brian Zhao

#include "asgn2_helper_funcs.h"
#include "connection.h"
#include "debug.h"
#include "response.h"
#include "request.h"
#include "rwlock.h"
#include "queue.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

void handle_connection(void);
void handle_get(conn_t *);
void handle_put(conn_t *);
void handle_unsupported(conn_t *);

//Adding pthread mutex lock
pthread_mutex_t lock;
typedef struct lock {
    char *filename;
    rwlock_t *rw;
    int activeThreads;
    int total;
} lock_t;
lock_t **lockArray;
queue_t *q;
int main(int argc, char **argv) {

    if (argc < 2) {
        warnx("wrong arguments: %s port_num", argv[0]);
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    /* Use getopt() to parse the command-line arguments */
    int opt;
    int threads = 0;
    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
        case 't': threads = atoi(optarg); break;
        default: break;
        }
    }
    size_t port;
    if (opt == -1 && argc == 2) {
        threads = 4;
        port = strtoull(argv[1], NULL, 10);
        printf("argc is 2\n");
    } else if (argc > 2 && threads == 0) {
        printf("too many arguments\n");
        return 2;
    } else {
        port = strtoull(argv[optind + 0], NULL, 10);
        if (argv[optind + 1] != NULL) {
            printf("too many arguments\n");
            return 3;
        }
    }
    opt = 0;
    if (port < 1 || port > 65535) {
        fprintf(stderr, "Invalid Port\n");
        return EXIT_FAILURE;
    }
    signal(SIGPIPE, SIG_IGN);
    Listener_Socket sock;
    if (listener_init(&sock, port) < 0) {
        fprintf(stderr, "Invalid Port\n");
        return EXIT_FAILURE;
    }
    /* Initialize worker threads, the queue, and other data structures */
    pthread_t pthread[threads];
    q = queue_new(100);
    //uri_list = list_create();
    pthread_mutex_init(&lock, NULL);
    for (int i = 0; i < threads; i++) {
        pthread_create(&pthread[i], NULL, (void *(*) (void *) ) handle_connection, NULL);
    }
    /*    lockArray = (lock_t **) malloc(threads * sizeof(lock_t**));
 */
    lockArray = (lock_t **) malloc(100 * sizeof(lock_t));
    for (int i = 0; i < 100; i++) {
        lockArray[i] = (lock_t *) malloc(sizeof(lock_t));
        lockArray[i]->filename = NULL;
        lockArray[i]->rw = rwlock_new(N_WAY, 5);
        lockArray[i]->activeThreads = 0;
        lockArray[i]->total = 100;
    }
    int connfd = 0;
    uintptr_t conn = 0;
    /* Hint: You will need to change how handle_connection() is used */
    while (1) {
        connfd = listener_accept(&sock);
        conn = (uintptr_t) connfd;
        queue_push(q, (void *) conn);
        handle_connection();
        close(connfd);
    }
    return EXIT_SUCCESS;
}

void audit_log(char *message) {
    fprintf(stderr, "%s", message);
}

void handle_connection(void) {
    //  while (1) {
    uintptr_t connfd = 0;
    queue_pop(q, (void **) &connfd);
    conn_t *conn = conn_new(connfd);
    const Response_t *res = conn_parse(conn);
    if (res != NULL) {
        conn_send_response(conn, res);
    } else {
        //debug("%s", conn_str(conn));
        const Request_t *req = conn_get_request(conn);
        if (req == &REQUEST_GET) {
            handle_get(conn);
        } else if (req == &REQUEST_PUT) {
            handle_put(conn);
        } else {
            handle_unsupported(conn);
        }
    }

    conn_delete(&conn);
    // }
}
lock_t *get_lock(char *uri) {
    lock_t *lockt = NULL;
    for (int i = 0; i < lockArray[0]->total; i++) {
        if (lockArray[i]->filename == NULL) {
            break;
        }
        if (strcmp(lockArray[i]->filename, uri) == 0) {
            lockt = lockArray[i];
            break;
        }
    }
    //&& uri not null?
    if (lockt == NULL) {
        for (int i = 0; i < lockArray[0]->total; i++) {
            if (lockArray[i]->filename == NULL) {
                lockArray[i]->filename = uri;
                lockt = lockArray[i];
                break;
            }
        }
    }
    return lockt;
}

void handle_get(conn_t *conn) {
    char *uri = conn_get_uri(conn);
    int code = 0;
    //debug("Handling GET request for %s", uri);
    pthread_mutex_lock(&lock);
    lock_t *lockt = NULL;
    //USING GET RWLOCK TO GET FROM STRUCT ARRAY
    lockt = get_lock(uri);
    reader_lock(lockt->rw);
    pthread_mutex_unlock(&lock);

    // What are the steps in here?
    int exists = access(uri, F_OK);
    int fdout = open(uri, O_RDONLY);
    struct stat file_stat;
    uint64_t fileSize = 0;
    const Response_t *resp = NULL;
    // 1. Open the file.
    // If open() returns < 0, then use the result appropriately
    //   a. Cannot access -- use RESPONSE_FORBIDDEN
    //   b. Cannot find the file -- use RESPONSE_NOT_FOUND
    //   c. Other error? -- use RESPONSE_INTERNAL_SERVER_ERROR
    // (Hint: Check errno for these cases)!
    if (fdout < 0) {
        if (exists != 0) {
            resp = &RESPONSE_NOT_FOUND;
            code = 404;
        } else if (errno == EACCES) {
            //use handle_unsupported?
            resp = &RESPONSE_FORBIDDEN;
            code = 403;
        } else {
            resp = &RESPONSE_INTERNAL_SERVER_ERROR;
            code = 500;
        }
    }
    // 2. Get the size of the file.
    // (Hint: Checkout the function fstat())!
    if (fdout >= 0) {
        if (fstat(fdout, &file_stat) == -1) {
            //set resp to correct code
            resp = &RESPONSE_FORBIDDEN;
            code = 403;
        } else
            fileSize = file_stat.st_size;
    }
    // 3. Check if the file is a directory, because directories *will*
    // open, but are not valid.
    // (Hint: Checkout the macro "S_IFDIR", which you can use after you call fstat()!)
    if (S_ISDIR(file_stat.st_mode)) {
        resp = &RESPONSE_FORBIDDEN;
        code = 404;
    }
    // 4. Send the file
    // (Hint: Checkout the conn_send_file() function!)
    if (resp == NULL && fdout >= 0) {
        conn_send_file(conn, fdout, fileSize);
        code = 200;
        resp = &RESPONSE_OK;
    }
    close(fdout);
    /*if (resp2 != NULL) {
        resp = resp2;
        	
    }*/

    char *reqID = conn_get_header(conn, "Request-Id");
    //	resp = conn_send_response(conn, resp);

    fprintf(stderr, "GET,/%s,%d,%s\n", uri, code, reqID);
    // 5. Close the file
    reader_unlock(lockt->rw);
}

void handle_put(conn_t *conn) {
    char *uri = conn_get_uri(conn);
    int code;
    char *resID = NULL;
    //Commented this out to test arguments
    const Response_t *res = NULL;
    //debug("Handling PUT request for %s", uri);
    // What are the steps in here?
    // 1. Check if file already exists before opening it.
    // (Hint: check the access() function)!
    pthread_mutex_lock(&lock);
    lock_t *lockt = NULL;
    lockt = get_lock(uri);
    int exists = access(uri, F_OK);
    // 2. Open the file.
    // If open() returns < 0, then use the result appropriately
    //   a. Cannot access -- use RESPONSE_FORBIDDEN
    //   b. File is a directory -- use RESPONSE_FORBIDDEN
    //   c. Cannot find the file -- use RESPONSE_FORBIDDEN
    //   d. Other error? -- use RESPONSE_INTERNAL_SERVER_ERROR
    // (Hint: Check errno for these cases)!
    int fdout = open(uri, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fdout < 0) {
        if (errno == EACCES) {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            code = 500;
        } else if (errno == EISDIR) {
            res = &RESPONSE_FORBIDDEN;
            code = 403;
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            code = 500;
        }
    } else if (exists == 0) {
        res = &RESPONSE_OK;
        code = 200;
    }

    pthread_mutex_unlock(&lock);
    writer_lock(lockt->rw);
    // 3. Receive the file
    // (Hint: Checkout the conn_recv_file() function)!
    //NEED TO USE DIFFERENT CODE IF EXISTS IS TRUE;
    if (fdout >= 0) {
        res = conn_recv_file(conn, fdout);
        code = 201;
    }
    //res might not be null still should put 201 above
    //  audit_log("middle of put");
    if (res == NULL) {
        code = 201;
        res = &RESPONSE_CREATED;
    }
    // 4. Send the response
    // (Hint: Checkout the conn_send_response() function)!
    res = conn_send_response(conn, res);
    //while (res != NULL) {

    resID = conn_get_header(conn, "Request-Id");
    // 5. Close the file
    close(fdout);
    fprintf(stderr, "PUT,/%s,%d,%s\n", uri, code, resID);
    writer_unlock(lockt->rw);
}

void handle_unsupported(conn_t *conn) {
    //    debug("Handling unsupported request");

    // Send responses
    conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
}

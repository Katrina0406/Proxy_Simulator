/*
 * I wrote separate cache.c & cache.h files to implement the cache part
 * pthread_mutex_lock and pthread_mutex-unlock ensure that we only do one
 * write at a time and exclude any interferences while writting to cache
 */

/* Some useful includes to help you get started */
//@author Yuqiao Hu
//@andrew id: yuqiaohu

#include "cache.h"
#include "csapp.h"
#include "http_parser.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

pthread_mutex_t mutex;

void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void *thread(void *vargp);

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
//  */
static const char *header_user_agent = "User-Agent: Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20191101 Firefox/63.0.1\r\n";
static const char *header_connection = "Connection: close\r\n";
static const char *header_proxy_connection = "Proxy-Connection: close\r\n";

int main(int argc, char **argv) {
    int listenfd, *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    // Check command-line args
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    signal(SIGPIPE, SIG_IGN);

    init_cache();
    pthread_mutex_init(&mutex, NULL);
    // opening a listening socket
    listenfd = open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = malloc(sizeof(int));
        *connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        getnameinfo((struct sockaddr *)&clientaddr, clientlen, hostname,
                    MAXLINE, port, MAXLINE, 0);
        if (*connfd < 0) {
            printf("error");
            free(connfd);
        }
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        pthread_create(&tid, NULL, thread, connfd);
    }
    close(listenfd);
    free_cache();
    return 0;
}

// thread routine
void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    doit(connfd);
    close(connfd);
    return NULL;
}

/*
 * Handles one HTTP transaction
 */
void doit(int client_fd) {
    int server_fd;
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE],
        hdrs[MAXLINE], tmp[MAXLINE];
    const char *hostname, *path, *port;
    rio_t client, server;
    parser_t *p;
    header_t *h;

    // read request line and headers
    rio_readinitb(&client, client_fd);
    rio_readlineb(&client, buf, MAXLINE);
    p = parser_new();
    printf(buf);
    parser_parse_line(p, buf);
    sscanf(buf, "%s %s %s", method, url, version);
    strcpy(hdrs, "");
    strcat(hdrs, buf);
    // check method is GET
    if (strcasecmp(method, "GET")) {
        clienterror(client_fd, method, "501", "Not implemented",
                    "Tiny does not implement this method");
        return;
    }
    // check version
    if (!strcasecmp(version, "HTTP/1.1")) {
        strcpy(version, "HTTP/1.0");
    }
    printf("check here\n");
    // have previously stored requests of this url
    if (read_cache(url, client_fd))
        return;
    printf("check here again\n");

    while (rio_readlineb(&client, buf, MAXLINE) > 0) {
        if (!strcmp(buf, "\r\n"))
            break;
        parser_parse_line(p, buf);
    }
    parser_retrieve(p, HOST, &hostname);
    parser_retrieve(p, PORT, &port);
    parser_retrieve(p, PATH, &path);

    server_fd = open_clientfd(hostname, port);
    if (server_fd < 0) {
        printf("connection failed\n");
        return;
    }
    rio_readinitb(&server, server_fd);

    while ((h = parser_retrieve_next_header(p)) != NULL) {
        // ignore following request headers from client
        if (strstr(h->name, "Host") != NULL)
            continue;
        if (strstr(h->name, "User-Agent") != NULL)
            continue;
        if (strstr(h->name, "Connection") != NULL)
            continue;
        if (strstr(h->name, "Proxy-Connection") != NULL)
            continue;
        // forward any additional headers from client
        sprintf(tmp, "%s: %s\r\n", h->name, h->value);
        strcat(hdrs, tmp);
    }

    // add required headers
    sprintf(tmp, "Host: %s:%s\r\n", hostname, port);
    strcat(hdrs, tmp);
    sprintf(tmp, "%s%s%s", header_connection, header_proxy_connection,
            header_user_agent);
    strcat(hdrs, tmp);
    sprintf(tmp, "%s\r\n", tmp);
    strcat(hdrs, tmp);
    printf(hdrs);

    rio_writen(server_fd, hdrs, strlen(hdrs));
    int n;
    int size = 0;
    char object[MAX_OBJECT_SIZE * 2];
    // server response
    while ((n = rio_readnb(&server, buf, MAXLINE))) {
        if (size <= MAX_OBJECT_SIZE) {
            memcpy(object + size, buf, n);
            size += n;
        }
        rio_writen(client_fd, buf, n);
    }
    // if not exceeding max limit of object size, store object on cache
    if (size <= MAX_OBJECT_SIZE) {
        pthread_mutex_lock(&mutex);
        write_cache(url, object, size);
        pthread_mutex_unlock(&mutex);
    }
    parser_free(p);
    close(server_fd);
    return;
}

/*
 * error-handling function - sends an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    // build the http response body
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body,
            "%s<body bgcolor="
            "ffffff"
            ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    // print the http response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}

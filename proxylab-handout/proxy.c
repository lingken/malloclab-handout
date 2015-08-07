/*
       Name: Ken Ling
  Andrew ID: kling1

  A web proxy with approximate LRU strategy cache.
  P.S: exit(0) is removed in unix_error() in csapp.c to prevent unexpected
  termination of the whole process
*/
#include <stdio.h>
#include <regex.h>
#include <string.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

/* Global variables for regular expression */
static const char *pattern_urn = "http://[^/]*/(.+)";
static const char *pattern_hostname = "Host: ([^:\r\n]*).*\r\n";
static const char *pattern_port = "Host: .*:(.*)\r\n";
regex_t reg_hostname;
regex_t reg_port;
regex_t reg_urn;
const int nmatch = 10;
/* Global cache */
Cache *cache;

/* Function prototypes for proxy process */
void process_request(int fd);
int read_requesthdrs(rio_t *rp, char request[MAXBUF], 
    char host[MAXLINE], char port[MAXLINE]);
void clienterror(int fd, char *cause, char *errnum, 
    char *shortmsg, char *longmsg);
void get_content(char request[MAXBUF], int client_fd, char host[MAXLINE], 
    char port[MAXLINE], char urn[MAXLINE]);
void parse_uri(char uri[MAXLINE], char urn[MAXLINE]);
void *thread(void *vargp);
/* Function prototype for regular expression */
void initialize_regex();

int main(int argc, char **argv)
{
    int listenfd, *client_connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGPIPE, SIG_IGN);
    initialize_regex();
    /* Initialize the global cache */
    cache = Malloc(sizeof(Cache));
    if (cache == NULL) {
        exit(0);
    }
    memset(cache, 0, sizeof(Cache));
    initialize_cache(cache, MAX_CACHE_SIZE);

    listenfd = Open_listenfd(argv[1]);
    if (listenfd < 0) {
        exit(0);
    }

    /* Loop for connections from clients */
    while (1) {
        clientlen = sizeof(clientaddr);
        client_connfd = Malloc(sizeof(int));
        if (client_connfd == NULL) {
            continue;
        }
        *client_connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        if (*client_connfd < 0) {
            continue;
        }
        int code = 0;
        if ((code = getnameinfo((SA *) &clientaddr, clientlen, hostname, 
            MAXLINE, port, MAXLINE, 0)) != 0) {
            fprintf(stderr, "getnameinfo error: %s\n", gai_strerror(code));
            continue;
        }
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        pthread_create(&tid, NULL, thread, client_connfd);
    }
    return 0;
}

/* Process function of a thread for a connection */
void *thread(void *vargp) {
    int client_connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    process_request(client_connfd);
    close(client_connfd);
    return NULL;
}

/*
  Process the a request from a client.
  First modify the request header.
  Then try to fetch the content either from cache or from server.
*/
void process_request(int client_fd) 
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE], urn[MAXLINE];
    char request[MAXBUF];
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, client_fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) {
        return;
    }

    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        clienterror(client_fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }

    parse_uri(uri, urn);
    sprintf(request, "%s /%s HTTP/1.0\r\n", method, urn);

    if (read_requesthdrs(&rio, request, host, port) < 0) { // error occurs
        return;
    }
    get_content(request, client_fd, host, port, urn);
}

/* Get content from cache or server and send it back to the client */
void get_content(char request[MAXBUF], int client_fd, char host[MAXLINE], 
    char port[MAXLINE], char urn[MAXLINE]) {

    char *response = read_from_cache(cache, urn, host);
    if (response) { // Cache Hit
        Rio_writen(client_fd, response, strlen(response));
        free(response);
        return;
    }

    /* Cache Miss, fetch content from server */
    int server_fd;
    rio_t rio;
    
    server_fd = Open_clientfd(host, port);
    if (server_fd < 0) {
        return;
    }
    Rio_readinitb(&rio, server_fd);
    if (rio_writen(server_fd, request, strlen(request)) != strlen(request)) {
        fprintf(stderr, "rio_writen error: %s\n", strerror(errno));
        return;
    }
    char body[MAXLINE];
    char buffer[MAX_OBJECT_SIZE];
    int content_size = 0;
    int n = 0;
    while ((n = Rio_readlineb(&rio, body, MAXLINE)) != 0) {
        if (n < 0) {
            return; // Rio_readlineb error
        }
        if (content_size + n < MAX_OBJECT_SIZE) {
            memcpy(buffer + content_size, body, n);
        }
        content_size += n;
        Rio_writen(client_fd, body, n);
    }
    /* Content is small enough to be cached */
    if (content_size < MAX_OBJECT_SIZE) {
        write_to_cache(cache, content_size, buffer, urn, host);
    }
}

/* Parse URI in a HTTP request into URL:URN form */
void parse_uri(char uri[MAXLINE], char urn[MAXLINE]) {
    regmatch_t pm[10];
    memset(urn, 0, MAXLINE);
    if (regexec(&reg_urn, uri, nmatch, pm, 0) == 0) {
        int n_urn = (int)(pm[1].rm_eo - pm[1].rm_so);
        strncpy(urn, uri + pm[1].rm_so, n_urn);
    }
}

/*
    Modify the header of request from client.
    Extract hostname and port from the header for initiate connection.
    Return 0 when everything is fine, -1 when error occurs.
*/
int read_requesthdrs(rio_t *rp, char request[MAXBUF], char host[MAXLINE], 
    char port[MAXLINE]) {

    char buf[MAXLINE];
    regmatch_t pm[10];
    memset(host, 0, MAXLINE);
    memset(port, 0, MAXLINE);
    if (Rio_readlineb(rp, buf, MAXLINE) < 0) {
        return -1;
    }

    while(strcmp(buf, "\r\n")) {
        /* Omit the headers that need to be replaced */
        if (!(strstr(buf, "User-Agent:") || 
            strstr(buf, "Connection:") || 
            strstr(buf, "Proxy-Connection:") ||
            strstr(buf, "Accept:") ||
            strstr(buf, "Accept-Encoding")
            )) {
            sprintf(request, "%s%s", request, buf);
        }
        if (strstr(buf, "Host:")) {
            /* Get hostname from Host field*/
            if (regexec(&reg_hostname, buf, nmatch, pm, 0) == 0) {
                int n_hostname = (int)(pm[1].rm_eo - pm[1].rm_so);
                strncpy(host, buf + pm[1].rm_so, n_hostname);
            }
            /* Get port from Host field*/
            if (regexec(&reg_port, buf, nmatch, pm, 0) == 0) {
                int n_port = (int)(pm[1].rm_eo - pm[1].rm_so);
                strncpy(port, buf + pm[1].rm_so, n_port);
            } else {
                strncpy(port, "80", 2);
            }
        }
        if (Rio_readlineb(rp, buf, MAXLINE) < 0) {
            return -1;
        }
    }
    /* Add headers as suggested in the handout */
    sprintf(request, "%s%s", request, user_agent_hdr);
    sprintf(request, "%s%s", request, accept_hdr);
    sprintf(request, "%s%s", request, accept_encoding_hdr);
    sprintf(request, "%s%s", request, connection_hdr);
    sprintf(request, "%s%s", request, proxy_connection_hdr);
    /* End the header of GET method */
    sprintf(request, "%s\r\n", request);
    return 0;
}

/* Report client error */
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

void initialize_regex() {
    regcomp(&reg_hostname, pattern_hostname, REG_EXTENDED | REG_ICASE);
    regcomp(&reg_port, pattern_port, REG_EXTENDED | REG_ICASE);
    regcomp(&reg_urn, pattern_urn, REG_EXTENDED | REG_ICASE);
}

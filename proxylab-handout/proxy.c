/*
       Name: Ken Ling
  Andrew ID: kling1
*/
#include <stdio.h>
#include <regex.h>
#include <string.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
/* Pattern for regular expression */
static const char *pattern_host = "http://([^:/]*)(:\\d+)?/?.*";
static const char *pattern_port = "http://.*:([^/]*)";
static const char *pattern_urn = "http://[^/]*/(.+)";

regex_t reg_host;
regex_t reg_port;
regex_t reg_urn;
const int nmatch = 10;

void process_request(int fd);
void read_requesthdrs(rio_t *rp, char request[MAXBUF]);
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg);
void get_content(char request[MAXBUF], int client_fd, char host[MAXLINE], char port[MAXLINE]);
int parse_uri(char uri[MAXLINE], char host[MAXLINE], char port[MAXLINE], char urn[MAXLINE]);
void initialize_regex();
void free_regex();
void *thread(void *vargp);

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
    initialize_regex();
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        client_connfd = malloc(sizeof(int));
        *client_connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
            Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                        port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        pthread_create(&tid, NULL, thread, client_connfd);
        // process_request(client_connfd);
        // Close(client_connfd);
    }
    return 0;
}

void *thread(void *vargp) {
    int client_connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    process_request(client_connfd);
    close(client_connfd);
    return NULL;
}

void process_request(int client_fd) 
{
    int server_fd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE], urn[MAXLINE];
    char request[MAXBUF];
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, client_fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) {
        return;
    }
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        clienterror(client_fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }

    parse_uri(uri, host, port, urn);
    sprintf(request, "%s /%s HTTP/1.0\r\n", method, urn);

    read_requesthdrs(&rio, request);
    printf("FINAL:\n");
    printf("%s\n", request);

    get_content(request, client_fd, host, port);
}

void get_content(char request[MAXBUF], int client_fd, char host[MAXLINE], char port[MAXLINE]) {
    printf("THIS IS REQUEST:\n%s", request);
    int server_fd;
    rio_t rio;
    printf("MY HOST: %s\n", host);
    printf("MY PORT: %s\n", port);
    server_fd = Open_clientfd(host, port);

    Rio_readinitb(&rio, server_fd);
    Rio_writen(server_fd, request, strlen(request));

    char body[MAXLINE];
    int n = 0;
    while ((n = Rio_readlineb(&rio, body, MAXLINE)) != 0) {
        // printf("%s\n", body);
        Rio_writen(client_fd, body, n);
    }
    // Rio_readlineb(&rio, body, MAXBUF);
    // printf("CONTENT:\n%s\n", body);
    // Fputs(body, stdout);
}

int parse_uri(char uri[MAXLINE], char host[MAXLINE], char port[MAXLINE], char urn[MAXLINE]) {
    printf("PARSE URI\n");
    regmatch_t pm[10];
    printf("%d %d %d\n", sizeof(host), sizeof(port), sizeof(urn));
    memset(host, 0, MAXLINE);
    memset(port, 0, MAXLINE);
    memset(urn, 0, MAXLINE);

    if (regexec(&reg_host, uri, nmatch, pm, 0) == 0) {
        int n_host = (int)(pm[1].rm_eo - pm[1].rm_so);
        printf("n_host: %d\n", n_host);
        strncpy(host, uri + pm[1].rm_so, n_host);
    } else {
        return -1;
    }
    if (regexec(&reg_port, uri, nmatch, pm, 0) == 0) {
        int n_port = (int)(pm[1].rm_eo - pm[1].rm_so);
        strncpy(port, uri + pm[1].rm_so, n_port);
    } else {
        strncpy(port, "80", 2);
    }
    if (regexec(&reg_urn, uri, nmatch, pm, 0) == 0) {
        int n_urn = (int)(pm[1].rm_eo - pm[1].rm_so);
        strncpy(urn, uri + pm[1].rm_so, n_urn);
    }
    printf("uri: %s\n", uri);
    printf("host: %s\n", host);
    printf("port: %s\n", port);
    printf("urn: %s\n", urn);
    return 0;
}

void read_requesthdrs(rio_t *rp, char request[MAXBUF]) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {
        if (!(strstr(buf, "User-Agent:") || 
            strstr(buf, "Connection:") || 
            strstr(buf, "Proxy-Connection:") ||
            strstr(buf, "Accept:") ||
            strstr(buf, "Accept-Encoding")
            )) {
            sprintf(request, "%s%s", request, buf);
        }
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    sprintf(request, "%s%s", request, user_agent_hdr);
    // sprintf(request, "%s%s", request, accept_hdr);
    // sprintf(request, "%s%s", request, accept_encoding_hdr);
    // sprintf(request, "%s%s", request, connection_hdr);
    // sprintf(request, "%s%s", request, proxy_connection_hdr);
    /* end the header of GET method */
    sprintf(request, "%s\r\n", request);
    return;
}

void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

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
    regcomp(&reg_host, pattern_host, REG_EXTENDED | REG_ICASE);
    regcomp(&reg_port, pattern_port, REG_EXTENDED | REG_ICASE);
    regcomp(&reg_urn, pattern_urn, REG_EXTENDED | REG_ICASE);
}

void free_regex() {
    regfree(&reg_host);
    regfree(&reg_port);
    regfree(&reg_urn);
}
#include <stdio.h>

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

// proxy server main function
int main(int argc, char **argv)
{
  int listenfd, connfd;                  // listening socket, Connecting socket discriptor
  char hostname[MAXLINE], port[MAXLINE]; // Hostname & Port of Client Request
  socklen_t clientlen;                   // size of slientaddr stucture
  struct sockaddr_storage clientaddr;    // structure of client address storage

  /* Check command line args */
  if (argc != 2) // assert(argc !=2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // Creating Listening Socket Discriptor
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

/* --------------- 연결된 client에 대해서 한 HTTP 트랜잭션 처리 --------------- */
void doit(int fd)
{
  int toservfd;                                                       // 서버로 연결하는 socket
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // 요청내용 저장 buf, buf로부터 요청 method, uri, version 파싱하여 저장
  char filename[MAXLINE], cgiargs[MAXLINE];                           // filename 저장, 동적 처리의 경우 인자들 cgiargs에 저장
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);           // fd를 통해 읽어오기 위한 초기화
  Rio_readlineb(&rio, buf, MAXLINE); // 초기화된 rio를 통해서 요청내용을 buf에 저장
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); // buf로부터 method, uri, version 파싱
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))
  {                                                                                           // method가 GET이 아니면 0이 아닌 수 반환됨
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method"); // 클라이언트로 에러 Response 응답하는 함수 호출
    return;
  }

  read_requesthdrs(&rio); // 나머지 request header 부분 버림

  serve(fd, method, uri, version);
}

/* --------------- 확실한 error에 대해서 처리하여 client에게 응답함 --------------- */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  // sprintf는 buffer 변수에 내용을 '덮어씀' -> 중첩해서 작성
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor='ffffff'>\r\n",
          body);
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
  Rio_writen(fd, body, strlen(body)); // 위에서 작성한 response body 아래에 붙임
}

/* --------------- 불필요한 request header 부분 버림 --------------- */
void read_requesthdrs(rio_t *rp) // 호출부에서 이미 init한 상태로 rio_t 구조체를 넘겨줌
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE); // buf에서 한 줄 읽음
  while (strcmp(buf, "\r\n"))      // 읽은 한 줄이 \r\n과 같은지 비교. 같지 않은 동안 반복
  {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/* 서버로 요청 및 응답받은 내용 반환 */
void serve(int fd, char *method, char *uri, char *version)
{
  int toservfd;
  int size;
  char *srcp;
  char *ptr;
  char buf[MAXBUF];
  rio_t rio;

  toservfd = Open_clientfd("localhost", "45807");

  sprintf(buf, "%s %s HTTP/1.0\r\n", method, uri);
  sprintf(buf, "%sHost: localhost\r\n", buf);
  sprintf(buf, "%s%s", buf, user_agent_hdr);
  sprintf(buf, "%sConnection: Close\r\n", buf);
  sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf);
  Rio_writen(toservfd, buf, strlen(buf));
  printf("Request headers :\n");
  printf("%s\n", buf);

  Rio_readinitb(&rio, toservfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  while (strcmp(buf, "\r\n"))
  {
    Rio_writen(fd, buf, strlen(buf));
    Rio_readlineb(&rio, buf, MAXLINE);
    if (strstr(buf, "Content-length"))
    {
      ptr = index(buf, ':');
      size = atoi(ptr + 1);
    }
  }
  if (!strcmp(method, "GET"))
  {
    Rio_writen(fd, "\r\n", strlen("\r\n"));
    srcp = (char *)malloc(size);
    Rio_readnb(&rio, srcp, size);
    Rio_writen(fd, srcp, size);
    free(srcp);
  }
  Close(toservfd);
}
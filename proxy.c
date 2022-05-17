#include <stdio.h>

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_conn_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_fmt = "Host: %s\r\n";
static const char *request_hdr_fmt = "%s %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *conn_key = "Connection";
static const char *user_agent_key = "User-Agent";
static const char *prox_conn_key = "Proxy-Connection";
static const char *host_key = "Host";

/* Prototypes */
void doit(int fd);                                                                                      /* --------------- 연결된 client에 대해서 유효성 확인 및 요청에 대한 응답 처리 --------------- */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);                     /* --------------- 확실한 error에 대해서 처리하여 client에게 응답함 --------------- */
void serve(int fd, char *method, char *uri, char *version, rio_t *rio);                                 /* 서버로 요청 및 응답받은 내용 반환 */
void parse_uri(char *uri, char *hostname, int *port, char *path);                                       /* uri로부터 hostname, port, path파싱 */
void build_requesthdrs(char *http_header, char *method, char *hostname, char *path, rio_t *client_rio); /* endserver로의 request를 위해 header 작성 */
int Open_endServer(char *hostname, int port);                                                           /* 파싱한 port가 int형이기 때문에 문자열로 변환 및 endserver와 연결 */

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

/* --------------- 연결된 client에 대해서 유효성 확인 및 요청에 대한 응답 처리 --------------- */
void doit(int fd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // 요청내용 저장 buf, buf로부터 요청 method, uri, version 파싱하여 저장
  rio_t rio;                                                          // Client와 소통에서의 버퍼가 들어있는 rio 구조체

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);           // socket를 통해 읽어오기 위한 초기화
  Rio_readlineb(&rio, buf, MAXLINE); // 초기화된 rio를 통해서 요청내용을 buf에 저장
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);                                              // buf로부터 method, uri, version 파싱
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))                                // GET or HEAD만 요청시 응답
  {                                                                                           // method가 GET이 아니면 0이 아닌 수 반환됨
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method"); // 클라이언트로 에러 Response 응답하는 함수 호출
    return;
  }

  serve(fd, method, uri, version, &rio); // 엔드 서버로 요청을 보내 데이터를 처리하고, 응답받은 내용을 클라이언트에게 다시 전달
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

/* 서버로 요청 및 응답받은 내용 반환 */
void serve(int fd, char *method, char *uri, char *version, rio_t *rio)
{
  int endserverfd;  // endserver 소켓
  char *ptr;        // 필요시 response body 부분 처리하기 위한 ptr
  char buf[MAXBUF]; // 서버로부터 읽고, 클라이언트한테 쓰기 위한 버퍼
  rio_t serv_rio;   // 리오 버퍼

  // uri 파싱
  char hostname[MAXLINE], path[MAXLINE];
  int port;
  parse_uri(uri, hostname, &port, path);

  // request headers 작성
  char request_hdrs[MAXLINE];
  build_requesthdrs(request_hdrs, method, hostname, path, rio);

  // end server 연결하고 request 보내기
  endserverfd = Open_endServer(hostname, port); // 서버로 연결
  Rio_writen(endserverfd, request_hdrs, strlen(request_hdrs));

  int size;   // response body size
  char *srcp; // response body 저장할 pointer
  /* 응답받은 내용 클라이언트로 forwarding */
  Rio_readinitb(&serv_rio, endserverfd);
  Rio_readlineb(&serv_rio, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) // response header forwarding
  {
    Rio_writen(fd, buf, strlen(buf));
    Rio_readlineb(&serv_rio, buf, MAXLINE);
    if (strstr(buf, "Content-length"))
    {
      ptr = index(buf, ':');
      size = atoi(ptr + 1);
    }
  }

  // GET일 경우에만 Response Body 부분 처리 (없으면 HEAD요청시에도 실행되어 불필요한 부분 참조하게됨)
  if (!strcmp(method, "GET"))
  {
    Rio_writen(fd, buf, strlen(buf));
    srcp = (char *)malloc(size);
    Rio_readnb(&serv_rio, srcp, size);
    Rio_writen(fd, srcp, size);
    free(srcp);
  }
  Close(endserverfd);
}

// URI Parsing - request header로 들어온 uri에서 hostname, port, path 추출
void parse_uri(char *uri, char *hostname, int *port, char *path)
{
  *port = 80; // HTTP 기본포트 80으로 default 세팅

  char *ptr;
  ptr = strstr(uri, "//");   // http:// 이후부분으로 파싱
  ptr = ptr ? ptr + 2 : uri; // http:// 없어도 문제 없음

  char *ptr2 = strchr(ptr, ':'); // 포트번호
  if (ptr2)                      // 있으면
  {
    *ptr2 = '\0';                         // eof넣고
    sscanf(ptr, "%s", hostname);          // \0전까지 읽어서 hostname 파싱
    sscanf(ptr2 + 1, "%d%s", port, path); // 나머지 port, path 파싱
  }
  else // 없으면
  {
    ptr2 = strchr(ptr, '/'); // hostname 뒤 / 위치
    if (ptr2)                // 있으면
    {
      *ptr2 = '\0';
      sscanf(ptr, "%s", hostname);
      *ptr2 = '/';
      sscanf(ptr2, "%s", path); // 포트번호 없을때는 뒤에 바로 path
    }
    else                          // 없으면
      scanf(ptr, "%s", hostname); // path없으면 바로 hostname으로 파싱
  }
}

/* endserver로의 request를 위해 header 작성 */
void build_requesthdrs(char *http_header, char *method, char *hostname, char *path, rio_t *client_rio)
{
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];

  // Request Header 첫번째줄 세팅
  sprintf(request_hdr, request_hdr_fmt, method, path);

  // 클라이언트로부터 읽어오면서 데이터가 있는동안
  while (Rio_readlineb(client_rio, buf, MAXLINE))
  {
    // 끝에 도달했으면 멈춤
    if (!strcmp(buf, endof_hdr))
      break;

    // Host : 가 있는 경우 처리
    if (!strncasecmp(buf, host_key, strlen(host_key)))
    {
      strcpy(host_hdr, buf);
      continue;
    }

    // 나머지 header 처리 - conn / prox conn / user agent 부분은 이미 저장되어있는 fmt 대로 쓸거라서 제외
    if (strncasecmp(buf, conn_key, strlen(conn_key)) && strncasecmp(buf, prox_conn_key, strlen(prox_conn_key)) && strncasecmp(buf, user_agent_key, strlen(user_agent_key)))
      strcat(other_hdr, buf);
  }
  if (!strlen(host_hdr))
    sprintf(host_hdr, host_hdr_fmt, hostname);

  // 한번에 모아서 http_header에 저장
  sprintf(http_header, "%s%s%s%s%s%s%s", request_hdr, host_hdr, conn_hdr, prox_conn_hdr, user_agent_hdr, other_hdr, endof_hdr);
  return;
}

/* 파싱한 port가 int형이기 때문에 문자열로 변환 및 endserver와 연결 */
inline int Open_endServer(char *hostname, int port)
{
  char portStr[100];
  sprintf(portStr, "%d", port);
  return Open_clientfd(hostname, portStr);
}
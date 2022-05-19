/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);                                                       // 연결된 client에 대해서 한 HTTP 트랜잭션 처리
void read_requesthdrs(rio_t *rp);                                        // 불필요한 request header 부분 버림
int parse_uri(char *uri, char *filename, char *cgiargs);                 // URI 분석하여 filename과 cgiargs를 적절히 parsing하고, 정적 요청인지 동적 요청인지 판단하여 리턴함
void serve_static(int fd, char *filename, int filesize, char *method);   // 정적 컨텐츠를 처리함
void get_filetype(char *filename, char *filetype);                       // file의 type을 확인하여 filetype에 저장함
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method); // 동적 컨텐츠를 처리함
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg); // 확실한 error에 대해서 처리하여 client에게 응답함

/* --------------- main --------------- */
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

  // 프록시 구현시 프록시 서버에서 오는것만 받을 수 있도록 수정해 줄 필요 있어보임?
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
  return 0;
}

/* --------------- 연결된 client에 대해서 한 HTTP 트랜잭션 처리 --------------- */
void doit(int fd)
{
  int is_static;                                                      // 정적 or 동적 요청 확인 플래그
  struct stat sbuf;                                                   // 파일의 정보 저장하기 위한 stat 구조체
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

  /* Parse URI from GET request */
  is_static = parse_uri(uri, filename, cgiargs); // uri 파싱 및 정적/동적 판단
  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static) // Serve static content
  {
    // S_ISREG : 일반 파일인지 확인, S_IRUSR : 읽기권한이 있는지 확인. (st_mode에 파일의 종류와 퍼미션 저장됨)
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file"); // 일반 파일이 아니거나 권한이 없으면 에러처리
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method); // 정적 요청에 대한 처리 함수 호출
  }
  else // Serve dynamic content
  {
    // 좌측 상기 동일, 우측 S_IXUSR : 실행권한이 있는지 확인.
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbiddnen", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method); // 동적 요청에 대한 처리 함수 호출
  }
}

/* --------------- 확실한 error에 대해서 처리하여 client에게 응답함 --------------- */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  // sprintf는 buffer 변수에 내용을 '덮어씀' -> 중첩해서 작성
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "fffff"
                ">\r\n",
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

// URI 분석하여 filename과 cgiargs를 적절히 parsing하고, 정적 요청인지 동적 요청인지 판단하여 리턴함
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin")) // Static content인 경우 분기
  {
    strcpy(cgiargs, "");
    strcpy(filename, ".");           // 루트부분 .
    strcat(filename, uri);           // 루트 이후부분 붙여줌 (cpy는 덮어쓰므로 cat으로 이어붙여줌)
    if (uri[strlen(uri) - 1] == '/') // 파일 이름이 명시되지 않은 경우 home.html로 연결해줌
      strcat(filename, "home.html");
    return 1; // 정적일 경우 1 리턴
  }
  else // Dynamic content인 경우
  {
    ptr = index(uri, '?'); // ? 이후는 cgiarg임
    if (ptr)               // arg 있는 경우
    {
      strcpy(cgiargs, ptr + 1); // cgiargs에 저장
      *ptr = '\0';              // 해당위치에 eof 넣어주어서 filename에 uri 넣을때 args 빼고 넣도록 함
    }
    else
      strcpy(cgiargs, "");
    strcpy(filename, "."); // 루트부분 .
    strcat(filename, uri); // 루트 이후부분 & args 제외하고 이어붙임
    return 0;              // 동적일 경우 0 리턴
  }
}

/* --------------- 정적 컨텐츠를 처리함 --------------- */
void serve_static(int fd, char *filename, int filesize, char *method)
{
  int srcfd;                                  // 소스파일 discriptor
  char *srcp, filetype[MAXLINE], buf[MAXBUF]; // 소스파일을 메모리에 올린 후 메모리의 포인터 srcp, file 유형, 출력버퍼 buf
  rio_t rio;

  /* Send response headers to clinet */
  get_filetype(filename, filetype); // filename으로부터 파일 확장자 확인하여 return해줄 content-type 세팅함
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf)); // response header를 클라이언트로 보내줌
  printf("Response headers:\n");    // 서버쪽 출력
  printf("%s", buf);

  if (!strcasecmp(method, "GET"))
  {
    /* Send response body to client if method is GET */
    srcfd = Open(filename, O_RDONLY, 0); // 읽기 권한으로 파일 open
    // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // open된 파일을 메모리 영역으로 mapping
    srcp = malloc(filesize);          // mmap대신 malloc 사용으로 변경
    Rio_readinitb(&rio, srcfd);       // rio와 srcfd 연결하여 버퍼 생성
    Rio_readnb(&rio, srcp, filesize); // malloc으로 할당받은 영역으로 파일 읽어들임
    Close(srcfd);                     // 파일 내용 메모리로 다 올렸으니까 file discriptor 반납
    Rio_writen(fd, srcp, filesize);   // 파일내용 그대로 클라이언트로 보내줌
    // Munmap(srcp, filesize);         // 내용 다 보냈으니까 메모리 반납
    free(srcp);
  }
}

/* --------------- file의 type을 확인하여 filetype에 저장함 --------------- */
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mpeg"))
    strcpy(filetype, "video/mpeg");
  else
    strcpy(filetype, "text/plain");
}

/* --------------- 동적 컨텐츠를 처리함 --------------- */
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method)
{
  char buf[MAXLINE], *arglist[] = {NULL}; // 출력 버퍼, 파일 실행시 args인자로 넣을 빈 리스트 (어차피 인자들은 환경변수로 전달됨)

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) // 자식프로세스 생성. 자식프로세스만 if문 진입
  {
    setenv("QUERY_STRING", cgiargs, 1); // cgiargs를 자식프로세스의 QUERY STRING 환경변수에 등록해줌
    setenv("METHOD", method, 1);        // method도 환경변수로 전달
    // 자식프로세스의 stout 파일식별자를 fd(클라이언트 소켓)으로 덮어써줌
    // -> 실행되는 프로그램에서는 실행시키는 프로세스에서의 클라이언트 소켓을 알 수 없으므로 stout에 클라이언트 소켓을 세팅하여 stout으로 출력시 클라이언트에게 응답이 가도록 함
    Dup2(fd, STDOUT_FILENO);
    Execve(filename, arglist, environ); // 컨텐츠 실행함
  }
  Wait(NULL); // 자식프로세스 종료되기까지 기다림
}

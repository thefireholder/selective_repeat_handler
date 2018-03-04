#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h> //types for socket.h and netinet/in.h
#include <sys/socket.h>  // structures for sockets
#include <strings.h> //bzero bcopy
#include <stdlib.h>

#define BUFSIZE 2048
#define ARGMSG "argument failure: ./client hostname port# file"

int debug = 0; //when on, prints more detail to stderr msg.

int reportError(char* msg, int errorCode)
//print error msg  and exit with error code
//1: general error (buffer overflow)
//2: network connection error
//3: protocol error
{
  fprintf(stderr,"Error: %s\n", msg);
  exit(errorCode);
}

int main(int argc, char *argv[])
{
  struct sockaddr_in serv_addr;

  //argument check
  if (argc < 4) reportError(ARGMSG,1);
  if (argc > 4) debug = 1;

  //get host name and port
  int port = atoi(argv[2]);
  struct hostent *host = gethostbyname(argv[1]);
  if (host==NULL) reportError("no such host",2);

  //initialize socket
  int sockfd = socket(PF_INET, SOCK_DGRAM, 0);
  if (sockfd == -1) reportError("socket creation failed", 2);

  //initialize serv_addr
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy((char *)host->h_addr, (char *)&serv_addr.sin_addr.s_addr, host->h_length);
  serv_addr.sin_port = htons(port);

  //UDP needs no connection, transmit msg from here
  char buffer[BUFSIZE] = "hello world";
  int n = strlen(buffer);
  n = sendto(sockfd, buffer, n, 0
	     ,(struct sockaddr *)&serv_addr
	     , sizeof(serv_addr));
  if (n < 0) reportError("sendto failed", 2);
}

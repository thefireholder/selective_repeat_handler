
#include <stdio.h>
#include <sys/types.h> //types for socket.h and netinet/in.h
#include <sys/socket.h>  // structures for sockets
#include <netinet/in.h>  // structures for sockaddr_in
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>

#define BUFSIZE 2048

//show greater detail if debug flag is on
int debug = 0; 

int reportError(char* msg, int errorCode)
//report error according to msg and exit with errorcode
//1: general technical difficulty (i.e wrong argc, allocation error)
//2: network connectivity
//3: synchronization error (i.e. ack num mismatch)
{
  fprintf(stderr,"Error: %s\n", msg);
  exit(errorCode);
}

int main(int argc, char * argv[])
{
  //address structures
  struct sockaddr_in modelA;
  struct sockaddr_in clientA;

  //argument handler
  if (argc < 2) reportError("argument failure: ./server portNumber", 1);
  if (argc > 2) debug = 1;
  int port = atoi(argv[1]);
  if (debug) fprintf(stderr, "Server Port: %d\n", port);
  
  //create and bind socket
  int sockfd = socket(PF_INET, SOCK_DGRAM, 0); //ipv4, UDP
  if (sockfd == -1) reportError("socket creation failed", 2);
  
  modelA.sin_family = AF_INET; //communication domain
  modelA.sin_addr.s_addr = INADDR_ANY; // ip (accepts any internet address)
  modelA.sin_port = htons(port); //server port

  if (bind(sockfd, (struct sockaddr *) &modelA, sizeof(modelA)) < 0)
    reportError("socket binding failed",2);

  //no listening since no SYNC!
  //hence can start recv/send msg!

  while(1) 
  {
    //receive client msg
    char buffer[BUFSIZE]; socklen_t clientA_len; //stores clientA length

    if (debug) fprintf(stderr, "waiting for msg\n");
    int recvlen = recvfrom(sockfd, buffer, BUFSIZE, 0
			   ,(struct sockaddr *) &clientA, &clientA_len);
    
    if (debug) { //printing client ip & port 
      unsigned char *ip = (unsigned char *)&(clientA.sin_addr.s_addr); 
      fprintf(stderr, "Found client %d.%d.%d.%d:%d\n"
	      ,clientA.sin_addr.s_addr, ip[1], ip[2], ip[3]
	      ,ntohs(((struct sockaddr_in)clientA).sin_port));
    }

    //read
    if(recvlen >= 2048) reportError("Buffer overflow", 1);
    if(recvlen > 0) {
      if (debug) fprintf(stderr, "Message from client:\n");
      buffer[recvlen]=0;
      //printf("%s",buffer);
      //printf("okay %d msg is here: [%s] \n", recvlen, buffer);
      printf("%s\n\n", buffer);
    }
  }
}




#include <stdio.h>
#include <sys/types.h> //types for socket.h and netinet/in.h
#include <sys/socket.h>  // structures for sockets
#include <netinet/in.h>  // structures for sockaddr_in
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#define BUFSIZE 2048
#define HSIZE 4 //header size
#define ACK 1
#define SYN 2
#define FIN 4
#define FOF 8
#define PACKETSIZE 1024
#define RTO 1

//show greater detail if debug flag is on
int debug = 0; 

union header
{
  char bytes[4];
  struct fields {
    unsigned short seq;        //seq number
    unsigned short flags_size; //flag (4bit) + size of payload & header (10bit) << 4                                           
  } fields;
};

int reportError(char* msg, int errorCode)
//report error according to msg and exit with errorcode
//1: general technical difficulty (i.e wrong argc, allocation error)
//2: network connectivity
//3: synchronization error (i.e. ack num mismatch)
{
  fprintf(stderr,"Error: %s\n", msg);
  exit(errorCode);
}

int parseMsg(char* msg, char*payload, int* flags, int* seq, int n)
//given headed msg, parse msg (find size from header)
//return payload size (if only header is sent, return 0)
{
  union header h; int size;
  memcpy(h.bytes, msg, HSIZE);

  unsigned int flags_size = h.fields.flags_size;
  size = (flags_size >> 4) - HSIZE; //payload size
  *flags = flags_size & 15;
  *seq = h.fields.seq;
  if (n - HSIZE != size) {
    // doesn't match!
    return -1;
  }
  memcpy(payload, msg + HSIZE, size);
  return size;
}

int main(int argc, char * argv[])
{
  //address structures
  fprintf(stderr, "hello\n");
  struct sockaddr_in modelA;
  struct sockaddr_storage clientA; //note I didn't use sockaddr_in!

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

  // getting filename here
  char filename[BUFSIZE-HSIZE];
  socklen_t clientA_len;

  char msg[BUFSIZE]; char payload[BUFSIZE-HSIZE]; 

  while(1) {
    //receive client msg
    int flags; int seq; 

    if (debug) fprintf(stderr, ">waiting for msg\n");
    fprintf(stderr, "waiting\n");
    int n = recvfrom(sockfd, msg, BUFSIZE, 0,(struct sockaddr *) &clientA, &clientA_len); // 1 dgram only

    fprintf(stderr, "got client msg\n");
    //check for error & parse msg
    if(n >= BUFSIZE) reportError("Buffer overflow", 1);
    if(n < HSIZE) reportError("recvfrom error", 2);

    fprintf(stderr, "%d\n", n);
    int payloadSize = parseMsg(msg, payload, &flags, &seq, n);
    if (payloadSize < 0) {
      fprintf(stderr, "Got nonmatching error\n");
      continue;
    }
    
    //read payload
    payload[payloadSize]=0;
    printf("Got filename: %d, %s\n\n", payloadSize, payload);
    
    memcpy(filename, payload, payloadSize);
    break;

    //send msg
    // if (debug) fprintf(stderr,"sending msg: %s\n", payload);
    // while(sendto(sockfd, msg, n+4, 0,(struct sockaddr *)&clientA, clientA_len)<0);
    // if (debug) fprintf(stderr,"sent\n");
  }

  // sending ACK for filename until SYN
  union header ACK_filename;
  if (access(payload, F_OK) != -1){
    ACK_filename.fields.flags_size = ACK;
  } else {
    ACK_filename.fields.flags_size = FOF;
  }

  int timeout_sockfd = socket(PF_INET, SOCK_DGRAM, 0);
  struct timeval tv;
  tv.tv_sec = 2 * RTO;
  tv.tv_usec = 0;
  setsockopt(timeout_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

  while(1) {
    while(sendto(timeout_sockfd, &ACK_filename, sizeof(union header), 0,(struct sockaddr *)&clientA, clientA_len) <= sizeof(union header));
    int rcved = recvfrom(timeout_sockfd, msg, sizeof(union header), 0, (struct sockaddr *)&clientA, &clientA_len);
    
    if (rcved < HSIZE) {
          fprintf(stderr, "%d\n", rcved);
          fprintf(stderr, "timeout\n");

      continue;
    }
    int flags; int seq;
    int payloadSize = parseMsg(msg, payload, &flags, &seq, rcved);
    if(payloadSize >= 0 && flags == SYN) {
      fprintf(stderr, "yay\n");
      break;
    }
  }
  
  // make SYNAck



}



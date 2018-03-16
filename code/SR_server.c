
#include <stdio.h>
#include <sys/types.h> //types for socket.h and netinet/in.h
#include <sys/socket.h>  // structures for sockets
#include <netinet/in.h>  // structures for sockaddr_in
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>


#define BUFSIZE 2048
#define HSIZE 4 //header size
#define ACK 1
#define SYN 2
#define FIN 4
#define FOF 8
#define PACKETSIZE 1024
#define RTO 1
#define WND 5120
// TODO: make sure mod 30720
#define MAXSEQ 30720

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

int formatMsg(char* n_msg, char* o_msg, int o_msgSize, int seq, int flags)
{
    //find o_msg Length
    if(o_msgSize == -1) o_msgSize = strlen(o_msg);
    
    //set header field
    union header h;
    h.fields.seq = seq;
    h.fields.flags_size = flags + ((o_msgSize+4)<<4);
    
    //copy header + payload into n_msg
    memcpy(n_msg, h.bytes, 4);
    memcpy(n_msg+4, o_msg, o_msgSize);
    
    return o_msgSize+4;
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

int in_range(int base_seq, int seq) {

  int i;
  for(i=0;i<5;i++){
    if(seq==base_seq)
      return i;
    base_seq += PACKETSIZE;
    if(base_seq > MAXSEQ)
      base_seq = 0;
  }
  return -1;
}

int cycle(int seq, int num_pack) {
  int i;
  for(i=0;i<num_pack;i++){
    seq += PACKETSIZE;
    if(seq > MAXSEQ)
      seq = 0;
  }
  return seq;
}

int try_fill(int fd, char* file_buf) {
  // return how many in payload. < PACKETSIZE - HSIZE if less
  int num_read = 0;
  while(num_read < PACKETSIZE - HSIZE) {
    int r = read(fd, file_buf + num_read, PACKETSIZE - HSIZE - num_read);
    if(r < 0){
      reportError("Error reading file\n", 2);
    }
    if(r==0){
      break; //eof
    }
    num_read+=r;
  }
  return num_read;
}


struct ent {
  char arr[PACKETSIZE];
  int length;
};
struct ent ents[5];

unsigned long times[5] = {0};

unsigned long now() {
  struct timespec cur;
  clock_gettime(CLOCK_REALTIME, &cur);
  return cur.tv_sec * 1000 + cur.tv_nsec/1000000;
}

// void add_timer() {
//   if(num_times>=5)
//     return;
//   struct timespec cur;
//   clock_gettime(CLOCK_REALTIME, &cur);
//   times[num_times] = cur.tv_sec * 1000 + cur.tv_nsec/1000000;
//   if(num_times==0){
//     struct itimerval add;
//     set_time(&add, 500);
//   }
//   num_times++;
// }

int main(int argc, char * argv[])
{
  setbuf(stdout, NULL);
  //address structures
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
    int n = recvfrom(sockfd, msg, BUFSIZE, 0,(struct sockaddr *) &clientA, &clientA_len); // 1 dgram only

    //check for error & parse msg
    if(n >= BUFSIZE) reportError("Buffer overflow", 1);
    if(n < HSIZE) reportError("recvfrom error", 2);

    int payloadSize = parseMsg(msg, payload, &flags, &seq, n);
    if (payloadSize < 0) {
      continue;
    }
    
    //read payload
    payload[payloadSize]=0;
    
    memcpy(filename, payload, payloadSize);
    break;

    //send msg
    // if (debug) fprintf(stderr,"sending msg: %s\n", payload);
    // while(sendto(sockfd, msg, n+4, 0,(struct sockaddr *)&clientA, clientA_len)<0);
    // if (debug) fprintf(stderr,"sent\n");
  }

  // sending ACK for filename until SYN
  char ACK_filename[4];
  if (access(payload, F_OK) != -1){
    formatMsg(ACK_filename, "", 0, 0, ACK);
  } else {
    formatMsg(ACK_filename, "", 0, 0, FOF | ACK | FIN);
    while(1) {
      int sent;
      while((sent = sendto(sockfd, ACK_filename, sizeof(union header), 0,(struct sockaddr *)&clientA, clientA_len)) < sizeof(union header)){
      }
      char client_finack[4];
      int rcved = recvfrom(sockfd, client_finack, HSIZE, MSG_DONTWAIT, (struct sockaddr *)&clientA, &clientA_len);
      if(rcved < HSIZE)
        continue;
      char payl;
      int flags; int seq;
      int payload_size = parseMsg(client_finack, &payl, &flags, &seq, HSIZE);
      if(payload_size>=0 && (flags & FIN) && (flags & ACK)){
        exit(0);
      }
    }
    // TODO: 404 = send 404 ACK and then go "expect close" method
  }

  // set up timeout_sockfd
  // int timeout_sockfd = socket(PF_INET, SOCK_DGRAM, 0);
  // if (timeout_sockfd == -1) reportError("socket creation failed", 2);
  // struct sockaddr_in timeout_model;
  // timeout_model.sin_family = AF_INET; //communication domain
  // timeout_model.sin_addr.s_addr = INADDR_ANY; // ip (accepts any internet address)
  // timeout_model.sin_port = htons(port); //server port
  // if (bind(timeout_sockfd, (struct sockaddr *) &timeout_model, sizeof(timeout_model)) < 0)
  //   reportError("socket binding failed",2);
  // struct timeval tv;
  // tv.tv_sec = 2 * RTO;
  // tv.tv_usec = 0;
  // setsockopt(timeout_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
  //

  while(1) {
    int sent;
    while((sent = sendto(sockfd, ACK_filename, sizeof(union header), 0,(struct sockaddr *)&clientA, clientA_len)) < sizeof(union header)) {
    }
    //sleep(2 * RTO);

    int rcved = recvfrom(sockfd, msg, sizeof(union header), MSG_DONTWAIT, (struct sockaddr *)&clientA, &clientA_len);

    if (rcved < HSIZE) {
      continue;
    }
    int flags; int seq;
    int payloadSize = parseMsg(msg, payload, &flags, &seq, rcved);
    if(payloadSize >= 0 && (flags & SYN)) {
      break;
    }
  }
  // end of filename transfer

  int seqnum = 0;

  // make SYNAck (seqnum = 0)
  char SYNack[4];
  formatMsg(SYNack, "", 0, 0, ACK | SYN);
  while (1) {
    int sent;
    while((sent = sendto(sockfd, SYNack, sizeof(union header), seqnum,(struct sockaddr *)&clientA, clientA_len)) < sizeof(union header)) {
    }
    //fprintf(stderr, "sent: %d\n", sent);
    //fprintf(stderr, "here synack\n");
    printf("Sending packet %d %d SYN\n", 0, WND);

    //sleep(2 * RTO);
    int rcved = recvfrom(sockfd, msg, sizeof(union header), MSG_DONTWAIT, (struct sockaddr *)&clientA, &clientA_len);

    if (rcved < HSIZE) {
      continue;
    }
    int flags; int seq;
    int payloadSize = parseMsg(msg, payload, &flags, &seq, rcved);
    if(payloadSize >= 0 && flags == ACK && seq == 0) {
      //fprintf(stderr, "yay got ACK for SYNACK\n");
      printf("Receiving packet %d\n", 0);
      seqnum += 4; // 1 byte SYN
      break;
    }
  }

  // send file chunks seqnumstart = 1
  int file_fd = open(filename, O_RDONLY);
  char file_buf[PACKETSIZE - HSIZE]; // max payload
  int window[5] = {0};
  // window timers here
  int base_seq = seqnum;
  int done = -1; //last seq
  int wind = 0;
  while (1) {
    if (done < 0 && wind < 5) {
      if (window[wind]==0) {
        int pack_payload = try_fill(file_fd, file_buf);
        //int cur_sn = (base_seq + wind * PACKETSIZE) % MAXSEQ;

        int cur_sn = cycle(base_seq, wind);

        if(pack_payload < PACKETSIZE - HSIZE) {done = cur_sn;}
        //fprintf(stderr, "pack_payload: %d\n", pack_payload);
        char send_msg[HSIZE + pack_payload];
        int is_fin = 0;
        if(done > 0) {
          formatMsg(send_msg, file_buf, pack_payload, cur_sn, FIN);
          is_fin = 1;
        } else {
          formatMsg(send_msg, file_buf, pack_payload, cur_sn, 0);
        }
        int sent;
        while((sent = sendto(sockfd, send_msg, HSIZE+pack_payload, 0,(struct sockaddr *)&clientA, clientA_len)) < HSIZE+pack_payload) {
          //fprintf(stderr, "sent: %d\n", sent);
        }
        if(is_fin){
          printf("Sending packet %d %d FIN\n", cur_sn, WND);
        } else {
          printf("Sending packet %d %d\n", cur_sn, WND);
        }
        //fprintf(stderr, "sent seq: %d\n", seqnum);
        //change next seqnum
        times[wind] = now() + 500;
        memcpy(ents[wind].arr, send_msg, HSIZE+pack_payload);
        ents[wind].length = HSIZE+pack_payload;

        window[wind] = 1; //sent not ack
      }
      wind ++;
    } else {
      //done seq out of range
      if(done >= 0){
        int i;
        int brek=1;
        for(i=0;i<5;i++){
          if(window[i]!=0) brek=0;
        }
        if(brek)
          break;
      }
      char client_ACK[HSIZE];
      //wait & recv
      //printf("waiting for ACK\n");
      int rcved = recvfrom(sockfd, client_ACK, HSIZE, MSG_DONTWAIT, (struct sockaddr *)&clientA, &clientA_len);
      if(rcved >= 0) {
          
        //fprintf(stderr, "rcved: %d\n", rcved);
        char client_payload;
        int flags; int seq;
        int payload_size = parseMsg(client_ACK, &client_payload, &flags, &seq, rcved);
        //fprintf(stderr, "flags: %d\n", flags);
        if(payload_size < 0) reportError("not matching!\n", 2);
        // ignore the rest
        int got_index = in_range(base_seq, seq);
        if((flags & FIN) && got_index != -1) {
        }
        if((flags & ACK) &&  got_index != -1){
          printf("Receiving packet %d\n", seq);
          // int dist = (seq + MAXSEQ - base_seq) % MAXSEQ;
          int mark_ind = got_index; //dist / (PACKETSIZE);
          window[mark_ind] = 2;

          times[mark_ind] = 0;

          if(mark_ind == 0){
            // move forward
            int i;
            for(i=0;i<5;i++){
              if(window[i]!=2) break;
            }
            // TODO: add save for timers
            if(i<5) {
              memmove(window, window+i, (5-i) * sizeof(int));
              memmove(times, times+i, (5-i) * sizeof(unsigned long));
              memmove(ents, ents+i, (5-i) * sizeof(struct ent));
            }
            int j;
            for(j=5-i;j<5;j++){
              window[j]=0;
              times[j]=0;
            }
            wind = 0;
            //base_seq = (base_seq + i*(PACKETSIZE)) % MAXSEQ;
            base_seq = cycle(base_seq, i);
          }
        }
      }
    }

    //check times here
    unsigned long n = now();
    int x;
    for(x=0;x<5;x++){
      //printf("%lu ", times[x]);
      if(times[x] != 0 && n >= times[x] && window[x] == 1){
        while(sendto(sockfd, ents[x].arr, ents[x].length, 0,(struct sockaddr *)&clientA, clientA_len) < ents[x].length){
	      }
        times[x] = now() + 500;
        printf("Sending packet %d 5120 Retransmission\n", cycle(base_seq, x));
      }
    }
    //printf("\n");

  }

}



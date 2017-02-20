#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <strings.h>
#include <unistd.h>
#include "uftp.h"

#define MAX_LINE    256
#define MAX_CLIENTS 16
int main(int argc, char* argv[]){
  FILE *fp;
  struct hostent *host;
  struct sockaddr_in sin;
  struct sockaddr_in client_sin;
  seqnum_t lfr =0;
  seqnum_t lfa =0;
  seqnum_t rws;
  seqnum_t seq_max = 8;
  socklen_t addr_len = sizeof(sin);
  unsigned int port; // port number to communicate with server
  socklen_t len;
  int s;
  int recvlen;
  int new_s;
  int first_time = 1;
  int drop_prob = 3; // for testing purposes
  struct frame frame;
  struct frame frame_buf[BUF_SIZE];
  struct ack ack_frame;
  int i;
  int got_eof=0;
  rws = 4;
  seq_max = 8;

  srand(123);
  
  /* Initialize the buffer */
  for(i = 0; i < BUF_SIZE; i++){
    frame_buf[i].type = EMPTY;
  }
  
  // Read Port Number from argv[1]
  port = atoi(argv[1]);

  /* Populate address fields */
  bzero((char *)&sin, sizeof(sin)); // Clear all fields
  sin.sin_family = AF_INET;         // Use ip
  sin.sin_addr.s_addr = INADDR_ANY; // ??
  sin.sin_port = htons(port);// Set the listening port

  /* Open the port and start listening */
  if ((s = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("clouldn't get a socket");
    exit(1);
  }
  
  if ((bind(s, (struct sockaddr *) &sin, sizeof(sin))) < 0) {
      perror("clouldn't bind socket");
      exit(1);
  }
    
  /* Receive the incoming file */
  while(1){
    /* Loop until a message with nonzero length is received */
    while(recvfrom(s, &frame, sizeof(struct frame), 0,
		   (struct sockaddr *) &client_sin, &addr_len) <= 0);
    
    /* Check if the mesage is in the permitted window. Ignore frames outside of 
       window. */
    
    /* Compose and send an ack */    
    ack_frame.type = ACK;
    ack_frame.seq = frame.seq;

    /* Send an ack for every frame */
    if(sendto(s, (char*) &ack_frame, sizeof(struct ack), 0,
	      (struct sockaddr *) &client_sin, sizeof(client_sin) ) < 0){
      perror("Send To failed");
      exit(1);
    }
 
    /* Ignore the frame if it is outside the receive window */
    if(!seq_ok((lfa+1) % seq_max, rws, frame.seq, seq_max)){     
      continue;
    }

    /* Write this frame to the buffer */
    frame_buf[frame.seq % BUF_SIZE] = frame;

    /* Update LFR if this is the next frame expected */
    if(frame.seq == (lfa+1)%seq_max){
      do{
	lfa = (lfa+1)%seq_max;
	if(frame_buf[lfa%BUF_SIZE].type == DATA){
	  frame_buf[lfa%BUF_SIZE].type = EMPTY;
	  got_eof = write_frame(fp, &(frame_buf[lfa%BUF_SIZE]));
	}
	else if(frame_buf[lfa%BUF_SIZE].type == FT_REQ){
	  frame_buf[lfa%BUF_SIZE].type = EMPTY;
	  if((fp = fopen((char*) &frame_buf[lfa%BUF_SIZE].data, "wb")) == NULL){
	    fprintf(stderr, "Couldn't open file.");
	    exit(1);
	  }	  
	}
      }
      while(frame_buf[((lfa+1)%seq_max)%BUF_SIZE].type == DATA);
    }
    
    if(got_eof == 1){
      if(fp != NULL){
	fclose(fp);
	fp = NULL;
      }
      printf("The file has been transferred. Please press ctrl z to exit.\n"); 
      //break;
    }
  }
        
  return 0;
}

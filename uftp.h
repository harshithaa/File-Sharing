#ifndef UFTP_H
#define UFTP_H
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

#define DATA_SIZE 256 // in bytes.
#define MAX_PENDING 256
#define BUF_SIZE 64
/* Frame Flags */
typedef enum{EMPTY, DATA, ACK, FT_REQ} frametype_t; // Frame Types
typedef enum{OK, ERROR} reqStatus_t;                // Frame delivery success?
typedef uint32_t seqnum_t;                          // Type for Sequence number

/* Format for frame sent over the network */
struct frame{
  frametype_t type;
  seqnum_t seq;
  int eof_pos; // position of eof if there is one. else negative
  unsigned char data[DATA_SIZE];
};

/* Format for non-data frame */
struct ack{
  frametype_t type;
  seqnum_t seq;
};

/* Arguments for timeout thread */
struct timeout_args{
  int s;
  unsigned int duration;
  struct frame *frame; // pointer to frame to resend
  struct sockaddr_in remote_sin;
};

/* A slot in the send buffer: frame + metadata */
struct send_slot{
  struct timeout_args timeout_state;
  struct frame send_frame;
  pthread_t timeout;
  int has_ack; // boolean: has the frame in this slot been acked?
};

/* The state of the client, for use by recv_ack and send threads */
struct send_file_args{
  struct sockaddr_in remote_sin, local_sin;
  pthread_mutex_t mutex; // mutex to protect the state.
  int s; // socket id
  char *file_name;
  char *new_filename;
  seqnum_t lar; // last ack received
  seqnum_t lfs; // [seq_num of] last frame received
  seqnum_t sws; // send window size
  seqnum_t seq_max; // highest possible sequence number  
  struct send_slot frame_buf[BUF_SIZE]; // frame buffer
};

/****************************************************************************
 * Name: read_frame                                                         *
 * Arguments:                                                               *
 * Argument Name    type         Description                                *
 * -------------    ----         -----------                                *
 * fp               FILE*        A file pointer opened in "rb" mode         *
 * f                struct frame Holds the next block of data read from fp  *
 * seq              seqnum_t     sequence number for the new file           *
 *                                                                          *
 * RETURNS: 1 if EOF was reached while reading from fp, otherwise 0.        *
 ****************************************************************************/
int readFrame(FILE *fp, struct frame* f, seqnum_t seq){
  int i;
  f->seq = seq;
  f->type = DATA;
  f->eof_pos = -1;
  /* populate data field */
  for(i = 0; i < DATA_SIZE; i++){
    f->data[i] = fgetc(fp);
    if(feof(fp)){
      //printf("readFrame: EOF reached");
      f->eof_pos = i;
      return 1;
    }
  }    
  return 0;
}

/****************************************************************************
 * Name: write_frame                                                        *
 * Arguments:                                                               *
 * Argument Name    type          Description                               *
 * -------------    ----          -----------                               *
 * fp               FILE*         File Pointer to open in "wb" mode         *
 * f                struct frame  Struct whose data field is written to fp  *
 *                                                                          *
 * RETURNS: 1 if EOF was reached while writing to fp, otherwise 0.          *
 ****************************************************************************/
int write_frame(FILE *fp, struct frame *f){
  int i;
  int c;
  int num_bytes;
  int retval;
  
  if(f->eof_pos < 0){
    num_bytes = DATA_SIZE;
    retval = 0;
  }
  else{
    num_bytes = f->eof_pos;
    retval = 1;
  }
  
  for(i = 0; i < num_bytes; i++){
    c = f->data[i];    
    fputc(c, fp);
  }
  return retval; 
}

/****************************************************************************
 * Name: circ offset                                                        *
 * Arguments:                                                               *
 * Argument Name    type          Description                               *
 * -------------    ----          -----------                               *
 * first            int           The "low" integer of a range in a circular*
 *                                buffer.                                   *
 * last             int           The "high" index.                         *
 * seqnum           seqnum_t      size of a circular buffer                 *
 *                                                                          *
 * RETURNS: the number of slots in the circular range between high and low  *
 ****************************************************************************/
int circ_offset(int first, int last, int buf_size){
  if( first <= last )
    return last - first;
  return buf_size + last - first;
}

/****************************************************************************
 * Name: seq_ok                                                             *
 * Arguments:                                                               *
 * Argument Name    type          Description                               *
 * -------------    ----          -----------                               *
 * win_base         seqnum_t      The minimum alowable seqnum               *
 * win_size         seqnum_t      The size of the window of permissible nums*
 * frame_num        seqnum_t      The seqnum to be evalueated               *
 * seq_max          seqnum_t      The highest allowable sequence number.    *
 *                                                                          *
 * RETURNS: 1 if frame_num is in a window of size win_size begining at      *
 *          win_base.                                                       *
 ****************************************************************************/
int seq_ok(seqnum_t win_base, seqnum_t win_size, seqnum_t frame_num,
	   seqnum_t seq_max){
  //printf("in seq-ok\n");
  int retval;
  seqnum_t win_last = (win_base + win_size - 1) % seq_max;
  //printf("in seq-ok\n");
  if( win_base > win_last){
    //printf("base > base + size\n");
    retval = !(frame_num > win_last && frame_num < win_base);
  }
  else{
    //printf("base <= base + size\n");
    retval = frame_num <= win_last && frame_num >= win_base;
  }
  //printf("retval = %d\n", retval);
  return retval;
}

/****************************************************************************
 * Name: timeout                                                             *
 * Arguments:                                                               *
 * Argument Name    type          Description                               *
 * -------------    ----          -----------                               *
 * args             timeout_args* Parameters needed to perform the timeout
 *                                and resend a particular frame
 *
 *                                                                          *
 * RETURNS: NULL
 ****************************************************************************/
void* timeout(void *args){
  struct timeout_args *params = (struct timeout_args*) args;
  time_t resend_time;
 
  while(1){
    usleep(params->duration);
    if(sendto(params->s, (char*) params->frame, sizeof(struct frame), 0, (struct sockaddr *) &(params->remote_sin), sizeof(params->remote_sin) ) < 0){
      perror("Send To failed");
      exit(1);
    }
    
  }
  return NULL;
}

/****************************************************************************
 * Name: send_file                                                          *
 * Arguments:                                                               *
 * Argument Name    type          Description                               *
 * -------------    ----          -----------                               *
 * args             void*         Parameters containing the state of an ftp *
 *                                client.                                   *
 *                                                                          *
 * RETURNS: NULL                                                            *
 ****************************************************************************/
void* send_file(void *args){
  FILE *fp;
  seqnum_t i;
  seqnum_t send_seqnum;
  int got_eof;  
  struct send_file_args *state = (struct send_file_args*) args;

  /* Open the file */
  if((fp = fopen(state->file_name, "rb")) == NULL){
    fprintf(stderr, "Couldn't open file.");
    exit(1);
  }

  /* Send the first frame: FT Request */
  send_seqnum = (state->lfs + 1) % state->seq_max;

  i = send_seqnum % BUF_SIZE;

  state->frame_buf[i].send_frame.eof_pos = -1;
  state->frame_buf[i].send_frame.seq = send_seqnum;
  state->frame_buf[i].send_frame.type = FT_REQ;
  bcopy(state->new_filename, &(state->frame_buf[i].send_frame.data), strlen(state->new_filename) + 1);

  /* Add timeout to first frame */
  state->frame_buf[i].timeout_state.s = state->s;
  state->frame_buf[i].timeout_state.duration = 100000;
  state->frame_buf[i].timeout_state.frame = &(state->frame_buf[i].send_frame);
  state->frame_buf[i].timeout_state.remote_sin = state->remote_sin;
  pthread_create(&(state->frame_buf[i].timeout), NULL, timeout, &(state->frame_buf[i].timeout_state));

  /* Set has ack */
  state->frame_buf[i].has_ack = 0;

  state->lfs = send_seqnum;

  /* Actually send the frame */
  if(sendto(state->s, (char*) &(state->frame_buf[i].send_frame), sizeof(struct frame), 0, (struct sockaddr *) &(state->remote_sin), sizeof(state->remote_sin) ) < 0){
    perror("Send To failed");
    exit(1);
  }
  
  while(1){
    /* Check if it's permissible to send. if not, give up the processor.*/    
    //printf("send_file starts while loop\n");

    /* get the next seqnumber */
    send_seqnum = (state->lfs + 1) % state->seq_max;
    
   
    if(!seq_ok(((state->lar)+1) % state->seq_max, state->sws, send_seqnum, state->seq_max)){
      sched_yield();
      continue;
    }

    /* Add this frame to the queue */
    i = send_seqnum % BUF_SIZE;
    
    /* Read frame from file */
    got_eof = readFrame(fp, &(state->frame_buf[i].send_frame), send_seqnum);
   

    /* Add a timeout */
    state->frame_buf[i].timeout_state.s = state->s;
    state->frame_buf[i].timeout_state.duration = 100000;
    state->frame_buf[i].timeout_state.frame = &(state->frame_buf[i].send_frame);
    state->frame_buf[i].timeout_state.remote_sin = state->remote_sin;
    pthread_create(&(state->frame_buf[i].timeout), NULL, timeout, &(state->frame_buf[i].timeout_state));
    
    /* Set has ack */
    state->frame_buf[i].has_ack = 0;

    /* Update lfs */
    state->lfs = send_seqnum;
    
    /* Actually send the frame */
    if(sendto(state->s, (char*) &(state->frame_buf[i].send_frame), sizeof(struct frame), 0, (struct sockaddr *) &(state->remote_sin), sizeof(state->remote_sin) ) < 0){
      perror("Send To failed");
      exit(1);
    }
    
    /* Terminate the thread if eof was reached */
    if(got_eof ==1)
      break;
  }
  fclose(fp);
  return NULL;
}

/****************************************************************************
 * Name: ack_listen                                                         *
 * Arguments:                                                               *
 * Argument Name    type          Description                               *
 * -------------    ----          -----------                               *
 * args             void*         Parameters containing the state of an ftp *
 *                                client.                                   *
 *                                                                          *
 * RETURNS: NULL                                                            *
 ****************************************************************************/
void* ack_listen(void *args){
  /* wait to receive an ack */
  
  struct send_file_args *state = (struct send_file_args*) args;
  struct ack ack_frame;
  seqnum_t ack_seq;
  socklen_t addr_len = sizeof(state->local_sin);
  int recvlen;
  int got_eof = 0;
  /* Wait to receive an Ack */
  while(1){
    recvlen = recvfrom(state->s, &ack_frame, sizeof(struct ack), 0, 0, &addr_len);
    if(recvlen <= 0){
      
      continue;
      
    }

    /* The acknowledge sequence number */
    ack_seq = ack_frame.seq;
    
    /* ignore ack if already reeceived */
    if(state->frame_buf[ack_seq % BUF_SIZE].has_ack == 1){
    
      continue;
    }

    

    /* indicate ack was received and cancel the timeout.*/
    state->frame_buf[ack_seq % BUF_SIZE].has_ack = 1;

    seqnum_t conf_sec = state->frame_buf[ack_seq % BUF_SIZE].send_frame.seq;

    seqnum_t nfe = ((state->lar) + 1) % state->seq_max; // next frame expected
    seqnum_t nfe_i = nfe % BUF_SIZE; // index of nfe in buffer
    seqnum_t nnfe = ((state->lar) + 2) % state->seq_max;

    /* Cancel the timeout for this frame. */
    pthread_cancel(state->frame_buf[ack_seq % BUF_SIZE].timeout);
    
    /* Update LAR to actual last acknowledged frame number. */
    
    
    //while(state->frame_buf[((state->lar)+1) % BUF_SIZE].has_ack == 1){
    while(state->frame_buf[nfe % BUF_SIZE ].has_ack==1){	
      /* Reset the has_ack field */
      state->frame_buf[nfe % BUF_SIZE].has_ack = 0;
      if(state->frame_buf[nfe % BUF_SIZE].send_frame.eof_pos >= 0){
	got_eof = 1;
      }
      state->lar = ((state->lar)+1) % state->seq_max;
      nfe = ((state->lar) + 1) % state->seq_max;
      //printf("ACK_LISTEN incremented LAR to %d\n", state->lar);      
      
    }
    if(got_eof){
      printf("Transfer Complete\n");
      return NULL;
    }
  }  
}



#endif

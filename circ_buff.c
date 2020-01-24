 /**
 * This program simply maintains a circular buffer of a given size indefinitely.
 */
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h> /* C99 only */
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>

int c_read(int fd, char * buf, unsigned int size, unsigned int * head_in, unsigned int * tail_in);
int c_write(int fd, char * buf, unsigned int size, unsigned int * head_in, unsigned int * tail_in);
bool empty_buf(unsigned int head, unsigned int tail);
bool setblock(int fd, bool block);
#define FD_SET_SET(set, fd, max) FD_SET(fd, &set); max = ((fd > max) ? fd : max);
#define FD_SET_UNSET(set, fd, max) FD_CLR(fd, &set); max = ((fd == max) ? max - 1  : max);  //not ideal. Do while ISFDSET...

int main(int argc, char **argv)
{
  char * buf;
  unsigned int buf_size = 0;
  unsigned int buf_head = 0;
  unsigned int buf_tail = 0;

  // Check args.
  if(argc != 2) {
    fprintf(stderr, "Usage: %s <buffer size in bytes>\n", __FILE__);
    exit(EXIT_FAILURE);
  }
  sscanf(argv[1], "%d", &buf_size);
  buf_size = ( buf_size < 2 ) ? 2 : buf_size;

  // Note the usable buffer space is buf_size-1.
  fprintf(stderr, "Allocating %d\n", buf_size);
  buf = (char*)malloc(buf_size);

  bool done_reading = false;
  int maxfd = 0;
  fd_set r_set, w_set, r_tempset, w_tempset;
  setblock(STDIN_FILENO, false);
  setblock(STDOUT_FILENO, false);
  FD_ZERO(&r_set);
  FD_ZERO(&w_set);
  FD_ZERO(&r_tempset);
  FD_ZERO(&w_tempset);
  FD_SET_SET(r_tempset, STDIN_FILENO, maxfd);
  FD_SET_SET(w_tempset, STDOUT_FILENO, maxfd);
  r_set = r_tempset;
  while(true) {
    select((maxfd + 1), &r_set, &w_set, NULL, NULL);
    if(FD_ISSET(STDIN_FILENO, &r_set)) {
      int c = c_read(STDIN_FILENO, buf, buf_size, &buf_head, &buf_tail);
      if(c == -1) { // EOF, disable select on the input.
        fprintf(stderr, "No more bytes to read\n");
        done_reading = true;
        FD_ZERO(&r_set);
      }
    }
    if(!done_reading) {
      r_set = r_tempset;
    }
    if(FD_ISSET(STDOUT_FILENO, &w_set)) {
      c_write(STDOUT_FILENO, buf, buf_size, &buf_head, &buf_tail);
    }
    if(!empty_buf(buf_head, buf_tail)) { // Enable select on write whenever there is bytes.
      w_set = w_tempset;
    }
    else {
      FD_ZERO(&w_set);
      if(done_reading) { // Finish.
        fprintf(stderr, "No more bytes to write\n");
        break;
      }
    }
  }
  fflush(stderr);
  return 0;
}

bool empty_buf(unsigned int head, unsigned int tail) {
  return head == tail;
}

/**
 * Keep reading until we can read no more. Keep on pushing the tail forward as we overflow.
 * Expects fd to be non blocking.
 * @returns number of byte read, 0 on non stopping error, or -1 on error or EOF.
 */
int c_read(int fd, char * buf, unsigned int size, unsigned int * head_in, unsigned int * tail_in) {
  fprintf(stderr, "In c_read()\n");
  unsigned int head = *head_in;
  unsigned int tail = *tail_in;
  bool more_bytes = true;
  int n = 0;
  int c = 0;

  while(more_bytes) {
    bool in_front = tail > head;
    fprintf(stderr, "Read %d %d %d\n", size, head, tail);

    n = read(fd, buf+head, size - head);
    if(n == -1) {
      more_bytes = false;
      if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) { // Not EOF but the read would block.
        c = 0;
      }
      else {
        c = -1;
      }
    }
    else if(n == 0) { // EOF. No more bytes possible.
      more_bytes = false;
      c = -1;
    }
    else if(n != (size - head)) { // if not full read adjust pointers and break.
      more_bytes = false;
      c += n;
      head = (head+n)%size;
      if(in_front && (head >= tail || head == 0)) {
        tail = (head+1)%size;
      }
    }
    else {
      c = 0;
      head = 0;
      tail = (tail == 0) ? 1 : tail;
    }
  }
  *head_in = head;
  *tail_in = tail;
  return c;
}

/**
 * Try flush the buffer to fd. fd should be non blocking.
 */
int c_write(int fd, char * buf, unsigned int size, unsigned int * head_in, unsigned int * tail_in) {
  fprintf(stderr, "In c_write()\n");
  unsigned int head = *head_in;
  unsigned int tail = *tail_in;
  int n = 0;
  fprintf(stderr, "Write %d %d %d\n", size, head, tail);

  if(tail < head) {
    n = write(fd, buf+tail, head-tail);
    tail += n;
  }
  else if(head < tail) {
    n = write(fd, buf+tail, size-tail);
    if(n == size-tail) {
      n = write(fd, buf, head);
      tail = n;
    }
  }
  *head_in = head;
  *tail_in = tail;
  return n;
}

bool setblock(int fd, bool block)
{
  int flags;
  flags = fcntl(fd, F_GETFL);
  if (block)
      flags &= ~O_NONBLOCK;
  else
      flags |= O_NONBLOCK;
  fcntl(fd, F_SETFL, flags);
  return true;
}

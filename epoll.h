#ifndef _EPOLL_HEADER
#define _EPOLL_HEADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/timerfd.h>

#include "error.h"

#include <map>

extern int timer_fd;
extern void timer_loop();
extern int schedule_next_timer();
extern void handle_timer_event(epoll_event *);

#define MAX_EVENTS 1024

struct fd_status
{
};

void epoll_add(int epfd, int fd, int events);

void epoll_del(int epfd, int fd, int events);

void handle_event(epoll_event *event);

void init_main_loop();

void run_main_loop();

#endif
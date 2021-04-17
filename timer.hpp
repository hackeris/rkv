#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#if !__cpp_impl_coroutine
#define __cpp_impl_coroutine
#endif

#include <queue>
#include <chrono>
#include <coroutine>
#include <functional>

#include <iostream>

#include "error.h"
#include "task.hpp"

extern int timer_fd;
extern int main_loop_epoll;

void init_timer_fd();

void init_timer();

void init_timer_fd();

int schedule_next_timer();

void timer_loop();

void handle_timer_event(epoll_event *ev);

task<int> co_sleep(int millsec);

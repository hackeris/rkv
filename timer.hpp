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
#include "loop.hpp"

extern event_loop loop;

task<int> co_sleep(int millsec);

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

#if !__cpp_impl_coroutine
#define __cpp_impl_coroutine
#endif

#include <map>
#include <queue>
#include <chrono>
#include <thread>
#include <iostream>
#include <coroutine>
#include <functional>

#include "loop.hpp"
#include "task.hpp"

event_loop loop;

extern task<int> co_main(int argc, char **argv);

int main(int argc, char **argv)
{
  loop.init();

  task<int> t = co_main(argc, argv);

  loop.run();

  return t.result();
}

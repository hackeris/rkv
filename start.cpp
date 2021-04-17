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

#include "epoll.h"
#include "task.hpp"

extern task<int> co_main(int argc, char **argv);

int main(int argc, char **argv)
{
  init_main_loop();

  task<int> t = co_main(argc, argv);

  run_main_loop();

  return t.result();
}

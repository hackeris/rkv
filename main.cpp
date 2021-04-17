#include "epoll.h"
#include "task.hpp"
#include "timer.hpp"

task<int> co_main(int argc, char **argv)
{
  std::cout << "before sleep" << std::endl;

  int t = co_await co_sleep(1000);

  std::cout << "after sleep" << std::endl;

  co_return 0;
}

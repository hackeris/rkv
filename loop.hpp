#ifndef _LOOP_HREADER
#define _LOOP_HREADER

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
#include <queue>
#include <chrono>
#include <functional>

#define MAX_EVENTS 1024

class timer
{
public:
  using clock = std::chrono::system_clock;
  using time_point = clock::time_point;
  using duration = clock::duration;

  struct timer_event
  {
    time_point time;
    std::function<void()> callback;
    friend bool operator<(timer_event a, timer_event b)
    {
      return a.time < b.time;
    }
  };

  bool has_event()
  {
    return !_event_q.empty();
  }

  duration next_wait()
  {
    auto now = tick();
    if (_event_q.empty())
    {
      return duration{};
    }
    const auto &e = top();
    return e.time - now;
  }

  time_point tick()
  {
    auto precise = std::chrono::milliseconds(1);
    auto now = clock::now();
    while (!_event_q.empty())
    {
      auto &e = top();
      if (e.time - precise < now)
      {
        e.callback();
        pop();
      }
      else
      {
        break;
      }
    }
    return now;
  }

  void pop()
  {
    _event_q.pop();
  }

  const timer_event &top()
  {
    return _event_q.top();
  }

  void schedule(duration delay, std::function<void()> callback)
  {
    auto now = clock::now();
    _event_q.push(timer_event{now + delay, callback});
  }

  bool wait_next_on(int timer_fd)
  {
    auto to_wait = std::chrono::duration_cast<std::chrono::milliseconds>(next_wait());
    if (has_event())
    {
      struct itimerspec t;
      ::memset(&t, 0, sizeof(t));

      t.it_value.tv_sec = to_wait.count() / 1000;
      t.it_value.tv_nsec = (to_wait.count() % 1000) * 1000 * 1000;
      if (timerfd_settime(timer_fd, TFD_TIMER_CANCEL_ON_SET, &t, nullptr) < 0)
      {
        panic("error set timer");
      }
      return true;
    }
    return false;
  }

private:
  std::priority_queue<timer_event> _event_q;
};

struct io_fd_status
{
};

class event_loop
{
public:
  void init();

  void run();

  void schedule(timer::duration delay, std::function<void()> callback);

private:
  void epoll_add(int fd, int events);

  void epoll_del(int fd, int events);

  void handle_event(epoll_event* event);

private:

  int timer_fd;
  int loop_epoll_fd;

  std::map<int, io_fd_status> io_fds;

  timer tmr;
};

#endif

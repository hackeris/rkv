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

#include <map>
#include <queue>
#include <chrono>
#include <iostream>
#include <coroutine>
#include <functional>

#define MAX_EVENTS 1024

int main_loop_epoll = 0;
struct epoll_event events[MAX_EVENTS];

int timer_fd = 0;

struct fd_status
{
};

std::map<int, fd_status> fds;

class async_timer
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

private:
  std::priority_queue<timer_event> _event_q;
};

async_timer timer;

struct timer_awaitable
{
  int millsec;
  async_timer &_tmr;
  bool await_ready()
  {
    return millsec <= 0;
  }

  void await_suspend(std::coroutine_handle<> handle)
  {
    auto dur = std::chrono::milliseconds(millsec);
    _tmr.schedule(dur, handle);
  }

  void await_resume()
  {
  }
};

struct main_task
{
  struct promise_type;
  using handle = std::coroutine_handle<promise_type>;

  struct promise_type
  {

    main_task get_return_object() { return main_task{handle::from_promise(*this)}; }

    std::suspend_never initial_suspend() noexcept { return {}; }

    std::suspend_never final_suspend() noexcept { return {}; }

    void return_void() {
    }

    void unhandled_exception() {}
  };

  handle _handle;
};

void error(const char *msg);

void handle_event(epoll_event *event);

void init_timer();

void epoll_add(int epfd, int fd, int events);

void epoll_del(int epfd, int fd, int events);

timer_awaitable async_sleep(int millsec)
{
  if (millsec > 0)
  {
    if (!timer_fd)
    {
      init_timer();
    }
    if (!timer.has_event())
    {
      epoll_add(main_loop_epoll, timer_fd, EPOLLIN | EPOLLHUP | EPOLLERR);
    }
  }

  return timer_awaitable{millsec, timer};
}

void init_main_loop()
{
  main_loop_epoll = epoll_create(MAX_EVENTS);
  if (main_loop_epoll < 0)
  {
    error("Error creating epoll..\n");
  }
}

void init_timer()
{
  timer_fd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
  if (timer_fd < 0)
  {
    error("error creating timer...");
  }
}

int schedule_next_timer()
{
  auto to_wait = std::chrono::duration_cast<std::chrono::milliseconds>(timer.next_wait());
  if (timer.has_event())
  {
    struct itimerspec t
    {
    };
    t.it_value.tv_sec = to_wait.count() / 1000;
    t.it_value.tv_nsec = (to_wait.count() % 1000) * 1000 * 1000;
    if (timerfd_settime(timer_fd, TFD_TIMER_CANCEL_ON_SET, &t, nullptr) < 0)
    {
      error("error set timer");
    }
    return 1;
  }
  return 0;
}

void timer_loop()
{
  int has_event = schedule_next_timer();
  if (!has_event)
  {
    epoll_del(main_loop_epoll, timer_fd, EPOLLIN | EPOLLHUP | EPOLLERR);
  }
}

void run_main_loop()
{
  schedule_next_timer();

  int new_events = 0;

  do
  {
    new_events = epoll_wait(main_loop_epoll, events, MAX_EVENTS, -1);

    for (int i = 0; i < new_events; ++i)
    {
      handle_event(&events[i]);
    }

    timer_loop();

  } while (new_events > 0 && fds.size() > 0);

  close(main_loop_epoll);
  if (timer_fd)
  {
    close(timer_fd);
  }
  //  TODO: maybe error("Error in epoll_wait..\n");
}

void epoll_add(int epfd, int fd, int events)
{
  struct epoll_event ev
  {
  };
  ev.data.fd = fd;
  ev.events = events;
  int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
  if (ret < 0)
  {
    error("error to add fd to epoll");
  }

  fds.emplace(fd, fd_status{});
}

void epoll_del(int epfd, int fd, int events)
{
  struct epoll_event ev;
  ::memset(&ev, 0, sizeof(ev));

  ev.data.fd = fd;
  ev.events = events;
  int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev);
  if (ret < 0)
  {
    error("error to delete fd from epoll");
  }

  fds.erase(fd);
}

void handle_timer_event(epoll_event *ev)
{
  int fd = ev->data.fd;
  uint64_t count = 0;
  if (0 != (EPOLLIN & ev->events))
  {
    read(fd, (void *)&count, sizeof(count));
    timer.tick();
  }
}

void handle_event(epoll_event *event)
{
  if (event->data.fd == timer_fd)
  {
    handle_timer_event(event);
  }
}

void error(const char *msg)
{
  perror(msg);
  exit(1);
}

main_task co_main(int argc, char **argv)
{
  std::cout << "before sleep" << std::endl;

  co_await async_sleep(1000);

  std::cout << "sleep after 1s" << std::endl;
}

int main(int argc, char **argv)
{
  init_main_loop();

  auto task = co_main(argc, argv);

  run_main_loop();

  return 0;
}

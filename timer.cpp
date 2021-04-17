#include "epoll.h"
#include "timer.hpp"

class timer_impl;

int timer_fd = 0;

class timer_impl
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

timer_impl timer;

struct timer_awaitable
{
  int millsec;
  timer_impl &_tmr;
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

void init_timer_fd();

void init_timer()
{
  if (!timer_fd)
  {
    init_timer_fd();
  }
  if (!timer.has_event())
  {
    epoll_add(main_loop_epoll, timer_fd, EPOLLIN | EPOLLHUP | EPOLLERR);
  }
}

void init_timer_fd()
{
  timer_fd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
  if (timer_fd < 0)
  {
    panic("error creating timer...");
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
      panic("error set timer");
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

task<int> co_sleep(int millsec)
{
  if (millsec > 0)
  {
    init_timer();
  }

  co_await timer_awaitable{millsec, timer};

  co_return 0;
}
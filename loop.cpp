#include "loop.hpp"

void event_loop::init()
{
  loop_epoll_fd = epoll_create(MAX_EVENTS);
  if (loop_epoll_fd < 0)
  {
    panic("Error creating epoll..\n");
  }

  timer_fd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
  if (timer_fd < 0)
  {
    panic("error creating timer...");
  }

  epoll_add(timer_fd, EPOLLIN | EPOLLHUP | EPOLLERR);
}

void event_loop::run()
{
  tmr.wait_next_on(timer_fd);

  int new_events;
  epoll_event *events = new epoll_event[MAX_EVENTS];
  while (tmr.has_event() || !io_fds.empty())
  {
    new_events = epoll_wait(loop_epoll_fd, events, MAX_EVENTS, -1);
    if (new_events <= 0)
    {
      break;
    }

    for (int i = 0; i < new_events; ++i)
    {
      handle_event(&events[i]);
    }

    tmr.wait_next_on(timer_fd);
  }

  delete[] events;
}

void event_loop::schedule(timer::duration delay, std::function<void()> callback)
{
  tmr.schedule(delay, callback);
}

void event_loop::handle_event(epoll_event *ev)
{
  int fd = ev->data.fd;
  if (fd == timer_fd)
  {
    uint64_t count = 0;
    if (0 != (EPOLLIN & ev->events))
    {
      read(fd, (void *)&count, sizeof(count));
      tmr.tick();
    }
  }
}

void event_loop::epoll_add(int fd, int events)
{
  struct epoll_event ev;
  ::memset(&ev, 0, sizeof(ev));

  ev.data.fd = fd;
  ev.events = events;
  int ret = epoll_ctl(loop_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  if (ret < 0)
  {
    panic("error to add fd to epoll");
  }

  if (timer_fd != fd)
  {
    io_fds.emplace(fd, io_fd_status{});
  }
}

void event_loop::epoll_del(int fd, int events)
{
  struct epoll_event ev;
  ::memset(&ev, 0, sizeof(ev));

  ev.data.fd = fd;
  ev.events = events;
  int ret = epoll_ctl(loop_epoll_fd, EPOLL_CTL_DEL, fd, &ev);
  if (ret < 0)
  {
    panic("error to delete fd from epoll");
  }

  io_fds.erase(fd);
}
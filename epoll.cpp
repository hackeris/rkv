#include "epoll.h"

int main_loop_epoll = 0;
struct epoll_event events[MAX_EVENTS];

std::map<int, fd_status> fds;

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
    panic("error to add fd to epoll");
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
    panic("error to delete fd from epoll");
  }

  fds.erase(fd);
}

void init_main_loop()
{
  main_loop_epoll = epoll_create(MAX_EVENTS);
  if (main_loop_epoll < 0)
  {
    panic("Error creating epoll..\n");
  }
}

void run_main_loop()
{
  schedule_next_timer();

  int new_events = 0;

  while (fds.size() > 0)
  {
    new_events = epoll_wait(main_loop_epoll, events, MAX_EVENTS, -1);
    if (new_events <= 0)
    {
      break;
    }

    for (int i = 0; i < new_events; ++i)
    {
      handle_event(&events[i]);
    }

    timer_loop();
  }

  close(main_loop_epoll);
  if (timer_fd)
  {
    close(timer_fd);
  }
  //  TODO: maybe panic("Error in epoll_wait..\n");
}

void handle_event(epoll_event *event)
{
  if (event->data.fd == timer_fd)
  {
    handle_timer_event(event);
  }
}
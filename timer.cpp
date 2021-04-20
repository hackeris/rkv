#include "timer.hpp"

struct timer_awaitable
{
  int millsec;
  event_loop &loop;
  bool await_ready()
  {
    return millsec <= 0;
  }

  void await_suspend(std::coroutine_handle<> handle)
  {
    auto dur = std::chrono::milliseconds(millsec);
    loop.schedule(dur, handle);
  }

  void await_resume()
  {
  }
};

task<int> co_sleep(int millsec)
{
  co_await timer_awaitable{millsec, loop};
  co_return 0;
}

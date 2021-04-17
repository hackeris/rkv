#include "task.hpp"

task<void> task_promise<void>::get_return_object() noexcept
{
  return task<void>{std::coroutine_handle<task_promise>::from_promise(*this)};
}
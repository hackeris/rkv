#ifndef _TASK_HEADER
#define _TASK_HEADER

#if !__cpp_impl_coroutine
#define __cpp_impl_coroutine
#endif

#include <mutex>
#include <atomic>
#include <cassert>
#include <coroutine>
#include <condition_variable>

template <typename T>
class task;

class task_promise_base
{
  friend struct final_awaitable;

  struct final_awaitable
  {
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> coroutine)
    {
      task_promise_base &promise = coroutine.promise();
      if (promise._state.exchange(true, std::memory_order_acq_rel))
      {
        promise._continuation.resume();
      }
    }

    void await_resume() noexcept {}
  };

public:
  task_promise_base() noexcept : _state(false) {}

  auto initial_suspend()
  {
    return std::suspend_never{};
  }

  auto final_suspend() noexcept
  {
    return final_awaitable{};
  }

  bool try_set_continuation(std::coroutine_handle<> continuation)
  {
    _continuation = continuation;
    return !_state.exchange(true, std::memory_order_acq_rel);
  }

private:
  std::atomic<bool> _state;
  std::coroutine_handle<> _continuation;
};

template <typename T>
class task_promise final : public task_promise_base
{
public:
  task_promise() {}

  ~task_promise()
  {
    switch (_result_type)
    {
    case result_type::value:
      _value.~T();
      break;
    case result_type::exception:
      _exception.~exception_ptr();
      break;
    default:
      break;
    }
  }

  task<T> get_return_object() noexcept
  {
    return task<T>{std::coroutine_handle<task_promise>::from_promise(*this)};
  }

  void unhandled_exception()
  {
    ::new (static_cast<void *>(std::addressof(_exception))) std::exception_ptr(
        std::current_exception());
    _result_type = result_type::exception;
  }

  template <typename Value>
  void return_value(Value &&value) noexcept
  {
    ::new (static_cast<void *>(std::addressof(_value))) T(std::forward<Value>(value));
    _result_type = result_type::value;
  }

  T &result() &
  {
    if (_result_type == result_type::exception)
    {
      std::rethrow_exception(_exception);
    }
    assert(_result_type == result_type::value);

    return _value;
  }

private:
  enum class result_type
  {
    empty,
    value,
    exception
  };

  result_type _result_type = result_type::empty;

  union
  {
    T _value;
    std::exception_ptr _exception;
  };
};

template <>
class task_promise<void> final : public task_promise_base
{
public:
  task_promise() {}

  ~task_promise()
  {
  }

  task<void> get_return_object() noexcept;

  void unhandled_exception()
  {
    _exception = std::current_exception();
  }

  void return_void()
  {
  }

  void result()
  {
    if (_exception)
    {
      std::rethrow_exception(_exception);
    }
  }

private:
  std::exception_ptr _exception;
};

template <typename T = void>
class task
{

public:
  using promise_type = task_promise<T>;

  using value_type = T;

private:
  struct awaitable_base
  {
    std::coroutine_handle<promise_type> _coro;

    awaitable_base(std::coroutine_handle<promise_type> coro) noexcept : _coro(coro)
    {
    }

    bool await_ready() noexcept
    {
      return !_coro || _coro.done();
    }

    bool await_suspend(std::coroutine_handle<> awaiting_coro) noexcept
    {
      // _coro.resume();
      return _coro.promise().try_set_continuation(awaiting_coro);
    }
  };

public:
  task() noexcept : _coro(nullptr) {}

  explicit task(std::coroutine_handle<promise_type> coro) : _coro(coro)
  {
  }

  task(task &&t) noexcept : _coro(t._coro)
  {
    t._coro = nullptr;
  }

  /// Disable copy construction/assignment.
  task(const task &) = delete;
  task &operator=(task &&) = delete;
  task &operator=(const task &) = delete;

  ~task()
  {
    if (_coro)
    {
      _coro.destroy();
    }
  }

  bool is_ready()
  {
    return !_coro || _coro.done();
  }

  auto operator co_await() const &noexcept
  {
    struct awaitable : awaitable_base
    {
      using awaitable_base::awaitable_base;

      decltype(auto) await_resume()
      {
        if (!this->_coro)
        {
          //  TODO: exception, broken promise
        }
        return this->_coro.promise().result();
      }
    };

    return awaitable{_coro};
  }

  auto when_ready() const noexcept
  {
    struct awaitable : awaitable_base
    {
      using awaitable_base::awaitable_base;

      void await_resume() const noexcept {}
    };

    return awaitable{_coro};
  }

  T result()
  {
    return this->_coro.promise().result();
  }

private:
  std::coroutine_handle<promise_type> _coro;
};

#endif
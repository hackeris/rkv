#include <unistd.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <list>
#include <queue>
#include <chrono>
#include <vector>
#include <iostream>
#include <memory>
#include <functional>

#include <optional>
#include <variant>
#include <coroutine>

struct handle {
	virtual void run() = 0;
	virtual ~handle() = default;
};

using callback = handle*;

struct event {
	int fd;
	uint32_t events;
	callback cb;
};

using duration = std::chrono::milliseconds;

using timer_handle
	= std::pair<
		duration,
		callback
	>;

class event_loop {

public:

	event_loop() :_fd(epoll_create1(0)) {
		if (_fd < 0) {
			std::cerr << "failed to create epoll, errno: " << errno << std::endl;
			::exit(-1);
		}
	}

	std::vector<event> select(int timeout) {

		constexpr int _max_select = 64;

		std::vector<epoll_event> events;
		events.resize(_max_select);

		int nfds = epoll_wait(_fd, &events[0], _max_select, timeout);

		std::vector<event> result;
		for (int i = 0; i < nfds; i += 1) {
			auto& e = events[i];
			result.emplace_back(event{
				.fd = e.data.fd,
				.events = e.events,
				.cb = (callback)e.data.ptr
				});
		}
		return std::move(result);
	}

	void register_event(const event& e) {
		epoll_event ev{ .events = e.events, .data = {.ptr = e.cb} };
		if (epoll_ctl(_fd, EPOLL_CTL_ADD, e.fd, &ev) == 0) {
			_event_count += 1;
		}
	}

	void remove_event(const event& e) {
		epoll_event ev{ .events = e.events };
		if (epoll_ctl(_fd, EPOLL_CTL_DEL, e.fd, &ev) == 0) {
			_event_count -= 1;
		}
	}

	void schedule(callback func) {
		_ready.push(func);
	}

	template<typename F>
	void run_until_complete(F&& f) {
		schedule(&f.get_resumable());
		main_loop();
	}

	struct event_awaiter {
		bool await_ready() { return false; }
		template<typename Promise>
		void await_suspend(std::coroutine_handle<Promise> h) noexcept {
			auto& promise = h.promise();
			_evt.cb = static_cast<callback>(&promise);
			_loop.register_event(_evt);
		}
		void await_resume() noexcept {
			_loop.remove_event(_evt);
		}
		event_loop& _loop;
		event _evt;
	};
	auto wait(const event& e) {
		return event_awaiter{ *this, e };
	}

	~event_loop() {
		if (_fd > 0) { close(_fd); }
	}

private:

	void main_loop() {
		while (!is_stop()) {
			run_once();
		}
	}

	void run_once() {

		int timeout;
		if (_ready.empty()) {
			timeout = 5;
		}
		else {
			timeout = 0;
		}
		
		auto events = select(timeout);
		for (auto& e : events) {
			_ready.push(e.cb);
		}

		while (!_ready.empty()) {
			callback top = _ready.front();
			_ready.pop();
			top->run();
		}
	}

	bool is_stop() {
		return _ready.empty() && _event_count <= 1;
	}

private:

	//	epoll fd
	int _fd{ 0 };

	int _event_count{ 1 };

	std::queue<callback> _ready;
	//	heap, by time
	//	std::vector<timer_handle> _schedules;
};

event_loop g_loop;

event_loop& get_loop() { return g_loop; }

template<typename R>
struct result {

	R get_result() {
		return std::get<R>(_container);
	}

	template<typename T>
	void return_value(T&& v) noexcept {
		_container.template emplace<R>(std::forward<T>(v));
	}

	void set_exception(std::exception_ptr e) noexcept { _container = e; }
	void unhandled_exception(void) noexcept { _container = std::current_exception(); }

private:
	std::variant<std::monostate, R, std::exception_ptr> _container;
};

template<>
struct result<void> {

	void get_result() {}

	void return_void() {
		_container.emplace(nullptr);
	}

	void set_exception(std::exception_ptr e) noexcept { _container = e; }
	void unhandled_exception(void) noexcept { _container = std::current_exception(); }

private:
	std::optional<std::exception_ptr> _container;
};

template<typename R = void>
struct task {

	struct promise_type;

	using coro_handle = std::coroutine_handle<promise_type>;

	template<typename Promise>
	struct coro_cleaner : handle {
		coro_cleaner(std::coroutine_handle<Promise> coro) : _coro(coro) {}

		std::coroutine_handle<Promise> _coro;

		void run() override {
			_coro.destroy();
			delete this;
		}
	};

	struct promise_type : handle, result<R> {
		task get_return_object() { return task{ coro_handle::from_promise(*this) }; }
		auto initial_suspend() { return std::suspend_always{}; }

		struct final_awaiter {
			bool await_ready() noexcept { return false; }
			template<typename Promise>
			void await_suspend(std::coroutine_handle<Promise> h) noexcept {
				auto& _loop = get_loop();
				if (h.promise()._next) {
					_loop.schedule(h.promise()._next);		
				}
				_loop.schedule(new coro_cleaner(h));
			}
			void await_resume() noexcept {}
		};
		auto final_suspend() noexcept {
			return final_awaiter{};
		}
		void run() override {
			coro_handle::from_promise(*this).resume();
		}

		handle* _next{ nullptr };
	};

	struct task_awaiter {
		bool await_ready() { return _handle.done(); }
		template<typename Promise>
		void await_suspend(std::coroutine_handle<Promise> resumer) noexcept {
			_handle.promise()._next = &resumer.promise();
			auto& loop = get_loop();
			loop.schedule(&_handle.promise());
		}
		auto await_resume() noexcept {
			return _handle.promise().get_result();
		}
		coro_handle _handle;
	};
	auto operator co_await() noexcept {
		return task_awaiter{ _handle };
	}

	handle& get_resumable() {
		return _handle.promise();
	}

	bool done() { return _handle.done(); }

	task(coro_handle handle) : _handle(handle) {}
private:
	coro_handle _handle;
};

using buffer = std::vector<char>;

struct stream {
	stream(int fd): _fd(fd) {}

	stream(stream&) = delete;

	stream(stream&& other) : _fd{ std::exchange(other._fd, -1) } {}

	task<buffer> read(ssize_t size) {
		
		buffer buf(size, 0);

		event ev {.fd = _fd, .events = EPOLLIN};
		auto& loop = get_loop();
		co_await loop.wait(ev);

		size = ::read(_fd, buf.data(), size);
		if (size == -1) {
			throw std::system_error(std::make_error_code(static_cast<std::errc>(errno)));
		}
		buf.resize(size);

		co_return buf;
	}

	task<ssize_t> write(const buffer& buf) {
		
		event ev {.fd = _fd, .events = EPOLLOUT};
		auto& loop = get_loop();
		
		ssize_t total_written = 0;
		while (total_written < buf.size()) {
			co_await loop.wait(ev);
			ssize_t sz = ::write(_fd, buf.data() + total_written, buf.size() - total_written);
			if (sz == -1) {
				throw std::system_error(std::make_error_code(static_cast<std::errc>(errno)));
			}
			total_written += sz;
		}
		co_return total_written;
	}

	~stream() { close(); }

	void close() {
		if (_fd > 0) {
			::close(_fd);
		}
		_fd = -1;
	}

private:
	int _fd{ -1 };
};

template<typename F>
decltype(auto) schedule(F&& f) {
	auto& loop = get_loop();
	loop.schedule(&f.get_resumable());
	return std::forward<F>(f);
}

int server_socket(uint16_t port) {

	int sock_fd;
	if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		std::cerr << "socket error" << std::endl;
		::exit(1);
	}

	struct sockaddr_in server_addr;

	::memset(&server_addr, 0, sizeof(struct sockaddr_in));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	if (bind(sock_fd, (struct sockaddr*)(&server_addr), sizeof(struct sockaddr)) == -1) {
		std::cerr << "bind error" << std::endl;
		exit(1);
	}

	if (listen(sock_fd, 5) == -1) {
		std::cerr << "listen error" << std::endl;
		exit(1);
	}

	return sock_fd;
}

template<typename Cb>
struct server {

	server(int fd, Cb cb) : _fd(fd), _cb(cb) {}

	task<void> run_forever() {

		event ev {.fd = _fd, .events = EPOLLIN};
		auto& loop = get_loop();

		while (true) {

			co_await loop.wait(ev);
			sockaddr_storage remoteaddr{};
			socklen_t addrlen = sizeof(remoteaddr);
			int clientfd = ::accept(_fd, reinterpret_cast<sockaddr*>(&remoteaddr), &addrlen);
			if (clientfd == -1) {
				continue;
			}

			schedule(_cb(stream{ clientfd }));
		}
	}

	~server() { close(); }

private:

	void close() {
		if (_fd > 0) { ::close(_fd); }
		_fd = -1;
	}

private:

	Cb _cb;
	int _fd{ -1 };
};

task<void> on_connected(stream stream) {

	while (true) {
		
		auto buf = co_await stream.read(4096);
		if (buf.empty()) {
			break;
		}

		auto written = co_await stream.write(buf);
		if (written <= 0) {
			break;
		}
	}

	stream.close();
}

task<void> main_task() {

	int server_fd = server_socket(8000);

	server svr(server_fd, on_connected);

	co_await svr.run_forever();
	
	co_return;
}

template<typename F>
void run(F&& f) {
	auto& loop = get_loop();
	loop.run_until_complete(std::forward<F>(f));
}

int main() {

	auto& loop = get_loop();

	run(main_task());

	std::cout << "server stopped" << std::endl;

	return 0;
}

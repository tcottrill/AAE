#include <chrono>
#include <cstdlib>
#include <iostream>
#include <native_window/window.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
int main() {
  nw::ContextOptions options;
  options.displayName = "/tmp/native-window-does-not-exist.sock";
  nw::Error error;
  auto begin = std::chrono::steady_clock::now();
  if (nw::Context::create(options, error) ||
      error.code != nw::ErrorCode::Connection)
    return 1;
  // A Unix socket that accepts but never implements Wayland must time out.
  std::string path =
      "/tmp/native-window-silent-" + std::to_string(getpid()) + ".sock";
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return 2;
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  path.copy(addr.sun_path, sizeof(addr.sun_path) - 1);
  if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 ||
      listen(fd, 1) < 0)
    return 3;
  options.displayName = path;
  options.connectTimeout = std::chrono::milliseconds{60};
  auto context = nw::Context::create(options, error);
  close(fd);
  unlink(path.c_str());
  if (context || error.code != nw::ErrorCode::Timeout)
    return 4;
  if (std::chrono::steady_clock::now() - begin > std::chrono::seconds{2})
    return 5;
  fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0 ||
      bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 ||
      listen(fd, 1) < 0)
    return 6;
  std::thread disconnect([fd] {
    int client = accept(fd, nullptr, nullptr);
    if (client >= 0)
      close(client);
  });
  options.connectTimeout = std::chrono::milliseconds{500};
  context = nw::Context::create(options, error);
  disconnect.join();
  close(fd);
  unlink(path.c_str());
  if (context || (error.code != nw::ErrorCode::Connection &&
                  error.code != nw::ErrorCode::Protocol))
    return 7;
  options.maxQueuedEvents = 1;
  if (nw::Context::create(options, error) ||
      error.code != nw::ErrorCode::Invalid)
    return 8;
  std::cout << "connection failure and bounded discovery passed\n";
}

#include "internal.h"
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <unistd.h>
static void check(bool okay) {
  if (!okay) {
    std::cerr << "native state check failed\n";
    std::exit(1);
  }
}
int main() {
  using namespace nw;
  using namespace nw::detail;
  Core c;
  c.options.maxQueuedEvents = 32;
  auto s = std::make_unique<Seat>();
  s->core = &c;
  s->id = 1;
  s->held.keys.insert(30);
  s->held.buttons.insert(272);
  c.seats[1] = std::move(s);
  Event e;
  e.type = EventType::PointerMotion;
  e.window = 1;
  e.x = 10;
  c.emit(e);
  e.x = 20;
  c.emit(e);
  check(c.events.size() == 1 && c.events.back().x == 20);
  e.type = EventType::Key;
  e.code = 30;
  e.pressed = true;
  c.emit(e);
  e.type = EventType::PointerMotion;
  c.emit(e);
  check(c.events.size() == 3);
  c.events.clear();
  c.seats[1]->reset();
  check(c.seats[1]->held.keys.empty() && c.seats[1]->held.buttons.empty());
  check(c.events.size() == 3 && c.events[0].type == EventType::Key &&
        !c.events[0].pressed && c.events[1].type == EventType::PointerButton &&
        !c.events[1].pressed && c.events[2].type == EventType::InputReset);
  c.events.clear();
  c.seats[1]->held.keys.insert(31);
  for (int i = 0; i < 40; ++i) {
    e.type = EventType::Key;
    e.code = i;
    c.emit(e);
  }
  check(c.events.size() <= 32 && c.events[0].type == EventType::InputReset &&
        c.seats[1]->held.keys.empty());
  c.events.clear();
  c.fail(ErrorCode::Connection, "test disconnect");
  auto count = c.events.size();
  c.fail(ErrorCode::Connection, "duplicate");
  check(c.events.size() == count &&
        c.events.back().type == EventType::ConnectionError && !c.pump(1000));
  // Pointer and keyboard focus can belong to different windows. Destroying the
  // pointer window must release only its buttons, never the other window's
  // keys.
  {
    Core lifecycle;
    auto seat = std::make_unique<Seat>();
    seat->core = &lifecycle;
    seat->id = 2;
    auto keyboardWindow = std::make_unique<NativeWindow>();
    keyboardWindow->core = &lifecycle;
    keyboardWindow->id = 10;
    auto pointerWindow = std::make_unique<NativeWindow>();
    pointerWindow->core = &lifecycle;
    pointerWindow->id = 20;
    seat->keyWindow = keyboardWindow.get();
    seat->pointerWindow = pointerWindow.get();
    seat->held.keys.insert(30);
    seat->held.buttons.insert(272);
    seat->modifiers = Control;
    seat->repeatKey = 30;
    lifecycle.seats[2] = std::move(seat);
    pointerWindow.reset();
    auto &remaining = *lifecycle.seats[2];
    check(remaining.held.buttons.empty() && remaining.pointerWindow == nullptr);
    check(remaining.keyWindow == keyboardWindow.get() &&
          remaining.held.keys.count(30) == 1 &&
          remaining.modifiers == Control && remaining.repeatKey == 30);
    check(lifecycle.events.size() == 2 &&
          lifecycle.events[0].type == EventType::PointerButton &&
          lifecycle.events[0].window == 20 && !lifecycle.events[0].pressed &&
          lifecycle.events[1].type == EventType::PointerFocus &&
          lifecycle.events[1].window == 20 && !lifecycle.events[1].focused);
    lifecycle.events.clear();
    keyboardWindow.reset();
    check(remaining.keyWindow == nullptr && remaining.held.keys.empty());
    check(lifecycle.events.back().type == EventType::KeyboardFocus &&
          lifecycle.events.back().window == 10 &&
          !lifecycle.events.back().focused);
  }
  Core transfers;
  transfers.options.maxTransferBytes = 4;
  transfers.options.transferTimeout = std::chrono::seconds{1};
  int fd[2];
  check(pipe2(fd, O_NONBLOCK | O_CLOEXEC) == 0);
  Transfer t;
  t.fd = fd[0];
  t.deadline = Clock::now() + std::chrono::seconds{1};
  check(write(fd[1], "12345", 5) == 5);
  close(fd[1]);
  transfers.transferReady(t, POLLIN);
  check(t.fd == -1 && transfers.events.back().error.code == ErrorCode::Invalid);
  transfers.events.clear();
  check(pipe2(fd, O_NONBLOCK | O_CLOEXEC) == 0);
  Transfer expired;
  expired.fd = fd[0];
  expired.deadline = Clock::now() - std::chrono::seconds{1};
  transfers.transferReady(expired, 0);
  close(fd[1]);
  check(expired.fd == -1 &&
        transfers.events.back().error.code == ErrorCode::Timeout);
  // Writing to an exited clipboard receiver must not terminate with SIGPIPE.
  check(pipe2(fd, O_NONBLOCK | O_CLOEXEC) == 0);
  Transfer output;
  output.fd = fd[1];
  output.writing = true;
  output.bytes = "text";
  output.deadline = Clock::now() + std::chrono::seconds{1};
  close(fd[0]);
  transfers.transferReady(output, POLLOUT);
  check(output.fd == -1);
  std::cout << "state ordering, focus reset, overflow, disconnect and transfer "
               "limits passed\n";
}

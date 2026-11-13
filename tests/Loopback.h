#pragma once
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <algorithm>
inline uint32_t millis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
inline uint32_t esp_random() {
  return arc4random();
}
class WiFiClient {
  struct Socket {
    int fd = -1;
    ~Socket() {
      if (fd >= 0) {
        ::close(fd);
      }
    }
  };
  std::shared_ptr<Socket> sock;

 public:
  explicit WiFiClient(int fd = -1) {
    if (fd >= 0) {
      sock = std::make_shared<Socket>();
      sock->fd = fd;
    }
  }
  explicit operator bool() const {
    return bool(sock);
  }
  bool connected() const {
    if (!sock) {
      return false;
    }
    char c;
    return recv(sock->fd, &c, 1, MSG_PEEK | MSG_DONTWAIT) != 0;
  }
  void stop() {
    sock.reset();
  }
  int available() {
    int n = 0;
    if (sock) {
      ioctl(sock->fd, FIONREAD, &n);
    }
    return n;
  }
  int read(uint8_t* p, int n) {
    return sock ? ::read(sock->fd, p, n) : -1;
  }
  int read() {
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
  }
  size_t write(const uint8_t* p, size_t n) {
    return sock ? ::write(sock->fd, p, n) : 0;
  }
  void printf(const char* fmt, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n > 0) {
      write((uint8_t*)b, std::min<int>(n, sizeof b - 1));
    }
  }
};
class WiFiServer {
  int listener = -1;

 public:
  explicit WiFiServer(int) {}
  void begin() {
    listener = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (sockaddr*)&addr, sizeof addr) || listen(listener, 8)) {
      std::abort();
    }
    fcntl(listener, F_SETFL, O_NONBLOCK);
    socklen_t size = sizeof addr;
    getsockname(listener, (sockaddr*)&addr, &size);
    port = ntohs(addr.sin_port);
  }
  void end() {
    if (listener >= 0) {
      close(listener);
    }
    listener = -1;
  }
  WiFiClient accept() {
    return WiFiClient(::accept(listener, nullptr, nullptr));
  }
  inline static unsigned port = 0;
};

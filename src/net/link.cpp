#include "net/link.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <utility>

#ifdef _WIN32
// Windows: Winsock, its calls under the POSIX names used below.
#include <winsock2.h>
#include <ws2tcpip.h>
using ssize_t = long long;
#define poll WSAPoll
#define MSG_NOSIGNAL 0
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace net {

namespace {

#ifdef _WIN32
// Winsock starts once, before the first socket.
struct Winsock {
  Winsock() {
    WSADATA d;
    WSAStartup(MAKEWORD(2, 2), &d);
  }
  ~Winsock() { WSACleanup(); }
};
void startup() { static Winsock w; }
void closeSocket(int fd) { closesocket(SOCKET(fd)); }
int lastError() { return WSAGetLastError(); }
bool wouldBlock(int e) { return e == WSAEWOULDBLOCK || e == WSAEINTR; }
bool inProgress(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
const char* errorText(int e) {
  static char buf[32];
  std::snprintf(buf, sizeof buf, "error %d", e);
  return buf;
}
void setNonBlocking(int fd) {
  u_long on = 1;
  ioctlsocket(SOCKET(fd), FIONBIO, &on);
}
#else
void startup() {}
void closeSocket(int fd) { ::close(fd); }
int lastError() { return errno; }
bool wouldBlock(int e) { return e == EAGAIN || e == EWOULDBLOCK || e == EINTR; }
bool inProgress(int e) { return e == EINPROGRESS; }
const char* errorText(int e) { return std::strerror(e); }
void setNonBlocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
#endif

void nonBlocking(int fd) {
  setNonBlocking(fd);
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
}

constexpr uint32_t kMaxMessage = 16u << 20;

}  // namespace

Link& Link::operator=(Link&& o) noexcept {
  if (this != &o) {
    close();
    fd_ = std::exchange(o.fd_, -1);
    pending_ = o.pending_;
    peer_ = std::move(o.peer_);
    out_ = std::move(o.out_);
    in_ = std::move(o.in_);
    sent_ = std::exchange(o.sent_, 0);
  }
  return *this;
}

void Link::adopt(int fd, const std::string& peer) {
  close();
  fd_ = fd;
  peer_ = peer;
  nonBlocking(fd_);
}

bool Link::connect(const std::string& address, int defaultPort) {
  close();
  startup();
  std::string host = address;
  int port = defaultPort;
  if (const size_t colon = address.rfind(':'); colon != std::string::npos) {
    host = address.substr(0, colon);
    port = std::atoi(address.c_str() + colon + 1);
  }
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return false;
  fd_ = int(socket(res->ai_family, res->ai_socktype, res->ai_protocol));
  if (fd_ < 0) {
    freeaddrinfo(res);
    return false;
  }
  nonBlocking(fd_);
  const int r = ::connect(fd_, res->ai_addr, int(res->ai_addrlen));
  freeaddrinfo(res);
  if (r != 0 && !inProgress(lastError())) {
    close();
    return false;
  }
  pending_ = r != 0;
  peer_ = host + ":" + std::to_string(port);
  return true;
}

bool Link::connected() {
  if (fd_ < 0) return false;
  if (pending_) {
    pollfd p{decltype(pollfd::fd)(fd_), POLLOUT, 0};
    if (poll(&p, 1, 0) <= 0) return false;
    int err = 0;
    socklen_t len = sizeof err;
    getsockopt(fd_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
    if (err != 0) {
      close();
      return false;
    }
    pending_ = false;
  }
  return true;
}

void Link::close() {
  if (fd_ >= 0) closeSocket(fd_);
  fd_ = -1;
  pending_ = false;
  out_.clear();
  in_.clear();
  sent_ = 0;
}

void Link::send(uint8_t type, const std::vector<uint8_t>& bytes) {
  if (fd_ < 0) return;
  const uint32_t n = uint32_t(bytes.size());
  const auto* p = reinterpret_cast<const uint8_t*>(&n);
  out_.insert(out_.end(), p, p + 4);
  out_.push_back(type);
  out_.insert(out_.end(), bytes.begin(), bytes.end());
  flush();
}

void Link::flush() {
  if (fd_ < 0 || pending_) return;
  while (sent_ < out_.size()) {
    const ssize_t n = ::send(fd_, reinterpret_cast<const char*>(out_.data() + sent_), int(out_.size() - sent_), MSG_NOSIGNAL);
    if (n > 0) {
      sent_ += size_t(n);
      continue;
    }
    if (n < 0 && wouldBlock(lastError())) break;
    close();
    return;
  }
  if (sent_ == out_.size()) out_.clear(), sent_ = 0;
  else if (sent_ > (1u << 20)) out_.erase(out_.begin(), out_.begin() + long(sent_)), sent_ = 0;
}

bool Link::receive(uint8_t& type, std::vector<uint8_t>& bytes) {
  if (fd_ < 0 || pending_) return false;
  uint8_t buf[65536];
  for (;;) {
    const ssize_t n = ::recv(fd_, reinterpret_cast<char*>(buf), int(sizeof buf), 0);
    if (n > 0) {
      in_.insert(in_.end(), buf, buf + n);
      continue;
    }
    if (n < 0 && wouldBlock(lastError())) break;
    // Closed by the other end, or broken: what came before it is still read.
    const std::vector<uint8_t> rest = std::move(in_);
    close();
    in_ = rest;
    break;
  }
  if (in_.size() < 5) return false;
  uint32_t len;
  std::memcpy(&len, in_.data(), 4);
  if (len > kMaxMessage) {
    close();
    return false;
  }
  if (in_.size() < 5 + size_t(len)) return false;
  type = in_[4];
  bytes.assign(in_.begin() + 5, in_.begin() + 5 + long(len));
  in_.erase(in_.begin(), in_.begin() + 5 + long(len));
  return true;
}

bool Listener::listen(int port, std::string& error) {
  close();
  startup();
  fd_ = int(socket(AF_INET, SOCK_STREAM, 0));
  if (fd_ < 0) {
    error = errorText(lastError());
    return false;
  }
  int one = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(uint16_t(port));
  if (bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd_, 2) != 0) {
    error = "port " + std::to_string(port) + ": " + errorText(lastError());
    close();
    return false;
  }
  setNonBlocking(fd_);
  return true;
}

void Listener::close() {
  if (fd_ >= 0) closeSocket(fd_);
  fd_ = -1;
}

bool Listener::accept(Link& link) {
  if (fd_ < 0) return false;
  sockaddr_in a{};
  socklen_t len = sizeof a;
  const int fd = int(::accept(fd_, reinterpret_cast<sockaddr*>(&a), &len));
  if (fd < 0) return false;
  char ip[INET_ADDRSTRLEN] = "?";
  inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
  link.adopt(fd, ip);
  return true;
}

// ---------------------------------------------------------------- finding the games

namespace {
constexpr const char* kAdvertTag = "HIND-REBUILT-1";
// The adverts' multicast group: every game listening hears them, on this computer too (a
// datagram sent to one address goes to one of the sockets bound to its port only).
constexpr uint32_t kAdvertGroup = (239u << 24) | (255u << 16) | (70u << 8) | 24u;  // 239.255.70.24
std::string clean(std::string s) {
  std::replace(s.begin(), s.end(), '|', '/');
  std::replace(s.begin(), s.end(), '\n', ' ');
  return s.substr(0, 40);
}
}  // namespace

std::string Advert::encode() const {
  std::string w;
  for (const std::string& n : wingmen) w += (w.empty() ? "" : ",") + clean(n);
  std::replace(w.begin(), w.end(), ';', ',');
  return std::string(kAdvertTag) + "|" + clean(game) + "|" + clean(pilot) + "|" + (host ? "1" : "0") + "|" + std::to_string(port) + "|" +
         clean(hostAddress) + "|" + clean(crew[0]) + "|" + clean(crew[1]) + "|" + clean(crew[2]) + "|" + w;
}

bool Advert::decode(const std::string& text) {
  std::vector<std::string> f;
  std::stringstream in(text);
  for (std::string x; std::getline(in, x, '|');) f.push_back(x);
  if (text.back() == '|') f.emplace_back();
  if (f.size() < 10 || f[0] != kAdvertTag) return false;
  game = f[1], pilot = f[2], host = f[3] == "1", port = std::atoi(f[4].c_str()), hostAddress = f[5];
  for (int i = 0; i < 3; ++i) crew[i] = f[size_t(6 + i)];
  wingmen.clear();
  std::stringstream w(f[9]);
  for (std::string x; std::getline(w, x, ',');)
    if (!x.empty()) wingmen.push_back(x);
  return !game.empty() && port > 0 && port < 65536;
}

bool Beacon::open() {
  close();
  startup();
  fd_ = int(socket(AF_INET, SOCK_DGRAM, 0));
  if (fd_ < 0) return false;
  int one = 1;
  setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof one);
  // The group: the local network only, and back to this computer's games.
  const unsigned char ttl = 1, loop = 1;
  setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof ttl);
  setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loop), sizeof loop);
  setNonBlocking(fd_);
  return true;
}

void Beacon::close() {
  if (fd_ >= 0) closeSocket(fd_);
  fd_ = -1;
}

void Beacon::announce(const Advert& a) {
  if (fd_ < 0 && !open()) return;
  const std::string text = a.encode();
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(uint16_t(kDiscoveryPort));
  // The group; the local network's broadcast and this computer's loopback for the games that
  // cannot hear the group (a broadcast does not come back to its sender).
  for (uint32_t ip : {kAdvertGroup, uint32_t(INADDR_BROADCAST), uint32_t(INADDR_LOOPBACK)}) {
    to.sin_addr.s_addr = htonl(ip);
    sendto(fd_, text.data(), int(text.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
  }
}

bool Finder::open(std::string& error) {
  close();
  startup();
  fd_ = int(socket(AF_INET, SOCK_DGRAM, 0));
  if (fd_ < 0) {
    error = errorText(lastError());
    return false;
  }
  // Several games on one computer each hear the adverts (the group's; SO_REUSEPORT would hand
  // each datagram to one of them only).
  int one = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(uint16_t(kDiscoveryPort));
  if (bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
    error = "port " + std::to_string(kDiscoveryPort) + ": " + errorText(lastError());
    close();
    return false;
  }
  // The adverts' group, on the network's interface and on the loopback (whichever there is).
  for (uint32_t iface : {uint32_t(INADDR_ANY), uint32_t(INADDR_LOOPBACK)}) {
    ip_mreq m{};
    m.imr_multiaddr.s_addr = htonl(kAdvertGroup);
    m.imr_interface.s_addr = htonl(iface);
    setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&m), sizeof m);
  }
  setNonBlocking(fd_);
  return true;
}

void Finder::close() {
  if (fd_ >= 0) closeSocket(fd_);
  fd_ = -1;
  games_.clear();
}

void Finder::update(double now) {
  if (fd_ >= 0) {
    char buf[2048];
    for (;;) {
      sockaddr_in from{};
      socklen_t len = sizeof from;
      const ssize_t n = recvfrom(fd_, buf, int(sizeof buf), 0, reinterpret_cast<sockaddr*>(&from), &len);
      if (n <= 0) break;
      Advert a;
      if (!a.decode(std::string(buf, size_t(n)))) continue;
      char ip[INET_ADDRSTRLEN] = "?";
      inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
      a.address = ip;
      a.heard = now;
      // One line a pilot's game: the same one heard by broadcast and over the loopback is one.
      auto same = [&](const Advert& g) {
        const bool local = (g.address.rfind("127.", 0) == 0) != (a.address.rfind("127.", 0) == 0);
        return g.port == a.port && g.pilot == a.pilot && g.game == a.game && (g.address == a.address || local);
      };
      auto it = std::find_if(games_.begin(), games_.end(), same);
      if (it == games_.end()) games_.push_back(a);
      else {
        // Keep the network's address over the loopback's (others can be told it).
        const std::string keep = it->address.rfind("127.", 0) == 0 ? a.address : it->address;
        *it = a, it->address = keep;
      }
    }
  }
  games_.erase(std::remove_if(games_.begin(), games_.end(), [&](const Advert& g) { return now - g.heard > 3.5; }), games_.end());
}

}  // namespace net

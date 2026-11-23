#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The network game's connection between the pilot's game (which listens) and the gunner's
// (which connects): TCP on the local network, never blocking the frame. A message is its length
// (4 bytes), its type (1 byte) and its bytes.
namespace net {

constexpr int kDefaultPort = 7024;

class Link {
public:
  Link() = default;
  Link(const Link&) = delete;
  Link& operator=(const Link&) = delete;
  Link(Link&& o) noexcept { *this = std::move(o); }
  Link& operator=(Link&& o) noexcept;
  ~Link() { close(); }

  // Starts connecting to "host" or "host:port"; connected() says when it is through.
  bool connect(const std::string& address, int defaultPort = kDefaultPort);
  // Through (a connection being made counts once it is established); false after it broke.
  bool connected();
  bool open() const { return fd_ >= 0; }
  void close();
  // The other end's address, for the messages.
  const std::string& peer() const { return peer_; }

  // Queues a message and sends what the socket takes now; the rest goes with the next calls.
  void send(uint8_t type, const std::vector<uint8_t>& bytes);
  void flush();
  // The next whole message received, if there is one.
  bool receive(uint8_t& type, std::vector<uint8_t>& bytes);
  // Bytes still waiting to be sent (a slow other end).
  size_t backlog() const { return out_.size() - sent_; }

private:
  friend class Listener;
  void adopt(int fd, const std::string& peer);
  int fd_ = -1;
  bool pending_ = false;  // connect() under way
  std::string peer_;
  std::vector<uint8_t> out_, in_;
  size_t sent_ = 0;
};

class Listener {
public:
  ~Listener() { close(); }
  bool listen(int port, std::string& error);
  void close();
  bool open() const { return fd_ >= 0; }
  // A new connection, if one came in.
  bool accept(Link& link);

private:
  int fd_ = -1;
};

// The games on the local network: each pilot's game says every second who flies and which
// seats of his Hind are free (Beacon); the menus of the others hear it (Finder). UDP broadcast
// to kDiscoveryPort (as the plain text Advert::encode makes).
constexpr int kDiscoveryPort = 7024;

struct Advert {
  std::string game;     // the game's name, as its host gave it
  std::string pilot;    // who flies this Hind
  bool host = true;     // the game's host (else a wingman in it)
  int port = kDefaultPort;  // where this pilot's game takes the crew (and, the host's, the wingmen)
  std::string hostAddress;  // a wingman's: where the game's host is ("" for the host itself)
  std::string crew[3];  // who sits at the gunner's sight and the left and the right door gun ("" free)
  std::vector<std::string> wingmen;  // the host's: the pilots of the other Hinds
  // Filled in by the Finder: where it came from, when (seconds of the Finder's clock).
  std::string address;
  double heard = 0.0;
  std::string encode() const;
  bool decode(const std::string& text);
};

class Beacon {
public:
  ~Beacon() { close(); }
  bool open();
  void close();
  // Sends the advert to the whole local network (and this computer's other games).
  void announce(const Advert& a);

private:
  int fd_ = -1;
};

class Finder {
public:
  ~Finder() { close(); }
  bool open(std::string& error);
  void close();
  bool isOpen() const { return fd_ >= 0; }
  // Reads what came; adverts not heard for a few seconds are dropped. `now` in seconds.
  void update(double now);
  const std::vector<Advert>& games() const { return games_; }

private:
  int fd_ = -1;
  std::vector<Advert> games_;
};

}  // namespace net

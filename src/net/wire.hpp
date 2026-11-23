#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

// The bytes of the network game's messages: values as they lie in memory (both ends are
// little-endian x86-64 builds of the same source), strings and vectors with their lengths first.
namespace net {

class Writer {
public:
  template <typename T>
  void pod(const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    bytes.insert(bytes.end(), p, p + sizeof(T));
  }
  void str(const std::string& s) {
    pod(uint32_t(s.size()));
    bytes.insert(bytes.end(), s.begin(), s.end());
  }
  template <typename T>
  void vec(const std::vector<T>& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    pod(uint32_t(v.size()));
    const auto* p = reinterpret_cast<const uint8_t*>(v.data());
    bytes.insert(bytes.end(), p, p + v.size() * sizeof(T));
  }
  std::vector<uint8_t> bytes;
};

// Reading past the end (a damaged or foreign message) leaves the values zero and ok() false.
class Reader {
public:
  Reader(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}
  explicit Reader(const std::vector<uint8_t>& b) : Reader(b.data(), b.size()) {}
  template <typename T>
  void pod(T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (!take(sizeof(T))) {
      std::memset(static_cast<void*>(&v), 0, sizeof(T));
      return;
    }
    std::memcpy(static_cast<void*>(&v), p_ - sizeof(T), sizeof(T));
  }
  template <typename T>
  T get() {
    T v;
    pod(v);
    return v;
  }
  void str(std::string& s) {
    const uint32_t n = get<uint32_t>();
    if (!take(n)) return s.clear();
    s.assign(reinterpret_cast<const char*>(p_ - n), n);
  }
  template <typename T>
  void vec(std::vector<T>& v) {
    const uint32_t n = get<uint32_t>();
    if (n > size_t(end_ - p_) / sizeof(T) || !take(n * sizeof(T))) {
      ok_ = false;
      return v.clear();
    }
    v.resize(n);
    std::memcpy(static_cast<void*>(v.data()), p_ - n * sizeof(T), n * sizeof(T));
  }
  bool ok() const { return ok_; }
  bool done() const { return p_ == end_; }

private:
  bool take(size_t n) {
    if (!ok_ || size_t(end_ - p_) < n) return ok_ = false;
    p_ += n;
    return true;
  }
  const uint8_t* p_;
  const uint8_t* end_;
  bool ok_ = true;
};

}  // namespace net

#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// Plain binary writer/reader for snapshots. Values are copied as they sit in
// memory: snapshots only travel between identical builds (same build id).
class ByteWriter {
public:
  template <typename T> void Put(const T &v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto *b = reinterpret_cast<const uint8_t *>(&v);
    m_data.insert(m_data.end(), b, b + sizeof(T));
  }
  void PutBytes(const uint8_t *data, size_t n) {
    m_data.insert(m_data.end(), data, data + n);
  }
  std::vector<uint8_t> &Data() { return m_data; }

private:
  std::vector<uint8_t> m_data;
};

class ByteReader {
public:
  ByteReader(const uint8_t *data, size_t size) : m_data(data), m_size(size) {}
  explicit ByteReader(const std::vector<uint8_t> &v)
      : ByteReader(v.data(), v.size()) {}

  template <typename T> T Get() {
    static_assert(std::is_trivially_copyable_v<T>);
    T v;
    Need(sizeof(T));
    std::memcpy(&v, m_data + m_pos, sizeof(T));
    m_pos += sizeof(T);
    return v;
  }
  const uint8_t *Take(size_t n) {
    Need(n);
    const uint8_t *p = m_data + m_pos;
    m_pos += n;
    return p;
  }
  bool Done() const { return m_pos == m_size; }

private:
  void Need(size_t n) const {
    if (m_pos + n > m_size)
      throw std::runtime_error("snapshot is truncated");
  }
  const uint8_t *m_data;
  size_t m_size;
  size_t m_pos = 0;
};

namespace Base64 {
std::string Encode(const std::vector<uint8_t> &data);
// False on characters outside the alphabet
bool Decode(const std::string &text, std::vector<uint8_t> &out);
} // namespace Base64

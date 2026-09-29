#pragma once
#include <atomic>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ix {
class WebSocket;
}

// One WebSocket to the match server. IXWebSocket runs it on its own thread;
// messages are queued and handed to the game loop by Poll(). Connection
// changes show up in the same stream as {"type":"_open"} and
// {"type":"_closed","reason":...}.
class NetClient {
public:
  NetClient();
  ~NetClient();
  NetClient(const NetClient &) = delete;
  NetClient &operator=(const NetClient &) = delete;

  // ws://, wss://, or an http(s):// address such as an ngrok tunnel's
  // (turned into ws(s)://, with /ws when it has no path). HTTP redirects are
  // followed first, off the game thread.
  void Connect(const std::string &url);
  void Close();
  bool IsOpen() const { return m_open; }

  void Send(const nlohmann::json &message);
  std::vector<nlohmann::json> Poll();

  // GET a REST endpoint ("http://host:8080/api/...") with the guest token
  static std::future<std::optional<nlohmann::json>>
  GetJsonAsync(const std::string &url, const std::string &token);

  // ws://host:port/ws -> http://host:port
  static std::string HttpBase(const std::string &wsUrl);
  // http(s):// -> ws(s)://, and /ws when there's no path
  static std::string WebSocketUrl(const std::string &url);

private:
  std::unique_ptr<ix::WebSocket> m_ws;
  std::jthread m_connecting; // follows redirects, then opens the socket
  std::mutex m_mutex;
  std::deque<nlohmann::json> m_inbox;
  std::atomic<bool> m_open{false};
};

#include "whas/net/net_client.h"
#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

NetClient::NetClient() {
  ix::initNetSystem();
  m_ws = std::make_unique<ix::WebSocket>();
}

NetClient::~NetClient() { Close(); }

namespace {

bool StartsWith(const std::string &s, const char *prefix) {
  return s.rfind(prefix, 0) == 0;
}

// ws(s):// -> http(s)://, for asking where an address really is
std::string ProbeUrl(const std::string &wsUrl) {
  if (StartsWith(wsUrl, "ws://"))
    return "http://" + wsUrl.substr(5);
  if (StartsWith(wsUrl, "wss://"))
    return "https://" + wsUrl.substr(6);
  return wsUrl;
}

// Where a WebSocket address ends up. Tunnels and proxies (ngrok) answer
// the first request with a 3xx to another address, often https://;
// IXWebSocket would follow those itself but keeps using a plain socket for
// an https:// location, so they're followed here and the scheme kept right.
std::string FollowRedirects(std::string url, std::stop_token stop) {
  for (int hop = 0; hop < 5 && !stop.stop_requested(); ++hop) {
    ix::HttpClient http;
    auto args = http.createRequest();
    args->followRedirects = false;
    args->connectTimeout = 5;
    args->transferTimeout = 5;
    // ngrok's free tier shows browsers a warning page instead
    args->extraHeaders["ngrok-skip-browser-warning"] = "1";
    auto response = http.get(ProbeUrl(url), args);
    int code = response->statusCode;
    auto location = response->headers.find("Location");
    if (code < 300 || code >= 400 || location == response->headers.end())
      break;
    std::string next = location->second;
    if (StartsWith(next, "/")) // relative: same host
      next = NetClient::HttpBase(url) + next;
    url = NetClient::WebSocketUrl(next);
  }
  return url;
}

} // namespace

std::string NetClient::WebSocketUrl(const std::string &url) {
  std::string out = url;
  if (StartsWith(out, "http://"))
    out = "ws://" + out.substr(7);
  else if (StartsWith(out, "https://"))
    out = "wss://" + out.substr(8);
  else if (!StartsWith(out, "ws://") && !StartsWith(out, "wss://"))
    out = "ws://" + out;
  // The server listens on /ws
  size_t host = out.find("//") + 2;
  if (out.find('/', host) == std::string::npos)
    out += "/ws";
  return out;
}

void NetClient::Connect(const std::string &url) {
  Close();
  m_ws->setExtraHeaders({{"ngrok-skip-browser-warning", "1"}});
  // Reconnecting is the session's job (it has to say hello and rejoin)
  m_ws->disableAutomaticReconnection();
  m_ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
    nlohmann::json j;
    switch (msg->type) {
    case ix::WebSocketMessageType::Open:
      m_open = true;
      j = {{"type", "_open"}};
      break;
    case ix::WebSocketMessageType::Close:
      m_open = false;
      j = {{"type", "_closed"}, {"reason", msg->closeInfo.reason}};
      break;
    case ix::WebSocketMessageType::Error:
      m_open = false;
      j = {{"type", "_closed"}, {"reason", msg->errorInfo.reason}};
      break;
    case ix::WebSocketMessageType::Message:
      j = nlohmann::json::parse(msg->str, nullptr, false);
      if (j.is_discarded() || !j.is_object() || !j.contains("type"))
        return;
      break;
    default:
      return;
    }
    std::lock_guard lock(m_mutex);
    m_inbox.push_back(std::move(j));
  });
  m_connecting = std::jthread([this, url](std::stop_token stop) {
    std::string target = FollowRedirects(WebSocketUrl(url), stop);
    if (stop.stop_requested())
      return;
    m_ws->setUrl(target);
    m_ws->start();
  });
}

void NetClient::Close() {
  if (m_connecting.joinable()) {
    m_connecting.request_stop();
    m_connecting.join();
  }
  if (m_ws)
    m_ws->stop();
  m_open = false;
}

void NetClient::Send(const nlohmann::json &message) {
  if (m_open)
    m_ws->sendText(message.dump());
}

std::vector<nlohmann::json> NetClient::Poll() {
  std::lock_guard lock(m_mutex);
  std::vector<nlohmann::json> out(std::make_move_iterator(m_inbox.begin()),
                                  std::make_move_iterator(m_inbox.end()));
  m_inbox.clear();
  return out;
}

std::string NetClient::HttpBase(const std::string &wsUrl) {
  std::string url = WebSocketUrl(wsUrl);
  if (url.rfind("ws://", 0) == 0)
    url = "http://" + url.substr(5);
  else if (url.rfind("wss://", 0) == 0)
    url = "https://" + url.substr(6);
  if (auto slash = url.find('/', url.find("//") + 2); slash != std::string::npos)
    url = url.substr(0, slash);
  return url;
}

std::future<std::optional<nlohmann::json>>
NetClient::GetJsonAsync(const std::string &url, const std::string &token) {
  return std::async(std::launch::async, [url, token]() -> std::optional<nlohmann::json> {
    ix::HttpClient http;
    auto args = http.createRequest();
    args->extraHeaders["Authorization"] = "Bearer " + token;
    args->connectTimeout = 5;
    args->transferTimeout = 20;
    args->followRedirects = true;
    args->extraHeaders["ngrok-skip-browser-warning"] = "1";
    auto response = http.get(url, args);
    if (response->statusCode != 200)
      return std::nullopt;
    auto j = nlohmann::json::parse(response->body, nullptr, false);
    if (j.is_discarded())
      return std::nullopt;
    return j;
  });
}

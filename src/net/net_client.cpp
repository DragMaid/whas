#include "whas/net/net_client.h"
#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

NetClient::NetClient() {
  ix::initNetSystem();
  m_ws = std::make_unique<ix::WebSocket>();
}

NetClient::~NetClient() { Close(); }

void NetClient::Connect(const std::string &url) {
  m_ws->stop();
  m_ws->setUrl(url);
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
  m_ws->start();
}

void NetClient::Close() {
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
  std::string url = wsUrl;
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
    auto response = http.get(url, args);
    if (response->statusCode != 200)
      return std::nullopt;
    auto j = nlohmann::json::parse(response->body, nullptr, false);
    if (j.is_discarded())
      return std::nullopt;
    return j;
  });
}

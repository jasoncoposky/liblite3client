#include "lite3-cpp/smart_client.hpp"
#include "document.hpp"
#include "json.hpp"
#include "lite3/ring.hpp"
#include <iostream>

namespace lite3 {

SmartClient::SmartClient(std::string_view seed_host, int seed_port)
    : seed_host_(seed_host), seed_port_(seed_port) {}

SmartClient::~SmartClient() = default;

Result<void> SmartClient::connect() {
  std::unique_lock lock(mutex_);
  return refresh_topology_unsafe();
}

Result<void> SmartClient::refresh_topology_unsafe() {
  try {
    Client seed(seed_host_, seed_port_);
    auto res = seed.impl_raw_get("/cluster/map");
    if (!res)
      return res.error();

    auto body = res.value();
    std::string json_str(body.begin(), body.end());
    lite3cpp::Buffer buf = lite3cpp::lite3_json::from_json_string(json_str);
    lite3cpp::Document doc(std::move(buf));
    auto root = doc.root_obj();

    ring_ = lite3::ConsistentHash();
    clients_.clear();

    if (root.contains("peers") &&
        root["peers"].type() == lite3cpp::Type::Array) {
      lite3cpp::Value peers_val = root["peers"];
      auto &arr = static_cast<lite3cpp::Array &>(peers_val);
      for (size_t i = 0; i < arr.size(); ++i) {
        auto p = arr[static_cast<uint32_t>(i)];
        if (p.type() != lite3cpp::Type::Object)
          continue;

        lite3cpp::Object p_obj = static_cast<lite3cpp::Object &>(p);

        uint32_t id = 0;
        if (p_obj["id"].type() == lite3cpp::Type::Int64)
          id = static_cast<uint32_t>(static_cast<int64_t>(p_obj["id"]));

        std::string host = "127.0.0.1";
        if (p_obj["host"].type() == lite3cpp::Type::String) {
          auto host_view = static_cast<std::string_view>(p_obj["host"]);
          host = std::string(host_view.data(), host_view.size());
          if (host == "0.0.0.0")
            host = "127.0.0.1";
        }

        int port = 8080;
        if (p_obj["http_port"].type() == lite3cpp::Type::Int64)
          port = static_cast<int>(static_cast<int64_t>(p_obj["http_port"]));

        if (id != 0) {
          ring_.add_node(id);
          clients_[id] = std::make_shared<Client>(host, port);
          std::cout << "SmartClient: Added node " << id << " (" << host << ":"
                    << port << ")\n";
        }
      }
    }
    return Result<void>();
  } catch (const std::exception &e) {
    return Error{ErrorCode::NetworkError, e.what()};
  }
}

std::shared_ptr<Client> SmartClient::get_client_for_key(std::string_view key) {
  std::shared_lock lock(mutex_);
  uint32_t node = ring_.get_node(key);
  auto it = clients_.find(node);
  if (it != clients_.end())
    return it->second;

  // Fallback?
  if (!clients_.empty())
    return clients_.begin()->second;
  return nullptr;
}

Result<void> SmartClient::put(std::string_view key, std::string_view value) {
  auto client = get_client_for_key(key);
  if (!client)
    return Error{ErrorCode::NetworkError, "No nodes available"};
  return client->put(key, value);
}

Result<void> SmartClient::put(std::string_view key,
                              const lite3cpp::Buffer &buf) {
  auto client = get_client_for_key(key);
  if (!client)
    return Error{ErrorCode::NetworkError, "No nodes available"};
  return client->put(key, buf);
}

Result<std::vector<uint8_t>> SmartClient::get(std::string_view key) {
  auto client = get_client_for_key(key);
  if (!client)
    return Error{ErrorCode::NetworkError, "No nodes available"};
  return client->get(key);
}

Result<void> SmartClient::del(std::string_view key) {
  auto client = get_client_for_key(key);
  if (!client)
    return Error{ErrorCode::NetworkError, "No nodes available"};
  return client->del(key);
}

Result<void> SmartClient::patch_int(std::string_view key,
                                    std::string_view field, int64_t value) {
  auto client = get_client_for_key(key);
  if (!client)
    return Error{ErrorCode::NetworkError, "No nodes available"};
  return client->patch_int(key, field, value);
}

Result<void> SmartClient::patch_str(std::string_view key,
                                    std::string_view field,
                                    std::string_view value) {
  auto client = get_client_for_key(key);
  if (!client)
    return Error{ErrorCode::NetworkError, "No nodes available"};
  return client->patch_str(key, field, value);
}

} // namespace lite3

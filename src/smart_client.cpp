#include "lite3/smart_client.hpp"
#include "lite3/ring.hpp" // Ensure ring is available
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace lite3 {

SmartClient::SmartClient(std::string_view seed_host, int seed_port)
    : seed_host_(seed_host), seed_port_(seed_port) {}

SmartClient::~SmartClient() = default;

Result<void> SmartClient::connect() {
  std::unique_lock lock(mutex_);
  return refresh_topology_unsafe();
}

Result<void> SmartClient::refresh_topology_unsafe() {
  // Connect to seed
  try {
    Client seed(seed_host_, seed_port_);

    // We need a way to do a raw GET request that isn't key-value based.
    // The current Client interface exposes put/get/del for KV only.
    // However, we can use the 'get' method with a special key if we hack it,
    // OR we add a proper raw method to Client.
    // HACK: The client::get appends "/kv/" to key.
    // We need "/cluster/map".
    // Let's rely on the implementation detail that we can path traverse?
    // "/kv/../cluster/map" -> "/cluster/map" ?
    // If server normalizes, maybe.

    // BETTER: Add `Client::raw_get(path)` ?
    // OR: Inherit Client logic.

    // Let's assume we can add raw_get to Client later. For now, let's use the
    // hack "ignore the prefix" if we can, but we can't easily. Since I am
    // editing liblite3client anyway, I will add `raw_get` to Client.

    // Wait, I can't invoke `refresh_topology_unsafe` without that change.
    // I'll proceed assuming I adding `raw_get` to `Client` in the next step.

    auto res = seed.impl_raw_get("/cluster/map"); // Need to expose this
    if (!res)
      return res.error();

    auto body = res.value();
    std::string json_str(body.begin(), body.end());
    json j = json::parse(json_str);

    // Rebuild ring
    ring_ = lite3::ConsistentHash();
    clients_.clear();

    if (j.contains("peers") && j["peers"].is_array()) {
      for (auto &p : j["peers"]) {
        uint32_t id = p.value("id", 0u);
        std::string host = p.value("host", "127.0.0.1");
        int port = p.value("http_port", 8080);

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

Result<lite3cpp::Buffer> SmartClient::get(std::string_view key) {
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

#include "lite3-cpp/smart_client.hpp"
#include "document.hpp"
#include "json.hpp"
#include "lite3/ring.hpp"
#include <iostream>

namespace lite3 {

SmartClient::SmartClient(std::string_view seed_host, int seed_port)
    : seed_host_(seed_host), seed_port_(seed_port) {
  ring_ = std::make_shared<ConsistentHash>();
  async_base_ = std::make_unique<AsyncClient>(1);
  coordinator_ = std::make_unique<WriteCoordinator>(*async_base_, ring_);
}

SmartClient::~SmartClient() = default;

Result<void> SmartClient::connect() {
  std::unique_lock lock(mutex_);
  return refresh_topology_unsafe();
}

Result<void> SmartClient::refresh_topology_unsafe() {
  try {
    Client seed(seed_host_, seed_port_);
    auto res = seed.impl_raw_get("/cluster/map");
    if (!res) {
      std::cerr << "SmartClient: Failed to get cluster map from " << seed_host_ << ":" << seed_port_ << " - " << res.error().message << std::endl;
      return res.error();
    }

    auto body = res.value();
    std::string json_str(body.begin(), body.end());
    std::cout << "SmartClient: Cluster map received: " << json_str << std::endl;
    std::cout << "DEBUG: Parsing cluster map JSON..." << std::endl;
    lite3cpp::Buffer buf = lite3cpp::lite3_json::from_json_string(json_str);
    std::cout << "DEBUG: Buffer created. Initializing Document..." << std::endl;
    lite3cpp::Document doc(std::move(buf));
    std::cout << "DEBUG: Document initialized. Getting root..." << std::endl;
    auto root = doc.root_obj();

    // Reset topology in ring and clients
    ring_ = std::make_shared<ConsistentHash>();
    clients_.clear();

    if (root.contains("peers") &&
        root["peers"].type() == lite3cpp::Type::Array) {
      std::cout << "DEBUG: Peer array found." << std::endl;
      for (uint32_t i = 0; ; ++i) {
        try {
          auto p = root["peers"][static_cast<int>(i)];
          if (p.type() == lite3cpp::Type::Null)
            break;
          if (p.type() != lite3cpp::Type::Object)
            continue;

          std::cout << "DEBUG: Processing peer " << i << std::endl;
          lite3cpp::Object p_obj = static_cast<lite3cpp::Object &>(p);
          uint32_t id = 0;
          if (p_obj["id"].type() == lite3cpp::Type::Int64)
            id = static_cast<uint32_t>(static_cast<int64_t>(p_obj["id"]));
          else if (p_obj["id"].type() == lite3cpp::Type::Float64)
            id = static_cast<uint32_t>(static_cast<double>(p_obj["id"]));

          std::string host = "127.0.0.1";
          if (p_obj["host"].type() == lite3cpp::Type::String) {
            auto host_view = static_cast<std::string_view>(p_obj["host"]);
            host = std::string(host_view.data(), host_view.size());
            if (host == "0.0.0.0") host = "127.0.0.1";
          }

          int port = 8080;
          if (p_obj["http_port"].type() == lite3cpp::Type::Int64)
            port = static_cast<int>(static_cast<int64_t>(p_obj["http_port"]));
          else if (p_obj["http_port"].type() == lite3cpp::Type::Float64)
            port = static_cast<int>(static_cast<double>(p_obj["http_port"]));

          if (id != 0) {
            ring_->add_node(id);
            clients_[id] = std::make_shared<Client>(host, port);
            async_base_->add_endpoint(id, host, port);
            std::cout << "SmartClient: Added node " << id << " (" << host << ":"
                      << port << ")\n";
          }
        } catch (const std::exception& e) {
          std::cerr << "DEBUG: Exception during peer loop: " << e.what() << std::endl;
          break;
        } catch (...) {
          std::cerr << "DEBUG: Unknown exception during peer loop" << std::endl;
          break;
        }
      }
    }
    std::cout << "DEBUG: Topology refresh complete." << std::endl;
    // Re-initialize coordinator with new ring
    coordinator_ = std::make_unique<WriteCoordinator>(*async_base_, ring_);
    std::cout << "DEBUG: Coordinator re-initialized." << std::endl;
    return Result<void>();
  } catch (const std::exception &e) {
    return Error{ErrorCode::NetworkError, e.what()};
  }
}

std::shared_ptr<Client> SmartClient::get_client_for_key(std::string_view key) {
  std::shared_lock lock(mutex_);
  if (!ring_) return nullptr;
  uint32_t node = ring_->get_node(key);
  auto it = clients_.find(node);
  if (it != clients_.end())
    return it->second;

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

Result<lite3cpp::Buffer> SmartClient::batch_get(const std::vector<std::string> &keys) {
  std::shared_lock lock(mutex_);
  if (clients_.empty())
    return Error{ErrorCode::NetworkError, "No nodes available"};

  std::map<uint32_t, std::vector<std::string>> node_keys;
  for (const auto &k : keys) {
    node_keys[ring_->get_node(k)].push_back(k);
  }

  lite3cpp::Buffer result;
  result.init_object();

  for (auto &[node_id, keys_list] : node_keys) {
    lite3cpp::Buffer batch_req;
    batch_req.init_object();
    size_t arr_ofs = batch_req.set_arr(0, "keys");
    for (const auto &k : keys_list) {
      batch_req.arr_append_str(arr_ofs, k);
    }

    auto it = clients_.find(node_id);
    std::shared_ptr<Client> client = (it != clients_.end()) ? it->second : clients_.begin()->second;
    
    auto res = client->batch_get(batch_req);
    if (res) {
        lite3cpp::Buffer sub_res(res.value());
        size_t root = 0;
        for (auto it_res = sub_res.begin(root); it_res != sub_res.end(root); ++it_res) {
            std::string key(it_res->key);
            auto type = sub_res.get_type(root, key);
            if (type == lite3cpp::Type::String) {
                result.set_str(0, key, sub_res.get_str(root, key));
            } else if (type == lite3cpp::Type::Bytes) {
                result.set_bytes(0, key, sub_res.get_bytes(root, key));
            }
        }
    }
  }
  return result;
}

Result<void> SmartClient::batch_put(const lite3cpp::Buffer &batch) {
  std::shared_lock lock(mutex_);
  if (clients_.empty())
    return Error{ErrorCode::NetworkError, "No nodes available"};

  std::map<uint32_t, lite3cpp::Buffer> node_batches;
  size_t root = 0;
  for (auto it = batch.begin(root); it != batch.end(root); ++it) {
    std::string key(it->key);
    uint32_t owner = ring_->get_node(key);
    if (!node_batches.contains(owner))
      node_batches[owner].init_object();

    auto type = batch.get_type(root, key);
    if (type == lite3cpp::Type::String) {
      node_batches[owner].set_str(0, key, batch.get_str(root, key));
    } else if (type == lite3cpp::Type::Bytes) {
      auto b = batch.get_bytes(root, key);
      node_batches[owner].set_bytes(0, key, b);
    }
  }

  for (auto &[node_id, sub_batch] : node_batches) {
    auto it = clients_.find(node_id);
    if (it != clients_.end()) {
      auto res = it->second->batch_put(sub_batch);
      if (!res) return res;
    } else {
      auto res = clients_.begin()->second->batch_put(sub_batch);
      if (!res) return res;
    }
  }
  return Result<void>();
}

std::future<Result<void>> SmartClient::put_async(std::string_view key, std::string_view value) {
  return coordinator_->batch_put(std::string(key), std::string(value));
}

std::future<Result<void>> SmartClient::put_async(std::string_view key, const lite3cpp::Buffer &buf) {
  std::string val(reinterpret_cast<const char*>(buf.data()), buf.size());
  return coordinator_->batch_put(std::string(key), val);
}

Result<std::vector<uint8_t>> SmartClient::get(std::string_view key) {
  auto client = get_client_for_key(key);
  if (!client)
    return Error{ErrorCode::NetworkError, "No nodes available"};
  return client->get(key);
}

std::future<Result<std::vector<uint8_t>>> SmartClient::get_async(std::string_view key) {
  printf("DEBUG: SmartClient::get_async starting for %s\n", std::string(key).c_str()); fflush(stdout);
  if (!ring_) { printf("DEBUG: ring_ is NULL!\n"); fflush(stdout); }
  uint32_t node = ring_->get_node(key);
  printf("DEBUG: Node for %s is %u\n", std::string(key).c_str(), node); fflush(stdout);
  if (!async_base_) { printf("DEBUG: async_base_ is NULL!\n"); fflush(stdout); }
  return async_base_->get(node, key);
}

Result<void> SmartClient::del(std::string_view key) {
  auto client = get_client_for_key(key);
  if (!client)
    return Error{ErrorCode::NetworkError, "No nodes available"};
  return client->del(key);
}

std::future<Result<void>> SmartClient::del_async(std::string_view key) {
  uint32_t node = ring_->get_node(key);
  return async_base_->del(node, key);
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

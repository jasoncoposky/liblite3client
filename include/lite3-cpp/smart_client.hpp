#pragma once

#include "async_client.hpp"
#include "write_coordinator.hpp"
#include "lite3/ring.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <future>

namespace lite3 {

class SmartClient {
public:
  SmartClient(std::string_view seed_host, int seed_port);
  ~SmartClient();

  // Connect to seed and fetch cluster topology
  Result<void> connect();

  // Synchronous API
  Result<void> put(std::string_view key, std::string_view value);
  Result<void> put(std::string_view key, const lite3cpp::Buffer &buf);
  Result<void> batch_put(const lite3cpp::Buffer &batch);
  Result<lite3cpp::Buffer> batch_get(const std::vector<std::string> &keys);
  Result<std::vector<uint8_t>> get(std::string_view key);
  Result<void> del(std::string_view key);

  Result<void> patch_int(std::string_view key, std::string_view field,
                         int64_t value);
  Result<void> patch_str(std::string_view key, std::string_view field,
                         std::string_view value);

  // Asynchronous API (High Performance)
  std::future<Result<void>> put_async(std::string_view key, std::string_view value);
  std::future<Result<void>> put_async(std::string_view key, const lite3cpp::Buffer &buf);
  std::future<Result<std::vector<uint8_t>>> get_async(std::string_view key);
  std::future<Result<void>> del_async(std::string_view key);

private:
  Result<void> refresh_topology_unsafe();
  std::shared_ptr<Client> get_client_for_key(std::string_view key);

  std::string seed_host_;
  int seed_port_;

  std::shared_mutex mutex_;
  std::shared_ptr<lite3::ConsistentHash> ring_;
  std::map<uint32_t, std::shared_ptr<Client>> clients_; // NodeID -> Client

  // Async Foundation
  std::unique_ptr<AsyncClient> async_base_;
  std::unique_ptr<WriteCoordinator> coordinator_;
};

} // namespace lite3

#pragma once

#include "client.hpp"
#include <lite3/ring.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace lite3 {

class SmartClient {
public:
  SmartClient(std::string_view seed_host, int seed_port);
  ~SmartClient();

  // Connect to seed and fetch cluster topology
  Result<void> connect();

  Result<void> put(std::string_view key, std::string_view value);
  Result<void> put(std::string_view key, const lite3cpp::Buffer &buf);
  Result<lite3cpp::Buffer> get(std::string_view key);
  Result<void> del(std::string_view key);

  Result<void> patch_int(std::string_view key, std::string_view field,
                         int64_t value);
  Result<void> patch_str(std::string_view key, std::string_view field,
                         std::string_view value);

private:
  Result<void> refresh_topology_unsafe();
  std::shared_ptr<Client> get_client_for_key(std::string_view key);

  std::string seed_host_;
  int seed_port_;

  std::shared_mutex mutex_;
  lite3::ConsistentHash ring_;
  std::map<uint32_t, std::shared_ptr<Client>> clients_; // NodeID -> Client
};

} // namespace lite3

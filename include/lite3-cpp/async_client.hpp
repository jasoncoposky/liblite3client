#pragma once

#include "client.hpp"
#include <zmq.hpp>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace lite3 {

using NodeID = uint32_t;

/**
 * @brief Asynchronous Client with ZeroMQ transport.
 * High-performance non-blocking I/O for standalone KV operations.
 */
class AsyncClient {
public:
  AsyncClient(size_t thread_count = 4);
  ~AsyncClient();

  void add_endpoint(NodeID node_id, const std::string &host, int port);

  // Core Async Operations
  std::future<Result<void>> put(NodeID node_id, std::string_view key, std::string_view value);
  std::future<Result<void>> put(NodeID node_id, std::string_view key, const lite3cpp::Buffer &buf);
  std::future<Result<void>> batch_put(NodeID node_id, const lite3cpp::Buffer &batch, std::vector<std::shared_ptr<std::promise<Result<void>>>> promises = {});
  std::future<Result<std::vector<uint8_t>>> get(NodeID node_id, std::string_view key);
  
  // Future extensions
  std::future<Result<void>> del(NodeID node_id, std::string_view key);
  std::future<Result<void>> patch_int(NodeID node_id, std::string_view key, std::string_view field, int64_t value);
  std::future<Result<void>> patch_str(NodeID node_id, std::string_view key, std::string_view field, std::string_view value);

private:
  struct Session {
    std::mutex mu;
    std::unique_ptr<zmq::socket_t> socket;
  };

  std::shared_ptr<Session> get_session(NodeID node_id);

  std::unordered_map<NodeID, std::shared_ptr<Session>> peer_sessions_;
  std::mutex endpoints_mutex_;

  zmq::context_t context_;
};

} // namespace lite3


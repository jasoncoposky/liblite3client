#pragma once

#include "async_client.hpp"
#include "lite3/ring.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lite3 {

/**
 * @brief Client-side Write Coordinator for automatic sharded batching.
 * Ported from L3KV Server internal WriteCoordinator.
 */
class WriteCoordinator {
public:
  struct BatchEntry {
    std::string key;
    std::string val;
  };

  struct BatchShard {
    std::mutex mu;
    std::vector<BatchEntry> buffer;
    std::vector<std::shared_ptr<std::promise<Result<void>>>> promises;
  };

  WriteCoordinator(AsyncClient &client, std::shared_ptr<ConsistentHash> ring);
  ~WriteCoordinator();

  std::future<Result<void>> batch_put(const std::string &key, const std::string &val);

private:
  void flush_loop();
  bool flush_shard(size_t shard_idx);

  AsyncClient &client_;
  std::shared_ptr<ConsistentHash> ring_;

  static constexpr size_t NUM_SHARDS = 64;
  std::unique_ptr<BatchShard[]> shards_;
  std::thread flush_thread_;
  std::atomic<bool> stop_flusher_{false};
  std::condition_variable cv_;
  std::mutex cv_mu_;
};

} // namespace lite3

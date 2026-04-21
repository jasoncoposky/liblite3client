#include "lite3-cpp/write_coordinator.hpp"
#include <map>

namespace lite3 {

WriteCoordinator::WriteCoordinator(AsyncClient &client, std::shared_ptr<ConsistentHash> ring)
    : client_(client), ring_(ring) {
  shards_ = std::make_unique<BatchShard[]>(NUM_SHARDS);
  flush_thread_ = std::thread(&WriteCoordinator::flush_loop, this);
}

WriteCoordinator::~WriteCoordinator() {
  stop_flusher_ = true;
  cv_.notify_all();
  if (flush_thread_.joinable())
    flush_thread_.join();
}

std::future<Result<void>> WriteCoordinator::batch_put(const std::string &key, const std::string &val) {
  auto prom = std::make_shared<std::promise<Result<void>>>();
  auto fut = prom->get_future();

  uint32_t owner = ring_ ? ring_->get_node(key) : 0;
  size_t shard_idx = owner % NUM_SHARDS;
  auto &shard = shards_[shard_idx];
  {
    std::lock_guard<std::mutex> lock(shard.mu);
    shard.buffer.push_back({key, val});
    shard.promises.push_back(prom);
  }
  cv_.notify_one();
  return fut;
}

void WriteCoordinator::flush_loop() {
  while (!stop_flusher_) {
    bool flushed_any = false;
    for (size_t i = 0; i < NUM_SHARDS; ++i) {
      if (flush_shard(i))
        flushed_any = true;
    }

    if (!flushed_any && !stop_flusher_) {
      std::unique_lock<std::mutex> lock(cv_mu_);
      cv_.wait_for(lock, std::chrono::milliseconds(10));
    } else {
      static int flush_count = 0;
      if (++flush_count % 100 == 0) {
          printf("DEBUG: WriteCoordinator flushed 100 batches\n"); fflush(stdout);
      }
      std::this_thread::yield();
    }
  }

  // Final flush
  for (size_t i = 0; i < NUM_SHARDS; ++i)
    while(flush_shard(i));
}

bool WriteCoordinator::flush_shard(size_t shard_idx) {
  std::vector<BatchEntry> to_flush;
  std::vector<std::shared_ptr<std::promise<Result<void>>>> promises;
  auto &shard = shards_[shard_idx];
  {
    std::lock_guard<std::mutex> lock(shard.mu);
    if (shard.buffer.empty())
      return false;
    to_flush.swap(shard.buffer);
    promises.swap(shard.promises);
  }

  // Group by destination NodeID
  std::map<uint32_t, lite3cpp::Buffer> node_batches;
  std::map<uint32_t, std::vector<std::shared_ptr<std::promise<Result<void>>>>> node_promises;

  for (size_t i = 0; i < to_flush.size(); ++i) {
    uint32_t owner = ring_ ? ring_->get_node(to_flush[i].key) : 0;
    if (!node_batches.contains(owner))
      node_batches[owner].init_object();
    
    // Efficiently set the bytes in the buffer
    node_batches[owner].set_str(0, to_flush[i].key, to_flush[i].val);
    node_promises[owner].push_back(promises[i]);
  }

  for (auto &[owner, batch_buf] : node_batches) {
    client_.batch_put(owner, batch_buf, std::move(node_promises[owner]));
  }

  return true;
}

} // namespace lite3

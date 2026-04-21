#include "lite3-cpp/client.hpp"
#include <zmq.hpp>
#include <zmq_addon.hpp>
#include <iostream>
#include <string>

namespace lite3 {

// --- PIMPL Implementation ---

class ClientImpl {
public:
  std::string endpoint_;
  zmq::context_t context_;
  zmq::socket_t socket_;

  ClientImpl(std::string_view host, int port)
      : context_(1), socket_(context_, ZMQ_DEALER) {
    endpoint_ = "tcp://" + std::string(host) + ":" + std::to_string(port);
    socket_.connect(endpoint_);
    
    // Set socket options for performance
    int linger = 0;
    socket_.set(zmq::sockopt::linger, linger);
    socket_.set(zmq::sockopt::sndhwm, 10000);
    socket_.set(zmq::sockopt::rcvhwm, 10000);
  }

  ~ClientImpl() {
    socket_.close();
  }

  Result<std::vector<uint8_t>>
  perform_op(char opcode, std::string_view key, const std::vector<uint8_t> &body = {}) {
    try {
      // ZeroMQ DEALER framing (DEALER automatically adds identity if needed, but we start with empty delimiter for the ROUTER)
      socket_.send(zmq::message_t(), zmq::send_flags::sndmore); // Delimiter
      socket_.send(zmq::message_t(&opcode, 1), zmq::send_flags::sndmore); // OpCode
      
      if (opcode == 'B') {
          // Batch Payload
          socket_.send(zmq::message_t(body.data(), body.size()), zmq::send_flags::none);
      } else {
          socket_.send(zmq::message_t(key.data(), key.size()), body.empty() ? zmq::send_flags::none : zmq::send_flags::sndmore);
          if (!body.empty()) {
              socket_.send(zmq::message_t(body.data(), body.size()), zmq::send_flags::none);
          }
      }

      // Receive Response
      std::vector<zmq::message_t> recv_msgs;
      auto received = zmq::recv_multipart(socket_, std::back_inserter(recv_msgs));
      if (!received || recv_msgs.size() < 2) {
          return Error{ErrorCode::NetworkError, "No response from ZMQ server"};
      }

      // First frame is the empty delimiter (matched to our request)
      auto& resp_msg = recv_msgs[1];
      
      if (resp_msg.to_string() == "Key not found") {
          return Error{ErrorCode::NotFound, "Key not found"};
      }

      std::vector<uint8_t> result(static_cast<uint8_t*>(resp_msg.data()), 
                                  static_cast<uint8_t*>(resp_msg.data()) + resp_msg.size());
      return result;
    } catch (const std::exception &e) {
      return Error{ErrorCode::NetworkError, e.what()};
    }
  }
};

// --- Client Methods ---

Client::Client(std::string_view host, int port)
    : impl_(std::make_unique<ClientImpl>(host, port)) {}

Client::~Client() = default;

Client::Client(Client &&) noexcept = default;
Client &Client::operator=(Client &&) noexcept = default;

Result<void> Client::put(std::string_view key, std::string_view value) {
  if (key.empty())
    return Error{ErrorCode::BadRequest, "Key cannot be empty"};

  std::vector<uint8_t> vec(value.begin(), value.end());
  auto res = impl_->perform_op('P', key, vec);
  if (!res) {
    return Result<void>(res.error());
  }
  return Result<void>();
}

Result<void> Client::put(std::string_view key, const lite3cpp::Buffer &buf) {
  if (key.empty())
    return Error{ErrorCode::BadRequest, "Key cannot be empty"};

  std::vector<uint8_t> vec(buf.data(), buf.data() + buf.size());
  auto res = impl_->perform_op('P', key, vec);
  if (!res) {
    return Result<void>(res.error());
  }
  return Result<void>();
}

Result<void> Client::batch_put(const lite3cpp::Buffer &batch) {
  std::vector<uint8_t> vec(batch.data(), batch.data() + batch.size());
  auto res = impl_->perform_op('B', "", vec);
  if (!res) {
    return Result<void>(res.error());
  }
  return Result<void>();
}

Result<std::vector<uint8_t>> Client::batch_get(const lite3cpp::Buffer &batch) {
  // Batch GET uses same framing but OpCode 'G' with payload (multi-key)
  std::vector<uint8_t> vec(batch.data(), batch.data() + batch.size());
  return impl_->perform_op('G', "", vec);
}

Result<std::vector<uint8_t>> Client::get(std::string_view key) {
  if (key.empty())
    return Error{ErrorCode::BadRequest, "Key cannot be empty"};

  auto res = impl_->perform_op('G', key);
  if (!res)
    return res.error();

  return std::move(res.value());
}

Result<void> Client::del(std::string_view key) {
  if (key.empty())
    return Error{ErrorCode::BadRequest, "Key cannot be empty"};

  auto res = impl_->perform_op('D', key); // OpCode 'D' for delete
  if (!res) {
    if (res.error().code == ErrorCode::NotFound)
      return Result<void>();
    return Result<void>(res.error());
  }
  return Result<void>();
}

Result<void> Client::patch_int(std::string_view key, std::string_view field,
                               int64_t value) {
  // PATCH is not yet fully implemented in ZmqServer, for now we skip or return error
  return Error{ErrorCode::Unknown, "PATCH_INT not implemented in ZeroMQ mode"};
}

Result<void> Client::patch_str(std::string_view key, std::string_view field,
                               std::string_view value) {
  return Error{ErrorCode::Unknown, "PATCH_STR not implemented in ZeroMQ mode"};
}

Result<std::vector<uint8_t>> Client::impl_raw_get(std::string_view path) {
    return Error{ErrorCode::Unknown, "RAW_GET not supported in ZeroMQ mode"};
}

} // namespace lite3

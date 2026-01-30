#pragma once

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include <buffer.hpp>

namespace lite3 {

// --- Error Handling ---

enum class ErrorCode {
  ConnectionRefused,
  NetworkError,
  Timeout,
  BadRequest,
  NotFound,
  ServerError,
  SerializationError,
  Unknown
};

struct Error {
  ErrorCode code;
  std::string message;
};

template <typename T> class Result {
  std::variant<T, Error> val_;

public:
  Result(T val) : val_(std::move(val)) {}
  Result(Error err) : val_(std::move(err)) {}

  bool has_value() const { return std::holds_alternative<T>(val_); }
  const T &value() const {
    if (!has_value())
      throw std::runtime_error("Result has no value: " +
                               std::get<Error>(val_).message);
    return std::get<T>(val_);
  }
  const Error &error() const { return std::get<Error>(val_); }

  // Monadic-like check
  operator bool() const { return has_value(); }
};

// Void specialization
template <> class Result<void> {
  std::optional<Error> err_;

public:
  Result() {}
  Result(Error err) : err_(std::move(err)) {}

  bool has_value() const { return !err_.has_value(); }
  void value() const {
    if (!has_value())
      throw std::runtime_error("Result has error: " + err_->message);
  }
  const Error &error() const { return *err_; }
  operator bool() const { return has_value(); }
};

// --- Forward Declarations ---
class ClientImpl;

// --- Client Class ---

class Client;

// Proxy object for map-like syntax: client["key"]
class KeyProxy {
  Client &client_;
  std::string key_;

public:
  KeyProxy(Client &c, std::string_view k) : client_(c), key_(k) {}

  // Assignment -> PUT
  // Overload for string types to bypass JSON serialization (efficient & safe)
  KeyProxy &operator=(std::string_view val);
};

class Client {
  friend class KeyProxy;
  std::unique_ptr<ClientImpl> impl_;

public:
  Client(std::string_view host, int port);
  ~Client();

  // Copying a client is expensive (new connection), moving is fine.
  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;
  Client(Client &&) noexcept;
  Client &operator=(Client &&) noexcept;

  // --- Map-like Access ---
  KeyProxy operator[](std::string_view key) { return KeyProxy(*this, key); }

  // --- Core Operations (snake_case) ---

  // Raw String/Bytes operations
  Result<void> put(std::string_view key, std::string_view value);
  Result<void> put(std::string_view key, const lite3cpp::Buffer &buf);
  Result<lite3cpp::Buffer> get(std::string_view key);
  Result<void> del(std::string_view key);

  // Helper to check existence
  bool contains(std::string_view key) { return get(key).has_value(); }

  Result<void> patch_int(std::string_view key, std::string_view field,
                         int64_t value);
  Result<void> patch_str(std::string_view key, std::string_view field,
                         std::string_view value);
};

// --- Implementations of Proxy templates ---

inline KeyProxy &KeyProxy::operator=(std::string_view val) {
  auto res = client_.put(key_, val);
  if (!res) {
    throw std::runtime_error("Lite3 Client Error (PUT " + key_ +
                             "): " + res.error().message);
  }
  return *this;
}

} // namespace lite3

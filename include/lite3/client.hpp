#pragma once

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

// We use nlohmann/json for the 'Serializable' concept and object support
#include <nlohmann/json.hpp>

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

// --- Concepts ---

// Concept for types that nlohmann::json can serialize
template <typename T>
concept Serializable = requires(T a) {
  { nlohmann::json(a) } -> std::convertible_to<nlohmann::json>;
};

// Concept for types that nlohmann::json can deserialize
template <typename T>
concept Deserializable = requires(const nlohmann::json &j, T &a) {
  { j.get_to(a) };
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
  // If PUT fails, this might throw or logging error depending on preference.
  // For a map-like feel, we usually expect it to "just work" or throw exception
  // on critical failure.
  template <Serializable T>
    requires(!std::convertible_to<T, std::string_view>)
  KeyProxy &operator=(const T &val);

  // Overload for string types to bypass JSON serialization (efficient & safe)
  KeyProxy &operator=(std::string_view val);

  // Casting -> GET
  template <Deserializable T> operator T() const;

  // Explicit conversion helper
  template <Deserializable T> T as() const;
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
  Result<std::string> get(std::string_view key);
  Result<void> del(std::string_view key);

  // JSON Object operations
  template <Serializable T>
    requires(!std::convertible_to<T, std::string_view>)
  Result<void> put(std::string_view key, const T &obj) {
    try {
      nlohmann::json j = obj;
      return put(key, j.dump());
    } catch (const std::exception &e) {
      return Error{ErrorCode::SerializationError, e.what()};
    }
  }

  template <Deserializable T> Result<T> get_as(std::string_view key) {
    auto res = get(key);
    if (!res)
      return res.error();

    try {
      auto j = nlohmann::json::parse(res.value());
      T obj = j.get<T>();
      return obj;
    } catch (const std::exception &e) {
      return Error{ErrorCode::SerializationError, e.what()};
    }
  }

  // Helper to check existence
  bool contains(std::string_view key) { return get(key).has_value(); }

  Result<void> patch_int(std::string_view key, std::string_view field,
                         int64_t value);
};

// --- Implementations of Proxy templates ---

template <Serializable T>
  requires(!std::convertible_to<T, std::string_view>)
KeyProxy &KeyProxy::operator=(const T &val) {
  auto res = client_.put(key_, val);
  if (!res) {
    throw std::runtime_error("Lite3 Client Error (PUT " + key_ +
                             "): " + res.error().message);
  }
  return *this;
}

inline KeyProxy &KeyProxy::operator=(std::string_view val) {
  auto res = client_.put(key_, val);
  if (!res) {
    throw std::runtime_error("Lite3 Client Error (PUT " + key_ +
                             "): " + res.error().message);
  }
  return *this;
}

template <Deserializable T> KeyProxy::operator T() const {
  auto res = client_.get_as<T>(key_);
  if (!res) {
    throw std::runtime_error("Lite3 Client Error (GET " + key_ +
                             "): " + res.error().message);
  }
  return res.value();
}

template <Deserializable T> T KeyProxy::as() const {
  return static_cast<T>(*this);
}

} // namespace lite3

#include "lite3/client.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <string>

namespace beast = boost::beast; // from <boost/beast.hpp>
namespace http = beast::http;   // from <boost/beast/http.hpp>
namespace net = boost::asio;    // from <boost/asio.hpp>
using tcp = net::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

namespace lite3 {

// --- PIMPL Implementation ---

class ClientImpl {
public:
  std::string host_;
  std::string port_;
  net::io_context ioc_;
  tcp::resolver resolver_;
  beast::tcp_stream stream_;

  ClientImpl(std::string_view host, int port)
      : host_(host), port_(std::to_string(port)), ioc_(), resolver_(ioc_),
        stream_(ioc_) {}

  // Helper to perform a single request
  // Beast is lower-level; we need to connect, write, read.
  // Ideally we keep the connection open (Keep-Alive).
  Result<std::string> perform_request(http::verb method,
                                      std::string_view target,
                                      std::string_view body = "") {
    try {
      // Check if connected, if not connect.
      // For simplicity in this first pass, we might reconnect every time or try
      // to reuse. Let's try to reuse, but handle disconnection.
      if (!stream_.socket().is_open()) {
        connect();
      }

      // Set up an HTTP request message
      http::request<http::string_body> req{method, std::string(target), 11};
      req.set(http::field::host, host_);
      req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
      req.set(http::field::content_type, "application/octet-stream");
      if (!body.empty()) {
        req.body() = std::string(body);
        req.prepare_payload();
      } else {
        req.prepare_payload();
      }

      // Send the HTTP request to the remote host
      http::write(stream_, req);

      // This buffer is used for reading and must be persisted
      beast::flat_buffer buffer;

      // Declare a container to hold the response
      http::response<http::string_body> res;

      // Receive the HTTP response
      http::read(stream_, buffer, res);

      // Handle status codes
      if (res.result() == http::status::ok) {
        return res.body();
      } else if (res.result() == http::status::not_found) {
        return Error{ErrorCode::NotFound, "Key not found"};
      } else {
        return Error{ErrorCode::ServerError,
                     "Server error: " + std::to_string(res.result_int())};
      }

    } catch (const std::exception &e) {
      // If we failed, maybe connection closed? specific logic could go here.
      // For now, return network error.
      // Try to close gracefully if possible
      beast::error_code ec;
      stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
      stream_.close();

      return Error{ErrorCode::NetworkError, e.what()};
    }
  }

  void connect() {
    auto const results = resolver_.resolve(host_, port_);
    stream_.connect(results);
  }

  // Explicitly close
  void close() {
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
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
  std::string path = "/kv/";
  path.append(key);

  auto res = impl_->perform_request(http::verb::put, path, value);
  if (!res) {
    // If it was a network error, maybe retry once?
    // simple implementation for now.
    return Result<void>(res.error());
  }
  return Result<void>();
}

Result<std::string> Client::get(std::string_view key) {
  if (key.empty())
    return Error{ErrorCode::BadRequest, "Key cannot be empty"};
  std::string path = "/kv/";
  path.append(key);

  auto res = impl_->perform_request(http::verb::get, path);
  if (!res)
    return res.error();
  return res.value();
}

Result<void> Client::del(std::string_view key) {
  if (key.empty())
    return Error{ErrorCode::BadRequest, "Key cannot be empty"};
  std::string path = "/kv/";
  path.append(key);

  // DELETE usually returns 200 or 404. Our helper returns error on 404.
  // We should treat 404 as success for delete (idempotency).
  auto res = impl_->perform_request(http::verb::delete_, path);
  if (!res) {
    if (res.error().code == ErrorCode::NotFound)
      return Result<void>();
    return Result<void>(res.error());
  }
  return Result<void>();
}

Result<void> Client::patch_int(std::string_view key, std::string_view field,
                               int64_t value) {
  if (key.empty())
    return Error{ErrorCode::BadRequest, "Key cannot be empty"};

  std::string path = "/kv/";
  path.append(key);
  path += "?op=set_int&field=" + std::string(field) +
          "&val=" + std::to_string(value);

  auto res = impl_->perform_request(http::verb::post, path);
  if (!res)
    return Result<void>(res.error());
  return Result<void>();
}

} // namespace lite3

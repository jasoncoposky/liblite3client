#include "lite3/client.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <iostream>
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

  // Helper to parse URL: http://host:port/path
  // Very basic for internal redirects
  bool parse_location(const std::string &loc, std::string &out_host,
                      int &out_port, std::string &out_target) {
    if (loc.rfind("http://", 0) != 0)
      return false;
    auto host_start = loc.begin() + 7;
    auto port_sep = std::find(host_start, loc.end(), ':');
    if (port_sep == loc.end())
      return false;
    auto path_sep = std::find(port_sep, loc.end(), '/');

    out_host = std::string(host_start, port_sep);
    std::string port_str(port_sep + 1, path_sep);
    try {
      out_port = std::stoi(port_str);
    } catch (...) {
      return false;
    }

    if (path_sep != loc.end())
      out_target = std::string(path_sep, loc.end());
    else
      out_target = "/";
    return true;
  }

  // Helper to perform a single request
  // Beast is lower-level; we need to connect, write, read.
  // Ideally we keep the connection open (Keep-Alive).
  Result<std::vector<uint8_t>>
  perform_request(http::verb method, std::string_view target,
                  const std::vector<uint8_t> &body = {}, int depth = 0) {
    if (depth > 5) {
      return Error{ErrorCode::NetworkError, "Too many redirects"};
    }

    try {
      // Check if connected, if not connect.
      if (!stream_.socket().is_open()) {
        // std::cout << "Client: Reconnecting to " << host_ << ":" << port_ <<
        // "\n"; // DEBUG
        connect();
      }

      // Set up an HTTP request message
      http::request<http::vector_body<uint8_t>> req{method, std::string(target),
                                                    11};
      req.set(http::field::host, host_);
      req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
      req.set(http::field::content_type, "application/octet-stream");
      req.keep_alive(true);
      if (!body.empty()) {
        req.body() = body;
        req.prepare_payload();
      } else {
        req.prepare_payload();
      }

      // Send the HTTP request to the remote host
      http::write(stream_, req);

      // This buffer is used for reading and must be persisted
      beast::flat_buffer buffer;

      // Declare a container to hold the response
      http::response<http::vector_body<uint8_t>> res;

      // Receive the HTTP response
      http::read(stream_, buffer, res);

      // Handle status codes
      if (res.result() == http::status::ok) {
        return std::move(res.body());
      } else if (res.result() == http::status::temporary_redirect) {
        // Follow Redirect
        auto loc_it = res.find(http::field::location);
        if (loc_it != res.end()) {
          std::string location(loc_it->value());
          std::string new_host;
          int new_port = 0;
          std::string new_target;
          if (parse_location(location, new_host, new_port, new_target)) {
            // Create one-off client to follow redirect
            // We don't want to close OUR connection.
            ClientImpl temp_client(new_host, new_port);
            return temp_client.perform_request(method, new_target, body,
                                               depth + 1);
          }
        }
        return Error{ErrorCode::ServerError, "Invalid Redirect Location"};

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
    stream_.socket().set_option(tcp::no_delay(true));
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

  std::vector<uint8_t> vec(value.begin(), value.end());
  auto res = impl_->perform_request(http::verb::put, path, vec);
  if (!res) {
    return Result<void>(res.error());
  }
  return Result<void>();
}

Result<void> Client::put(std::string_view key, const lite3cpp::Buffer &buf) {
  if (key.empty())
    return Error{ErrorCode::BadRequest, "Key cannot be empty"};
  std::string path = "/kv/";
  path.append(key);

  // Buffer data is already a vector<uint8_t>
  std::vector<uint8_t> vec(buf.data(), buf.data() + buf.size());
  auto res = impl_->perform_request(http::verb::put, path, vec);
  if (!res) {
    return Result<void>(res.error());
  }
  return Result<void>();
}

Result<lite3cpp::Buffer> Client::get(std::string_view key) {
  if (key.empty())
    return Error{ErrorCode::BadRequest, "Key cannot be empty"};
  std::string path = "/kv/";
  path.append(key);

  auto res = impl_->perform_request(http::verb::get, path);
  if (!res)
    return res.error();
  return lite3cpp::Buffer(std::move(res.value()));
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

Result<void> Client::patch_str(std::string_view key, std::string_view field,
                               std::string_view value) {
  if (key.empty())
    return Error{ErrorCode::BadRequest, "Key cannot be empty"};

  std::string path = "/kv/";
  path.append(key);
  // Simple URL parameter construction (assuming safe characters for benchmark)
  path +=
      "?op=set_str&field=" + std::string(field) + "&val=" + std::string(value);

  auto res = impl_->perform_request(http::verb::post, path);
  if (!res)
    return Result<void>(res.error());
  return Result<void>();
}

Result<std::vector<uint8_t>> Client::impl_raw_get(std::string_view path) {
  auto res = impl_->perform_request(http::verb::get, path);
  if (!res)
    return res.error();
  return std::move(res.value());
}

} // namespace lite3

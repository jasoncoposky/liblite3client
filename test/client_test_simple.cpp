#include "lite3/client.hpp"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <winsock2.h> // Add this

using json = nlohmann::json;

// Simple test runner since GTest might be tricky to locate without full setup
// We will build this as a standalone executable to verify the library.

struct UserConfig {
  int id;
  std::string name;
  std::vector<std::string> roles;

  // Serialization for nlohmann::json
  NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserConfig, id, name, roles)
};

void fail(const std::string &msg) {
  std::cerr << "[FAIL] " << msg << std::endl;
  exit(1);
}

void assert_true(bool cond, const std::string &msg) {
  if (!cond)
    fail(msg);
}

void test_raw_socket() {
  std::cout << "[Test] Raw Socket Test..." << std::endl;
  SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET)
    fail("socket failed");

  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8080);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    fail("raw connect failed: " + std::to_string(WSAGetLastError()));
  }

  std::string req = "PUT /kv/raw_key HTTP/1.1\r\nHost: "
                    "127.0.0.1\r\nContent-Length: 5\r\n\r\nHello";
  send(s, req.c_str(), req.size(), 0);

  char buf[1024];
  int n = recv(s, buf, sizeof(buf), 0);
  if (n <= 0)
    fail("raw recv failed");
  buf[n] = 0;
  std::cout << "[Test] Raw response: " << buf << std::endl;
  closesocket(s);
}

int main() {
  // Manually init winsock
  WSADATA wsaData;
  int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (err != 0) {
    std::cerr << "WSAStartup failed: " << err << std::endl;
    return 1;
  }

  std::cout << "[Test] Starting Lite3 Client Integration Test..." << std::endl;

  test_raw_socket(); // Run raw test first
  std::cout << "[Test] Raw socket test passed. Proceeding to Client lib..."
            << std::endl;

  try {
    // Use 127.0.0.1 to avoid potential localhost resolution issues
    lite3::Client db("127.0.0.1", 8080);

    // 1. Basic PUT/GET
    std::cout << "[Test] 1. Basic PUT/GET" << std::endl;
    std::string val;
    // Verify JSON library stability
    try {
      json j_test = {{"val", "Hello Lite3 JSON"}};
      val = j_test.dump();
      std::cout << "[Test] JSON dump success: " << val << std::endl;

      auto res = db.put("user:1", std::string_view(val));
      if (!res) {
        fail("Put failed: " + res.error().message);
      }
    } catch (const std::exception &e) {
      fail(std::string("JSON Exception: ") + e.what());
    }
    std::cout << "[Test] Put success" << std::endl;

    auto get_res = db.get("user:1");
    if (!get_res) {
      fail("Get failed: " + get_res.error().message);
    }

    if (get_res.value() != val) {
      fail("Get mismatch: " + get_res.value());
    }
    std::cout << "[Test] Get success: " << get_res.value() << std::endl;

    // 2. Map-like Syntax
    std::cout << "[Test] 2. Map-like Syntax" << std::endl;
    db["test_key_char_valid"] = "{\"v\":\"Map Value\"}";
    std::string s = db["test_key_char_valid"];
    assert_true(s == "{\"v\":\"Map Value\"}", "Map syntax get failed");

    // 3. Object Serialization
    std::cout << "[Test] 3. Object Serialization" << std::endl;
    UserConfig u{101, "Alice", {"admin", "editor"}};
    db.put("user:101", u);

    UserConfig u2 = db.get_as<UserConfig>("user:101").value();
    assert_true(u2.id == 101, "Object ID mismatch");
    assert_true(u2.name == "Alice", "Object Name mismatch");
    assert_true(u2.roles.size() == 2, "Object Rules size mismatch");

    // 4. Map-like Object
    std::cout << "[Test] 4. Map-like Object" << std::endl;
    db["user:102"] = UserConfig{102, "Bob", {"viewer"}};
    UserConfig u3 = db["user:102"]; // implicit cast
    assert_true(u3.name == "Bob", "Map-object mismatch");

    // 5. Delete
    std::cout << "[Test] 5. Delete" << std::endl;
    db.del("test_key_1");
    assert_true(!db.contains("test_key_1"), "Delete failed, key still exists");

    std::cout << "[PASS] All tests passed!" << std::endl;

  } catch (const std::exception &e) {
    fail("Exception: " + std::string(e.what()));
  }

  return 0;
}

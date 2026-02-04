# liblite3client

![lite3client logo](lite3client_logo.png "")


A modern C++20 client library for the **Lite3** Key-Value Service.

## Features
- **Modern API**: Python-like `db["key"]` syntax for ease of use.
- **High Performance**: Built on `Boost.Beast` for robust, asynchronous networking.
- **Zero-Parse**: Raw `lite3cpp::Buffer` API bypasses all JSON parsing overhead.
- **Efficient**: Zero-copy raw string API (`put`, `get`) and `patch_str` support.
- **Reliable**: Automatic connection management and error handling.

## Requirements
- C++20 compatible compiler
- Boost (Asio, Beast)
- lite3-cpp (for Buffer API)

## Usage

```cpp
#include <lite3/client.hpp>

int main() {
    lite3::Client db("127.0.0.1", 8080);

    // 1. Raw String Operations (Zero-Copy, Fast)
    db["user:1"] = "Alice";
    std::string name = db["user:1"]; 

    // 2. JSON Object Serialization
    UserConfig config{1, "admin"};
    db.put("config:1", config); 

    // 3. Explicit Methods
    auto res = db.get("user:1");
    if (res) {
        std::cout << "Got: " << *res << "\n";
    }
}
```

## Build
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

# liblite3client

A modern C++20 client library for the **Lite3** Key-Value Service.

## Features
- **Modern API**: Python-like `db["key"]` syntax for ease of use.
- **High Performance**: Built on `Boost.Beast` for robust, asynchronous networking.
- **Efficient**: Zero-copy raw string API (`put`, `get`) bypasses serialization overhead.
- **Type-Safe**: Template support for JSON object serialization (via `nlohmann/json`).
- **Reliable**: Automatic connection management and error handling.

## Requirements
- C++20 compatible compiler
- Boost (Asio, Beast)
- nlohmann/json

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

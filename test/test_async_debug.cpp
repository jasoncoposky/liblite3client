#include "lite3-cpp/smart_client.hpp"
#include <iostream>

int main() {
    std::cout << "Starting Test..." << std::endl;
    try {
        lite3::SmartClient client("127.0.0.1", 8080);
        std::cout << "Connecting..." << std::endl;
        auto res = client.connect();
        if (!res) {
            std::cerr << "Connect failed: " << res.error().message << std::endl;
            return 1;
        }
        std::cout << "Connected!" << std::endl;

        std::cout << "Async Put..." << std::endl;
        auto fut = client.put_async("test_key", "test_val");
        std::cout << "Waiting for future..." << std::endl;
        auto put_res = fut.get();
        if (!put_res) {
            std::cerr << "Put failed: " << put_res.error().message << std::endl;
            return 1;
        }
        std::cout << "Put Success!" << std::endl;

        std::cout << "Async Get..." << std::endl;
        auto get_fut = client.get_async("test_key");
        auto get_res = get_fut.get();
        if (!get_res) {
            std::cerr << "Get failed: " << get_res.error().message << std::endl;
            return 1;
        }
        std::cout << "Get Success: " << std::string(get_res.value().begin(), get_res.value().end()) << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

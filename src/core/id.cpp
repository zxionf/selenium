#include "core/id.hpp"

#include "core/hash.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <regex>

namespace core::id {

namespace {

constexpr const char* kCrockford = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

std::string encode_ulid(const std::array<std::uint8_t, 16>& bytes) {
    std::array<std::uint8_t, 16> data = bytes;
    std::string out(26, '0');
    for (int index = 25; index >= 0; --index) {
        std::uint32_t remainder = 0;
        for (std::size_t byte = 0; byte < data.size(); ++byte) {
            const std::uint32_t current = (remainder << 8) | data[byte];
            data[byte] = static_cast<std::uint8_t>(current / 32);
            remainder = current % 32;
        }
        out[static_cast<std::size_t>(index)] = kCrockford[remainder];
    }
    return out;
}

} // namespace

std::string new_ulid() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    auto millis = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());

    std::array<std::uint8_t, 16> bytes{};
    for (int index = 5; index >= 0; --index) {
        bytes[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(millis & 0xFFu);
        millis >>= 8;
    }

    std::random_device device;
    for (std::size_t index = 6; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(device() & 0xFFu);
    }
    return encode_ulid(bytes);
}

std::string new_task_uid() { return "tsk_" + new_ulid(); }

std::string hash16(const std::string& data) { return core::hash::sha256_hex(data).substr(0, 16); }

bool valid_name(const std::string& value) {
    static const std::regex pattern("^[A-Za-z0-9][A-Za-z0-9_-]{0,40}$");
    return std::regex_match(value, pattern);
}

std::string task_bucket(const std::string& uid) { return hash16(uid).substr(0, 2); }

std::string task_file(const std::string& uid) { return hash16(uid).substr(2); }

} // namespace core::id

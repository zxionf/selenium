#pragma once

#include <string>

namespace core::id {

// 26-char Crockford Base32 ULID (48-bit timestamp + 80-bit randomness).
std::string new_ulid();

// Task uid: "tsk_" + ULID. Immutable, used for storage.
std::string new_task_uid();

// First 16 hex chars of sha256(data); used for task file bucketing.
std::string hash16(const std::string& data);

// Semantic id: ^[A-Za-z0-9][A-Za-z0-9_-]{0,40}$ (no dots, no slashes).
bool valid_name(const std::string& value);

// Returns the parent directory bucket and file name for a task uid.
std::string task_bucket(const std::string& uid);
std::string task_file(const std::string& uid);

} // namespace core::id

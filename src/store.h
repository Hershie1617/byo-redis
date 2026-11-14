#pragma once

#include <string>
#include <unordered_map>
#include <optional>

// KVStore manages the in-memory key-value dictionary for Redis string commands.
// It supports SET and GET operations.
class KVStore {
public:
    KVStore() = default;

    // Sets key to hold the given string value.
    // If key already holds a value, it is overwritten.
    void set(const std::string& key, const std::string& value);

    // Gets the value of key.
    // Returns std::nullopt if the key does not exist in the store.
    std::optional<std::string> get(const std::string& key) const;

private:
    // We use std::unordered_map because Redis is an in-memory key-value database
    // requiring O(1) average-time complexity for lookups and insertions.
    std::unordered_map<std::string, std::string> data_;
};

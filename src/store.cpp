#include "store.h"

// Inserts or overwrites the key-value pair.
// In an interview: Explain that std::unordered_map operator[] or insert_or_assign
// will update an existing key in place or insert a new bucket in O(1) average time.
void KVStore::set(const std::string& key, const std::string& value) {
    data_[key] = value;
}

// Looks up a key without modifying the map.
// In an interview: Explain why find() is used instead of operator[] here:
// 1. KVStore::get is const-qualified, whereas operator[] is non-const because it default-inserts missing keys.
// 2. find() lets us cleanly distinguish between a key that exists vs one that does not,
//    returning std::nullopt (cache miss) so Redis can emit a RESP null bulk string ($-1\r\n).
std::optional<std::string> KVStore::get(const std::string& key) const {
    auto it = data_.find(key);
    if (it != data_.end()) {
        return it->second;
    }
    return std::nullopt;
}

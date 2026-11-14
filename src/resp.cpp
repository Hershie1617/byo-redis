#include "resp.h"
#include <cctype>
#include <cstdlib>

// Helper to safely parse an integer string without throwing exceptions.
// In an interview: Explain that std::stoi throws std::invalid_argument / std::out_of_range.
// In network protocol parsing, we avoid throwing exceptions on invalid client input
// to prevent denial-of-service (DoS) via exception overhead.
static bool parse_int(const std::string& s, long long& out) {
    if (s.empty()) {
        return false;
    }
    size_t idx = 0;
    bool negative = false;
    if (s[0] == '-') {
        negative = true;
        idx = 1;
        if (s.size() == 1) return false;
    }

    long long val = 0;
    for (; idx < s.size(); ++idx) {
        if (!std::isdigit(static_cast<unsigned char>(s[idx]))) {
            return false;
        }
        val = val * 10 + (s[idx] - '0');
    }
    out = negative ? -val : val;
    return true;
}

// Streaming parser for RESP array of bulk strings (*<count>\r\n$<len>\r\n<data>\r\n...).
// Designed to handle TCP packet fragmentation: if the buffer holds only part of a command,
// it returns ParseStatus::Incomplete so the caller can wait for the next socket read.
ParseResult parse_resp_command(const std::string& buffer) {
    ParseResult result;
    if (buffer.empty()) {
        result.status = ParseStatus::Incomplete;
        return result;
    }

    // In RESP, arrays always begin with '*'
    if (buffer[0] != '*') {
        result.status = ParseStatus::Error;
        return result;
    }

    // Look for the end of the array count line (\r\n)
    size_t crlf = buffer.find("\r\n", 0);
    if (crlf == std::string::npos) {
        // We have not received the full array header line yet
        result.status = ParseStatus::Incomplete;
        return result;
    }

    long long count = 0;
    std::string count_str = buffer.substr(1, crlf - 1);
    if (!parse_int(count_str, count) || count <= 0) {
        // Redis commands must have at least 1 element (the command name)
        result.status = ParseStatus::Error;
        return result;
    }

    // Advance cursor past "*<count>\r\n"
    size_t pos = crlf + 2;
    std::vector<std::string> args;
    args.reserve(count);

    for (long long i = 0; i < count; ++i) {
        // Check if we have enough bytes for at least "$" and "\r\n"
        if (pos >= buffer.size()) {
            result.status = ParseStatus::Incomplete;
            return result;
        }

        // Each command argument in RESP must start with '$' (Bulk String)
        if (buffer[pos] != '$') {
            result.status = ParseStatus::Error;
            return result;
        }

        size_t len_crlf = buffer.find("\r\n", pos);
        if (len_crlf == std::string::npos) {
            // Bulk string length header not fully received
            result.status = ParseStatus::Incomplete;
            return result;
        }

        long long str_len = 0;
        std::string len_str = buffer.substr(pos + 1, len_crlf - (pos + 1));
        if (!parse_int(len_str, str_len) || str_len < 0) {
            // Null or negative length arguments are invalid in client command arrays
            result.status = ParseStatus::Error;
            return result;
        }

        // Advance cursor past "$<len>\r\n"
        pos = len_crlf + 2;

        // Ensure the full payload plus trailing \r\n is in the buffer
        // Size required from pos: str_len bytes of payload + 2 bytes for \r\n
        if (buffer.size() < pos + static_cast<size_t>(str_len) + 2) {
            // Full bulk string data has not arrived yet
            result.status = ParseStatus::Incomplete;
            return result;
        }

        // Verify trailing delimiter is \r\n
        if (buffer[pos + str_len] != '\r' || buffer[pos + str_len + 1] != '\n') {
            result.status = ParseStatus::Error;
            return result;
        }

        // Extract the bulk string payload
        args.emplace_back(buffer.data() + pos, static_cast<size_t>(str_len));

        // Advance cursor past "<payload>\r\n"
        pos += static_cast<size_t>(str_len) + 2;
    }

    // Entire command successfully parsed
    result.status = ParseStatus::Ok;
    result.args = std::move(args);
    result.bytes_consumed = pos;
    return result;
}

// Formats a Simple String: +<msg>\r\n
// Used for status replies such as +OK\r\n or +PONG\r\n
std::string serialize_simple_string(const std::string& msg) {
    return "+" + msg + "\r\n";
}

// Formats a Bulk String: $<length>\r\n<data>\r\n
// Used for string values (e.g., GET responses, ECHO payload)
std::string serialize_bulk_string(const std::string& data) {
    return "$" + std::to_string(data.size()) + "\r\n" + data + "\r\n";
}

// Formats a Null Bulk String: $-1\r\n
// Sent by Redis when a requested key does not exist (cache miss)
std::string serialize_null_bulk_string() {
    return "$-1\r\n";
}

// Formats a Simple Error: -ERR <msg>\r\n
// Sent when an invalid command or wrong argument count is provided
std::string serialize_error(const std::string& msg) {
    return "-ERR " + msg + "\r\n";
}

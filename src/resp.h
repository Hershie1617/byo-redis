#pragma once

#include <string>
#include <vector>
#include <cstddef>

// Status returned by the streaming RESP parser.
enum class ParseStatus {
    Ok,          // A complete command was parsed successfully.
    Incomplete,  // Buffer ended prematurely; wait for more data from TCP socket.
    Error        // Protocol violation / malformed syntax received.
};

// Result of attempting to parse a command from an input buffer.
struct ParseResult {
    ParseStatus status{ParseStatus::Incomplete};
    std::vector<std::string> args; // Parsed command arguments (e.g., ["SET", "k", "v"])
    size_t bytes_consumed{0};      // Number of bytes consumed from the buffer if Ok
};

// Attempts to parse one RESP array of bulk strings from `buffer`.
// If incomplete, returns ParseStatus::Incomplete without consuming bytes.
ParseResult parse_resp_command(const std::string& buffer);

// RESP serialization helpers for building server responses.
// In an interview: Explain that RESP defines 5 basic types. We serialize:
// - Simple Strings (+OK\r\n)
// - Bulk Strings ($<len>\r\n<data>\r\n or $-1\r\n for null)
// - Errors (-ERR <msg>\r\n)
std::string serialize_simple_string(const std::string& msg);
std::string serialize_bulk_string(const std::string& data);
std::string serialize_null_bulk_string();
std::string serialize_error(const std::string& msg);

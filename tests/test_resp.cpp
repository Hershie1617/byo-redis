#include "../src/resp.h"
#include "../src/store.h"

#include <iostream>
#include <cassert>
#include <string>
#include <vector>

void test_store() {
    std::cout << "[RUNNING] test_store...\n";
    KVStore store;

    // 1. Initial key does not exist
    assert(!store.get("foo").has_value());

    // 2. Insert key
    store.set("foo", "bar");
    auto val = store.get("foo");
    assert(val.has_value());
    assert(val.value() == "bar");

    // 3. Overwrite key
    store.set("foo", "baz");
    val = store.get("foo");
    assert(val.has_value());
    assert(val.value() == "baz");

    std::cout << "[PASSED] test_store\n";
}

void test_resp_complete_command() {
    std::cout << "[RUNNING] test_resp_complete_command...\n";

    // *2\r\n$4\r\nECHO\r\n$5\r\nhello\r\n
    std::string input = "*2\r\n$4\r\nECHO\r\n$5\r\nhello\r\n";
    ParseResult res = parse_resp_command(input);

    assert(res.status == ParseStatus::Ok);
    assert(res.args.size() == 2);
    assert(res.args[0] == "ECHO");
    assert(res.args[1] == "hello");
    assert(res.bytes_consumed == input.size());

    std::cout << "[PASSED] test_resp_complete_command\n";
}

void test_resp_split_reads() {
    std::cout << "[RUNNING] test_resp_split_reads...\n";

    std::string full_command = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";
    std::string buffer = "";

    // Simulate byte-by-byte incoming network stream
    for (size_t i = 0; i < full_command.size() - 1; ++i) {
        buffer.push_back(full_command[i]);
        ParseResult partial_res = parse_resp_command(buffer);
        // Before the final byte is received, the command must be Incomplete
        assert(partial_res.status == ParseStatus::Incomplete);
    }

    // Append final byte
    buffer.push_back(full_command.back());
    ParseResult final_res = parse_resp_command(buffer);
    assert(final_res.status == ParseStatus::Ok);
    assert(final_res.args.size() == 3);
    assert(final_res.args[0] == "SET");
    assert(final_res.args[1] == "key");
    assert(final_res.args[2] == "value");
    assert(final_res.bytes_consumed == full_command.size());

    std::cout << "[PASSED] test_resp_split_reads\n";
}

void test_resp_pipelined_commands() {
    std::cout << "[RUNNING] test_resp_pipelined_commands...\n";

    // Two commands received in a single TCP read
    std::string cmd1 = "*1\r\n$4\r\nPING\r\n";
    std::string cmd2 = "*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n";
    std::string buffer = cmd1 + cmd2;

    // Parse 1st command
    ParseResult res1 = parse_resp_command(buffer);
    assert(res1.status == ParseStatus::Ok);
    assert(res1.args.size() == 1);
    assert(res1.args[0] == "PING");
    assert(res1.bytes_consumed == cmd1.size());

    // Consume 1st command
    buffer.erase(0, res1.bytes_consumed);

    // Parse 2nd command
    ParseResult res2 = parse_resp_command(buffer);
    assert(res2.status == ParseStatus::Ok);
    assert(res2.args.size() == 2);
    assert(res2.args[0] == "GET");
    assert(res2.args[1] == "foo");
    assert(res2.bytes_consumed == cmd2.size());

    buffer.erase(0, res2.bytes_consumed);
    assert(buffer.empty());

    std::cout << "[PASSED] test_resp_pipelined_commands\n";
}

void test_resp_serialization() {
    std::cout << "[RUNNING] test_resp_serialization...\n";

    assert(serialize_simple_string("PONG") == "+PONG\r\n");
    assert(serialize_simple_string("OK") == "+OK\r\n");
    assert(serialize_bulk_string("hello") == "$5\r\nhello\r\n");
    assert(serialize_null_bulk_string() == "$-1\r\n");
    assert(serialize_error("unknown command") == "-ERR unknown command\r\n");

    std::cout << "[PASSED] test_resp_serialization\n";
}

int main() {
    std::cout << "Starting unit tests...\n";
    test_store();
    test_resp_complete_command();
    test_resp_split_reads();
    test_resp_pipelined_commands();
    test_resp_serialization();
    std::cout << "All unit tests passed successfully!\n";
    return 0;
}

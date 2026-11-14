#!/usr/bin/env python3
"""
End-to-end test client for the C++ Redis server.
Tests:
1. PING / PING <msg>
2. ECHO <msg>
3. SET <key> <val> and GET <key>
4. GET non-existent key (null bulk string)
5. Split packet handling (sending commands byte-by-byte over TCP)
6. Command pipelining (multiple commands in one TCP write)
"""

import socket
import time
import sys

def connect(host="127.0.0.1", port=6379):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))
    return s

def test_ping():
    print("[RUNNING] test_ping...")
    s = connect()
    # PING
    s.sendall(b"*1\r\n$4\r\nPING\r\n")
    reply = s.recv(1024)
    assert reply == b"+PONG\r\n", f"Expected +PONG\\r\\n, got {reply}"

    # PING with argument
    s.sendall(b"*2\r\n$4\r\nPING\r\n$5\r\nhello\r\n")
    reply = s.recv(1024)
    assert reply == b"$5\r\nhello\r\n", f"Expected $5\\r\\nhello\\r\\n, got {reply}"
    s.close()
    print("[PASSED] test_ping")

def test_echo():
    print("[RUNNING] test_echo...")
    s = connect()
    s.sendall(b"*2\r\n$4\r\nECHO\r\n$11\r\nhello world\r\n")
    reply = s.recv(1024)
    assert reply == b"$11\r\nhello world\r\n", f"Expected $11\\r\\nhello world\\r\\n, got {reply}"
    s.close()
    print("[PASSED] test_echo")

def test_set_and_get():
    print("[RUNNING] test_set_and_get...")
    s = connect()
    # SET key value
    s.sendall(b"*3\r\n$3\r\nSET\r\n$4\r\nuser\r\n$5\r\nalice\r\n")
    reply = s.recv(1024)
    assert reply == b"+OK\r\n", f"Expected +OK\\r\\n, got {reply}"

    # GET key
    s.sendall(b"*2\r\n$3\r\nGET\r\n$4\r\nuser\r\n")
    reply = s.recv(1024)
    assert reply == b"$5\r\nalice\r\n", f"Expected $5\\r\\nalice\\r\\n, got {reply}"

    # GET nonexistent key
    s.sendall(b"*2\r\n$3\r\nGET\r\n$11\r\nmissing_key\r\n")
    reply = s.recv(1024)
    assert reply == b"$-1\r\n", f"Expected $-1\\r\\n, got {reply}"
    s.close()
    print("[PASSED] test_set_and_get")

def test_split_packets():
    print("[RUNNING] test_split_packets (byte-by-byte TCP streaming)...")
    s = connect()
    # Command: SET stream_test chunked_val
    cmd = b"*3\r\n$3\r\nSET\r\n$11\r\nstream_test\r\n$11\r\nchunked_val\r\n"
    for b in cmd:
        s.sendall(bytes([b]))
        time.sleep(0.001)  # small pause to force multiple TCP segments

    reply = s.recv(1024)
    assert reply == b"+OK\r\n", f"Expected +OK\\r\\n, got {reply}"

    # Verify retrieval
    s.sendall(b"*2\r\n$3\r\nGET\r\n$11\r\nstream_test\r\n")
    reply = s.recv(1024)
    assert reply == b"$11\r\nchunked_val\r\n", f"Expected $11\\r\\nchunked_val\\r\\n, got {reply}"
    s.close()
    print("[PASSED] test_split_packets")

def test_pipelining():
    print("[RUNNING] test_pipelining...")
    s = connect()
    # Send PING and GET back-to-back in a single write
    pipeline = (
        b"*1\r\n$4\r\nPING\r\n"
        b"*2\r\n$3\r\nGET\r\n$4\r\nuser\r\n"
    )
    s.sendall(pipeline)
    reply = s.recv(1024)
    expected = b"+PONG\r\n$5\r\nalice\r\n"
    assert reply == expected, f"Expected {expected}, got {reply}"
    s.close()
    print("[PASSED] test_pipelining")

if __name__ == "__main__":
    print("Testing Redis server at 127.0.0.1:6379...")
    try:
        test_ping()
        test_echo()
        test_set_and_get()
        test_split_packets()
        test_pipelining()
        print("\nAll integration tests passed successfully!")
    except ConnectionRefusedError:
        print("\nError: Could not connect to 127.0.0.1:6379.")
        print("Please start './redis-server' first before running this test script.")
        sys.exit(1)

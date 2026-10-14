#!/usr/bin/env python3
import json
import socket
import struct

HOST = "127.0.0.1"
PORT = 27183

def recv_exact(sock, size):
    chunks = []
    remaining = size
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ConnectionError("Android connection closed")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)

def receive_frame(sock):
    length = struct.unpack("!I", recv_exact(sock, 4))[0]
    if length > 64 * 1024 * 1024:
        raise ValueError(f"Response too large: {length} bytes")
    return recv_exact(sock, length).decode("utf-8")

def save_json(filename, data):
    with open(filename, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    print(f"Saved: {filename}")

def main():
    with socket.create_connection((HOST, PORT), timeout=10) as sock:
        sock.settimeout(40)
        print("Commands: DUMP, CLICKABLES, CLICK <name-or-path>, PING, EXIT")
        while True:
            raw = input("> ").strip()
            if not raw:
                continue
            if raw.upper() == "EXIT":
                break
            upper = raw.upper()
            if upper in ("DUMP", "CLICKABLES", "PING"):
                command = upper
            elif upper.startswith("CLICK "):
                target = raw[6:].strip()
                if not target:
                    print("Usage: CLICK <exact name or full path>")
                    continue
                command = "CLICK " + target
            else:
                print("Unknown command")
                continue
            try:
                sock.sendall((command + "\n").encode("utf-8"))
                response = receive_frame(sock)
            except (ConnectionError, TimeoutError, OSError, ValueError) as exc:
                print(f"Communication error: {exc}")
                break
            if command == "PING":
                print(response)
                continue
            try:
                payload = json.loads(response)
            except json.JSONDecodeError:
                print(response)
                continue
            if command == "DUMP":
                print(json.dumps(payload, ensure_ascii=False, indent=2))
                save_json("poco_tree.json", payload)
            elif command == "CLICKABLES":
                if not isinstance(payload, list):
                    print(json.dumps(payload, ensure_ascii=False, indent=2))
                    continue
                print(f"Clickable objects: {len(payload)}")
                for i, obj in enumerate(payload):
                    print(f"[{i}] {obj.get('name', '<unnamed>')}")
                    print(f"    {obj.get('path', '<no path>')}")
                save_json("poco_clickables.json", payload)
            else:
                print(json.dumps(payload, ensure_ascii=False, indent=2))
                print("Click request accepted." if payload.get("ok") else "Click failed.")

if __name__ == "__main__":
    main()

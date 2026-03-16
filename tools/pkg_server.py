#!/usr/bin/env python3

import argparse
import json
import os
import socket
import urllib.request
from pathlib import Path


MAX_REPLY_BYTES = 508
MAX_CHUNK_DATA = 420


def resolve_package_path(packages_dir: str, name: str) -> str | None:
    candidates = [
        name,
        f"{name}.pkg",
        f"{name}.txt",
        f"{name}.bas",
    ]

    for candidate in candidates:
        safe = os.path.normpath(candidate)
        if safe.startswith(".."):
            continue
        path = os.path.join(packages_dir, safe)
        if not os.path.isfile(path):
            continue
        return path
    return None


def is_http_url(value: str) -> bool:
    return value.startswith("http://") or value.startswith("https://")


def read_json_source(source: str) -> dict:
    if is_http_url(source):
        with urllib.request.urlopen(source, timeout=8) as resp:
            return json.loads(resp.read().decode("utf-8", errors="replace"))
    with open(source, "r", encoding="utf-8") as f:
        return json.load(f)


def load_registry(registry_source: str | None, packages_dir: str) -> dict[str, str]:
    resolved: dict[str, str] = {}

    if not registry_source:
        default_local = os.path.join(packages_dir, "registry.json")
        if not os.path.isfile(default_local):
            return resolved
        registry_source = default_local

    data = read_json_source(registry_source)
    packages = data.get("packages") if isinstance(data, dict) else None
    if not isinstance(packages, dict):
        return resolved

    for name, info in packages.items():
        if not isinstance(name, str) or not name.strip():
            continue

        source = ""
        if isinstance(info, str):
            source = info.strip()
        elif isinstance(info, dict):
            if isinstance(info.get("url"), str):
                source = info["url"].strip()
            elif isinstance(info.get("path"), str):
                source = info["path"].strip()

        if not source:
            continue
        resolved[name.strip()] = source

    return resolved


def resolve_registry_local_path(packages_dir: str, rel_path: str) -> str | None:
    safe = os.path.normpath(rel_path)
    if safe.startswith(".."):
        return None
    path = os.path.join(packages_dir, safe)
    return path if os.path.isfile(path) else None


def fetch_registry_payload(packages_dir: str, cache_dir: str, name: str, source: str) -> bytes | None:
    cache_path = os.path.join(cache_dir, f"{name}.pkg")

    if is_http_url(source):
        try:
            with urllib.request.urlopen(source, timeout=10) as resp:
                payload = resp.read()
            Path(cache_dir).mkdir(parents=True, exist_ok=True)
            with open(cache_path, "wb") as f:
                f.write(payload)
            return payload
        except Exception:
            if os.path.isfile(cache_path):
                with open(cache_path, "rb") as f:
                    return f.read()
            return None

    if local_path := resolve_registry_local_path(packages_dir, source):
        with open(local_path, "rb") as f:
            return f.read()
    return None


def list_packages(packages_dir: str) -> list[str]:
    items: list[str] = []
    for entry in sorted(os.listdir(packages_dir)):
        path = os.path.join(packages_dir, entry)
        if not os.path.isfile(path):
            continue
        name = entry
        if name.endswith(".bas"):
            name = name[:-4]
        elif name.endswith(".pkg"):
            name = name[:-4]
        elif name.endswith(".txt"):
            name = name[:-4]
        if name and name not in items:
            items.append(name)
    return items


def list_merged_packages(packages_dir: str, registry_map: dict[str, str]) -> list[str]:
    merged = list_packages(packages_dir)
    seen = set(merged)
    for name in sorted(registry_map.keys()):
        if name not in seen:
            merged.append(name)
            seen.add(name)
    return merged


def resolve_payload(packages_dir: str, cache_dir: str, registry_map: dict[str, str], name: str) -> tuple[bytes | None, str]:
    if path := resolve_package_path(packages_dir, name):
        with open(path, "rb") as f:
            return f.read(), path

    source = registry_map.get(name)
    if not source:
        return None, ""

    payload = fetch_registry_payload(packages_dir, cache_dir, name, source)
    return (payload, source) if payload is not None else (None, "")


def main() -> None:
    parser = argparse.ArgumentParser(description="Smiggles UDP package server")
    parser.add_argument("--host", default="0.0.0.0", help="Bind host (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=5555, help="UDP port (default: 5555)")
    parser.add_argument("--packages-dir", default="packages", help="Directory containing package files")
    parser.add_argument("--registry", default=None, help="Registry JSON path or URL (default: <packages-dir>/registry.json if present)")
    parser.add_argument("--cache-dir", default=".pkg-cache", help="Cache directory for internet-fetched packages")
    args = parser.parse_args()

    packages_dir = os.path.abspath(args.packages_dir)
    if not os.path.isdir(packages_dir):
        raise SystemExit(f"Packages dir not found: {packages_dir}")

    cache_dir = os.path.abspath(args.cache_dir)

    try:
        registry_map = load_registry(args.registry, packages_dir)
    except Exception as exc:
        print(f"[pkg-server] warning: failed to load registry: {exc}")
        registry_map = {}

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))

    print(f"[pkg-server] listening on udp://{args.host}:{args.port}")
    print(f"[pkg-server] packages dir: {packages_dir}")
    print(f"[pkg-server] cache dir: {cache_dir}")
    if registry_map:
        print(f"[pkg-server] registry entries: {len(registry_map)}")
    else:
        print("[pkg-server] registry entries: 0")

    while True:
        data, addr = sock.recvfrom(2048)
        text = data.decode("utf-8", errors="replace").strip()

        if text == "LIST":
            names = list_merged_packages(packages_dir, registry_map)
            body = "\n".join(names).encode("utf-8")
            reply = b"OK\n" + body
            if len(reply) > MAX_REPLY_BYTES:
                reply = b"ERR package list too large"
            sock.sendto(reply, addr)
            print(f"[pkg-server] listed {len(names)} package(s) for {addr[0]}:{addr[1]}")
            continue

        if text.startswith("GETCHUNK "):
            parts = text.split()
            if len(parts) != 4:
                sock.sendto(b"ERR usage GETCHUNK <name> <offset> <size>", addr)
                continue

            name = parts[1].strip()
            try:
                offset = int(parts[2])
                size = int(parts[3])
            except ValueError:
                sock.sendto(b"ERR invalid offset/size", addr)
                continue

            if not name:
                sock.sendto(b"ERR missing package name", addr)
                continue
            if offset < 0 or size <= 0:
                sock.sendto(b"ERR invalid range", addr)
                continue

            size = min(size, MAX_CHUNK_DATA)

            payload, source = resolve_payload(packages_dir, cache_dir, registry_map, name)
            if payload is None:
                sock.sendto(b"ERR package not found", addr)
                print(f"[pkg-server] miss {name!r} from {addr[0]}:{addr[1]}")
                continue

            if offset >= len(payload):
                sock.sendto(b"OK 0\n", addr)
                print(f"[pkg-server] chunk eof {name!r} to {addr[0]}:{addr[1]}")
                continue

            chunk = payload[offset: offset + size]
            header = f"OK {len(chunk)}\n".encode("utf-8")
            reply = header + chunk
            if len(reply) > MAX_REPLY_BYTES:
                chunk = payload[offset: offset + max(1, MAX_REPLY_BYTES - len(header))]
                header = f"OK {len(chunk)}\n".encode("utf-8")
                reply = header + chunk

            sock.sendto(reply, addr)
            print(f"[pkg-server] chunk {name!r} off={offset} n={len(chunk)} src={source} to {addr[0]}:{addr[1]}")
            continue

        if not text.startswith("GET "):
            sock.sendto(b"ERR expected GET/LIST/GETCHUNK", addr)
            continue

        name = text[4:].strip()
        if not name:
            sock.sendto(b"ERR missing package name", addr)
            continue

        payload, source = resolve_payload(packages_dir, cache_dir, registry_map, name)
        if payload is None:
            sock.sendto(b"ERR package not found", addr)
            print(f"[pkg-server] miss {name!r} from {addr[0]}:{addr[1]}")
            continue

        reply = b"OK\n" + payload
        if len(reply) > MAX_REPLY_BYTES:
            reply = b"ERR package too large for single datagram"

        sock.sendto(reply, addr)
        print(f"[pkg-server] served {name!r} ({len(reply)} bytes) src={source} to {addr[0]}:{addr[1]}")


if __name__ == "__main__":
    main()

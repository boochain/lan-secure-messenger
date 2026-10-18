#!/usr/bin/env bash
# Linux: run the daemon in the foreground; use ./lan-msg in another terminal (lan-msg --help).
cd "$(dirname "$0")"
exec ./lan-daemon run "$@"

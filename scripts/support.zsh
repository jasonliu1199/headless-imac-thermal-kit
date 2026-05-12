#!/bin/zsh

require_tested_host() {
  local component="${1:-this tool}"
  local model arch

  arch="$(/usr/bin/uname -m 2>/dev/null || true)"
  model="$(/usr/sbin/sysctl -n hw.model 2>/dev/null || true)"

  if [[ "$arch" != "x86_64" ]]; then
    print -u2 "Refusing to run ${component}: this toolkit uses Intel-only SMC/MSR/kernel-collection paths."
    print -u2 "Detected architecture: ${arch:-unknown}"
    exit 1
  fi

  if [[ "$model" != "iMac18,3" && "${ALLOW_UNTESTED_MODEL:-0}" != "1" ]]; then
    print -u2 "Refusing to run ${component} on untested model: ${model:-unknown}"
    print -u2 "Tested model: iMac18,3. Set ALLOW_UNTESTED_MODEL=1 only after auditing the code for this Mac."
    exit 1
  fi
}

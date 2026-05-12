#!/bin/zsh

detected_model() {
  /usr/sbin/sysctl -n hw.model 2>/dev/null || true
}

detected_arch() {
  /usr/bin/uname -m 2>/dev/null || true
}

detected_build() {
  /usr/bin/sw_vers -buildVersion 2>/dev/null || true
}

detected_product_version() {
  /usr/bin/sw_vers -productVersion 2>/dev/null || true
}

latest_kdk() {
  /bin/ls -d /Library/Developer/KDKs/*.kdk 2>/dev/null | /usr/bin/tail -1 || true
}

detected_system_volume() {
  local volume
  volume="$(/usr/sbin/diskutil info / 2>/dev/null | /usr/bin/awk -F': *' '/APFS Volume Disk/ {print $2; exit}')"
  if [[ -n "$volume" ]]; then
    print "$volume"
  else
    print "disk1s1"
  fi
}

require_tested_host() {
  local component="${1:-this tool}"
  local model arch

  arch="$(detected_arch)"
  model="$(detected_model)"

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

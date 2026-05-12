#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
ROOT_DIR="${SCRIPT_DIR:h}"
source "${SCRIPT_DIR}/support.zsh"
LABEL="local.smcfan.quietcurve"
PLIST_SRC="${ROOT_DIR}/launchd/${LABEL}.plist"
PLIST_DST="/Library/LaunchDaemons/${LABEL}.plist"
FAN_MODE="${FAN_MODE:-fan}"
FAN_INDEX="${FAN_INDEX:-0}"
FAN_BASE_RPM="${FAN_BASE_RPM:-1650}"
FAN_INTERVAL_SEC="${FAN_INTERVAL_SEC:-5}"

if [[ "${EUID}" -ne 0 ]]; then
  exec sudo -S env \
    ALLOW_UNTESTED_MODEL="${ALLOW_UNTESTED_MODEL:-}" \
    FAN_MODE="${FAN_MODE}" \
    FAN_INDEX="${FAN_INDEX}" \
    FAN_BASE_RPM="${FAN_BASE_RPM}" \
    FAN_INTERVAL_SEC="${FAN_INTERVAL_SEC}" \
    /bin/zsh "$0" "$@"
fi

require_tested_host "quiet fan daemon installer"

if [[ "$FAN_MODE" != "fan" && "$FAN_MODE" != "all" ]]; then
  print -u2 "FAN_MODE must be 'fan' or 'all'"
  exit 2
fi
if [[ "$FAN_INDEX" != <-> || "$FAN_BASE_RPM" != <-> || "$FAN_INTERVAL_SEC" != <-> ]]; then
  print -u2 "FAN_INDEX, FAN_BASE_RPM, and FAN_INTERVAL_SEC must be integers"
  exit 2
fi

make -C "${ROOT_DIR}/fan"
install -d /usr/local/sbin
install -m 0755 "${ROOT_DIR}/fan/smcfanctl" /usr/local/sbin/smcfanctl

if launchctl print "system/${LABEL}" >/dev/null 2>&1; then
  launchctl bootout system "${PLIST_DST}" || true
fi

install -m 0644 "${PLIST_SRC}" "${PLIST_DST}"
chown root:wheel "${PLIST_DST}"

PB="/usr/libexec/PlistBuddy"
"$PB" -c "Delete :ProgramArguments" "${PLIST_DST}" || true
"$PB" -c "Add :ProgramArguments array" "${PLIST_DST}"
arg_index=0
add_arg() {
  "$PB" -c "Add :ProgramArguments:${arg_index} string $1" "${PLIST_DST}"
  arg_index=$((arg_index + 1))
}
add_arg "/usr/local/sbin/smcfanctl"
add_arg "daemon"
if [[ "$FAN_MODE" == "all" ]]; then
  add_arg "--all"
else
  add_arg "--fan"
  add_arg "$FAN_INDEX"
fi
add_arg "--base"
add_arg "$FAN_BASE_RPM"
add_arg "--interval"
add_arg "$FAN_INTERVAL_SEC"

plutil -lint "${PLIST_DST}"

launchctl bootstrap system "${PLIST_DST}"
launchctl enable "system/${LABEL}"
launchctl kickstart -k "system/${LABEL}"

/usr/local/sbin/smcfanctl status
print "Installed and started ${LABEL} (${FAN_MODE} ${FAN_INDEX}, base ${FAN_BASE_RPM}, interval ${FAN_INTERVAL_SEC})"

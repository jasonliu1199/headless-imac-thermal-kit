#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
ROOT_DIR="${SCRIPT_DIR:h}"
source "${SCRIPT_DIR}/support.zsh"
LABEL="local.smcfan.quietcurve"
PLIST_SRC="${ROOT_DIR}/launchd/${LABEL}.plist"
PLIST_DST="/Library/LaunchDaemons/${LABEL}.plist"

if [[ "${EUID}" -ne 0 ]]; then
  exec sudo -S env ALLOW_UNTESTED_MODEL="${ALLOW_UNTESTED_MODEL:-}" /bin/zsh "$0" "$@"
fi

require_tested_host "quiet fan daemon installer"

make -C "${ROOT_DIR}/fan"
install -d /usr/local/sbin
install -m 0755 "${ROOT_DIR}/fan/smcfanctl" /usr/local/sbin/smcfanctl

if launchctl print "system/${LABEL}" >/dev/null 2>&1; then
  launchctl bootout system "${PLIST_DST}" || true
fi

install -m 0644 "${PLIST_SRC}" "${PLIST_DST}"
chown root:wheel "${PLIST_DST}"
plutil -lint "${PLIST_DST}"

launchctl bootstrap system "${PLIST_DST}"
launchctl enable "system/${LABEL}"
launchctl kickstart -k "system/${LABEL}"

/usr/local/sbin/smcfanctl status
print "Installed and started ${LABEL}"

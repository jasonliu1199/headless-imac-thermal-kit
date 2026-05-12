#!/bin/zsh
set -euo pipefail

LABEL="local.smcfan.quietcurve"
PLIST_DST="/Library/LaunchDaemons/${LABEL}.plist"

if [[ "${EUID}" -ne 0 ]]; then
  exec sudo -S /bin/zsh "$0" "$@"
fi

if launchctl print "system/${LABEL}" >/dev/null 2>&1; then
  launchctl bootout system "${PLIST_DST}" || true
fi

/usr/local/sbin/smcfanctl auto 2>/dev/null || true
rm -f "${PLIST_DST}"
print "Removed ${LABEL}; fan mode restored to auto."

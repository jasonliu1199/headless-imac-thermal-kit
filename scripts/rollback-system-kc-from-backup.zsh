#!/bin/zsh
set -euo pipefail

export PATH="/usr/bin:/bin:/usr/sbin:/sbin"

SCRIPT_DIR="${0:A:h}"
ROOT_DIR="${SCRIPT_DIR:h}"
MOUNT="${ROOT_DIR}/mnt-system"
SYSTEM_VOLUME="${SYSTEM_VOLUME:-disk1s1}"
BACKUP="${1:-}"
TARGET_KC_DIR="${MOUNT}/System/Library/KernelCollections"

if [[ "${EUID}" -ne 0 ]]; then
  exec sudo -S /bin/zsh "$0" "$@"
fi

if [[ -z "$BACKUP" || ! -d "$BACKUP/KernelCollections.before" ]]; then
  print -u2 "Usage: $0 /path/to/backup-directory"
  print -u2 "Expected: /path/to/backup-directory/KernelCollections.before"
  exit 1
fi

mkdir -p "$MOUNT"
if ! mount | grep -q " on $MOUNT "; then
  diskutil mount -mountPoint "$MOUNT" "$SYSTEM_VOLUME"
fi

print "Restoring KernelCollections from $BACKUP ..."
ditto "$BACKUP/KernelCollections.before" "$TARGET_KC_DIR"

print "Creating rollback boot snapshot ..."
bless --folder "$MOUNT/System/Library/CoreServices" --bootefi --create-snapshot --verbose

print "Rollback snapshot is set for next boot. Reboot to use it."

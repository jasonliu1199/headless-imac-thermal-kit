#!/bin/zsh
set -euo pipefail

export PATH="/usr/bin:/bin:/usr/sbin:/sbin"
export LC_ALL=C

SCRIPT_DIR="${0:A:h}"
ROOT_DIR="${SCRIPT_DIR:h}"
source "${SCRIPT_DIR}/support.zsh"
MOUNT="${ROOT_DIR}/mnt-system"
SYSTEM_VOLUME="${SYSTEM_VOLUME:-disk1s1}"
KDK="${KDK:-}"
STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="${ROOT_DIR}/backups/stage-acpi-smc-no-x86-${STAMP}"
LOG="${ROOT_DIR}/stage-acpi-smc-no-x86-${STAMP}.log"
TARGET_KC_DIR="${MOUNT}/System/Library/KernelCollections"

TARGET_ACPI="${MOUNT}/System/Library/Extensions/IOPlatformPluginFamily.kext/Contents/PlugIns/ACPI_SMC_PlatformPlugin.kext/Contents/Info.plist"
TARGET_X86="${MOUNT}/System/Library/Extensions/IOPlatformPluginFamily.kext/Contents/PlugIns/X86PlatformPlugin.kext/Contents/Info.plist"
TARGET_SHIM="${MOUNT}/System/Library/Extensions/IOPlatformPluginFamily.kext/Contents/PlugIns/X86PlatformShim.kext/Contents/Info.plist"

KDK_ACPI="${KDK}/System/Library/Extensions/IOPlatformPluginFamily.kext/Contents/PlugIns/ACPI_SMC_PlatformPlugin.kext/Contents/Info.plist"
KDK_X86="${KDK}/System/Library/Extensions/IOPlatformPluginFamily.kext/Contents/PlugIns/X86PlatformPlugin.kext/Contents/Info.plist"
KDK_SHIM="${KDK}/System/Library/Extensions/IOPlatformPluginFamily.kext/Contents/PlugIns/X86PlatformShim.kext/Contents/Info.plist"

SUCCESS=0
ROLLBACK_READY=0

if [[ "${EUID}" -ne 0 ]]; then
  exec sudo -S env ALLOW_UNTESTED_MODEL="${ALLOW_UNTESTED_MODEL:-}" KDK="${KDK:-}" SYSTEM_VOLUME="${SYSTEM_VOLUME:-}" /bin/zsh "$0" "$@"
fi

require_tested_host "sealed-system platform-plugin patch"

if [[ -z "${KDK}" ]]; then
  KDK="$(/bin/ls -d /Library/Developer/KDKs/*.kdk 2>/dev/null | /usr/bin/tail -1 || true)"
fi

if [[ -z "${KDK}" || ! -d "${KDK}" ]]; then
  print -u2 "No KDK found. Install the matching Kernel Debug Kit or set KDK=/path/to/KDK.kdk"
  exit 1
fi

/bin/mkdir -p "$BACKUP"
exec > >(/usr/bin/tee -a "$LOG") 2>&1

restore_kdk() {
  [[ -f "$BACKUP/KDK-ACPI.plist" ]] && /usr/bin/ditto "$BACKUP/KDK-ACPI.plist" "$KDK_ACPI" || true
  [[ -f "$BACKUP/KDK-X86.plist" ]] && /usr/bin/ditto "$BACKUP/KDK-X86.plist" "$KDK_X86" || true
  [[ -f "$BACKUP/KDK-SHIM.plist" ]] && /usr/bin/ditto "$BACKUP/KDK-SHIM.plist" "$KDK_SHIM" || true
}

rollback_on_error() {
  local rc=$?
  restore_kdk
  if [[ "$SUCCESS" != "1" && "$ROLLBACK_READY" == "1" ]]; then
    print "Rolling back target plists and KernelCollections ..."
    [[ -f "$BACKUP/target-ACPI.plist" ]] && /usr/bin/ditto "$BACKUP/target-ACPI.plist" "$TARGET_ACPI" || true
    [[ -f "$BACKUP/target-X86.plist" ]] && /usr/bin/ditto "$BACKUP/target-X86.plist" "$TARGET_X86" || true
    [[ -f "$BACKUP/target-SHIM.plist" ]] && /usr/bin/ditto "$BACKUP/target-SHIM.plist" "$TARGET_SHIM" || true
    [[ -d "$BACKUP/KernelCollections.before" ]] && /usr/bin/ditto "$BACKUP/KernelCollections.before" "$TARGET_KC_DIR" || true
  fi
  exit "$rc"
}

trap rollback_on_error EXIT

print "=== Stage ACPI_SMC on, X86Platform off ==="
print "Started: $(date)"
print "KDK: ${KDK}"

/bin/mkdir -p "$MOUNT"
if ! /sbin/mount | /usr/bin/grep -q " on $MOUNT "; then
  print "Mounting writable System volume at $MOUNT ..."
  /usr/sbin/diskutil mount -mountPoint "$MOUNT" "$SYSTEM_VOLUME"
else
  print "System volume already mounted at $MOUNT"
fi

for path in "$TARGET_ACPI" "$TARGET_X86" "$TARGET_SHIM" "$KDK_ACPI" "$KDK_X86" "$KDK_SHIM" "$TARGET_KC_DIR"; do
  if [[ ! -e "$path" ]]; then
    print -u2 "ERROR: required path missing: $path"
    exit 1
  fi
done

print "Backing up target/KDK plists and current KernelCollections ..."
/usr/bin/ditto "$TARGET_ACPI" "$BACKUP/target-ACPI.plist"
/usr/bin/ditto "$TARGET_X86" "$BACKUP/target-X86.plist"
/usr/bin/ditto "$TARGET_SHIM" "$BACKUP/target-SHIM.plist"
/usr/bin/ditto "$KDK_ACPI" "$BACKUP/KDK-ACPI.plist"
/usr/bin/ditto "$KDK_X86" "$BACKUP/KDK-X86.plist"
/usr/bin/ditto "$KDK_SHIM" "$BACKUP/KDK-SHIM.plist"
/usr/bin/ditto "$TARGET_KC_DIR" "$BACKUP/KernelCollections.before"
ROLLBACK_READY=1

print "Applying desired IOKit matching ..."
/usr/libexec/PlistBuddy -c "Set :IOKitPersonalities:ACPI_SMC_PlatformPlugin:IOProviderClass AppleACPICPU" "$TARGET_ACPI"
/usr/libexec/PlistBuddy -c "Set :IOKitPersonalities:X86PlatformPlugin:IOProviderClass AppleACPICPU_HeadlessDisabled" "$TARGET_X86"
/usr/libexec/PlistBuddy -c "Set :IOKitPersonalities:X86PlatformShim:IOProviderClass X86PlatformPlugin_HeadlessDisabled" "$TARGET_SHIM"

/usr/libexec/PlistBuddy -c "Set :IOKitPersonalities:ACPI_SMC_PlatformPlugin:IOProviderClass AppleACPICPU" "$KDK_ACPI"
/usr/libexec/PlistBuddy -c "Set :IOKitPersonalities:X86PlatformPlugin:IOProviderClass AppleACPICPU_HeadlessDisabled" "$KDK_X86"
/usr/libexec/PlistBuddy -c "Set :IOKitPersonalities:X86PlatformShim:IOProviderClass X86PlatformPlugin_HeadlessDisabled" "$KDK_SHIM"

print "Rebuilding Boot/System KernelCollections ..."
/usr/bin/kmutil install --volume-root "$MOUNT" --update-all --force --kdk "$KDK" --update-preboot --no-authorization

print "Restoring KDK plists; target System volume remains patched for the new snapshot ..."
restore_kdk

print "Verifying rebuilt SystemKernelExtensions ..."
if ! LC_ALL=C /usr/bin/grep -a -q "AppleACPICPU_HeadlessDisabled" "$TARGET_KC_DIR/SystemKernelExtensions.kc"; then
  print -u2 "ERROR: rebuilt SystemKernelExtensions.kc does not contain disabled X86 provider"
  exit 1
fi
if ! LC_ALL=C /usr/bin/grep -a -q "X86PlatformPlugin_HeadlessDisabled" "$TARGET_KC_DIR/SystemKernelExtensions.kc"; then
  print -u2 "ERROR: rebuilt SystemKernelExtensions.kc does not contain disabled X86 shim provider"
  exit 1
fi

print "Creating and blessing a new APFS boot snapshot ..."
/usr/sbin/bless --folder "$MOUNT/System/Library/CoreServices" --bootefi --create-snapshot --verbose

SUCCESS=1
print "Done. Reboot is required."
print "Backup: $BACKUP"
print "Log: $LOG"

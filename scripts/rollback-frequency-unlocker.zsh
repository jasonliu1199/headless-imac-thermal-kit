#!/bin/zsh
set -euo pipefail

KEXT_NAME="FrequencyUnlocker.kext"
DST="/Library/Extensions/${KEXT_NAME}"
AUX_KC="/Library/KernelCollections/AuxiliaryKernelExtensions.kc"
BOOT_KC="/System/Library/KernelCollections/BootKernelExtensions.kc"
SYSTEM_KC="/System/Library/KernelCollections/SystemKernelExtensions.kc"

if [[ "${EUID}" -ne 0 ]]; then
  exec sudo -S /bin/zsh "$0" "$@"
fi

print "Removing ${KEXT_NAME} ..."
kmutil unload --bundle-identifier local.kext.FrequencyUnlocker 2>/dev/null || true
rm -rf "${DST}"

print "Rebuilding auxiliary kernel collection ..."
kmutil clear-staging
kmutil create \
  --new aux \
  --auxiliary-path "${AUX_KC}" \
  --boot-path "${BOOT_KC}" \
  --system-path "${SYSTEM_KC}" \
  --repository /Library/Extensions \
  --no-authorization \
  --no-system-collection \
  --force

print "Done. Reboot to fully remove FrequencyUnlocker from the running kernel state."

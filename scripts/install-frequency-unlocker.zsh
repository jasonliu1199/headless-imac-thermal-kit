#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
ROOT_DIR="${SCRIPT_DIR:h}"
KEXT_NAME="FrequencyUnlocker.kext"
SRC="${ROOT_DIR}/kext/FrequencyUnlocker/build/Release/${KEXT_NAME}"
DST="/Library/Extensions/${KEXT_NAME}"
AUX_KC="/Library/KernelCollections/AuxiliaryKernelExtensions.kc"
BOOT_KC="/System/Library/KernelCollections/BootKernelExtensions.kc"
SYSTEM_KC="/System/Library/KernelCollections/SystemKernelExtensions.kc"

if [[ "${EUID}" -ne 0 ]]; then
  exec sudo -S /bin/zsh "$0" "$@"
fi

if [[ ! -d "${SRC}" ]]; then
  print "Building ${KEXT_NAME} ..."
  make -C "${ROOT_DIR}/kext/FrequencyUnlocker"
fi

if [[ ! -d "${SRC}" ]]; then
  print -u2 "Missing built kext: ${SRC}"
  exit 1
fi

print "Installing ${KEXT_NAME} ..."
rm -rf "${DST}"
cp -R "${SRC}" "/Library/Extensions/"
chown -R root:wheel "${DST}"
chmod -R go-w "${DST}"
xattr -cr "${DST}"

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

print "Auxiliary KC contains:"
kmutil inspect --no-authorization -A "${AUX_KC}" 2>&1 | egrep -i 'FrequencyUnlocker|GoodbyeBigSlow' || true

print "Done. Reboot if kmutil load says the kext requires a reboot."

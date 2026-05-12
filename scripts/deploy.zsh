#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
ROOT_DIR="${SCRIPT_DIR:h}"
source "${SCRIPT_DIR}/support.zsh"

usage() {
  cat <<'USAGE'
Usage:
  scripts/deploy.zsh doctor
  scripts/deploy.zsh print-profile
  scripts/deploy.zsh write-profile
  scripts/deploy.zsh deploy

Environment:
  PROFILE=/path/to/profile.conf       Use a specific profile.
  ALLOW_UNTESTED_MODEL=1              Permit high-risk installers on untested models.

The deploy command loads a model profile, then runs only the components enabled
by that profile.
USAGE
}

bool_enabled() {
  [[ "${1:-0}" == "1" || "${1:-}" == "yes" || "${1:-}" == "true" ]]
}

profile_path() {
  local model profile
  model="$(detected_model)"
  if [[ -n "${PROFILE:-}" ]]; then
    print "$PROFILE"
    return
  fi
  profile="${ROOT_DIR}/profiles/${model}.conf"
  if [[ -f "$profile" ]]; then
    print "$profile"
    return
  fi
  profile="${ROOT_DIR}/profiles/local.conf"
  if [[ -f "$profile" ]]; then
    print "$profile"
    return
  fi
  print ""
}

load_profile() {
  local profile
  profile="$(profile_path)"
  if [[ -z "$profile" || ! -f "$profile" ]]; then
    print -u2 "No profile found for model $(detected_model)."
    print -u2 "Run: scripts/deploy.zsh write-profile"
    exit 1
  fi
  source "$profile"
  ACTIVE_PROFILE="$profile"
}

doctor() {
  local model arch version build kdk systemVolume
  model="$(detected_model)"
  arch="$(detected_arch)"
  version="$(detected_product_version)"
  build="$(detected_build)"
  kdk="$(latest_kdk)"
  systemVolume="$(detected_system_volume)"

  print "Model: ${model:-unknown}"
  print "Architecture: ${arch:-unknown}"
  print "macOS: ${version:-unknown} (${build:-unknown})"
  print "System volume guess: ${systemVolume}"
  print "KDK: ${kdk:-missing}"
  print "Profile: $(profile_path)"

  if [[ "$arch" != "x86_64" ]]; then
    print "Status: unsupported architecture"
    return 1
  fi
  if [[ -z "$kdk" ]]; then
    print "Status: KDK missing"
    return 1
  fi

  if [[ -x "${ROOT_DIR}/fan/smcfanctl" ]]; then
    "${ROOT_DIR}/fan/smcfanctl" status || true
  else
    print "smcfanctl is not built yet; deploy will build it if fan control is enabled."
  fi
}

print_profile() {
  load_profile
  print "Profile file: $ACTIVE_PROFILE"
  print "PROFILE_NAME=${PROFILE_NAME:-}"
  print "PROFILE_MODEL=${PROFILE_MODEL:-}"
  print "PROFILE_MACOS_BUILD=${PROFILE_MACOS_BUILD:-}"
  print "INSTALL_FREQUENCY_UNLOCKER=${INSTALL_FREQUENCY_UNLOCKER:-0}"
  print "STAGE_PLATFORM_PATCH=${STAGE_PLATFORM_PATCH:-0}"
  print "INSTALL_FAN_DAEMON=${INSTALL_FAN_DAEMON:-0}"
  print "SYSTEM_VOLUME=${SYSTEM_VOLUME:-$(detected_system_volume)}"
  print "FAN_MODE=${FAN_MODE:-fan}"
  print "FAN_INDEX=${FAN_INDEX:-0}"
  print "FAN_BASE_RPM=${FAN_BASE_RPM:-1700}"
  print "FAN_INTERVAL_SEC=${FAN_INTERVAL_SEC:-5}"
}

write_profile() {
  local target model build systemVolume
  target="${ROOT_DIR}/profiles/local.conf"
  model="$(detected_model)"
  build="$(detected_build)"
  systemVolume="$(detected_system_volume)"

  if [[ -f "$target" && "${OVERWRITE:-0}" != "1" ]]; then
    print -u2 "$target already exists. Set OVERWRITE=1 to replace it."
    exit 1
  fi

  cat > "$target" <<EOF
# Local profile generated on $(/bin/date).
# Review before running scripts/deploy.zsh deploy.

PROFILE_NAME="Local profile for ${model:-unknown}"
PROFILE_MODEL="${model:-unknown}"
PROFILE_MACOS_BUILD="${build:-unknown}"

INSTALL_FREQUENCY_UNLOCKER=0
STAGE_PLATFORM_PATCH=0
INSTALL_FAN_DAEMON=1

SYSTEM_VOLUME="\${SYSTEM_VOLUME:-${systemVolume}}"
FAN_MODE="fan"
FAN_INDEX=0
FAN_BASE_RPM=1700
FAN_INTERVAL_SEC=5
EOF

  print "Wrote $target"
  print "Edit it, then run: scripts/deploy.zsh deploy"
}

deploy() {
  load_profile

  print "Using profile: ${PROFILE_NAME:-$ACTIVE_PROFILE}"
  print "Detected: $(detected_model), macOS $(detected_product_version) ($(detected_build))"

  if [[ "${PROFILE_MODEL:-}" != "$(detected_model)" && "${ALLOW_UNTESTED_MODEL:-0}" != "1" ]]; then
    print -u2 "Profile model (${PROFILE_MODEL:-unset}) does not match this Mac ($(detected_model))."
    print -u2 "Set ALLOW_UNTESTED_MODEL=1 only after auditing the profile and scripts."
    exit 1
  fi

  if bool_enabled "${INSTALL_FREQUENCY_UNLOCKER:-0}"; then
    ALLOW_UNTESTED_MODEL="${ALLOW_UNTESTED_MODEL:-0}" "${SCRIPT_DIR}/install-frequency-unlocker.zsh"
  fi

  if bool_enabled "${STAGE_PLATFORM_PATCH:-0}"; then
    ALLOW_UNTESTED_MODEL="${ALLOW_UNTESTED_MODEL:-0}" \
      SYSTEM_VOLUME="${SYSTEM_VOLUME:-$(detected_system_volume)}" \
      "${SCRIPT_DIR}/stage-acpi-smc-no-x86.zsh"
  fi

  if bool_enabled "${INSTALL_FAN_DAEMON:-0}"; then
    ALLOW_UNTESTED_MODEL="${ALLOW_UNTESTED_MODEL:-0}" \
      FAN_MODE="${FAN_MODE:-fan}" \
      FAN_INDEX="${FAN_INDEX:-0}" \
      FAN_BASE_RPM="${FAN_BASE_RPM:-1700}" \
      FAN_INTERVAL_SEC="${FAN_INTERVAL_SEC:-5}" \
      "${SCRIPT_DIR}/install-smcfan-quietcurve.zsh"
  fi

  print "Deploy finished. Reboot if the frequency or platform patch components ran."
}

cmd="${1:-}"
case "$cmd" in
  doctor) doctor ;;
  print-profile) print_profile ;;
  write-profile) write_profile ;;
  deploy) deploy ;;
  -h|--help|help|"") usage ;;
  *) usage; exit 2 ;;
esac

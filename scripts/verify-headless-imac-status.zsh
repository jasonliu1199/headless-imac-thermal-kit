#!/bin/zsh
set -euo pipefail

echo "== loaded kexts =="
kmutil showloaded | egrep -i 'FrequencyUnlocker|GoodbyeBigSlow|ACPI_SMC_PlatformPlugin|X86PlatformPlugin|X86PlatformShim|AppleSMC' || true

echo
echo "== thermal policy =="
pmset -g therm || true

echo
echo "== xcpm =="
sysctl machdep.xcpm.hard_plimit_max_100mhz_ratio \
       machdep.xcpm.soft_plimit_max_100mhz_ratio \
       machdep.xcpm.bootpst \
       machdep.xcpm.cpu_thermal_level \
       machdep.xcpm.gpu_thermal_level 2>/dev/null || true

echo
echo "== fan =="
if [[ -x /usr/local/sbin/smcfanctl ]]; then
  /usr/local/sbin/smcfanctl status || true
else
  echo "smcfanctl is not installed"
fi

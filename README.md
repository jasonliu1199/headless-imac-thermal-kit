# Headless iMac Thermal Kit

Experimental macOS Ventura tooling for a headless Intel iMac that falsely enters
thermal or platform power limits when the internal display path is unavailable.

This repository intentionally excludes display/EDID override work. It contains:

- `scripts/stage-acpi-smc-no-x86.zsh`: stages a sealed-system snapshot with
  `ACPI_SMC_PlatformPlugin` matching enabled and `X86PlatformPlugin` /
  `X86PlatformShim` matching disabled.
- `kext/FrequencyUnlocker`: a small test kext that repeatedly clears BD-PROCHOT,
  enables SpeedStep/Turbo, and writes HWP/PERF_CTL target requests.
- `fan/smcfanctl`: a minimal AppleSMC fan reader/writer.
- `launchd/local.smcfan.quietcurve.plist`: a quiet LaunchDaemon fan curve that
  raises fan speed when CPU or GPU temperature rises.
- `scripts/verify-headless-imac-status.zsh`: quick status checks.

## Warning

This is not a general-purpose Mac tuning package. It modifies kernel collections,
creates APFS boot snapshots, installs an unsigned kext, and writes SMC fan keys.
Use only if you understand how to recover from Safe Mode or Recovery.

Known requirements from the tested machine:

- Intel iMac18,3
- macOS 13.7.8 / build 22H730
- Matching KDK installed under `/Library/Developer/KDKs`
- SIP / authenticated root configuration that permits root volume patching and
  auxiliary kext loading
- Command Line Tools for building the kext and `smcfanctl`

## Install Order

Build and install the frequency kext:

```zsh
./scripts/install-frequency-unlocker.zsh
```

Stage the sealed-system platform-plugin snapshot:

```zsh
./scripts/stage-acpi-smc-no-x86.zsh
```

If your System volume is not `disk1s1`, run with
`SYSTEM_VOLUME=diskXsY ./scripts/stage-acpi-smc-no-x86.zsh`.

Install the quiet fan daemon:

```zsh
./scripts/install-smcfan-quietcurve.zsh
```

Reboot, then verify:

```zsh
./scripts/verify-headless-imac-status.zsh
```

Healthy signs:

- `FrequencyUnlocker` is loaded.
- `ACPI_SMC_PlatformPlugin` is loaded.
- `X86PlatformPlugin` and `X86PlatformShim` are not loaded.
- `pmset -g therm` does not show `CPU_Speed_Limit = 24`.
- `powermetrics --samplers cpu_power` reports normal CPU frequency under load.

## Fan Curve

The default LaunchDaemon uses:

- base: 1650 rpm
- interval: 5 seconds
- approximately 1900 rpm around 60 C
- approximately 2300 rpm around 70 C
- approximately 3000 rpm around 80 C
- max near 90 C

It selects the hottest usable CPU/GPU SMC temperature sensor, so one shared fan
responds to either CPU or GPU heat.

## Rollback

Remove the frequency kext:

```zsh
./scripts/rollback-frequency-unlocker.zsh
```

Remove the fan daemon and return fan mode to auto:

```zsh
./scripts/uninstall-smcfan-quietcurve.zsh
```

Roll back a staged system kernel collection snapshot from a backup created by
`stage-acpi-smc-no-x86.zsh`:

```zsh
./scripts/rollback-system-kc-from-backup.zsh ./backups/stage-acpi-smc-no-x86-YYYYMMDD-HHMMSS
```

## Notes

The tested machine also had `GoodbyeBigSlow.kext` loaded during diagnosis. This
repository does not vendor third-party kext sources; `FrequencyUnlocker` includes
its own BD-PROCHOT clearing path and HWP/PERF_CTL writer.

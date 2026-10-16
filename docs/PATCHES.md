# Patch format

A patch is a zip named `l4d2-port-patch-<version>.zip`:

```
patch.json
files/l4d2.exe
files/l4d2_<module>_360.dll ...
files/rexruntime.dll, files/rexgpu-xenos.dll
files/l4d2_settings.default.ini
```

`patch.json`:

```json
{
  "name": "L4D2 PC Port",
  "version": "1.0.1",
  "build_date": "2026-10-16T05:00:00Z",
  "min_launcher": "1.0.0",
  "notes": "What changed",
  "files": [{ "path": "l4d2.exe", "size": 1050624, "sha256": "..." }]
}
```

## How the launcher applies a patch

1. Reads `patch.json`; refuses patches that need a newer launcher.
2. Extracts every file into `<install>\.staging` and checks size + SHA-256. Any mismatch aborts — the install is untouched.
3. Moves files that will be replaced into `<install>\.backup\<old version>\`.
4. Moves staged files into place, merges new default settings into `l4d2_settings.ini` without overwriting values the player changed, and writes `installed.json`.

Files a patch doesn't list are left alone, so patches can be partial.

## Channels

The launcher's **Update channel** decides which GitHub releases it offers:

- **Release** — the newest release that is *not* marked pre-release.
- **Pre-release** — the newest release of either kind (so testers also get full releases).

Only releases with an `l4d2-port-patch-*.zip` asset count; drafts are ignored. Releases are ordered by version, not upload date (`1.1.0-beta.2` < `1.1.0-rc.1` < `1.1.0`). Switching a pre-release install back to *Release* offers the newest full release even if its number is lower.

## Release checklist

1. `port\scripts\build.bat` (or `build.bat full` after manifest changes).
2. Test with `port\scripts\test_map.bat` (screenshots + `perf_summary.csv` in `port\testruns\`).
3. Either:
   - `port\scripts\pre_release_patch.bat 1.1.0-beta.1 "notes"` for testers, or
   - `port\scripts\release_patch.bat 1.1.0 "notes"` for everyone, or
   - `port\scripts\make_patch.bat 1.1.0 "notes"` and upload `port\dist\l4d2-port-patch-1.1.0.zip` to a release tagged `v1.1.0` by hand.

The publishing scripts create the release (or update an existing one with the same tag), replace assets with the same name, and attach `l4d2-launcher.exe` too. Log: `port\dist\publish-<version>.txt`.

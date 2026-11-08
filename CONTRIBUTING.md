# Contributing to Cobalt

Thanks for helping. Cobalt patches real hardware, so changes reach `main`
only after they have been tested on a bluebox desktop.

## Branches

- **`dev`**: open pull requests here. New features are collected and tested
  on hardware on this branch.
- **`main`**: releases only. Every push to `main` republishes the patcher
  page on GitHub Pages, so `main` changes only when `dev` is merged for a
  tagged release.

If a pull request was opened against `main`, its base will be changed to
`dev`.

## Before opening a pull request

- Build with Arm GNU Toolchain **13.2.Rel1** (see
  [Building from source](README.md#building-from-source)). Other compiler
  versions produce different payloads and a different output SHA256.
- Commit the regenerated `patch_bluebox.html`, `patch_bluebox.py`,
  `patch_manifest.json` and `docs/index.html` from `build.sh`.
- Run `python3 tests/run_checks.py /path/to/BLUEBOX.BIN` and say in the pull
  request which suites you ran.
- Put new code in its own flash gap and backup SRAM line, and say which
  addresses it uses.
- Say what you tested on hardware, and with which firmware.
- Never commit firmware images (`BLUEBOX.BIN`, patched or original).

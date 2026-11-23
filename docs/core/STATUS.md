# mGBA integration status

## Selected upstream
- Repository: https://github.com/mgba-emu/mgba
- Release tag: `0.10.5`
- License: MPL-2.0; preserve upstream notices and license when distributing source or binaries.

## What this repository currently does
- `scripts/fetch-core.sh` retrieves the pinned upstream source.
- CI configures/builds the upstream library-only profile on Linux.
- N-GBA keeps its own frontend model and `EmulatorBackend` interface.

## Not yet wired
The `core/mgba_backend.cpp` implementation is deliberately a fail-closed stub. It does not create an `mCore`, map ROM/BIOS, call `runFrame`, convert mGBA video output, or pull audio. The next integration step is a version-specific adapter using mGBA's `mCore` API, and a platform layer for filesystem, audio, framebuffer, and input. Do not describe the current package as a playable emulator.

## Host-only upstream build
The CI job is a source/build validation for mGBA. It does not cross-compile mGBA for Symbian and does not produce SIS/SISX.

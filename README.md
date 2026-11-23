# N-GBA — Stellar Dragon
Native C++ GBA emulator project scaffold with game-library model, configurable controls, core adapter boundary and host CI.

Status: library data model (filter/favorite/view/cover metadata) but no native renderer; controls remappable model but no platform input/turbo/HID frontend; mGBA candidate not vendored/adapted (mock is not emulator); host tests/package CI active, Symbian cross-build gated and no SIS output yet.

mGBA (MPL-2.0): https://github.com/mgba-emu/mgba. Run `scripts/fetch-core.sh` to fetch upstream for porting. Keep upstream notices/license. Host test commands: `cmake -S . -B build -DNGBA_HOST_TESTS=ON`, `cmake --build build`, `ctest --test-dir build --output-on-failure`. See BUILD.md, CONFIGURATION.md, INTEGRATION_NOTES.md. Modern symbian-platform targets EKA2 ARMv5T/ARMv6; legacy S60v3 FP1/FP2 requires separate Nokia SDK pipeline.

## Open-source emulator core integration

mGBA is the selected upstream core (MPL-2.0), pinned to tag `0.10.5`. Run `scripts/fetch-core.sh` to retrieve source and `scripts/build-mgba-host.sh` to validate its library-only host build. The N-GBA frontend adapter is currently a stub; see `docs/core/STATUS.md` for the precise integration boundary and remaining work.

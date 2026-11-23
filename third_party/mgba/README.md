# mGBA upstream dependency

N-GBA uses **mGBA** as the selected open-source GBA core (MPL-2.0). Source is fetched at build time and is not vendored into this repository.

Pinned release: `0.10.5` (tag `0.10.5`).

Fetch it with `scripts/fetch-core.sh`; host CI separately configures and builds mGBA's library-only target to verify the upstream source/toolchain. The current N-GBA `EmulatorBackend` is still an adapter seam: fetching/building upstream does **not** yet mean ROM execution is connected to N-GBA's frontend. See `docs/core/STATUS.md`.

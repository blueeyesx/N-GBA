# Build and test

## Portable frontend/model

```sh
cmake -S . -B build -G Ninja -DNGBA_HOST_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Fetch mGBA source and validate upstream host build

```sh
scripts/fetch-core.sh
scripts/build-mgba-host.sh
```

The upstream core is fetched at tag `0.10.5`; CI validates the library-only host build. This is **not** a Symbian cross-build and is not yet connected to the N-GBA frontend adapter.

## Symbian SDK pipeline

The `symbian-platform-probe` workflow is manually dispatched and gated by repository variable `SYMBIAN_PLATFORM_SDK_READY=true`. It installs the published tool and runs `symbian doctor` and `symbian toolchain probe`. The actual ARM target SDK must be provisioned using the platform project's documented SDK setup; the current project has no completed native Symbian frontend or SIS manifest, so this stage intentionally does not claim to create a SIS/SISX.

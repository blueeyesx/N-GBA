# Integration boundaries
`ui/game_library.hpp` provides a portable model, filtering and favorites. Native renderer must present Grid2x2/2x3/3x3/3x4/List, cover, title, code, last-played and favorite. File scanner, image decoder and Window Server widgets remain platform work.
`core/adapter/core_api.hpp` defines the boundary. The mock does not execute ROMs. mGBA is selected but not a drop-in Symbian binary. First milestone: core-only build without desktop frontend dependencies, then adapter, framebuffer, BIOS/ROM/save APIs, test ROM suite.
Targets: host model/tests; modern Symbian EKA2 ARMv5T/ARMv6 via symbian-platform after SDK workspace/package configured; legacy S60v3 FP1/FP2 needs separate GCCE/ARMV5 + Nokia SDK + SIS tools.

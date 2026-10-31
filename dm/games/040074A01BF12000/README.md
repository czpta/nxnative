# Donkey Kong Bananza (`040074A01BF12000`)

A Switch 2 game: update v458752 over the base game, SDK 20.5.19 built for the Switch 2 (`sdk` names its spec `Ounce`), NVN2. Nintendo EPD's engine (sead / agl / gsys, Phive), modules `main` + `sdk`. **In bring-up, not playable:** on the Mac it boots into its frame loop and runs (audio renders, frames are presented), but it draws nothing, because the shader chain does not translate NVN2's Ampere shader code yet ([Open](#open)).

## What it needs

| Setting | Where | Why |
|---|---|---|
| `sys_resource 0x0` | [`game.tsv`](game.tsv) | the NPDM declares no system resource: virtual address memory off, the game sets a heap. Without the key the host answers 32 MB, and the SDK's VammManager then maps memory up front (GetInfo 0x21) |
| `app_pool 0x240000000`, `mem_reserve 0x270000000` | `game.tsv` | the Switch 2 leaves games 9 GB of its 12 GB (not measured here); with a Switch 1 pool (3.2 GB) the game's heaps did not fit: `AudioSystemHeap` creation returned null (`main+0x564b48`) |
| `romfs_marker System/Resource/ResourceSizeTable.Product.rsizetable.zs` | `game.tsv` | a file every version has |

## Files

- Packed from a dump folder (`base/`, `updates/458752/`, `dlc/040074a01bf13001/`) with [`scripts/pack_dump.py`](../../../scripts/pack_dump.py): the update's RomFS is the patched one (80,273 files, 8.6 GB; it drops 42 base files, among them the `Product.100` tables its `Product.200` ones replace), so it is packed on its own. The DLC (add-on content index 1, 8,000 files, 0.9 GB) mounts as `EmeraldRush`.
- Shaders: `Shader/*.Nin_Ounce_NVN2.bfsha.zs` (BNSH) and `Shader/Project.Nin_Ounce_NVN2.release.sarc.zs`. The code is Volta / Ampere's 128-bit encoding (`0x794d` EXIT, `0x7918` NOP), not Maxwell's.
- NVN2 control sections (dumped with `DM_NVN_SHDUMP`): magic `0x98761234`, version `0xc9`, shorter than NVN's 0x880 bytes (the next one follows at +0x810) and holding pointers the game relocates. The host's shader key reads the code size at NVN's `+0x704`, which here is half a pointer, so keys are wrong.

## Boot (Mac)

Each step below stopped the boot before its fix:

1. `sdk: no SDK version string`: the version reads `nnSdk-Generic_20_5_19` (`loader.c` `dm_sdk_major` skips the variant name).
2. `GetInfo` 0x21, a Switch 2 info type: the kernel's page size. `nn::ro::ProtectRelro` takes value − 1 as the RELRO alignment mask; 0 aborted (`hle_svc.c` answers 0x1000). Type 28 is Horizon's AliasRegionExtraSize (0, named with 25–27 so that Switch 1 games print no new red line); 29 and 30 are still answered 0 with a red line: `nnosInitialize` builds an address space allocator over [type 29, type 29 + type 30), which region is unknown.
3. NVN2 reports API 231: the game requires major 231, minor ≥ 30 (`main+0x5c61a4`) and creates no graphics system otherwise (null read in its frame loop). `dm_sdk_ounce` (the sdk's `spec: Ounce`) selects 231.30 (`hle_nvn.c`).
4. 197 NVN2 entry points (ray tracing, mesh shaders, DLSS, device commands, HDR window metadata, a ProgramBuilder) were unknown names, so lookups returned null; they are in `names.txt` now, the unimplemented ones going to the red stub. Implemented: the ProgramBuilder (`nvnProgramInitializeFromBuilder` = Initialize + SetShaders), `QueueBuilderSetPrimaryQueue`, `CommandBufferSetRecordingMode` / `SetGeometryProcessingMode` / `SetComputeOverlapMode`.
5. Switch 2 SDK calls through services the host does not serve: `hid::InitializeJcms` and the Joy-Con mouse sensor (`hle_hid.c`), `oe::GetCurrentGraphicsScale` (`hle_oe.c`), `audio::IsDeviceOutputVolumeAutoTuneModeEnabled` (`hle_audren.c`), `streamplay` (GameShare, `hle_misc.c`), `vi` HDR / VRR / extended swap interval (`dm/gfx/hle_vi.c`).

## Open

- **Shaders:** the chain decodes Maxwell only. Needed: NVN2 control sections (layout, code size, constant buffer and resource reflection) for the host's shader keys and the shader split, an Ampere decoder in the translator, and the split reading the NVN2 bfsha files.
- `oe::GetCurrentGraphicsScale`: which tier the console reports in which mode is not measured (the host answers 0 handheld, 1 console).
- GetInfo types 29 and 30, and the 9 GB pool.
- The NVN2 device values (`nvnDeviceGetInteger`) are still the Switch 1 ones.
- iOS untested.

## Found on this game

- Everything under [Boot](#boot-mac) is new: the first Switch 2 game here.

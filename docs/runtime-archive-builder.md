# Runtime archive builder

The Diamond Dogs Together launcher builds `mgs-coop\mgs-coop-player-runtime.dat` itself, from the player's own game archives. It uses no external tools and downloads nothing. The loader (`native/archive_mount.cpp`) accepts only the exact file:

- SHA-256 `87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96`
- 16,333,824 bytes

The code lives in `native/launcher/` (CMake target `ddt_archive`, set up in `cmake/ddt_launcher_archive.cmake`). All of it is project-authored, written from the format facts below and RFC 1950/1951 / PKWARE APPNOTE:

| File | Contents |
|---|---|
| `runtime_dat.{hpp,cpp}` | `BuildRuntimeDat`, `BuildRuntimeDatBytes`, `VerifyRuntimeDat`, `CheckEmbeddedRuntimeAssets` |
| `qar.{hpp,cpp}` | QAR v1 reader (section-table lookup, decrypt1, zlib) and writer |
| `fpk.{hpp,cpp}` | FPK/FPKD parse and write |
| `fox2.{hpp,cpp}` | FOX2 string-table rewrite (first-use order) |
| `inflate.{hpp,cpp}`, `crc32.{hpp,cpp}` | Raw deflate and zlib decoder; CRC-32 and Adler-32 |
| `zip.{hpp,cpp}` | Zip reader (stored and deflated) and zip writer (stored), for save sharing and log collection |
| `hash.{hpp,cpp}`, `file_io.{hpp,cpp}` | MD5/SHA-256 via BCrypt; 64-bit positioned reads, whole-file I/O |
| `ddt_datbuild.cpp` | Developer CLI (not shipped) |

## Calling it

```cpp
std::string error;
bool ok = ddt::BuildRuntimeDat(gameDir, gameDir + L"\\mgs-coop\\mgs-coop-player-runtime.dat",
                               [](float fraction, const std::string& stage) { /* update the UI */ }, error);
```

- The builder reads only nine entries, from `master\chunk0.dat`, `chunk2.dat`, `chunk3.dat` and `chunk4.dat`. It opens them read-only and never reads a whole chunk.
- It checks every source against its pinned SHA-256, then every output entry, then the whole archive.
- It writes `<out>.tmp-<pid>-<tick>` next to the output, flushes it, hashes the file again from disk, and then moves it over the output with `MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH)`.
- On any failure it deletes the temporary file and leaves an existing output untouched. `error` is a plain sentence that names the failing source, for example "Source f30050.fpkd in master/chunk3.dat does not match the expected game data ...", and says to verify the game files in Steam.
- The output folder must already exist.
- The progress callback runs on the calling thread. It covers 0 to 0.8 for reading, 0.8 to 0.9 for assembly, and 0.9 to 1.0 for saving and verifying.
- A warm-cache build takes about 130 ms on the development PC.

## Sources and transforms

Sources come from Steam MGSV:TPP 1.0.15.4. All of them are zlib-compressed in the game archives. The hashes are of the inflated bytes.

| Source | Archive | Path hash | Bytes | SHA-256 |
|---|---|---|---|---|
| `two_players_game_obj.fpk` | chunk0 | `0x52292cbcda547cb2` | 112 | `60b461fd…` |
| `two_players_game_obj.fpkd` | chunk0 | `0xed512cbcda547cb2` | 2,400 | `8c8e557b…` |
| `mtbs_mission_fob.fpk` | chunk3 | `0x5229c558f8a01d7a` | 2,407,088 | `f2092c93…` |
| `f30050.fpk` / `.fpkd` | chunk3 | `0x522b852ee6759072` / `0xed53852ee6759072` | 989,168 / 311,088 | `05f6e682…` / `3b38bdb7…` |
| `f30010.fpk` / `.fpkd` | chunk2 | `0x522af337cf10ef1d` / `0xed52f337cf10ef1d` | 2,848,192 / 2,566,688 | `97c50bee…` / `1238d253…` |
| `f30020.fpk` / `.fpkd` | chunk4 | `0x522b11c3eac3c0e2` / `0xed5311c3eac3c0e2` | 2,674,336 / 2,112,144 | `c250778c…` / `d1dcf1e0…` |

The output has 13 entries, in this order. The full hashes are pinned in `runtime_dat.cpp`.

| # | Output path | Made from |
|---|---|---|
| 0 | `/Assets/tpp/pack/player/game_object/mgs_coop_four_players_game_obj.fpk` | Copy of `two_players_game_obj.fpk` |
| 1 | `…/mgs_coop_four_players_game_obj.fpkd` | `two_players_game_obj.fpkd`, with its FOX2 renamed to `mgs_coop_four_players_game_obj.fox2` (path MD5 recomputed) and the FOX2 string table rewritten (below) |
| 2, 7 | `…/mgs_coop_four_player_controller.fpk`, `…/mgs_coop_two_player_scarf_controller.fpk` | Copy of `mtbs_mission_fob.fpk` |
| 3, 8 | `…/mgs_coop_four_player_controller.fpkd`, `…/mgs_coop_two_player_scarf_controller.fpkd` | Embedded (see "Embedded data") |
| 4 | `/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua` | Embedded `lua/coop/archive_probe.lua`, CRLF turned into LF at build time |
| 5, 9, 11 | `/Assets/tpp/pack/mission2/free/f300N0/mgs_coop_f300N0.fpk` | Copy of `f30050.fpk`, `f30010.fpk` and `f30020.fpk` |
| 6, 10, 12 | `…/mgs_coop_f300N0.fpkd` | The mission FPKD minus one entry: `single_player2_instance.fox2` (f30050, f30020) or `single_pl2_instance_scarf.fox2` (f30010) |

## Formats

### QAR v1

All values are little-endian.

- **Header.** `"SQAR"`, then seven u32 values: flags, entry count, a second-table count (0), file size >> 10, offset of the first file, version (1), and 0. They are XOR-masked with `41441043 11C22050 D05608C3 532C7319 41441043 41441043 11C22050`. The runtime archive uses flags `0x3011E0`. The reader refuses flag `0x800`, which selects a different alignment.
- **Section table.** It starts at offset 32, with 8 bytes per entry. For entry *i*, the low u32 is XORed with `mask[(i + 8i/5) % 4]` and the high u32 with `mask[(i + (8i+4)/5) % 4]`, where `mask = {41441043, 11C22050, D05608C3, 532C7319}`. The value is `(offset >> 10) << 40 | (hash & 0xFF) << 32 | hash >> 32`. Matching the low 40 bits finds an entry without reading any data.
- **Entry header.** 32 bytes:
  - the path hash, as two u32 values each XORed with `41441043`;
  - the stored size, XOR `11C22050`;
  - the plain size, XOR `D05608C3`;
  - the MD5 of the plain bytes, as four u32 values XORed with `532C7319 41441043 41441043 11C22050`.
- **Entry data ("decrypt1").** Byte *p* of the data is XORed with byte `p % 8` of the 8-byte little-endian key `T[k] T[k+1]`, where `k = 2 * ((hashLow + 8*(p/8)/11) % 4)` and `T = {BB8ADEDB 65229958 08453206 88121302 4C344955 2C02F10C 4887F823 F3818583}`. A trailing partial block uses the same key bytes.
- **Compression.** If the stored size differs from the plain size, the data is a zlib stream.
- **Second layer.** Data that starts with `A0F8EFE6` or `E3F8EFE6` uses the second encryption layer ("decrypt2"). No source uses it, and the reader refuses it.
- **Writing.** The writer places entries in order at 1024-byte-aligned positions with zero padding.

### FPK / FPKD

- A 48-byte header: the magic `foxfpk\0win` or `foxfpkdwin`, a u32 file size, 18 zero bytes, then u32 values 2, the entry count, the reference count, and 0.
- 48-byte entry records: u64 data offset, u64 size, u64 string offset, u64 string length, and the MD5 of the path.
- 16-byte reference records: u64 string offset and u64 length.
- The null-terminated strings, entries first and then references, padded to 16 bytes.
- Each entry's data, padded to 16 bytes.

Rewriting every one of the nine source packages gives back the same bytes. Entry data, including the encrypted Lua inside mission packages, is copied as raw bytes.

### FOX2 string table (entry 1)

- **What changes.** The entity section (the first 1440 bytes) stays byte-identical. Only the string table is rebuilt: in first-use order instead of the game's alphabetical order, and with the empty string (hash `0xB8A0BF169F98`) added.
- **First-use order.** Each entity contributes its class name, then for each property its name followed by its String, Path and FilePtr values. For string maps it contributes each key followed by its value. Duplicates are dropped.
- **Table end.** After the table come a zero u64, padding to 16 bytes, `"\0\0end"`, and padding to 16 bytes again.
- **Refusals.** The rewrite refuses:
  - any property type whose string layout it does not know (EntityLink, PropertyInfo);
  - string maps of non-8-byte values;
  - a table string that no entity references.

## Embedded data

- **The controller packages.** `native/launcher/assets/mgs_coop_four_player_controller.fpkd.bin` (entry 3) and `mgs_coop_two_player_scarf_controller.fpkd.bin` (entry 8) are 6,624 bytes each. They were extracted once from the installed 0.13.6 runtime archive.
- **Their origin.** They hold FOX2 compiled from Dynamite's controller XML with project edits (`third_party/dynamite/player-runtime-source.json`, `player-asset-source.json`). They are covered by Dynamite's GPLv3 with Commons Clause, the same notice as the loader's Dynamite-derived code. The release notices still say that no game-derived runtime archive is shipped, so they must be updated when the launcher is packaged.
- **How they are built in.** `cmake/ddt_embed.cmake` turns the two `.bin` files and `lua/coop/archive_probe.lua` into generated headers (`build/.../generated/ddt/ddt_asset_*.hpp`).
- **The self-check.** `CheckEmbeddedRuntimeAssets()` checks all three against their pinned SHA-256. Editing `archive_probe.lua` therefore breaks the build of the archive, by design.

## Developer CLI and tests

```text
ddt_datbuild build <gameDir> <out.dat>          build and verify, prints timing
ddt_datbuild verify <file.dat>                  size + SHA-256
ddt_datbuild list <archive.dat>                 QAR entries: hash, offset, sizes, MD5
ddt_datbuild check <archive.dat>                read every entry (unscramble, inflate, MD5)
ddt_datbuild extract <archive.dat> <hash> <out> one entry's plain bytes
```

- **`ddt_archive_tests`.** Unit tests on synthetic data:
  - CRC-32, Adler-32, MD5 and SHA-256;
  - inflate of stored, fixed and dynamic blocks, a mixed multi-block stream, and error cases;
  - zip round trip, deflated zips from another tool, path safety and limits;
  - QAR write→read, including compressed and colliding-key entries and refusals;
  - FPK round trip;
  - the FOX2 rewrite;
  - the embedded asset check;
  - the builder refusing missing, mis-sized or mismatching sources without touching the output.
- **`ddt_runtime_dat_integration`.** Builds the archive from `D:/Games/steamapps/common/MGS_TPP` (CMake cache `DDT_TEST_GAME_DIR`, overridden by the `MGS_GAME_DIR` environment variable) into a temp folder. It checks the pinned hash, then rebuilds over the existing file. It is reported as skipped, not failed, when the folder has no game archives.

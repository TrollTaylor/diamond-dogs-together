# Local player-package inspector

Project-authored Go command that opens installed QAR archives read-only, looks up exact package paths, and writes local package copies plus decoded FOX2 and hash inventories. It neither installs nor rewrites archives. Output must be a new directory outside the game directory. The normal workflow uses `build/` in this project.

It uses the same `datfpk v0.3.0` parser as Dynamite, from the local checkout `work/upstream-datfpk`, commit `298affd0aaa3e9342e67c0c179f286719e034a28`. The module replacement is local; check that checkout's revision before building. `go.sum` records fetched dependencies. The v0.3.0 checkout has no root LICENSE file; this is local research tooling, and its binary/dependencies have not been included in a release. Upstream credits Atvaark's FoxTool and GzsTool. Distribution rights for this dependency remain to be resolved before shipping it.

From the project root, with Go installed:

```powershell
$env:GOCACHE = Join-Path (Get-Location) 'build/go-cache'
$env:GOMODCACHE = Join-Path (Get-Location) 'work/go-mod-cache'
go -C tools/archive_probe build -o ../../build/archive_probe.exe .
./build/archive_probe.exe -out build/player-package-inspection-new
```

The default exact requests are `two_players_game_obj.fpk` and its `.fpkd`. Every matching copy from `master/0/{00,01}.dat` and `chunk0.dat` through `chunk4.dat` is retained. This is not an exhaustive mounted-archive scan or runtime precedence resolver. No path dictionary or whole-game extraction is needed. Do not treat file presence as proof that a live mission loads it.

Only known local game inputs were exercised. This wraps the upstream parsers; it is not a hardened parser for hostile or corrupt archives.

Exact `-paths` requests also accept standalone `.lua` entries. Those are retained as raw bytes without passing them through an FPK parser; the extension alone does not establish whether they are plaintext or bytecode. For example, `-paths '/Assets/tpp/script/lib/Tpp.lua'` extracted the installed startup module for the bootstrap check. Extracted game scripts stay in local `build/` output and are not release assets.

// Project-authored local research tool using datfpk v0.3.0's file APIs.
// Game archives are opened read-only. This does not install or repack a mod.
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/unknown321/datfpk/fox2"
	"github.com/unknown321/datfpk/fpk"
	"github.com/unknown321/datfpk/qar"
)

type entryInfo struct {
	Path   string `json:"path"`
	SHA256 string `json:"sha256"`
	XML    string `json:"xml,omitempty"`
}
type packageInfo struct {
	Archive    string      `json:"archive"`
	Path       string      `json:"path"`
	SHA256     string      `json:"sha256"`
	SavedAs    string      `json:"saved_as"`
	Entries    []entryInfo `json:"entries"`
	References []string    `json:"references"`
}

func inspect(data []byte, archive, path, out string, index int) (packageInfo, error) {
	result := packageInfo{Archive: archive, Path: path, SHA256: fmt.Sprintf("%x", sha256.Sum256(data))}
	result.SavedAs = fmt.Sprintf("package-%02d%s", index, filepath.Ext(path))
	if err := os.WriteFile(filepath.Join(out, result.SavedAs), data, 0600); err != nil {
		return result, err
	}
	// Exact standalone script requests are retained byte-for-byte. Do not feed
	// them to the FPK parser or infer plaintext from the .lua extension.
	if strings.EqualFold(filepath.Ext(path), ".lua") {
		return result, nil
	}
	reader := bytes.NewReader(data)
	var pack fpk.Fpk
	if err := pack.Read(reader, false); err != nil {
		return result, err
	}
	for i, entry := range pack.Entries {
		if err := entry.ReadData(reader); err != nil {
			return result, err
		}
		row := entryInfo{Path: entry.FilePath.Data, SHA256: fmt.Sprintf("%x", sha256.Sum256(entry.Data))}
		if strings.HasSuffix(row.Path, ".fox2") {
			var fox fox2.Fox2
			if err := fox.Read(bytes.NewReader(entry.Data)); err != nil {
				return result, err
			}
			var xml bytes.Buffer
			if err := fox.ToXML(&xml); err != nil {
				return result, err
			}
			row.XML = fmt.Sprintf("package-%02d-entry-%03d.fox2.xml", index, i)
			if err := os.WriteFile(filepath.Join(out, row.XML), xml.Bytes(), 0600); err != nil {
				return result, err
			}
		}
		result.Entries = append(result.Entries, row)
	}
	for _, ref := range pack.References {
		result.References = append(result.References, ref.FilePath.Data)
	}
	return result, nil
}

func run() error {
	game := flag.String("game", "D:/Games/steamapps/common/MGS_TPP", "game directory, read only")
	out := flag.String("out", "", "new workspace output directory")
	requests := flag.String("paths", "/Assets/tpp/pack/player/game_object/two_players_game_obj.fpk,/Assets/tpp/pack/player/game_object/two_players_game_obj.fpkd", "comma-separated exact internal package paths")
	flag.Parse()
	if *out == "" {
		return fmt.Errorf("explicit workspace output directory required")
	}
	target, err := filepath.Abs(*out)
	if err != nil {
		return err
	}
	source, err := filepath.Abs(*game)
	if err != nil {
		return err
	}
	rel, err := filepath.Rel(source, target)
	if err == nil && rel != ".." && !strings.HasPrefix(rel, ".."+string(os.PathSeparator)) {
		return fmt.Errorf("output may not be inside game directory")
	}
	if err := os.Mkdir(target, 0700); err != nil {
		return fmt.Errorf("output must be new: %w", err)
	}
	paths := strings.Split(*requests, ",")
	results := []packageInfo{}
	found := map[string]bool{}
	// Include the patch archive and base chunks. Keep every occurrence: this
	// tool does not assume archive precedence or choose a runtime-winning copy.
	archives := []string{"0/00.dat", "0/01.dat", "chunk0.dat", "chunk1.dat", "chunk2.dat", "chunk3.dat", "chunk4.dat"}
	for _, name := range archives {
		file := filepath.Join(source, "master", filepath.FromSlash(name))
		if _, err := os.Stat(file); os.IsNotExist(err) {
			continue
		}
		var q qar.Qar
		if err := q.ReadFrom(file); err != nil {
			q.Close()
			return fmt.Errorf("%s: %w", name, err)
		}
		for _, path := range paths {
			data, err := q.ReadFile(path)
			if err != nil {
				if err.Error() == "file not found" {
					continue
				}
				q.Close()
				return fmt.Errorf("%s %s: %w", name, path, err)
			}
			result, err := inspect(data, name, path, target, len(results))
			if err != nil {
				q.Close()
				return fmt.Errorf("%s %s: %w", name, path, err)
			}
			results = append(results, result)
			found[path] = true
		}
		q.Close()
	}
	missing := []string{}
	for _, path := range paths {
		if !found[path] {
			missing = append(missing, path)
		}
	}
	report := map[string]any{"tool_dependency_commit": "298affd0aaa3e9342e67c0c179f286719e034a28", "packages": results, "missing": missing, "game_writes": false, "runtime_allocation_verified": false, "archive_precedence_resolved": false}
	data, err := json.MarshalIndent(report, "", "  ")
	if err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(target, "inventory.json"), append(data, '\n'), 0600); err != nil {
		return err
	}
	fmt.Printf("Inspected %d package copies; %d requested paths missing. Game archives unchanged by this tool.\n", len(results), len(missing))
	return nil
}

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

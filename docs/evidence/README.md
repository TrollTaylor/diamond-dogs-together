# docs/evidence

The research evidence that used to live here (debugger and CTest logs, contract
JSON reports, motion captures, game screenshots) was removed from the tree on
2026-10-23. It is kept in the annotated tag `archive/research-2026-10-23`
(commit cd25f22, version 0.13.6), together with the other research notes that
were removed at the same time.

Read an archived file without checking anything out:

```
git fetch origin tag archive/research-2026-10-23
git show archive/research-2026-10-23:docs/evidence/<name>
git checkout archive/research-2026-10-23 -- docs/evidence/<name>
```

Or browse it on GitHub:
<https://github.com/TrollTaylor/diamond-dogs-together/tree/archive/research-2026-10-23/docs/evidence>

Links in the kept docs and code comments that point at removed files use the
same tag, written as `archive/research-2026-10-23:<path>`.

## What stays here

- `job-lifetime-analysis-2026-09-09.json`: the input of
  `tools/generate_session_allocation_contract.py`, which writes
  `native/session_allocation_contract.hpp`.
- This folder itself. About 20 contract generators (`tools/generate_*_contract.py`)
  still write their JSON report here when they regenerate a header. Those reports
  are local output and `.gitignore` here hides them; force-add one
  (`git add -f`) only when a doc needs it.

# BRAI graphs

`src/wolf2009/BrainGraph` is a portable C++11 structural decoder shared with
the narrow startup handler. It reads NUL-delimited token records, leading-tab
nesting, node IDs and output targets. The observed `h1023/32/32/32/0` and
`g1023/32/32/32` envelopes are explicit formats. Duplicate IDs, unresolved
targets, missing parents, truncation and exceeded limits cause rejection.

The decoded representation retains every record, byte range, parent, token,
node and output. Empty strings and legacy 8-bit text are preserved. Output
flags and nested condition fields remain opaque. In particular, a parsed
output is not an instruction to execute its destination unconditionally.
Cycles and branches are valid structures and are preserved.

## Coverage

The native decoder passed the owner's 633 BRAI resources under `SP/base/maps`
without extracting or changing them. These contain 36,735 records, 1,502,214
tokens, 14,147 nodes, 13,158 outputs and 154 distinct node types. There are
632 `h1023` resources and one `g1023`. All outputs resolved within their own
graph. This measures structural coverage, not gameplay compatibility.

The default per-resource limits are 4 MiB source data, 20,000 records,
500,000 tokens, 1,024 bytes per token and depth 64. Synthetic tests cover
branches, cycles, forward references, nested conditions, unknown bytes,
empty tokens, both envelopes, malformed topology and each limit.

## Reproduce

The ordinary portable CMake build includes `wolf2009-brains` and its CTest
self-test. The Windows Zig build script also builds this executable.

```powershell
.\build\native\wolf2009-brains.exe --self-test
$packages = Get-ChildItem 'C:\path\to\Wolfenstein\SP\base\maps' -Recurse -File |
    Where-Object { $_.Extension -in '.mpk', '.spk' } |
    Select-Object -ExpandProperty FullName
.\build\native\wolf2009-brains.exe @packages
```

No game assets are included. `BrainStartup` still accepts only its established
direct OnSpawn → MoveToEnt profile with default output fields and a sentinel.
It deliberately refuses branches or unknown conditions. The existing mover,
arrival and door handlers do not yet constitute a general interpreter. The
next step is to attach independently verified action/condition handlers to
this common representation and compare behavior across missions.

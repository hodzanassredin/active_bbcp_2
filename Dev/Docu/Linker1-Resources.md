# Resource files in DevLinker1

`LinkPeExe64`, `LinkPeExe`, `LinkPe` and `LinkPeDll` accept resource files after
the module roots and before the optional final `~`:

```componentpascal
DevLinker1.LinkPeExe64 'program.exe' := Kernel+ WinKernel Program$ 1 Applogo.ico 1 BlackBox.res 2[1049] 'my.ico' ~
```

The syntax is `id [ "[" language "]" ] filename`. IDs and languages are integers
from 0 through 65535; a language omitted for standalone files defaults to 1033.
Quote paths containing separators, spaces or punctuation. Unquoted simple names
such as `Applogo.ico` work too. Files are searched in `Rsrc`, `Win/Rsrc`, then at
the supplied absolute or relative path. Parent output directories must exist.

| File | Embedded resources |
| --- | --- |
| `.ico` | RT_GROUP_ICON plus all RT_ICON images, with matching image IDs |
| `.cur` | RT_GROUP_CURSOR plus RT_CURSOR images and their hotspots |
| `.bmp` | RT_BITMAP DIB data, without the 14-byte bitmap file header |
| `.res` | All compiled records, including custom types, names and languages |
| `.tlb` | Named TYPELIB resource type |
| `.manifest`, `.xml` | RT_MANIFEST |

A single-record `.res` receives the command's numeric ID, matching the old
DevLinker convention (`1 BlackBox.res`). For multi-record `.res` containers, the
contained IDs and names are preserved, including image IDs referenced by icon
and cursor groups; the command's numeric prefix is ignored. Original `.res`
languages are preserved unless `[language]` explicitly overrides them.
Duplicate type/name/language keys are errors. When mixing compiled icon resources
and standalone icons, place `.res` containers first: generated image IDs avoid
already occupied IDs. A later `.res` collision is rejected rather than replaced.

The `.res` format is binary Windows resource data, not an RC script or COFF object.
For menus, accelerators, dialogs, arbitrary RCDATA, version info or custom types,
compile an RC file to `.res` first, for example:

```sh
x86_64-w64-mingw32-windres -O res app.rc app.res
```

Inline DevLinker MENU/ACCELERATOR descriptions and its legacy image-conversion
option characters are not part of this file-input syntax. Embed their compiled
`.res` equivalents. ELF commands reject Windows resources explicitly.

## Implementation and tests

`DevLnkResources` parses the inputs, validates bounds and duplicate keys and builds
the sorted type/name/language tree. Unicode identifiers are stored as UTF-16.
`DevLnkWritePe` includes the directory and payloads in a read-only `.rsrc` section,
sets the PE resource data directory, and includes its size in the image layout.
The same implementation is used by PE32 and PE32+; pdata/xdata/unwind layout stays
valid. Failed resource parsing stops linking before an output file is published.

```sh
python3 Dev/Test/check_pe_resources.py
# Also exercise the linker compiled for AMD64:
python3 Dev/Test/check_pe_resources.py --native-linux-dir build/blackbox-lin64
```

The test compares complete payloads, group-image links and directory sorting,
checks named Unicode and multilingual resources, validates matching PE32/PE64
resource trees and exercises native FindResourceExW/LoadResource/LockResource,
LoadIconW, LoadCursorW and LoadImageW under Wine. It checks missing/truncated files,
invalid languages, duplicate keys, ELF rejection and state reset between commands.
PE32 is inspected structurally; the executable API test runs on Windows x64.

`Win/Test/build_blackbox64.py` now embeds the standard BlackBox version resource,
eight icon groups and seven cursor groups using their existing framework IDs.
Its GUI tests pass with these resources in both bootstrap variants.

The optional native-linker check also guards the PE writer's local open-array
string parameter layout: using an imported array-type alias there previously
shifted following parameters on AMD64 and corrupted the DOS/PE headers.

PE section headers are emitted in ascending contiguous RVA order: .text, .data,
.var, .idata, .edata, .rsrc, .pdata, .xdata, .reloc (omitting absent sections).
The earlier resource integration placed .rsrc before the lower-address import
and export sections; Wine accepted it, but it violated the Windows PE layout
requirements and could be rejected before startup. `check_pe_layout.py` now
checks section order, alignment, raw-data bounds, image size and data directories
in both resource tests and every BlackBox Windows build. The old image fails
this check; the rebuilt image passes it. See Microsoft's
[PE format specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format).

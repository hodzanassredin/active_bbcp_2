# DevLinker1

DevLinker1 links Component Pascal object modules into executable images. Existing
`LinkPeExe`, `LinkPe`, `LinkPeDll`, and the unsuffixed ELF commands continue to select i386.

`LinkPeExe64` selects **Dev AMD64 OCF → Windows x64 PE32+ executable**:

```text
DevLinker1.LinkPeExe64 Program.exe := Kernel64+ Library Program$ ~
```

See [Windows x64 implementation, build commands and limitations](Linker1-Win64.md).
The command takes the same `output := modules` form as the existing linker.
`+` selects the allocation kernel, `$` selects the main module, and ordinary names
include additional module roots. Dependencies are included recursively.

Sources: [commands](../Mod/Linker1.cp), [shared model](../Mod/LnkBase.cp),
[i386 reader](../Mod/LnkLoad.cp), [AMD64 reader](../Mod/LnkLoad64.cp),
[PE writer](../Mod/LnkWritePe.cp).

`LinkElfExe64` selects **Dev AMD64 OCF → Linux x86-64 ELF executable**, using
`Kernel` and `LinKernel` directly. See [Linux ELF64](Linker1-Elf64.md).

PE commands also accept resource files after the module list, using the same
`id [language] filename` form as DevLinker. See [PE resources](Linker1-Resources.md).

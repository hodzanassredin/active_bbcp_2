# Packing dev0Linux

`dev0Linux` is the i386 Linux command-line host. It includes DevCompiler,
DevCompiler64, DevLibrarian, and the PE32/PE64/ELF32/ELF64 linker dependencies.

Run from the repository root:

```sh
./pack-dev0-linux
```

The script uses `/bin/sh`, the existing `./dev0Linux`, DevLinker1 and DevPacker.
It has a fixed OCF list, like pack-dev0-linux-old, and does not require Python
or run-BlackBoxInterp. It packs precompiled objects from the normal Code
folders; it does not recompile sources. Rebuild changed host modules with
DevCompiler first. Even DevCompiler64 must be compiled to i386 for this host.
Do not use Mod64 or AMD64 target objects when preparing the host.

The script links `dev0Linux.new`, packs the listed objects with DevPacker, and
checks the candidate from an empty temporary directory. That check loads the
interpreter, both compilers and DevLibrarian without external OCF directories.
Only after success does it back up dev0Linux to
`build/dev0-linux/dev0Linux.previous` and replace the installed executable.
The command logs are link.log, pack.log and standalone.log in that directory.

Update PACK_FILES in the script when adding dependencies or dynamically
loaded commands. Do not duplicate modules already linked into the bootstrap.
Keep the complete PackThis command below the LinConsole input limit of
2047 bytes, including its newline. The script checks that listed files are
i386 OCF objects and rejects compiler/linker errors before publication.

USE_BOOTLINKER=0 selects the normal ELF32 linker. The legacy BootLinker branch
is retained from the old script but was not exercised in this update.

Sources and symbol files are still needed when compiling projects. They are
not included by this runtime/tool packaging operation.

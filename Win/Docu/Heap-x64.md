# High-address heap on Windows AMD64

The Windows x64 runtime allocates GC clusters above 4 GiB by default. Every
ordinary NEW object, dynamic array, model, frame and native COM adapter therefore
uses full 64-bit heap pointers, including in the normal BlackBox workspace.
The aggregate live heap is no longer limited by the old 0x10000000–0x70000000 arena.

## Address-space layout

WinKernel separates two allocation paths:

* WkAlloc retains the low arena for executable code, module metadata/data,
  DLL entry stubs and unwind tables. The existing OCF v2 relocations and
  INTEGER-valued DLL-entry contract require this placement.
* WkAllocHeap allocates GC clusters starting at 0x100000000. The upper boundary
  comes from GetSystemInfo, rather than assuming a particular Windows version's
  virtual address-space limit. VirtualQuery skips occupied or undersized regions.
  Released clusters make their addresses available for subsequent allocations.

Both paths retain page ownership metadata in the Windows process heap, outside
GC. Partial module releases and whole-cluster releases use the same checked
VirtualFree bookkeeping. Allocation/commit exhaustion returns allocation failure;
it does not wrap the address or retry indefinitely outside the OS address range.

This is support for a high-address, multi-gigabyte data heap. It does not relocate
CP executable modules or their metadata above 2 GiB: removing that separate
constraint requires a compiler/loader relocation ABI change. Native Windows DLL
functions can already have arbitrary 64-bit addresses behind entry stubs.

## Collector and sizes

Kernel retains full ADDRESS values while manipulating tags, walking the
pointer-reversal mark graph and restoring descriptors. Free-block tags contain
addresses inside the high heap; they must never be compared through INTEGER.
Stack candidates and global pointer slots are read as 64-bit values. The stack
scan still advances by four bytes because CP records may contain unaligned
8-byte pointer slots. Finalizer lists and inter-object links are full pointers.

Kernel.Allocated and Kernel.Used report LONGINT totals. Individual cluster/block
sizes, array lengths, field offsets and serialized section sizes retain their
INTEGER contracts. A single allocation must fit below 2 GiB, including headers
and alignment; many such allocations can occupy a heap larger than 4 GiB.
Overflow near the maximum block size is rejected before rounding or allocating.
This change does not widen the Component Pascal INTEGER type.

Linux already uses unrestricted mmap for heap clusters; the shared Kernel
collector changes also preserve its full-width address operations.

## Verification

```sh
python3 Win/Test/check_heap64.py --out /tmp/winheap64
python3 Win/Test/check_heap64.py --out /tmp/winheap64 --large
# equivalent make targets: heap-test and heap-large-test
```

The default test checks objects above 4 GiB, virtual dispatch/type guards,
arrays of records containing pointers, VAR fields, GC, finalizers, Meta.New,
Meta.GetVal/ParamCallVal and Kernel.Try address arguments. It also allocates
128 MiB of byte arrays and rejects an overflowing block request.

`--large` allocates and touches 68 arrays of 64 MiB (4.25 GiB of live payload),
verifies LONGINT heap counters and data after full collection, releases the
arrays, checks that committed heap usage falls below 1 GiB, and allocates again.
Run it only with enough available memory. It is a real allocation test, not
just a sparse virtual reservation.

Kernel trap recovery, dynamic loading/unloading, GUI, files, drag/drop, clipboard
and external-call integration tests are also run with the high heap. Verification
is under Wine; native Windows remains unverified.

The shared collector also passed native Linux x64 checks, including the 4.25 GiB
live heap and GUI regression; see [Linux heap regression](../../Lin/Docu/Heap-x64.md).

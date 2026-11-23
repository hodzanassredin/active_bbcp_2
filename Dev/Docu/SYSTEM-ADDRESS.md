# SYSTEM.ADDRESS

SYSTEM.ADDRESS is the pointer-sized integer type provided by this version of
the Dev compiler. It is available after `IMPORT SYSTEM` and is intended for
memory addresses, byte offsets and native API values whose width matches a
pointer. This is an extension to the Component Pascal Language Report.

| Target | Size | Integer representation |
|---|---:|---|
| i386 | 4 bytes | INTEGER (signed 32-bit) |
| AMD64 | 8 bytes | LONGINT (signed 64-bit) |

The size depends on the compilation target, not the architecture of the host
running the compiler. INTEGER remains 32-bit on both targets; LONGINT remains
64-bit. SYSTEM.ADDRESS is not an unsigned type and does not enforce a distinct
set of address-only operations: integer arithmetic and comparisons are allowed.
Ordinary integer assignment and parameter compatibility rules apply.

## Memory access

SYSTEM.ADR returns SYSTEM.ADDRESS. GET, PUT and the source and destination
arguments of MOVE accept address values directly:

```componentpascal
	IMPORT SYSTEM;

	VAR address: SYSTEM.ADDRESS; value: INTEGER;

	PROCEDURE Example;
	BEGIN
		value := 42;
		address := SYSTEM.ADR(value);
		SYSTEM.GET(address, value);
		SYSTEM.PUT(address, value)
	END Example;
```

The value operand determines the size read by GET or written by PUT. In this
example it is four bytes on both targets. For a value declared SYSTEM.ADDRESS,
it is four or eight bytes respectively. MOVE copies the specified number of
bytes; address arithmetic also uses byte offsets.

Converting an address to ANYPTR merely to pass it to GET, PUT or MOVE is
unnecessary. To reinterpret an actual pointer as an address, use
`SYSTEM.VAL(SYSTEM.ADDRESS, pointer)`. The reverse conversion requires a
suitable pointer type; it does not allocate memory or validate the address.

## Native interfaces and storage

Use SYSTEM.ADDRESS for pointer-sized API arguments and results, such as native
pointers and handles. Keep fixed-width ABI fields at their prescribed sizes:
Windows DWORD, BOOL and LONG remain INTEGER on Windows AMD64. An address-sized
integer alone does not provide C-compatible record layout; alignment and
padding must also match the native ABI.

An address is an integer, not a managed pointer. Do not rely on an address
variable to retain a heap object during garbage collection; keep a reference
of an appropriate pointer type. Do not narrow AMD64 addresses to INTEGER.
Records containing SYSTEM.ADDRESS can change size between targets and should
not be used as an architecture-independent file or network representation.

Implementation: DevCPT.InitBasic creates the target-sized type; DevCPB assigns
it to SYSTEM.ADR results and checks the address operands of memory operations.

[Russian version](ru/SYSTEM-ADDRESS.md)

Platform-Specific Issues

1: Cross-plarform

1	Module SYSTEM
2	Startup of BlackBox
3	Using NEW and garbage collection in your applications
4	Component Pascal compiler options
5	Runtime range checks
6	Stack Size

10: Windows

10	Command line parameters of BlackBox
11	Using DLLs in BlackBox modules
12	Using COM without Direct-To-COM compiler
13	Windows-specific information in BlackBox
14	Runtime type system
15	Linking BlackBox applications

	Windows programming interfaces
	OLE Automation
	The BlackBox linker

20: Linux

21	Linux Environment parameters of BlackBox
22	Endless loop
25	Linking BlackBox applications

30: OpenBSD 


40: FreeBSD 





1 Module SYSTEM

Module SYSTEM contains certain procedures that are necessary to implement low-level operations. It is strongly recommended to restrict the use of these features to specific low-level modules, as such modules are inherently non-portable and usually unsafe. SYSTEM is not considered as part of the language Component Pascal proper.

The procedures contained in module SYSTEM are listed in the following table. v stands for a variable. x, y, and n stands for expressions. T stands for a type. P stands for a procedure. M[a] stands for memory value at address a.

Function procedures
Name	Argument types	Result type	Description
ADR(v)	any	INTEGER	address of variable v
ADR(P)	P: PROCEDURE	INTEGER	address of Procedure P
ADR(T)	T: a record type	INTEGER	address of Descriptor of T
LSH(x, n)	x, n: integer type*	type of x	logical shift (n > 0: left, n < 0: right)
ROT(x, n)	x, n: integer type*	type of x	rotation (n > 0: left, n < 0: right)
TYP(v)	record type	INTEGER	type tag of record variable v
VAL(T, x)	T, x: any type	T	x interpreted as of type T
* integer types without LONGINT

Proper procedures
Name	Argument types	Description
GET(a, v)	a: INTEGER; v: any basic type,	v := M[a]
	pointer type, procedure type
PUT(a, x)	a: INTEGER; x: any basic type,	M[a] := x
	pointer type, procedure type
GETREG(n, v)	n: integer constant, v: any basic type,	v := Register n
	pointer type, procedure type
PUTREG(n, x)	n: integer constant, x: any basic type,	Register n := x
pointer type, procedure type
MOVE(a0, a1, n)	a0, a1: INTEGER; n: integer type	M[a1..a1+n-1] :=
		M[a0..a0+n-1]

The register numbers for PUTREG and GETREG are:
0: EAX, 1: ECX, 2: EDX, 3: EBX, 4: ESP, 5: EBP, 6: ESI, 7: EDI.

Warning
VAL, PUT, PUTREG, and MOVE may crash BlackBox and/or Windows when not used properly.
Never use VAL (or PUT or MOVE) to assign a value to a BlackBox pointer. Doing this would corrupt the garbage collector, with fatal consequences.

System Flags

The import of module SYSTEM allows to override some default behavior of the compiler by the usage of system flags. System flags are used to configure type- and procedure declarations. The extended syntax is given below.

Type 	=	Qualident
		| ARRAY ["[" SysFlag "]"] [ConstExpr {"," ConstExpr}]
			OF Type 
		| RECORD ["[" SysFlag "]"] ["("Qualident")"] FieldList
			{";" FieldList} END
		| POINTER ["[" SysFlag "]"] TO Type
		| PROCEDURE [FormalPars].
ProcDecl	=	PROCEDURE ["[" SysFlag "]"] [Receiver] IdentDef
			[FormalPars] ";"
		DeclSeq [BEGIN StatementSeq] END ident.
FPSection	=	[VAR ["[" SysFlag "]"]] ident {"," ident} ":" Type.
SysFlag	=	ConstExpr | ident.

For SysFlags either the name of the flag or the corresponding numeric value can be used.

System flags for record types
Name	Value	Description
untagged	1	No type tag and no type descriptor is allocated.
		The garbage collector ignores untagged variables.
		NEW is not allowed on pointers to untagged variables.
		No type-bound procedures are allowed for the record.
		Pointers to untagged record type or extensions of this
		record type inherit the attribute of being untagged.
		The offsets of the fields are aligned to
		MIN(4-byte, size), where size is the size of the field.
		The size of the record and the offsets of the fields are
		aligned to 32-bit boundaries.
noalign	3	Same as untagged but without alignment.
align2	4	Same as untagged but with
		MIN(2-byte, size) alignment. 
align8	6	Same as untagged but with
		MIN(8-byte, size) alignment. 
union	7	Untagged record with all fields allocated at offset 0.
		The size of the record is equal to the size of the
		largest field.
		Used to emulate C union types.

System flags for array types
Name	Value	Description
untagged	1	No typetag and no type descriptor is allocated.
		The garbage collector ignores untagged variables.
		NEW is not allowed on pointers to untagged variables.
		Pointers to this array type inherit the attribute of
		being untagged.
		Only one-dimensional untagged open arrays
		are allowed.
		For open untagged arrays, index bounds are
		not checked.

System flags for pointer types
Name	Value	Description
untagged	1	Not traced by the garbage collector.
		No type-bound procedures are allowed for the pointer.
		Must point to an untagged record.

System flags for VAR parameters
Name	Value	Description
nil	1	NIL is accepted as formal parameter.
		Used in interfaces to C
		functions with pointer type parameters.

System flags for procedures
Name	Value	Description
code	1	Definition of a Code procedure (see below).
callback	2	WinApi callback function; uses __stdcall calling conventions.
ccall	-10	Procedure uses CCall calling convention.

Code procedures

Code procedures make it possible to use special code sequences not generated by the compiler. They are declared using the following special syntax:

ProcDecl	=	PROCEDURE "[" SysFlag "]" IdentDef [FormalPars]
			[ConstExpr {"," ConstExpr}] ";".

The list of constants declared with the procedure is interpreted as a byte string and directly inserted in the code ("in-line") whenever the procedure is called. If a parameter list is supplied, the actual parameters are pushed on the stack from right to left. The first parameter however is kept in a register. If the type of the first parameter is REAL or SHORTREAL, it is stored in the top floating-point register. Otherwise the parameter (or in the case of a VAR/IN/OUT parameter its address) is loaded into EAX. For function procedures the result is also expected to be either in the top floating-point register or in EAX, depending on its type. Be careful when using registers in code procedures. In general, the registers ECX, EDX, ESI, and EDI may be used. Parameters on the stack must be removed by the procedure.

Examples

	PROCEDURE [code] Sqrt (x: REAL): REAL		(* Math.Sqrt *)
		0D9H, 0FAH;			(* FSQRT *)

	PROCEDURE [code] Erase (adr, words: INTEGER)	(* erase memory area *)
		089H, 0C7H,			(* MOV EDI, EAX *)
		031H, 0C0H,			(* XOR EAX, EAX *)
		059H,			(* POP ECX *)
		0F2H, 0ABH;			(* REP STOS *)



2 Startup of BlackBox

The startup of BlackBox happens in several steps.

Step 1: The operating system starts the application

BlackBox consists of a small linked application and many unlinked code files (one per module). The linked application consists of at least the BlackBox Kernel module. When the operating system starts up BlackBox, it gives control to the module body of Kernel.

Step 2: The kernel loads all prelinked modules

The kernel initializes its data structures, in particular for memory management and exception handling. Then it executes the module bodies of the other modules which are linked in the application, in the correct order.
Usually, the module StdLoader is among the prelinked modules, along with several modules that it needs, in particular Files and LinFiles/WinFiles. Module StdLoader implements the linking loader which can dynamically link and load a module's code file. LinLoader/WinLoader loads LinInit/WinInit accordinly.

Step 3: LinLoader or WinLoader loads the BlackBox library and core framework

The standard implementation of Init imports all core framework modules and their standard implementations, but not extension subsystems such as the Text or Form subsystem. These modules are loaded before the body of Init performs the following actions:

• It tries to call Startup.Setup.
Usually, module Startup does not exist. It could be used to install some services before the main window is opened and before the text subsystem or other subsystems are loaded.

• It tries to load module DevDebug.
DevDebug is expected to replace the kernel's rudimentary exception handling facilities by a more convenient tool. Note that the standard implementation of DevDebug uses the text subsystem, which thus is loaded also.
If loading of DevDebug is not successful, it is attempted to load module StdDebug. This is a reduced version of DevDebug which can be distributed along with an application.

• It registers the document file converter (importer/exporter).
This enables BlackBox to open and save standard BlackBox documents.

• It tries to call StdMenuTool.UpdateAllMenus.
This command reads and interprets the System/Rsrc/Menus text, and builds up the menus accordingly. Note that the standard implementation of StdMenuTool uses the text subsystem.

• It tries to call StdConfig.Setup.
This is the usual way to configure BlackBox. The standard implementation of StdConfig.Setup installs the standard file converters, calls StdMenuTool.UpdateAllMenus to read and interpret the System/Rsrc/Menus text, and builds up the menus accordingly, then it creates workspace and opens the log text.

• It calls the main event loop (Loop.Start).
After the event loop is left (because the user wants to quit the application), the kernel is called to clean up, before the application is left entirely.


3 Using NEW and garbage collection in your applications

If you are calling NEW in your application and thereby implicitly use the garbage collector, you must link the Kernel into the application. The NEW-procedure is implemented in the kernel, the compiler just generates the code to call this procedure. So every module using NEW has a hidden import of the kernel.
Don't call WinApi.ExitProcess directly when "importing" the kernel, call Kernel.Quit with parameter 0 instead to assure that occupied system resources get properly released before the application is terminated.

Programs don't need to call the garbage collector explicitly. If the NEW-procedure cannot satisfy a request for heap space, it calls the garbage collector internally before allocating a new heap block from the Windows Memory Manager. The garbage collector marks pointers in stack frames and is able to run anytime.

The garbage collector reclaims all heap objects (dynamic record or array variables) that are not used anymore. "Unused" means that the object isn't directly reachable from some "root" pointer, or indirectly via a pointer chain starting from a root pointer. Any global variable which contains a pointer is such a root. If a root pointer isn't NIL, then it "anchors" a data structure. A heap object that isn't anchored anymore will eventually be collected by the garbage collector.

To allow the collector to follow pointer chains, there must be some information about the heap objects that are being traversed. In particular, it must be known where there are pointers in the object, if any. All objects of the same type share a common type descriptor, which is created when its defining module is loaded.
All dynamically allocated Component Pascal records contain a hidden field, which is called the type tag. The type tag points to the type descriptor that contains all type information needed at run-time. Method calls, type tests and the garbage collector all use the information which is stored in the type descriptor. The type descriptor is allocated only once for every record type in the system.
Dynamically allocated arrays use a size descriptor to check the bounds of indexes. The size descriptor also contains the type tag of the element type. For every single dynamic array a size descriptor is needed.

The additional storage required to "tag" objects makes their memory layout different from untagged records provided by the operating system or some procedural shared library. Type tags are critical; they must not be damaged, otherwise the garbage collector will crash the system at some point in time.


4 Component Pascal compiler options

The Component Pascal compiler supports the following options for compiling a module. Generally, it is recommended to use the default settings (no options) but there may be exceptions. 
	+	Switch on all runtime range checks. This generates slightly larger code files and increases the runtime. In most situations the difference is small. For the default behavior see Runtime range checks below.
	-	Switch off emitting the source code position. This generates slightly more compact code files but removes the link to the terminated procedure's source code position in the trap viewer.
	--	In addition to "-" switch off emitting symbolic information for structured global variables. 
	---	In addition to "--" switch off emitting symbolic information for all global variables.
	----	In addition to "---" switch off code file generation.
	!	Switch off runtime checking of ASSERT statements. Use this option only for very time critical code.
	!!	In addition to "!" switch off array index checks and pointer initialization. Use this option at your own risk.
	?	Switch on emitting of (COM related) warnings or hints even if there are no errors.
	@	Switch on trapping on first error.
	$	Switch on syntax extensions for compatibility with Oberon.
	(...)	DevSelector options.


5 Runtime range checks

The Component Pascal Language Report does not specify the out-of-range behaviour of the built in arithmetic and set functions, but deliberately allows the compiler writer freedom to make implementation decisions. Some of the current default decisions with this compiler are documented below, primarily because they can lead to unexpected behaviour. These behaviours are not considered to be bugs, and are considered to be within the freedom permitted by the Language Report. Developers are advised to exploit these behaviours with care as:
- other compilers may behave differently
- this compiler may behave differently in the future
- code that uses them may be hard for another reader to understand.

INTEGER arithmetic (+, -, *, DIV, INC, DEC) is not range checked, and the low order 32 bits of the result are returned. This is equivalent to reducing the result MOD 2^32, to a number in the range [-2^31 : 2^31).

LONGINT arithmetic (+, -, *, DIV) is range checked, and overflows lead to a run-time "undefined real result" exception. The explanation for this behaviour is that INTEGER arithmetic uses integer instructions on the main CPU, whereas LONGINT arithmetic uses floating point instructions on the co-processor. INC and DEC are not range-checked, and the low order 64 bits of the result are returned. This is equivalent to reducing the result MOD 2^64, to a number in the range [-2^63 : 2^63).

SHORT(x) is without range checking. In case of x: REAL an overflow leads to infinity. In case of x being any integer type, it returns the low order half of the argument.

ENTIER(x) is range checked. Overflows lead to a run-time "undefined real result" exception.

SHORT(ENTIER(x)) and SHORT(SHORT(ENTIER(x))) are range checked with respect to the outermost integer type. Overflows lead to a run-time "undefined real result" exception. A third SHORT leading to BYTE is range checked with respect to INTEGER.
Note carefully that using a compound expression in case of out-of-range values is not functionally equivalent to assigning the intermediate LONGINT value to a variable.
Explanation: Using the floating point unit to perform the compound type transfer in one operation is more efficient than performing the two operations sequentially. The SHORT function is defined as the identity operation, which is clearly only possible for in-range values. It is not defined for out-of-range values.

REAL arithmetic (+, -, *, /) is not range checked but overflows lead to infinity (INF) and underflows lead to zero (0). Internally, all REAL operations are done using the extended 80 bits precision format of the IA32 floating point unit, then converted to 64 bit precision when assigned to a REAL variable or when floating point registers are saved on the stack prior to a function call. This design choice generally reduces arithmetic rounding errors. It does mean that breaking a compound expression into parts may not lead to exactly equivalent results. Constant expression evaluation in the compiler uses 64 bits only because it needs to store intermediate values to memory.
Example:
x := Math.Pi(); ASSERT(x = Math.Pi(), 30)
leads to a TRAP, and so does
ASSERT(Math.Pi() = Math.Pi(), 30).
In this case the first call is rounded to 64 bits, the second is left at 80.

The SET expression e IN s does not check if e is within {0..31}. The resulting behavior depends on s being in a register or in memory. In case of a register e MOD 32 IN s is returned. In case of memory, s is treated as an ARRAY of SET and e MOD 32 IN s[e DIV 32] is returned, so out-of-range values for e attempt to read memory outside the actual memory allocated for s. This leads to an "illegal memory read" exception if the resulting address is not mapped. If the resulting address is mapped, it leads to a result which is very random.

INCL(s, e) and EXCL(s, e) use e MOD 32 in order not to violate memory integrity.


6 Stack Size

The reserved stack size of a linked executable is 2MB of which 64KB are committed immediately. The uncommitted stack space grows on demand up to 2MB where a stack overflow trap would be reported. These settings are hard-coded in the module DevLinker and are also used for the BlackBox.exe file in the standard BlackBox distribution. They are a good choice for most applications and it is best not to change them.

For rare cases, however, it might be necessary to configure a larger stack size. This can be accomplished by adapting the source code of DevLinker. The value marked as stack reserve size in DevLinker.WriteHeader defines the reserved stack size when linking a new exe file but there is a subtle dependency on the heap size reserved in WinKernel.AllocClusterMem (see constant monoClusterSize). The total size of the reserved heap plus the committed stack must not exceed some limit or otherwise Windows API functions may run out of memory. A good test for a working setup is to commit all the stack (i.e. set the value marked as stack commit size to the same value as stack reserve size), link a new exe file, start it, open a file dialog box with the menu item File->Open..., and see if it shows any anomalies. Examples are black squares instead of icons, missing tree view, or a TRAP 100 in Dialog.GetIntSpec with res = 2 indicating an out-of-memory error. The File->Open test should be repeated at least ten times. Experiments showed that for a stack reserve/commit size of 32MB the heap must be reduced to 150MB.






Windows Specific



10 Command line parameters of BlackBox

The command line parameters of BlackBox start with the letter '/' or alternatively the letter '-' immediately followed by a case insensitive parameter name. Depending on the parameter, further parameter values may be required. Each parameter value is separated by white space. If a parameter value contains spaces, it must be enclosed in single or double quotes.

The parameters are:

/USE path
	Use path for lookup of relative file paths prior to looking them up in the startup or custom directory.
	If path is relative it refers to the current directory.
/CUSTOM path
	Use path for lookup of relative file paths prior to looking them up in the startup directory.
/PAR parameters
	Specify parameters available via Dialog.commandLinePars.
/INIFILE iniFile
	Do not use the Windows registry but use the specified Windows .ini file instead. 
	The path to the iniFile may be relative or absolute.
/PORTABLE
	Short form of /INIFILE where the relative path Dialog.appName followed by ".ini" is used for the .ini file.
/O file
	Open the specified file upon startup.
	Any parameter value not belonging to a parameter name will also be opened, i.e. /O can be omitted.
/P file
	Open and print the specified file.
/LOAD module
	Load the specified module upon startup.
/EMBEDDING
	Start as server.
/NOAPPWIN
	Start without an application window.
/NOSCROLL
	Do not show scroll bars in the application window.
/FULLSIZE
	Start in fullscreen mode.
/LTRB left top right bottom
	Specify the window position of the next window to be opened with /O.
/LANG language
	Run BlackBox localized to the specified two letter language code.




11 Using DLLs in BlackBox modules

Any 32-bit DLL can be imported in Component Pascal like a normal Component Pascal module. This holds for Windows system modules (kernel, user, gdi, ...) as well as for any custom DLL written in any programming language. Be aware that the safety qualities of Component Pascal (no dangling pointers, strict type-checking, etc.) are lost if you interface to a DLL written in another programming language.

Interface modules

Type information about objects imported from a DLL must be present in a symbol file for the compiler to work properly. Such special symbol files can be generated through so-called interface modules. An interface module is a Component Pascal module marked by a system flag after the module name. The system flag consists of the name of the corresponding DLL enclosed in square brackets. An interface module can only contain declarations of constants, types, and procedures headings. No code file is generated when an interface module is compiled.

Name aliasing

The Component Pascal name of an object imported from a DLL can be different from the corresponding name in the export table of the DLL. To achieve this the DLL name is appended to the Component Pascal name as a system flag. In addition, the system flag may specify a different DLL than the one declared in the module header. This allows to use a single interface module for a whole set of related DLLs. It is also possible to have multiple interface modules referring to the same DLL. Interface procedures cannot have a body, nor an "END identifier" part, they are only signatures.

The extend syntax for interface modules is given below:

Module 	=	MODULE ident ["[" SysString "]"] ";"
		[ImportList] DeclSeq [BEGIN StatementSeq] END ident ".".
ProcDecl	=	PROCEDURE ["[" SysFlag "]"] [Receiver]
		IdentDef ["[" [SysString ","] SysString "]"] [FormalPars] ";"
		DeclSeq [[BEGIN StatementSeq] END ident].
SysString	=	string.

There is no aliasing for types and constants, because they are not present in export lists and thus may have arbitrary names.
The following example summarizes the aliasing capabilities:

MODULE MyInterface ["MyDll"];
	PROCEDURE Proc1*;				(* Proc1 from MyDll *)
	PROCEDURE Proc2* ["BlaBla"];				(* BlaBla from MyDll *)
	PROCEDURE Proc3* ["OtherDll", ""];				(* Proc3 from OtherDll *)
	PROCEDURE Proc4* ["OtherDll", "DoIt"];				(* DoIt from OtherDll *)
END MyInterface.

A SysString for a DLL may not contain an entire path name. It should be just the name of the DLL, without the ".dll" suffix. The following search strategy is used:

- BlackBox directory
- Windows\System directory
- Windows directory

Data types in interface modules

Always use untagged records as replacements for C structures, in order to avoid the allocation of a type tag for the garbage collector. The system flag [untagged] marks a type as untagged (no type information is available at run-time) with standard alignment rules for record fields (2-byte fields are aligned to 2-byte boundaries, 4-byte or larger fields to 4-byte boundaries). The system flags [noalign], [align2], and [align8] also identify untagged types but with different alignments for record fields.
Like all system flags, "untagged" can only be used if module SYSTEM is imported.

Example:

	RECORD [noalign]		(* untagged, size = 7 bytes *)
		c: SHORTCHAR;	(* offset 0, size = 1 byte *)
		x: INTEGER;	(* offset 1 , size = 4 bytes *)
		i: SHORTINT	(* offset 5, size = 2 bytes *)
	END

Procedures

Component Pascal procedure calls conform to the StdCall calling convention (parameters pushed from right to left, removed by called procedure). If the CCall convention (parameters removed by caller) is needed for some DLL procedures, the corresponding procedure declaration in the interface module must be decorated with the [ccall] system flag.
No special handling is required for callback procedures.

For parameters of type POINTER TO T it is often better to use a variable parameter of type T rather than to declare a corresponding pointer type. Declare the VAR parameter with system flag [nil] if NIL must be accepted as legal actual parameter.

Example:

C:	
	BOOL MoveToEx(HDC hdc, int X, int Y, LPPOINT lpPoint)

Component Pascal:
	PROCEDURE MoveToEx* (dc: Handle; x, y: INTEGER; VAR [nil] old: Point): Bool

Correspondence between Component Pascal and C data types

unsigned char	= SHORTCHAR	(1 byte)
WCHAR	= CHAR	(2 bytes)
signed char	= BYTE	(1 byte)
short	= SHORTINT	(2 bytes)
int	= INTEGER	(4 bytes)
long	= INTEGER	(4 bytes)
LARGE_INTEGER	= LONGINT	(8 bytes)
float	= SHORTREAL	(4 bytes)
double	= REAL	(8 bytes)

Note that Bool is not a data type in C but is defined as int (= INTEGER). 0 and 1 must be used for assignments of FALSE and TRUE and comparisons with 0 have to be used in conditional statements (IF b # 0 THEN ... END instead of IF b THEN ... END).

Note that it is not possible to unload a DLL from within BlackBox. To avoid having to exit and restart your development environment repeatedly, it is a good idea to test the DLL that you are developing from within another instance of the BlackBox application.


12 Using COM without Direct-To-COM compiler

Microsoft's Component Object Model (COM) is supported by a Direct-To-COM compiler. This compiler makes using COM safer and more convenient. However, for casual use of COM, the approach described in this chapter can be used. It uses normal untagged records and procedure variables to create COM-style method tables ("vtbl") for objects. The following example shows how it works:

MODULE Ddraw ["DDRAW.DLL"];

TYPE
  GUID = ARRAY 4 OF INTEGER;

  PtrIUnknown = POINTER TO RECORD [untagged]
    vtbl: POINTER TO RECORD [untagged]
      QueryInterface: PROCEDURE (this: PtrIUnknown; IN iid: GUID; OUT obj: PtrIUnknown): INTEGER;
      AddRef: PROCEDURE (this: PtrIUnknown): INTEGER;
      Release: PROCEDURE (this: PtrIUnknown): INTEGER;
    END
  END;

  PtrDirectDraw = POINTER TO RECORD [untagged]
    vtbl: POINTER TO RECORD [untagged]
      QueryInterface: PROCEDURE (this: PtrDirectDraw; IN iid: GUID; OUT obj: PtrIUnknown): INTEGER;
      AddRef: PROCEDURE (this: PtrDirectDraw): INTEGER;
      Release: PROCEDURE (this: PtrDirectDraw): INTEGER;
      Compact: PROCEDURE (this: PtrDirectDraw): INTEGER;
      ...
      SetCooperativeLevel: PROCEDURE (this: PtrDirectDraw; w, x: INTEGER): INTEGER;
      ...
    END
  END;

PROCEDURE DirectDrawCreate* (IN guid: GUID; OUT PDD: PtrDirectDraw; outer: PtrIUnknown) : INTEGER;

END Ddraw.


MODULE Directone;

IMPORT Out, Ddraw, SYSTEM;

CONST
  DDSCL_EXCLUSIVE = 00000010H;
  DDSCL_FULLSCREEN = 00000001H;

PROCEDURE Initialize;
VAR 
  Handle, Addr, Res: INTEGER;
  PDD: Ddraw.PtrDirectDraw;
  nul: Ddraw.GUID;
BEGIN
  PDD := NIL;
  nul[0] := 0; nul[1] := 0; nul[2] := 0; nul[3] := 0;
  Res := Ddraw.DirectDrawCreate(nul, PDD, NIL);
  Out.String("Res");  Out.Int(Res, 8);  Out.Ln();
  Res := SYSTEM.VAL(INTEGER, PDD);
  Out.String("Res");  Out.Int(Res, 8);  Out.Ln();
  Res := PDD.vtbl.SetCooperativeLevel(PDD, 0, DDSCL_EXCLUSIVE + DDSCL_FULLSCREEN);
  Out.String("Res");  Out.Int(Res, 8);  Out.Ln();
  Res := PDD.vtbl.Release(PDD)
END Initialize;

BEGIN
  Initialize
END Directone.


Some important points:
• COM GUIDs are 128 bit entities, not integers.
• DO NOT use ANYPTR or other BlackBox pointers for COM interface pointers. (BlackBox pointers are garbage collected, COM pointers are reference counted.)
• Use pointers to [untagged] records or integers instead.

Be careful to declare all methods in the method table in the correct order with the correct parameter list.



13 Windows-specific information in BlackBox

Windows -specific cursors

The module WinPorts exports constants for Windows-specific cursors which can be used like the standard cursors defined in module Ports:

	CONST
		resizeHCursor = 16;	(* cursors used for window resizing *)
		resizeVCursor = 17;
		resizeLCursor = 18;
		resizeRCursor = 19;
		resizeCursor = 20;
		busyCursor = 21;	(* busy cursor *)
		stopCursor = 22;	(* drag and drop cursors *)
		moveCursor = 23;
		copyCursor = 24;
		linkCursor = 25;
		pickCursor = 26;	(* drag and pick cursor *)
		
		
Specific mouse and keyboard modifiers

Modifier sets are used in Controllers.TrackMsg, Controllers.EditMsg, and Ports.Frame.Input. The BlackBox platform independant modifier elements doubleClick, extend, modify, popup, and pick are defined in module Controllers. Additional Windows platform-specific modifier elements are defined in module WinPorts. The correspondence between these elements is shown in the table below. If the WinPorts element is included it implies that the corresponding Controllers element will also be included.

	WinPorts	WinPorts	Controllers	Controllers	Mouse button
	element	value	element	value	or key
	--	--	doubleClick	0	double click
	left	16	--	--	left mouse button
	middle	17	pick	4	middle mouse button
	right	18	popup	3	right mouse button
	shift	24	extend	1	Shift key
	ctrl	25	modify	2	Ctrl key
	alt	28	pick	4	Alt key

Window and device context handles

Many of the functions in the Windows API refer either to a window or to a device context handle. In the Windows BlackBox implementation, both of them are stored in the WinPorts.Port record:

	TYPE
		Port = POINTER TO RECORD (Ports.PortDesc)
			dc: WinApi.Handle;
			wnd: WinApi.Handle
		END;
	
In the usual case where a frame (Ports.Frame or Views.Frame) is given, the handles can be obtained through one of the following selectors:

		frame.rider(WinPorts.Rider).port.dc
		or
		frame.rider(WinPorts.Rider).port.wnd

If the window handle is null, the port is a printer port.
		
Examples

The following simple DLL definition is a subset of the Windows standard library GDI32:

MODULE GDI ["GDI32"];

	CONST
		WhiteBrush* = 0; BlackBrush* = 4; NullBrush* = 5;
		WhitePen* = 6; BlackPen* = 7; NullPen* = 8;
		PSSolid* = 0; PSDash* = 1; PSDot* = 2;

	TYPE
		Bool* = INTEGER;
		Handle* = INTEGER;
		ColorRef* = INTEGER;
		Point* = RECORD [untagged] x*, y*: INTEGER END;
		Rect* = RECORD [untagged] left*, top*, right*, bottom*: INTEGER END;

	PROCEDURE CreatePen* (style, width: INTEGER; color: ColorRef): Handle;
	PROCEDURE CreateSolidBrush* (color: ColorRef): Handle;
	PROCEDURE GetStockObject* (object: INTEGER): Handle;
	PROCEDURE SelectObject* (dc, obj: Handle): Handle;
	PROCEDURE DeleteObject* (obj: Handle): Bool;
	PROCEDURE Rectangle* (dc: Handle; left, top, right, bottom: INTEGER): Bool;
	PROCEDURE SelectClipRgn* (dc, rgn: Handle): INTEGER;
	PROCEDURE IntersectClipRect* (dc: Handle; left, top, right, bottom: INTEGER): INTEGER;
	PROCEDURE SaveDC* (dc: Handle): INTEGER;
	PROCEDURE RestoreDC* (dc: Handle; saved: INTEGER): Bool;

END GDI.

The following example is a simplified version of the BlackBox standard DrawRect routines implemented in Ports and WinPorts. It uses the GDI interface presented above.

MODULE Ex;

	IMPORT Ports, WinPorts, GDI;

	PROCEDURE DrawRect (f: Ports.Frame; l, t, r, b, s: INTEGER; col: Ports.Color);
		VAR res, h, rl, rt, rr, rb: INTEGER; p: WinPorts.Port; dc, oldb, oldp: GDI.Handle;
	BEGIN
		(* change local universal coordinates to window pixel coordinates *)
		l := (f.gx + l) DIV f.unit; t := (f.gy + t) DIV f.unit;
		r := (f.gx + r) DIV f.unit; b := (f.gy + b) DIV f.unit;
		s := s DIV f.unit;
		(* get device context *)
		p := f.rider(WinPorts.Rider).port; dc := p.dc;
		(* set clip region *)
		IF p.wnd = 0 THEN res := GDI.SaveDC(dc)
		ELSE res := GDI.SelectClipRgn(dc, 0)
		END;
		f.rider.GetRect(rl, rt, rr, rb);
		res := GDI.IntersectClipRect(dc, rl, rt, rr, rb);
		(* use black as default color *)
		IF col = Ports.defaultColor THEN col := Ports.black END;
		IF s = 0 THEN s := 1 END;
		IF (s < 0) OR (r-l < 2*s) OR (b-t < 2*s) THEN	(* filled rectangle *)
			INC(r); INC(b);
			oldb := GDI.SelectObject(dc, GDI.CreateSolidBrush(col));
			oldp := GDI.SelectObject(dc, GDI.GetStockObject(GDI.NullPen));
			res := GDI.Rectangle(dc, l, t, r, b);
			res := GDI.DeleteObject(GDI.SelectObject(dc, oldb));
			res := GDI.SelectObject(dc, oldp)
		ELSE	(* outline rectangle *)
			h := s DIV 2; INC(l, h); INC(t, h); h := (s-1) DIV 2; DEC(r, h); DEC(b, h);
			oldb := GDI.SelectObject(dc, GDI.GetStockObject(GDI.NullBrush));
			oldp := GDI.SelectObject(dc, GDI.CreatePen(GDI.PSSolid, s, col));
			res := GDI.Rectangle(dc, l, t, r, b);
			res := GDI.SelectObject(dc, oldb);
			res := GDI.DeleteObject(GDI.SelectObject(dc, oldp))
		END;
		IF p.wnd = 0 THEN res := GDI.RestoreDC(dc, -1) END
	END DrawRect;

END Ex.


14 Runtime type system

All dynamically allocated Component Pascal records contain a hidden field, which is called the type tag. The type tag points to the type descriptor that contains all type information needed at runtime. Calls of type-bound procedures, type tests and the garbage collector all use the information which is stored in the type descriptor. The type descriptor is allocated only once for every record type in the system.
Dynamically allocated arrays use a size descriptor to check the bounds of indexes. The size descriptor also contains the type tag of the element type. For every single dynamic array a size descriptor is needed.


15 Linking BlackBox applications

If you want to distribute an application you have written in BlackBox, you may want to link all modules into a single file for distribution. In this case you need to link the framework to your application. To illustrate the necessary actions we will give an example.

Adapt the module StdConfig to your needs:


Then link the framework with your modules into the new application file.
Blue modules are platform-specific realizations for the set of abstract interfaces.
WinPackedFiles module should be linked if you packing some resoureces to the executable file after linking of application.

DevLinker.Link BlackBoxGameDemo.exe :=
Kernel$+ Utf WinKernel Files WinEnv WinFiles WinPackedFiles WinGui StdLoader
Math Strings Dates Meta Fonts Log Librarian Dialog
Services Ports Stores Converters Sequencers Models Printers Views
Controllers Properties Printing Mechanisms Containers
Documents Windows StdCFrames Controls
StdRegistry StdInterpreter
WinRegistry WinFonts WinDates
TextModels TextRulers TextSetters TextViews
StdScrollers StdDialog StdApi StdCmds
WinPorts OleData WinMechanisms WinPrinters WinDialog WinClipboard  WinCFrames WinCmds
TextControllers TextMappers TextCmds
StdLinks StdMenuTool StdScrollbars StdRasters
StdDocuments StdWindows StdMenus StdGrids StdLog StdTiles
FormModels FormViews FormControllers FormGen FormCmds
StdFolds StdDebug StdTextConv StdClocks StdStamps StdCoder StdStdCFrames 
OleWindows OleStorage 
Loop Console In Out StdInflate StdRastersPng
WinConsole  WinBackends WinShowHook WinRasters WinConfig WinInit
StdConfig
ObxBlackBox
1 Applogo.ico 2 Doclogo.ico 3 SFLogo.ico 4 CFLogo.ico 5 DtyLogo.ico
1 Move.cur 2 Copy.cur 3 Link.cur 4 Pick.cur 5 Stop.cur 6 Hand.cur 7 Table.cur

You may need to distribute some additional files such as forms and documetation with application or they can be packed into executable file.

DevPacker.PackThis BlackBoxGameDemo.exe :=
Obx/Rsrc/BlackBox.odc
Obx/Docu/BB-Rules.odc
System/Rsrc/Strings.odc


Do not forget to restore code file for regular StdConfig
DevCompiler.CompileThis StdConfig   (* the second commander here to terminate reading *)




Linux Specific Information


21 Linux Environment parameters of BlackBox

The command line parameters of BlackBox start with the letter '/' or alternatively the letter '-' immediately followed by a case insensitive parameter name. Depending on the parameter, further parameter values may be required. Each parameter value is separated by white space. If a parameter value contains spaces, it must be enclosed in single or double quotes.

The parameters are:

BB_STANDARD_DIR=path
	Use pat for "bottom" directory to lookup for files.
BB_USE_DIR=path
	Use path for lookup of relative file paths prior to looking them up in the startup or custom directory.
	If path is relative it refers to the current directory.
BB_CUSTOM_DIR=path
	Use path for lookup of relative file paths prior to looking them up in the startup directory.
PAR parameters
	Specify parameters available via Dialog.commandLinePars.
BB_PACKED_NAME
	Name of file for LinPackedFiles module to read packed files from it.
BB_PACKED_FIRST
	Set priority for reading files first from the packed file.
LANG language
	Run BlackBox localized to the specified two letter language code.


22 Endless loop

Endless loop

Send SIGILL signal to BlackBox to terminate endless loop.
SS


25 Linking BlackBox applications

If you want to distribute an application you have written in BlackBox, you may want to link all modules into a single file for distribution. In this case you need to link the framework to your application. To illustrate the necessary actions we will give an example.

Adapt the module StdConfig to your needs:



Then link the framework with your modules into the new application file.
Blue modules are platform-specific realizations for the set of abstract interfaces.
LinPackedFiles module should be linked if you packing some resoureces to the executable file after linking of application.

DevLinker1.LinkElf Linux BlackBoxGameDemo :=
Kernel$+ Utf LinKernel Files LinEnv LinFiles LinPackedFiles LinGui StdLoader 
Math Strings Dates Meta Fonts Log Librarian Dialog 
Services Ports Stores Converters Sequencers Models Printers Views 
Controllers Properties Printing Mechanisms Containers 
Documents Windows StdCFrames Controls 
StdRegistry StdInterpreter 
LinRegistry LinFonts LinDates
TextModels TextRulers TextSetters TextViews 
StdScrollers StdDialog StdApi StdCmds 
LinPorts LinMechanisms LinDialog LinClipboard
TextControllers TextMappers TextCmds 
StdLinks StdMenuTool StdScrollbars StdRasters
StdDocuments StdWindows StdMenus StdGrids StdLog StdTiles 
FormModels FormViews FormControllers FormGen FormCmds
StdFolds StdDebug StdTextConv StdClocks StdStamps StdCoder StdStdCFrames 
Loop Console In Out 
LinConsole  LinBackends LinRastersPng LinConfig LinInit
StdFilesBrowser StdConfig
ObxBlackBox 

You may need to distribute some additional files such as forms and documetation with application or they can be packed into executable file.

DevPacker.PackThis BlackBoxGameDemo :=
Obx/Rsrc/BlackBox.odc
Obx/Docu/BB-Rules.odc
System/Rsrc/Strings.odc



Do not forget to restore code file for regular StdConfig
DevCompiler.CompileThis StdConfig   (* the second commander here to terminate reading *)




Direct-To-COM Compiler

Introduction

COM

Microsoft's Component Object Model (COM) is a language and compiler independent interface standard for object interoperability on the binary level. COM specifies how objects implemented in one component can access services provided by objects implemented in another component. The services provided by objects are grouped in interfaces, which are sets of semantically related functions. For example, an ActiveX control is a COM object that extends OLE, by implementing concrete implementations of the necessary OLE interfaces. COM makes it possible that an extended software component can be updated without invalidating extending components, e.g., a new release of OLE can be introduced without breaking existing ActiveX controls.

In such an evolving, decentrally constructed system it is difficult to decide whether an object is still used or whether it can be removed from the system. The client of an object does not know whether other clients still have access to the same object, and a server does not know whether a client passed its reference to other clients. This lack of global knowledge can make it hard to determine whether an object is no longer referenced, and thus could be released. If an object is released prematurely, then unpredictable errors will occur. The problem is critical in component software environments, because an erroneous component of one vendor may destroy objects implemented by other vendors' components. It may even take some time until the destroyed objects begin to visibly malfunction. For the end user, it is next to impossible to determine which vendor is to blame.

As a consequence, some form of automatic garbage collection mechanism must be provided. In contrast to closed systems, this is a necessity with component software, and not merely an option or a convenience feature. COM uses reference counting as a rather simple form of garbage collection. Whenever a client gets access to a COM object, it must inform the object that a new reference exists. As soon as the client releases the COM object, it must inform the object that a reference will disappear. A COM object counts its references and thus can control its own lifetime.

Direct-To-COM Compiler

The problem with reference counting is that it depends on the discipline of the programmer. The situation is aggravated by additional subtle rules about who is responsible for the management of the reference counts if COM objects are passed as function arguments. Reliable software construction requires the management of reference counts to be automated. 

The Direct-To-COM compiler for Component Pascal completely automates memory management. The compiler and its small run-time system (module Kernel) hides the reference counting mechanism, i.e., a program need not call nor implement the AddRef and Release methods.

For programmers, the integration of COM into a strongly typed, garbage-collected language has two major advantages. Firstly, it brings all the amenities of automatic garbage collection to COM. Secondly, it makes the handling of COM interfaces typesafe.

Getting Started

How should you start to get acquainted with this product? Note that all documentation is available on-line, and can be reached from the help screen. After the Installation of the BlackBox Component Builder, we suggest that you start with the introduction text A Brief History of Pascal.

Then move on to the chapters of the user guide which deal with text editing and with the development tools (Text Subsystem and Dev Subsystem). After you thus have gained a working knowledge of BlackBox, it is time to delve into the COM-specific parts of the IDE and the Component Pascal compiler extensions for COM.

The section How to Develop new COM objects (p. 20) summarizes the main points of the user guide as far as it pertains to COM development. Then proceed with the various example implementations of COM objects, which demonstrate all basic aspects of COM programming. Knowledge of the Windows APIs (e.g. OLE) is assumed.

Further Reading

We recommend the following books on COM, OLE and ActiveX:

	Kraig Brockschmidt,
	Inside OLE,
	2nd Edition, Microsoft Press, 1995.
		
	David Chappell,
	Understanding ActiveX and OLE,
	Microsoft Press, 1996, ISBN 1-57231-216-5.

	Adam Denning,
	OLE Controls Inside Out,
	Microsoft Press, 1995.
	
	Dale Rogerson,
	Inside COM,
	Microsoft Press, 1997, ISBN 1-57231-349-8,
	
	Don Box,
	Creating Components with DCOM and C++,
	Addison Wesley, 1997.
	
	Microsoft
	COM Home Page, 
	http://www.microsoft.com/com
	
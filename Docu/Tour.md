
BlackBox Component Builder
Guided Tour

October 2021


This text is a quick introduction to the BlackBox Component Builder. Read it and follow the small hands-on examples to get a feeling for the way how BlackBox works.

Overview over the BlackBox Component Builder

The BlackBox Component Builder is an integrated development environment optimized for component-based software development. It consists of development tools, a library of reusable components, a framework that simplifies the development of robust custom components and applications, and a run-time environment for components.
In BlackBox, the development of applications and their components is done in Component Pascal. This language is a descendant of Pascal, Modula-2, and Oberon. It provides modern features such as objects, full type safety, components (in the form of modules), dynamic linking of components, and garbage collection. The entire BlackBox Component Builder is written in Component Pascal: all library components, all development tools including the Component Pascal compiler, and even the low-level run-time system with it's garbage collector. In spite of it's power, Component Pascal is a small language that is easy to learn and easy to teach.
The component library that comes with BlackBox contains components for user interface elements such as command buttons or checkboxes; various components that provide word processing functionality (Text subsystem); various components that provide layout management functionality for graphical user interfaces (Form subsystem); database access components (Sql subsystem); communication components (Comm subsystem); and a number of development tool components such as compiler, interface browser, debugger, and so on (Dev subsystem).
Component interactions are governed by the BlackBox Component Builder's Frameworks. These consist of a number of complementary programming interfaces. These interfaces are much simpler and safer, and platform-independent moreover, than basic APIs, such as the Windows APIs. For interactive applications, they define a quite unique compound document architecture. This architecture enables rapid application development (RAD), including the rapid development of new user interface components. The framework design strongly emphasizes robust component interaction. This is important for large-scale software projects that involve components from different sources and evolution of the software over long periods of time. To combine the productivity of a RAD environment with a high degree of architectural robustness was a major design goal for the BlackBox Component Builder. It was attempted to create an environment that is light-weight and flexible, yet doesn't sacrifice robustness and long-term maintainability of software produced with it. This was made possible by an architecture that decomposes the system into components with well-defined interfaces. Software is evolved incrementally, by adding, updating, or removing entire components.
The BlackBox run-time environment supports dynamic linking and loading (and unloading) of components. In this way, a system can be extended at run-time, without recompiling, relinking, or restarting existing code. Component objects (i.e., instances of classes contained in components) are automatically removed when they are not referenced anymore. This garbage collection service is a crucial safety feature of the run-time system, since it allows to prevent errors like memory leaks and dangling pointers, which are almost impossible to avoid in a heavily component-oriented system like BlackBox.


Views

Now let's have a look at some standard BlackBox components. Views are the most interesting objects implemented by BlackBox components; they can be embedded into documents or other views. Views can be edited and resized in place. This tour text contains several embedded views. Here is a first one: a picture view without editing capabilities.

	
	
	A picture view as an example of a component object embedded in a compound document.

Other examples of views are controls such as command buttons, checkboxes, alarm indicators, oil level meters, and so on. More complex views can implement full-fledged editors such as spreadsheets or graphics editors. The most complex views in BlackBox are container views, i.e., views that may contain other views. Text views are an important example of BlackBox container views. You are now looking at such a text view. Further below, there is an embedded text view containing a small program. The following sections demonstrate how simple programs can be written and tested, and how a graphical user interface can be constructed.


Software development

The source code below is a fully editable text, showing the complete implementation of a small Component Pascal module. To compile the module, focus the embedded view by clicking into it (e.g., click on the keyword PROCEDURE), and then execute Compile from menu Dev. As a result, the module is compiled into fast native machine which is written to disk (the file Obx/Code/Hello0).
	

ObxHello0 is a minimal "hello world" program in Component Pascal. It writes a single line to the system log text. Execute Open Log from menu Info to display the system log, if it is not open already.
Exported items in Component Pascal modules are marked by a trailing asterisk; there are no separate header files, definition modules, or the like. Consistency of interfaces and implementations is fully checked by the compiler; version integrity is checked by the dynamic linker.
Module ObxHello0 exports a single command Do. Commands are exported Component Pascal procedures that can be called by the user; i.e., they can be executed directly from the user interface. There is no need for a central "main" procedure or top-level module. A command can be added to a menu, attached to a button, or executed directly from within a text. Select the string "ObxHello0.Do" below, and then execute command Execute in menu Dev:

	ObxHello0.Do

When the compiler finds syntax errors, it flags them directly in the text. For example, the following module version erroneously imports the (nonexisting) module StdLok, instead of StdLog. Try to compile the module – the compiler inserts special embedded objects (error markers) flagging the errors that it found. The compiler also writes a report to the system log.

	

By clicking on an error marker, a short error message is displayed in the status bar. Correct the mistake (replace the "k" in IMPORT StdLok by a "g"), and compile again. The marker disappears, and the module is compiled successfully.
The set of currently loaded modules can be inspected by clicking on the Loaded Modules command in the Info menu. The interfaces of modules (loaded or not) can be displayed using the interface browser: select a module name and then execute Client Interface from menu Info. For example, you may find out the interface of the following module:
	 Math
A module remains loaded until it is explicitly unloaded, or until the BlackBox Component Builder is restarted. To explicitly unload a module, select the module name and execute Unload Module List from menu Dev. For example, unload ObxHello0, modify the string "Hello world", recompile ObxHello0, and execute ObxHello0.Do again. Note that your changes do not affect the running system until after you have unloaded the old module. Such an explicit unloading is a very useful mechanism to allow major changes in multiple modules, while still using and working with the previous version. For simple top-level modules, (modules that are not imported by other modules), the command Compile And Unload provides a convenient shortcut.


Linking programs to form documents

Besides the text and development subsystems, the BlackBox Component Builder also comes with a form subsystem, which includes a visual user interface designer. Forms can be data entry masks or dialog boxes.

The following module defines a simple record variable to be used for a data entry form.

	
After compiling the module, a dialog box can be created for the items exported by ObxAddress1 using command New Form... from menu Controls. Just enter the name ObxAddress1 into the Link field, and then click on the OK button. The type information extracted by the compiler is available to the BlackBox Component Builder at run-time, and is used to automatically create a data-entry form for the record declaration above. The form has a simple default layout. This default layout may be edited, and then opened as a dialog using the Open as Aux Dialog command in menu Controls.
The text entry fields and the checkbox of the form are directly linked to the fields name, city, country, customer  and update of the record ObxAddress1.adr. The button is linked to the command OpenText, i.e., to the procedure exported by module ObxAddress1. Clicking the button causes procedure OpenText to be called. As a result, a new text is created; a textual report based on the variable adr is written to this text; a new text view is created; and the view is opened in a window, displaying the report.
Text entry fields, checkboxes, and other so-called controls may have properties that could be inspected and modified by a suitable control property inspector. Instead of first writing a module and then creating an initial layout, as we have done above, the form can be constructed first, and the corresponding module written later. A BlackBox Component Builder dialog does not necessarily correspond to exactly one record variable. The individual controls of a dialog box may be linked to records in different modules, and a dialog box may also contain other views which are not controls, such as pictures.
A form can be saved from within the visual editor; thereafter it can be attached to a menu entry, or another dialog's button. Dialog boxes are saved in the standard document format, in a platform-independent way. This approach eliminates the need for an intermediate source code generator and allows to later modify the dialog boxes without having to recompile anything.


And more ...

After this first impression, you may want to consult your documentation for an in-depth coverage of the BlackBox Component Builder. Select the Contents item in the Help menu for an overview over the documentation. From there, the complete on-line documentation can be reached via hyperlinks.
How should you start to get acquainted with BlackBox? We suggest that you start with the introduction texts A Brief History of Pascal and Roadmap.
The documentation consists of four major parts:
•A user manual that describes the user interface and most important commands of the BlackBox Component Builder
•A tutorial that first introduces the general BlackBox design patterns (chapters 1 to 3). Graphical user interfaces, forms, and controls are discussed in chapter 4. The text subsystem is explained in chapter 5. The remaining chapter 6 deals with view programming.
•Overview by Example is a rich source of examples, ordered by category and difficulty.
•The programmer's reference consists of one documentation file per module. Each subsystem has a Sys-Map text which contains links to the individual texts.

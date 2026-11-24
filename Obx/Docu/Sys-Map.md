Map to "Overview by Example"

Text Commands
ObxHello0  docu/sources		write "Hello World" to log
ObxHello1  docu/sources		write "Hello World" to new text
ObxOpen0  docu/sources		open text document from a file
ObxOpen1  docu/sources		modify a text document on a file
ObxCaps  docu/sources		change string to uppercase
ObxDb  docu/sources		manage sorted list of records
ObxTabs  docu	ObxTabs  sources	transform some tabs into blanks
ObxMMerge  docu	ObxMMerge  sources	mail merge of template and database
ObxParCmd  docu	ObxParCmd  sources	interpret text which follows a command button
ObxLinks  docu	ObxLinks  sources	create a directory text with hyperlinks
ObxAscii  docu	ObxAscii  sources	traditional text file I/O

Form Commands
ObxAddress0  docu/sources		declare interactor for address dialog
ObxAddress1  docu/sources		write address to new text document
ObxAddress2  docu/sources		append address to existing text document
ObxOrders  docu	ObxOrders  sources	simple order processing example
ObxControls  docu	ObxControls  sources	guards and notifiers
ObxDialog  docu	ObxDialog  sources	selection and combo boxes
ObxUnitConv  docu	ObxUnitConv  sources 	dialog box that converts between inches and points
ObxFileTree  docu	ObxFileTree  sources	using a tree control to list files and directories

Other Commands
ObxTrap  docu	ObxTrap  sources	trap demo
ObxConv  docu	ObxConv  sources	file converter
ObxActions  docu	ObxActions  sources	actions as background tasks
ObxFact  docu	ObxFact  sources	factorials calculated with arbitrary precision integers
ObxPi  docu	ObxPi  sources	pi calculated with arbitrary precision integers
ObxRatCalc  docu	ObxRatCalc  sources	rational number calculator

Simple Views
ObxPatterns  docu	ObxPatterns  sources	view with rectangular pattern
ObxCalc  docu	ObxCalc  sources	calculator
ObxOmosi  docu	ObxOmosi  sources	editor for 3-D structures
ObxCubes  docu	ObxCubes  sources	actions as animation engines (rotating cube)
ObxTickers  docu	ObxTickers  sources	actions as animation engines (stock ticker)
ObxScroll  docu	ObxScroll  sources	demonstration of scrolling mechanism

Control Views
ObxButtons  docu	ObxButtons  sources	how to implement your own controls
not available	ObxCtrls	slider control
not available	ObxFldCtrls	text field control with special behavior

Editor Views
ObxLines  docu	ObxLines  sources	view which allows to draw lines
ObxGraphs  docu	ObxGraphs  sources	bar chart which interprets dropped text
ObxBlackBox  docu	ObxBlackBox  sources	deduction game

Wrapper Views
ObxWrappers  docu	ObxWrappers  sources	sample wrapper view

Special Container Views
ObxTwins  docu	ObxTwins  sources	two vertically arranged text views
ObxTabViews  docu	ObxTabViews  sources 	tabbed views

General Container Views
Form subsystem  map		complete sources of form subsystem
ObxContIter  docu	ObxContIter  sources	set focus in a particular control in some container

Other Examples
not available	ObxStores	example of how to use Stores as graph nodes

Communication Examples (part of the Comm subsystem)
CommObxStreamsServer		implements a simple server using CommStreams
docu	sources

CommObxStreamsClient		implements a simple client using CommStreams
docu	sources

Examples used in the Tutorial
ObxSample, ObxPhoneDB, ObxPhoneUI, ObxPhoneUI1, ObxControlShifter, ObxLabelLister,
ObxPDBRep0, ObxPDBRep1, ObxPDBRep2, ObxPDBRep3, ObxPDBRep 4,
ObxCount0, ObxCount1, ObxLookup0, ObxLookup1,
ObxViews0, ObxViews1, ObxViews2, ObxViews3, ObxViews4, ObxViews5, ObxViews6,
ObxViews10, ObxViews11, ObxViews12, ObxViews13, ObxViews14.


Tasks, Monitors and Channels
ObxTasks0  docu/sources		two tasks share a counter without a monitor
ObxTasks1  docu/sources		waiting: Park and Unpark, Sleep, Join
ObxTasks2  docu/sources		a server and a hundred clients on a module monitor
ObxTasks3  docu/sources		a task group: a failing child cancels the others
ObxTasks4  docu/sources		deadlock detection by the scheduler
ObxChan0  docu/sources		rendezvous: a channel that holds no messages
ObxChan1  docu/sources		a bounded buffer lets the producer run ahead
ObxChan2  docu/sources		a mailbox: sending never waits
ObxSelect0  docu/sources		selection with a timeout
ObxProto0  docu/sources		a dialogue protocol checked at every send
ObxOnce  docu/sources		a single-assignment variable
ObxStm0  docu/sources		a transaction: a trap in a transfer loses no money
ObxStm1  docu/sources		Retry: waiting for a condition over two variables
ObxStm2  docu/sources		OrElse: pay with one card or the other
ObxWork0  docu/sources		a blocking call on a worker thread
ObxComputon  docu/sources		a computon executed in a separate process
ObxActor0  docu/sources		the first actor: Spawn, Tell, Ask, Shutdown
ObxActor1  docu/sources		supervision: a trap, and a restart loses the state
ObxActor2  docu/sources		a bank: Resume keeps the account balances
ObxActor3  docu/sources		an account with a file journal survives a restart
ObxActor4  docu/sources		snapshots and an audit history
ObxActor5  docu/sources		CQRS: a turnover report catches up with the journal
ObxBank0  docu/sources		tutorial: an action and a task doing the same work
ObxBank1  docu/sources		tutorial: clerks, Join, Sleep, cancellation, a group
ObxBank2  docu/sources		tutorial: waiting for money without a monitor and with AWAIT
ObxBank3  docu/sources		tutorial: deadlock on the monitors of two accounts
ObxBank4  docu/sources		tutorial: transfers as transactions, Retry, OrElse
ObxBank5  docu/sources		tutorial: a cashier owns the accounts, requests via a mailbox
ObxBank6  docu/sources		tutorial: selecting a request, closing or a timeout; a promise

Content-Addressed Store
ObxCas0  docu/sources		a block named by its hash: deduplication
ObxCas1  docu/sources		cutting two nearly equal inputs by content
ObxCas2  docu/sources		two authors publish change sets, one rebases

Key-Value Store
ObxKv0  docu/sources		Kv store: open, write, read, walk a range
ObxKv1  docu/sources		an event log: append, tail, a reader's snapshot
ObxKv2  docu/sources		a transfer between accounts in one transaction and Abort

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

Tasks with a pass and monitor objects
ObxBufCtx  docu/sources		a buffer on the monitor object: pass, operation, flags and a condition
ObxBankCtx  docu/sources		a transfer by a repeated entry into a monitor and with two monitors in hand
ObxDownCtx  docu/sources		a parent, children and a deadline on the monitor object
ObxDownChan  docu/sources		the same download on a channel with a pass
ObxPipeCtx  docu/sources		a pipeline of three stages on channels with a pass
ObxRestart  docu/sources		one task started many times, without allocations

Tasks, monitors and channels
ObxTasks0  docu/sources		two tasks share a counter without a monitor
ObxTasks1  docu/sources		waiting: Park and Unpark, Sleep, Join
ObxTasks2  docu/sources		a server and one hundred clients on a module monitor
ObxTasks3  docu/sources		a task group: a child's failure cancels the others
ObxTasks4  docu/sources		deadlock: the scheduler prints who waits for what
ObxChan0  docu/sources		rendezvous: a channel without message storage
ObxChan1  docu/sources		a bounded buffer lets the producer run ahead
ObxChan2  docu/sources		mailbox: sending does not wait
ObxSelect0  docu/sources		selection with a timeout
ObxProto0  docu/sources		dialogue protocol, a check on every send
ObxOnce  docu/sources		a once variable
ObxStm0  docu/sources		transaction: a trap in a transfer loses no money
ObxStm1  docu/sources		Retry: waiting for a condition over two variables
ObxStm2  docu/sources		OrElse: payment from one card or the other
ObxWork0  docu/sources		a blocking call in a worker thread
ObxComputon  docu/sources		a computon in a separate process
ObxActor0  docu/sources		first actor: Spawn, Tell, Ask, Shutdown
ObxActor1  docu/sources		supervision: a trap and a restart lose state
ObxActor2  docu/sources		bank: Resume keeps account balances
ObxActor3  docu/sources		an account with a Kv journal survives a restart
ObxActor4  docu/sources		snapshots and change history (audit)
ObxActor5  docu/sources		CQRS: the turnover report catches up with the journal
ObxActor6  docu/sources		two nodes: Ask through a node, a forged address, loss of a node
ObxActor7  docu/sources		saga: a transfer between nodes survives the coordinator's crash
ObxActor8  docu/sources		virtual account: by number, migration on node failure
ObxRaft0  docu/sources		Raft: three in-memory nodes, elections, write, read, leader failure
ObxRaft1  docu/sources		a durable counter on the Raft journal survives its node's failure
ObxBank0  docu/sources		tutorial: an action and a task do one job
ObxBank1  docu/sources		tutorial: cashiers, Join, Sleep, cancellation, group
ObxBank2  docu/sources		tutorial: waiting for money without a monitor and with a monitor object
ObxBank3  docu/sources		tutorial: deadlock on the monitors of two accounts
ObxBank4  docu/sources		tutorial: transfers as transactions, Retry, OrElse
ObxBank5  docu/sources		tutorial: a cashier owns the accounts, requests through a mailbox
ObxBank6  docu/sources		tutorial: selection from a request, a closure and a timeout; a promise

Storage by content
ObxCas0  docu/sources		a block is named by its hash: deduplication
ObxCas1  docu/sources		slicing two nearly identical inputs by content
ObxCas2  docu/sources		two authors publish change sets, one moves its own

Key-value storage
ObxKv0  docu/sources		the Kv store: open, write, read, walk a range
ObxKv1  docu/sources		event journal: append, tail, reader snapshot
ObxKv2  docu/sources		a transfer between accounts in one transaction and the Abort refusal

Map of the Demo subsystem examples
Demo  subsystem  map		translations of well-known concurrency examples onto tasks and channels (Demo/Docu/Sys-Map)


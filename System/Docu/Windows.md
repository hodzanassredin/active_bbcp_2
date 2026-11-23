Windows

DEFINITION Windows;

	IMPORT Sequencers, Files, Ports, Views, Models, Documents, Converters;

	CONST
		allowDuplicates = 5;
		eager = FALSE;
		isAux = 1;
		isTool = 0;
		lazy = TRUE;
		neverDirty = 6;
		noHScroll = 2;
		noResize = 4;
		noVScroll = 3;

	TYPE
		Directory = POINTER TO ABSTRACT RECORD 
			l, t, r, b: INTEGER;
			minimized, maximized: BOOLEAN;
			(d: Directory) Close (w: Window), NEW, ABSTRACT;
			(d: Directory) First (): Window, NEW, ABSTRACT;
			(d: Directory) Focus (target: BOOLEAN): Window, NEW, ABSTRACT;
			(d: Directory) GetBounds (OUT w, h: INTEGER), NEW, ABSTRACT;
			(d: Directory) GetThisWindow (p: Ports.Port; px, py: INTEGER;
				OUT x, y: INTEGER; OUT w: Window), NEW, ABSTRACT;
			(d: Directory) New (): Window, NEW, ABSTRACT;
			(d: Directory) NewSequencer (): Sequencers.Sequencer, NEW;
			(d: Directory) Next (w: Window): Window, NEW, ABSTRACT;
			(d: Directory) Open (w: Window; doc: Documents.Document; flags: SET;
											name: Views.Title; loc: Files.Locator; fname: Files.Name;
											conv: Converters.Converter), NEW, EXTENSIBLE;
			(d: Directory) OpenSubWindow (w: Window; doc: Documents.Document;
											flags: SET; name: Views.Title), NEW, EXTENSIBLE;
			(d: Directory) Select (w: Window; lazy: BOOLEAN), NEW, ABSTRACT;
			(d: Directory) Update (w: Window), NEW
		END;

		Window = POINTER TO ABSTRACT RECORD 
			port: Ports.Port;
			frame-: Views.RootFrame;
			doc-: Documents.Document;
			seq-: Sequencers.Sequencer;
			link-: Window;
			sub-: BOOLEAN;
			flags-: SET;
			loc-: Files.Locator;
			name-: Files.Name;
			conv-: Converters.Converter;
			(w: Window) BroadcastModelMsg (VAR msg: Models.Message), NEW, EXTENSIBLE;
			(w: Window) BroadcastViewMsg (VAR msg: Views.Message), NEW, EXTENSIBLE;
			(w: Window) Close, NEW, EXTENSIBLE;
			(w: Window) ForwardCtrlMsg (VAR msg: Views.CtrlMessage), NEW, EXTENSIBLE;
			(w: Window) GetSize (OUT width, height: INTEGER), NEW, EXTENSIBLE;
			(w: Window) GetTitle (OUT title: Views.Title), NEW, ABSTRACT;
			(w: Window) Init (port: Ports.Port), NEW, EXTENSIBLE;
			(w: Window) KeyDown (ch: CHAR; modifiers: SET), NEW, EXTENSIBLE;
			(w: Window) MouseDown (x, y, time: INTEGER; modifiers: SET), NEW, ABSTRACT;
			(w: Window) RefreshTitle, NEW, ABSTRACT;
			(w: Window) Restore (l, t, r, b: INTEGER), NEW;
			(w: Window) SetSize (width, height: INTEGER), NEW, EXTENSIBLE;
			(w: Window) SetSpec (loc: Files.Locator; name: Files.Name;
											conv: Converters.Converter), NEW, EXTENSIBLE;
			(w: Window) SetTitle (title: Views.Title), NEW, ABSTRACT;
			(w: Window) Update, NEW
		END;

	VAR
		dir-: Directory;

	PROCEDURE Init;
	
	PROCEDURE GetBySpec (loc: Files.Locator; name: Files.Name;
											conv: Converters.Converter; flags: SET): Window;
	PROCEDURE SelectBySpec (loc: Files.Locator; name: Files.Name;
											conv: Converters.Converter; flags: SET; VAR done: BOOLEAN);
	PROCEDURE SelectByTitle (v: Views.View; flags: SET; title: Views.Title; VAR done: BOOLEAN);
	
	PROCEDURE SetupWindow (w: Window; frame: Views.RootFrame;
											doc: Documents.Document; seq: Sequencers.Sequencer; flags: SET);

END Windows.



CONST 
lazy = TRUE
window will be validated normally (at the end of current command)

eager = FALSE
window is validated immediately (which causes frame tree to be built). Now Select does not have lazy argument; rather, for the frame tree to be built immediately, call Validate explicitly. 


TYPE Directory
ABSTRACT
Directory for the lookup in and manipulation with windows

l, t, r, b: INTEGER;
The bounding box for the next window to be opened; a request, not a guarantee

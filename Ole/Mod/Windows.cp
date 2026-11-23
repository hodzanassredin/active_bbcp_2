MODULE OleWindows;

	IMPORT Views, Controllers, Windows, Documents, WinApi;
	
	TYPE
		Window* = POINTER TO ABSTRACT RECORD 
			wnd-: WinApi.HANDLE;
			doc-: Documents.Document;
			win-: Windows.Window;
			back: Backend;
		END;
		Backend* = POINTER TO ABSTRACT RECORD END;
		Hook* = POINTER TO ABSTRACT RECORD END;
		Directory* = POINTER TO ABSTRACT RECORD END;
	
	PROCEDURE ActivateWindow* (w: Window; on: BOOLEAN);
	(* <= WinWindows.ActivateWindow *)
	(* how is it different from Windows.Directory.Select? *)
	END ActivateWindow;
	
	PROCEDURE (w: Window) DoClose- (do: PROCEDURE (w: Window)), NEW, ABSTRACT;
	(** To be extended in OleServer *)
	
	PROCEDURE (b: Backend) DoClose-, NEW, ABSTRACT;
	(** To be extended by a backend (MDI or tiled) *)
	
	PROCEDURE DoClose (w: Window); BEGIN w.back.DoClose END DoClose;
	
	PROCEDURE Close* (w: Window); 
	BEGIN ASSERT(w # NIL, 20);
		w.DoClose(DoClose)
	END Close;
	
	PROCEDURE (w: Window) ForwardCtrlMsg* (VAR msg: Controllers.Message), NEW, ABSTRACT;
	PROCEDURE (w: Window) BroadcastViewMsg* (VAR msg: Views.Message), NEW, ABSTRACT;

	PROCEDURE Open* (v: Views.View; OUT w: Window; inPlace: BOOLEAN);
	BEGIN HALT(126)
	END Open;
	
	PROCEDURE Unit* (): INTEGER; BEGIN HALT(126); RETURN 0 END Unit;
	
END OleWindows.
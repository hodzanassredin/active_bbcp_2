MODULE StdInterpreter;
(**
	project	= "BlackBox 2.0"
	organization	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Oberon microsystems, A. Dmitriev, K8"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)
	
	IMPORT Kernel, Meta, Strings, Views, Dialog;

	TYPE
		IntValue = POINTER TO RECORD (Meta.Value) int: INTEGER; END;
		StrValue = POINTER TO RECORD (Meta.Value) str: Dialog.String; END;
		BoolValue = POINTER TO RECORD (Meta.Value) bool: BOOLEAN; END;
		(* additional values for type conversions *)
		BIntValue = POINTER TO RECORD (Meta.Value) bint: BYTE; END;
		SIntValue = POINTER TO RECORD (Meta.Value) sint: SHORTINT; END;
		LIntValue = POINTER TO RECORD (Meta.Value) lint: LONGINT; END;
		RealValue = POINTER TO RECORD (Meta.Value) rval: REAL; END;
		SRealValue = POINTER TO RECORD (Meta.Value) srval: SHORTREAL; END;
		
		CallHook = POINTER TO RECORD (Dialog.CallHook) END;

		
	PROCEDURE (hook: CallHook) Call (IN proc, errMsg: ARRAY OF CHAR; VAR res: INTEGER);
		TYPE Ident = Kernel.Name;
		CONST
			modNotFound = 10; procNotFound = 11; identExpected = 12; unknownIdent = 13;
			depositExpected = 14; noDepositExpected = 15; syntaxError = 16;
			lparenExpected = 17; rparenExpected = 18; containerExpected = 19;
			quoteExpected = 20; fileNotFound = 21; noController = 22;
			noDialog = 23; cannotUnload = 24; commaExpected = 25;
			incompParList = 26; stringTooLong = 27;
		CONST
			ident = 0; dot = 1; semicolon = 2; eot = 3;
			lparen = 4; rparen = 5; quote = 6; comma = 7; int = 8; real = 9;
		VAR
			i, type: INTEGER; ch: CHAR; id: Ident; x: INTEGER; r: REAL; qch: CHAR;
			par: ARRAY 100 OF POINTER TO Meta.Value; numPar: INTEGER;

		PROCEDURE Error (n: INTEGER; msg, par0, par1: ARRAY OF CHAR);
			VAR e, f: ARRAY 256 OF CHAR;
		BEGIN
			IF res = 0 THEN
				res := n;
				IF errMsg # "" THEN
					Dialog.MapString(errMsg, e);
					IF e = " " THEN Dialog.MapString("#System:CommandError", e) END;
					Dialog.MapParamString(msg, par0, par1, "", f);
					Dialog.ShowMsg(e + " " + f)
				END
			END
		END Error;
		
		PROCEDURE Init (VAR s: ARRAY OF CHAR);
			VAR i: INTEGER;
		BEGIN
			i := 0; WHILE i < LEN(s) DO s[i] := 0X; INC(i) END
		END Init;
		
		PROCEDURE ShowLoaderResult (IN mod: ARRAY OF CHAR);
			VAR res: INTEGER; importing, imported, object: ARRAY 256 OF CHAR;
		BEGIN
			Kernel.GetLoaderResult(res, importing, imported, object);
			CASE res OF
			| Kernel.fileNotFound:
				Error(Kernel.fileNotFound, "#System:CodeFileNotFound", imported, "")
			| Kernel.syntaxError:
				Error(Kernel.syntaxError, "#System:CorruptedCodeFileFor", imported, "")
			| Kernel.objNotFound:
				Error(Kernel.objNotFound, "#System:ObjNotFoundImpFrom", imported, importing)
			| Kernel.illegalFPrint:
				Error(Kernel.illegalFPrint, "#System:ObjInconsImpFrom", imported, importing)
			| Kernel.cyclicImport:
				Error(Kernel.cyclicImport, "#System:CyclicImpFrom", imported, importing)
			| Kernel.noMem:
				Error(Kernel.noMem, "#System:NotEnoughMemoryFor", imported, "")
			ELSE
				Error(res, "#System:CannotLoadModule", mod, "")
			END
		END ShowLoaderResult;

		(* convert ints to other integer types *)
		PROCEDURE FixParams (VAR i: Meta.Item; VAR args: ARRAY OF POINTER TO Meta.Value): BOOLEAN;
		VAR
			res: BOOLEAN;
			f, len: INTEGER;
			t: Meta.Item;
			par, npar: POINTER TO Meta.Value;
			bi: BIntValue;
			si: SIntValue;
			li: LIntValue;
			sr: SRealValue;
			rv: RealValue;
			smr, rrv: SHORTREAL;
		BEGIN
			res := TRUE; len := i.NumParam();
			f := 0; WHILE res & (f < len) DO
				i.GetParam(f, t);
				ASSERT(t.typ # Meta.undef, 100);
				par := args[f];
				IF par = NIL THEN res := FALSE
				ELSE npar := par;
					WITH
					| par: IntValue DO
						CASE t.typ OF
						| Meta.byteTyp:
							IF (par.int >= MIN(BYTE)) & (par.int <= MAX(BYTE)) THEN
								NEW(bi); bi.bint := SHORT(SHORT(par.int)); npar := bi
							ELSE npar := NIL
							END
						| Meta.sIntTyp:
							IF (par.int >= MIN(SHORTINT)) & (par.int <= MAX(SHORTINT)) THEN
								NEW(si); si.sint := SHORT(par.int); npar := si
							ELSE npar := NIL
							END
						| Meta.intTyp:	(* ok *)
						| Meta.longTyp:
							NEW(li); li.lint := LONG(par.int); npar := li
						| Meta.realTyp:	(* REAL can represent any INTEGER *)
							NEW(rv); rv.rval := par.int; npar := rv;
						| Meta.sRealTyp:
							smr := par.int; rrv := par.int;
							IF LONG(smr) # rrv THEN npar := NIL
							ELSE NEW(sr); sr.srval := smr; npar := sr
							END
						ELSE npar := NIL
						END
					| par: RealValue DO
						IF t.typ # Meta.realTyp THEN
							IF t.typ = Meta.sRealTyp THEN
								smr := SHORT(par.rval);
								IF LONG(smr) # par.rval THEN npar := NIL
								ELSE NEW(sr); sr.srval := smr; npar := sr
								END
							ELSE npar := NIL
							END
						END
					ELSE END;
					res := (npar # NIL);
					IF res THEN args[f] := npar END
				END;
				INC(f)
			END;
			RETURN res
		END FixParams;

		PROCEDURE CallProc (IN mod, proc: ARRAY OF CHAR);
			VAR i, t: Meta.Item; ok: BOOLEAN;
		BEGIN
			ok := FALSE;
			Meta.Lookup(mod, i);
			IF i.obj = Meta.modObj THEN
				i.Lookup(proc, i);
				IF i.obj = Meta.procObj THEN
					i.GetReturnType(t);
					IF (t.typ = 0) & (i.NumParam() = numPar) & FixParams(i, par) THEN
						i.ParamCallVal(par, t, ok)
					ELSE ok := FALSE
					END;
					IF ~ok THEN
						Error(incompParList, "#System:IncompatibleParList", mod, proc)
					END
				ELSE
					Error(Kernel.commNotFound, "#System:CommandNotFoundIn", proc, mod)
				END
			ELSE
				ShowLoaderResult(mod)
			END
		END CallProc;

		PROCEDURE GetCh;
		BEGIN
			IF i < LEN(proc) THEN ch := proc[i]; INC(i) ELSE ch := 0X END
		END GetCh;

		PROCEDURE Scan;
			VAR j: INTEGER; num: ARRAY 32 OF CHAR; ires: INTEGER;
		BEGIN
			IF res = 0 THEN
				WHILE (ch # 0X) & (ch <= " ") DO GetCh END;
				IF ch = 0X THEN
					type := eot
				ELSIF ch = "." THEN
					type := dot; GetCh
				ELSIF ch = ";" THEN
					type := semicolon; GetCh
				ELSIF ch = "(" THEN
					type := lparen; GetCh
				ELSIF ch = ")" THEN
					type := rparen; GetCh
				ELSIF (ch = "'") OR (ch = '"') OR (ch = "`") THEN
					qch := ch; type := quote; GetCh
				ELSIF ch = "," THEN
					type := comma; GetCh
				ELSIF (ch >= "0") & (ch <= "9") OR (ch = "-") OR (ch = "+") THEN
					type := int; j := 0;
					REPEAT
						num[j] := ch; INC(j); GetCh
					UNTIL (ch < "0") OR (ch > "9") & (ch < "A") OR (ch > "F");
					CASE ch OF
					| 'H':
						num[j] := ch; INC(j); GetCh
					| '.':
						type := real;
						REPEAT num[j] := ch; INC(j); GetCh UNTIL (ch < "0") OR (ch > "9")
					| '%':
						REPEAT num[j] := ch; INC(j); GetCh UNTIL (ch < "0") OR (ch > "9")
					ELSE
					END;
					num[j] := 0X;
					IF type = real THEN Strings.StringToReal(num, r, ires)
					ELSE Strings.StringToInt(num, x, ires)
					END;
					IF ires # 0 THEN Error(syntaxError, "#System:SyntaxError", "", "") END
				ELSIF Strings.IsIdentStart(ch) THEN
					type := ident;
					id[0] := ch; j := 1; GetCh;
					WHILE (ch # 0X) & (i < LEN(proc)) & Strings.IsIdent(ch) DO
						id[j] := ch; INC(j); GetCh
					END;
					id[j] := 0X
				ELSE Error(syntaxError, "#System:SyntaxError", "", "")
				END
			END
		END Scan;
		
		PROCEDURE String (VAR s: ARRAY OF CHAR);
			VAR j: INTEGER; s0: Dialog.String;
		BEGIN
			IF type = quote THEN
				ASSERT(qch # 0X, 100);
				j := 0;
				WHILE (ch # 0X) & (ch # qch) & (j < LEN(s) - 1) DO
					s[j] := ch; INC(j); GetCh
				END;
				s[j] := 0X;
				IF j >= LEN(s) -1 THEN
					Strings.IntToString(LEN(Dialog.String), s0);
					Error(stringTooLong, "#System:StringTooLong", s0, s)
				ELSIF ch = qch THEN
					GetCh; Scan
				ELSE Error(quoteExpected, "#System:QuoteExpected", "", "")
				END
			ELSE Error(quoteExpected, "#System:QuoteExpected", "", "")
			END
		END String;

		PROCEDURE ParamList ();
			VAR iv: IntValue; sv: StrValue; bv: BoolValue; rv: RealValue;
		BEGIN
			numPar := 0;
			IF type = lparen THEN Scan;
				WHILE (numPar < LEN(par)) & (type # rparen) & (res = 0) DO
					IF type = quote THEN
						NEW(sv);
						String(sv.str);
						par[numPar] := sv;
						INC(numPar)
					ELSIF type = int THEN
						NEW(iv);
						iv.int := x; Scan;
						par[numPar] := iv;
						INC(numPar)
					ELSIF type = real THEN
						NEW(rv);
						rv.rval := r; Scan;
						par[numPar] := rv;
						INC(numPar)
					ELSIF (type = ident) & ((id = "TRUE") OR (id = "FALSE")) THEN
						NEW(bv);
						bv.bool := (id = "TRUE"); Scan;
						par[numPar] := bv;
						INC(numPar)
					ELSE Error(syntaxError, "#System:SyntaxError", "", "")
					END;
					IF type = comma THEN Scan
					ELSIF type # rparen THEN
						Error(rparenExpected, "#System:RParenExpected", "", "")
					END
				END;
				Scan
			END
		END ParamList;

		PROCEDURE Command;
			VAR left, right: Ident;
		BEGIN
			(* protect from parasitic anchors on stack *)
			Init(left); Init(right);
			left := id; Scan;
			IF type = dot THEN	(* Oberon command *)
				Scan;
				IF type = ident THEN
					right := id; Scan; ParamList();
					IF res = 0 THEN CallProc(left, right) END
				ELSE Error(identExpected, "#System:IdentExpected", "", "")
				END
			ELSE Error(unknownIdent, "#System:UnknownIdent", id, "")
			END
		END Command;

	BEGIN
		(* protect from parasitic anchors on stack *)
		i := 0; type := 0; qch := 0X; Init(id); x := 0;
		Views.ClearQueue;
		res := 0; i := 0; GetCh;
		Scan;
		IF type = ident THEN
			Command; WHILE (type = semicolon) & (res = 0) DO Scan; Command END;
			IF type # eot THEN Error(syntaxError, "#System:SyntaxError", "", "") END
		ELSE Error(syntaxError, "#System:SyntaxError", "", "")
		END;
		IF (res = 0) & (Views.Available() > 0) THEN
			Error(noDepositExpected, "#System:NoDepositExpected", "", "")
		END;
		Views.ClearQueue
	END Call;
	
	PROCEDURE Init;
		VAR hook: CallHook;
	BEGIN
		NEW(hook); Dialog.SetCallHook(hook)
	END Init;

BEGIN
	Init
END StdInterpreter.

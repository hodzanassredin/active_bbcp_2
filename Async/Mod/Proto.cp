MODULE AsyncProto;
(** Протокол диалога канала по образцу dialogs языка Zonnon.

	Необязательный тип протокола у канала: грамматика допустимых
	последовательностей сообщений задана автоматом состояний, который
	строится из текстового описания. Проверяется вид записи-сообщения
	(Kernel.GetTypeName от динамического типа Msg), поэтому описание
	называет типы сообщений, например:

		start: RequestMsg > wait; wait: ResponseMsg > wait;
		wait: EndMsg > done; done:

	Пункт «состояние:» без стрелки объявляет состояние; состояние без
	исходящих переходов — конечное (диалог завершён). Разделитель
	пунктов — ";" или перевод строки, стрелка — ">" или "->".
	Автомат детерминированный: два перехода из одного состояния по
	одному имени типа — ошибка описания. Имя длиннее nameLen - 1 знаков
	и пункт без ":" — тоже ошибки описания.

	Проверка включается только прикреплением протокола к каналу
	(AsyncChan.SetProtocol); без протокола канал Step не зовёт, накладных
	расходов нет. Нарушение — трап отправителя с текстом в консоль;
	текст остаётся доступен через LastViolation(p, msg). *)

	CONST
		maxStates = 32;
		maxEdges = 128;
		nameLen = 32;
		msgLen = 256;

	TYPE
		Message* = ARRAY msgLen OF CHAR;

		Edge = RECORD
			from, to: INTEGER;
			label: ARRAY nameLen OF CHAR
		END;

		Protocol* = POINTER TO RECORD
			nstates, nedges, start: INTEGER;
			states: ARRAY maxStates OF ARRAY nameLen OF CHAR;
			edges: ARRAY maxEdges OF Edge;
			violation: Message
		END;

	VAR
		parseMsg: Message;

	PROCEDURE StrEqual (IN a, b: ARRAY OF CHAR): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (a[i] # 0X) & (b[i] # 0X) & (a[i] = b[i]) DO INC(i) END;
		RETURN a[i] = b[i]
	END StrEqual;

	PROCEDURE StrCopy (IN src: ARRAY OF CHAR; VAR dst: ARRAY OF CHAR);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(dst) - 1) & (src[i] # 0X) DO dst[i] := src[i]; INC(i) END;
		dst[i] := 0X
	END StrCopy;

	PROCEDURE Append (IN src: ARRAY OF CHAR; VAR dst: ARRAY OF CHAR);
		VAR i, j: INTEGER;
	BEGIN
		i := 0; WHILE (i < LEN(dst)) & (dst[i] # 0X) DO INC(i) END;
		j := 0;
		WHILE (i < LEN(dst) - 1) & (src[j] # 0X) DO dst[i] := src[j]; INC(i); INC(j) END;
		dst[i] := 0X
	END Append;

	PROCEDURE IsBlank (ch: CHAR): BOOLEAN;
	BEGIN
		RETURN (ch = " ") OR (ch = 09X)
	END IsBlank;

	PROCEDURE IsBreak (ch: CHAR): BOOLEAN;
	BEGIN
		RETURN (ch = 0X) OR (ch = ";") OR (ch = 0DX) OR (ch = 0AX)
	END IsBreak;

	PROCEDURE SkipBlanks (IN d: ARRAY OF CHAR; VAR i: INTEGER);
	BEGIN
		WHILE IsBlank(d[i]) DO INC(i) END
	END SkipBlanks;

	PROCEDURE SkipBreaks (IN d: ARRAY OF CHAR; VAR i: INTEGER);
	BEGIN
		WHILE IsBlank(d[i]) OR (d[i] = ";") OR (d[i] = 0DX) OR (d[i] = 0AX) DO INC(i) END
	END SkipBreaks;

	PROCEDURE ReadName (IN d: ARRAY OF CHAR; VAR i: INTEGER; stop: CHAR; OUT name: ARRAY OF CHAR): BOOLEAN;
	(* Имя до stop, конца пункта (IsBreak) или, при stop = ">", до "-".
	   Пост: FALSE — имя не помещается в name (parseMsg задан). *)
		VAR j: INTEGER; long: BOOLEAN;
	BEGIN
		SkipBlanks(d, i); j := 0; long := FALSE;
		WHILE ~IsBreak(d[i]) & (d[i] # stop) & ~((stop = ">") & (d[i] = "-")) DO
			IF j < LEN(name) - 1 THEN name[j] := d[i]; INC(j) ELSE long := TRUE END;
			INC(i)
		END;
		WHILE (j > 0) & IsBlank(name[j-1]) DO DEC(j) END;
		name[j] := 0X;
		IF long THEN parseMsg := "name too long: "; Append(name, parseMsg) END;
		RETURN ~long
	END ReadName;

	PROCEDURE FindState (p: Protocol; IN name: ARRAY OF CHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO p.nstates - 1 DO
			IF StrEqual(p.states[i], name) THEN RETURN i END
		END;
		RETURN -1
	END FindState;

	PROCEDURE FindOrAdd (p: Protocol; IN name: ARRAY OF CHAR; OUT idx: INTEGER): BOOLEAN;
	BEGIN
		idx := FindState(p, name);
		IF idx >= 0 THEN RETURN TRUE END;
		IF p.nstates >= maxStates THEN RETURN FALSE END;
		idx := p.nstates; INC(p.nstates);
		StrCopy(name, p.states[idx]);
		RETURN TRUE
	END FindOrAdd;

	PROCEDURE HasEdge (p: Protocol; from: INTEGER; IN label: ARRAY OF CHAR): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO p.nedges - 1 DO
			IF (p.edges[i].from = from) & StrEqual(p.edges[i].label, label) THEN RETURN TRUE END
		END;
		RETURN FALSE
	END HasEdge;

	PROCEDURE AddEdge (p: Protocol; from: INTEGER; IN label: ARRAY OF CHAR; to: INTEGER): BOOLEAN;
	BEGIN
		IF label = "" THEN parseMsg := "empty message name"; RETURN FALSE END;
		IF p.nedges >= maxEdges THEN parseMsg := "too many transitions"; RETURN FALSE END;
		IF HasEdge(p, from, label) THEN
			parseMsg := "duplicate transition for message "; Append(label, parseMsg);
			RETURN FALSE
		END;
		p.edges[p.nedges].from := from;
		p.edges[p.nedges].to := to;
		StrCopy(label, p.edges[p.nedges].label);
		INC(p.nedges);
		RETURN TRUE
	END AddEdge;

	PROCEDURE Parse (p: Protocol; IN d: ARRAY OF CHAR): BOOLEAN;
		VAR i, src, tgt: INTEGER; sname, lname, tname: ARRAY nameLen OF CHAR; ok: BOOLEAN;
	BEGIN
		i := 0; ok := TRUE; parseMsg := "";
		LOOP
			SkipBreaks(d, i);
			IF (d[i] = 0X) OR ~ok THEN EXIT END;
			IF ~ReadName(d, i, ":", sname) THEN ok := FALSE
			ELSIF sname = "" THEN parseMsg := "expected a state name"; ok := FALSE
			ELSIF d[i] # ":" THEN parseMsg := "expected ':' after state name"; ok := FALSE
			ELSE
				INC(i);
				IF ~FindOrAdd(p, sname, src) THEN parseMsg := "too many states"; ok := FALSE
				ELSE
					IF p.start < 0 THEN p.start := src END;
					SkipBlanks(d, i);
					IF ~IsBreak(d[i]) THEN
						IF ~ReadName(d, i, ">", lname) THEN ok := FALSE
						ELSE
							IF d[i] = "-" THEN INC(i) END;
							IF d[i] # ">" THEN parseMsg := "expected '>' or '->'"; ok := FALSE
							ELSE
								INC(i);
								IF ~ReadName(d, i, ";", tname) THEN ok := FALSE
								ELSIF tname = "" THEN parseMsg := "expected a target state"; ok := FALSE
								ELSIF ~FindOrAdd(p, tname, tgt) THEN parseMsg := "too many states"; ok := FALSE
								ELSE ok := AddEdge(p, src, lname, tgt)
								END
							END
						END
					END;
					IF ok & (d[i] = ";") THEN INC(i) END
				END
			END
		END;
		IF ok & (p.start < 0) THEN parseMsg := "empty description"; ok := FALSE END;
		RETURN ok
	END Parse;

	PROCEDURE New* (IN desc: ARRAY OF CHAR): Protocol;
	(** Построить автомат из описания; NIL — ошибка (текст в LastError). *)
		VAR p: Protocol;
	BEGIN
		NEW(p); p.nstates := 0; p.nedges := 0; p.start := -1; p.violation := "";
		IF ~Parse(p, desc) THEN RETURN NIL END;
		RETURN p
	END New;

	PROCEDURE LastError* (OUT msg: Message);
	(** Текст последней ошибки разбора описания. *)
	BEGIN
		msg := parseMsg
	END LastError;

	PROCEDURE Start* (p: Protocol): INTEGER;
	(** Начальное состояние (индекс). *)
	BEGIN
		IF p = NIL THEN RETURN 0 END;
		RETURN p.start
	END Start;

	PROCEDURE StateName* (p: Protocol; state: INTEGER; OUT name: ARRAY OF CHAR);
	(** Имя состояния по индексу; "" — индекса нет. *)
	BEGIN
		IF (p # NIL) & (state >= 0) & (state < p.nstates) THEN StrCopy(p.states[state], name)
		ELSE name := ""
		END
	END StateName;

	PROCEDURE IsFinal* (p: Protocol; state: INTEGER): BOOLEAN;
	(** Пред: p # NIL (трап 20). Пост: в состоянии нет исходящих переходов. *)
		VAR i: INTEGER;
	BEGIN
		ASSERT(p # NIL, 20);
		FOR i := 0 TO p.nedges - 1 DO
			IF p.edges[i].from = state THEN RETURN FALSE END
		END;
		RETURN TRUE
	END IsFinal;

	PROCEDURE Expected (p: Protocol; state: INTEGER; VAR s: ARRAY OF CHAR);
		VAR i: INTEGER; first: BOOLEAN;
	BEGIN
		s := ""; first := TRUE;
		FOR i := 0 TO p.nedges - 1 DO
			IF p.edges[i].from = state THEN
				IF ~first THEN Append(", ", s) END;
				Append(p.edges[i].label, s); first := FALSE
			END
		END;
		IF first THEN Append("(final state, no messages allowed)", s) END
	END Expected;

	PROCEDURE Step* (p: Protocol; VAR state: INTEGER; IN label: ARRAY OF CHAR; OUT ok: BOOLEAN);
	(** Проверить очередное сообщение и перейти в следующее состояние. *)
		VAR i: INTEGER; sname: ARRAY nameLen OF CHAR; exp: ARRAY msgLen OF CHAR;
	BEGIN
		ok := FALSE;
		IF p = NIL THEN ok := TRUE; RETURN END;
		IF (state < 0) OR (state >= p.nstates) THEN
			p.violation := "protocol: invalid state"; RETURN
		END;
		FOR i := 0 TO p.nedges - 1 DO
			IF (p.edges[i].from = state) & StrEqual(p.edges[i].label, label) THEN
				state := p.edges[i].to; ok := TRUE; RETURN
			END
		END;
		StrCopy(p.states[state], sname);
		Expected(p, state, exp);
		p.violation := "message '"; Append(label, p.violation);
		Append("' is not allowed in state '", p.violation);
		Append(sname, p.violation);
		Append("'; expected: ", p.violation);
		Append(exp, p.violation)
	END Step;

	PROCEDURE LastViolation* (p: Protocol; OUT msg: Message);
	(** Текст последнего нарушения протокола (для отладочного вывода). *)
	BEGIN
		IF p = NIL THEN msg := "" ELSE msg := p.violation END
	END LastViolation;

END AsyncProto.

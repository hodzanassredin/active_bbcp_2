MODULE Dev_devProfMap;
(** Карта кода для выборочного профайлера tools/prof.sh: для каждого
	загруженного модуля — имя, адрес и размер кода, для каждой его
	процедуры — имя и смещение конца кода. *)

	IMPORT S := SYSTEM, Kernel, Console, Strings;

	PROCEDURE Num (x: LONGINT);
		VAR s: ARRAY 24 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END Num;

	PROCEDURE Map*;
		VAR m: Kernel.Module; ref: S.ADDRESS; adr: INTEGER;
			name: Kernel.Utf8Name; mn: Kernel.Name;
	BEGIN
		m := Kernel.modList;
		WHILE m # NIL DO
			IF m.refcnt >= 0 THEN
				Kernel.GetModName(m, mn);
				Console.WriteStr("PROFMAP M "); Console.WriteStr(mn); Console.WriteStr(" ");
				Num(m.code); Console.WriteStr(" "); Num(m.csize); Console.WriteLn;
				ref := m.refs;
				IF ref # 0 THEN
					Kernel.GetRefProc(ref, adr, name);
					WHILE adr # 0 DO
						Console.WriteStr("PROFMAP P "); Num(adr); Console.WriteStr(" ");
						Console.WriteStr(name$); Console.WriteLn;
						Kernel.GetRefProc(ref, adr, name)
					END
				END
			END;
			m := m.next
		END
	END Map;

END Dev_devProfMap.

MODULE Taskarch;
(** Машинная часть Tasks для i386: размер слова, раскладка кадра
	переключателя и сам переключатель стеков [code] Switch.

	Соглашение вызовов CP x86, callee-saved — EBX/ESI/EDI/EBP; слоты
	сохранения по 4 байта. Пара — System/Mod64/Taskarch.cp (amd64);
	Tasks один на обе платформы и берёт отсюда только эти имена.
	Switch — [code]: вставляется в место вызова, межмодульного вызова нет. *)

	IMPORT S := SYSTEM;

	CONST
		word* = 4;			(** SIZE(SYSTEM.ADDRESS) *)
		savedRegs = 5;		(* edi esi ebp ebx eax *)
		entryOff* = savedRegs * 4;	(** смещение адреса входа от sp нового стека *)
		stackHead* = entryOff + 8;	(** запас под кадр TaskBoot у верха стека *)

	PROCEDURE [code] Switch* (VAR from, to: S.ADDRESS)
	(*
		entry: eax = &from, [esp] = &to.

		mov ecx,[esp]        ; &to (esp ещё указывает на параметр)
		add esp,4            ; параметр снят
		call $+5             ; push адрес следующей инструкции
		pop edx              ; edx = адрес продолжения
		add edx,27           ; + длина хвоста switch (до после ret)
		push edx             ; синтетический адрес возврата
		push eax             ; сохранить &from
		push ebx; push ebp; push esi; push edi
		mov edx,[esp+16]     ; &from (edi esi ebp ebx лежат выше)
		mov [edx],esp        ; from^ := esp
		mov esp,[ecx]        ; esp := to^
		pop edi; pop esi; pop ebp; pop ebx; pop eax
		ret
	*)
		08BH, 00CH, 024H,		(* mov ecx,[esp] *)
		083H, 0C4H, 004H,		(* add esp,4 *)
		0E8H, 000H, 000H, 000H, 000H,	(* call $+5 *)
		05AH,				(* pop edx — адрес продолжения *)
		081H, 0C2H, 01BH, 000H, 000H, 000H,	(* add edx,27 — после ret *)
		052H,				(* push edx: синтетический адрес возврата *)
		050H,				(* push eax (&from) *)
		053H, 055H, 056H, 057H,		(* push ebx; ebp; esi; edi *)
		08BH, 054H, 024H, 010H,		(* mov edx,[esp+16] *)
		089H, 022H,			(* mov [edx],esp *)
		08BH, 021H,			(* mov esp,[ecx] *)
		05FH, 05EH, 05DH, 05BH, 058H,	(* pop edi esi ebp ebx eax *)
		0C3H;				(* ret *)

END Taskarch.

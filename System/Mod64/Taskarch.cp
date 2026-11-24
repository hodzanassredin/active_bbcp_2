MODULE Taskarch;
(** Машинная часть Tasks для amd64: размер слова, раскладка кадра
	переключателя и сам переключатель стеков [code] Switch.

	Сохраняются rax, rbx, rbp, rsi, rdi, r8-r15 и rsp; слоты по 8 байт.
	Пара — System/Mod/Taskarch.cp (i386); Tasks один на обе платформы
	и берёт отсюда только эти имена. Switch — [code]: вставляется в место
	вызова, межмодульного вызова нет.

	Переключатель кончается косвенным jmp, а не ret: ret без парного call
	снимает чужую запись из буфера адресов возврата процессора, и после
	переключения ошибается каждый возврат вверх по цепочке кадров. С jmp
	цепочки двух задач на одном пути (Run → Yield → Leave) совпадают, и
	возвраты угадываются (docs/stacks-study.md, раздел 6). *)

	IMPORT S := SYSTEM;

	CONST
		word* = 8;			(** SIZE(SYSTEM.ADDRESS) *)
		savedRegs = 13;		(* rax rbx rbp rsi rdi r8-r15 *)
		entryOff* = savedRegs * 8;	(** смещение адреса входа от sp нового стека *)
		stackHead* = entryOff + 16;	(** запас выравнивания до верха стека *)

	PROCEDURE [code] Switch* (VAR from, to: S.ADDRESS)
	(*
		entry: rax = &from, [rsp] = &to.

		mov rcx,[rsp]        ; &to (rsp ещё указывает на параметр)
		add rsp,8            ; параметр снят
		lea r11,[rip+end]    ; адрес продолжения после jmp
		push r11             ; синтетический адрес возврата
		push rax             ; сохранить &from
		push rbx; push rbp; push rsi; push rdi
		push r8; push r9; push r10; push r11
		push r12; push r13; push r14; push r15
		mov rdx,[rsp+96]     ; &from (rax лежит выше всех)
		mov [rdx],rsp        ; from^ := rsp
		mov rsp,[rcx]        ; rsp := to^
		pop r15; pop r14; pop r13; pop r12
		pop r11; pop r10; pop r9; pop r8
		pop rdi; pop rsi; pop rbp; pop rbx; pop rax
		pop r11; jmp r11     ; продолжение нового стека
	*)
		048H, 08BH, 00CH, 024H,		(* mov rcx,[rsp] *)
		048H, 083H, 0C4H, 008H,		(* add rsp,8 *)
		04CH, 08DH, 01DH, 03CH, 000H, 000H, 000H,	(* lea r11,[rip+60] — адрес после jmp *)
		041H, 053H,			(* push r11: синтетический адрес возврата *)
		050H,				(* push rax (&from) *)
		053H, 055H, 056H, 057H,
		041H, 050H, 041H, 051H, 041H, 052H, 041H, 053H,
		041H, 054H, 041H, 055H, 041H, 056H, 041H, 057H,
		048H, 08BH, 054H, 024H, 060H,	(* mov rdx,[rsp+96] *)
		048H, 089H, 022H,		(* mov [rdx],rsp *)
		048H, 08BH, 021H,		(* mov rsp,[rcx] *)
		041H, 05FH, 041H, 05EH, 041H, 05DH, 041H, 05CH,
		041H, 05BH, 041H, 05AH, 041H, 059H, 041H, 058H,
		05FH, 05EH, 05DH, 05BH, 058H,
		041H, 05BH,			(* pop r11: адрес продолжения *)
		041H, 0FFH, 0E3H;		(* jmp r11 *)

END Taskarch.

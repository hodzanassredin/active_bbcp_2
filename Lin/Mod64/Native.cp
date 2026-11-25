MODULE LinNative ["libc.so.6"];
(** Дополнительные POSIX-вызовы для рабочих нитей и компутонов: только объявления.

	Отдельный модуль, а не LinLibc: LinLibc загружен в хост-процесс
	(dev0Linux/blackbox-console), и его символьный файл нельзя обновить,
	добавив новые процедуры. Новый модуль компилятор загружает с диска. *)

	IMPORT S := SYSTEM;

	CONST
		F_GETFL* = 3; F_SETFL* = 4;
		SIGKILL* = 9;
		O_NONBLOCK* = 2048;

	TYPE IntPtr* = POINTER [untagged] TO ARRAY [untagged] OF INTEGER;

	PROCEDURE [ccall] pipe* (fds: IntPtr): INTEGER;
	PROCEDURE [ccall] dup2* (oldfd, newfd: INTEGER): INTEGER;
	PROCEDURE [ccall] _exit* (status: INTEGER);
	PROCEDURE [ccall] kill* (pid, sig: INTEGER): INTEGER;
	PROCEDURE [ccall] fcntl* (fd, cmd, arg: INTEGER): INTEGER;
	PROCEDURE [ccall] usleep* (usec: INTEGER): INTEGER;

END LinNative.

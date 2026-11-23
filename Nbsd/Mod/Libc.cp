MODULE NbsdLibc ["libc.so.12"];

	(*
		NetBSD 10.1
		i386
	*)

	CONST
		NULL* = 0H;
		FALSE* = 0;
		TRUE* = 1;

		CLOCKS_PER_SEC* = 100;

		MAP_FAILED* = -1;

		(* MAP_PRIVATE, MAP_ANON (intFlags) *)
		MAP_SHARED* = {0};
		MAP_PRIVATE* = {1};
		MAP_FIXED* = {4};
		MAP_ANON* = {12};

		(* PROT_NONE, PROT_READ, PROT_WRITE, PROT_EXEC (intFlags) *)
		PROT_NONE* = {};
		PROT_READ* = {0};
		PROT_WRITE* = {1};
		PROT_EXEC* = {2};

		(* SIG_UNBLOCK, SIG_SETMASK (int) *)
		SIG_BLOCK* = 1;
		SIG_UNBLOCK* = 2;
		SIG_SETMASK* = 3;

		(* SIG_DFL, SIG_IGN, SIG_ERR *)
		SIG_DFL* = 0;
		SIG_IGN* = 1;
		SIG_ERR* = -1;

		(* FPE_INTDIV, FPE_INTOVF, FPE_FLTDIV, FPE_FLTOVF, FPE_FLTUND, FPE_FLTRES, FPE_FLTINV, FPE_FLTSUB (int) *)
		FPE_FLTDIV* = 3;
		FPE_FLTOVF* = 4;
		FPE_INTDIV* = 1;
		FPE_FLTSUB* = 8;
		FPE_INTOVF* = 2;
		FPE_FLTINV* = 7;
		FPE_FLTRES* = 6;
		FPE_FLTUND* = 5;

		(* SA_SIGINFO (intFlags) *)
		SA_SIGINFO* = {6};
		SA_ONSTACK* = {0};
		SA_RESETHAND* = {2};
		SA_NOCLDSTOP* = {3};
		SA_RESTART* = {1};
		SA_NOKERNINFO* = {7};
		SA_NOCLDWAIT* = {5};
		SA_NODEFER* = {4};

		SIGSTKSZ* = 40960;

		(* SIGINT, SIGILL, SIGFPE, SIGSEGV, SIGKILL, SIGSTOP, SIGWINCH, SIGTHR (int) *)
		SIGBUS* = 10;
		SIGTTIN* = 21;
		SIGPROF* = 27;
		SIGFPE* = 8;
		SIGHUP* = 1;
		SIGTTOU* = 22;
		SIGUSR1* = 30;
		SIGURG* = 16;
		SIGIO* = 23;
		SIGQUIT* = 3;
		SIGEV_NONE* = 0;
		SIGEV_SA* = 3;
		SIGABRT* = 6;
		SIGINFO* = 29;
		SIGTRAP* = 5;
		SIGEV_THREAD* = 2;
		SIGVTALRM* = 26;
		SIG_ATOMIC_MAX* = 7FFFFFFFH;
		SIGSEGV* = 11;
		SIG_ATOMIC_MIN* = -2147483648;
		SIG_HOLD* = 3;
		SIGCONT* = 19;
		SIGPIPE* = 13;
		SIGWINCH* = 28;
		SIGXFSZ* = 25;
		SIGRTMAX* = 63;
		SIGCHLD* = 20;
		SIGSYS* = 12;
		SIGSTOP* = 17;
		SIGRTMIN* = 33;
		SIGALRM* = 14;
		SIGUSR2* = 31;
		SIGTSTP* = 18;
		SIGEMT* = 7;
		SIGKILL* = 9;
		SIGXCPU* = 24;
		SIGEV_SIGNAL* = 1;
		SIGPWR* = 32;
		SIGILL* = 4;
		SIGINT* = 2;
		SIGIOT* = SIGABRT;
		SIGTERM* = 15;
		_NSIG* = 64;

(*
		PAGE_SIZE* = 4096;
*)
		_SC_PAGESIZE* = 28;

		(* ENOENT, EEXIST, EACCES, ENOMEM, EDQUOT, EMFILE, ENOTDIR (int) *)
		EMULTIHOP* = 94;
		EAFNOSUPPORT* = 47;
		EACCES* = 13;
		EDESTADDRREQ* = 39;
		EILSEQ* = 85;
		ESPIPE* = 29;
		EMLINK* = 31;
		EREMOTE* = 71;
		EPROGUNAVAIL* = 74;
		EOWNERDEAD* = 97;
		ENOTTY* = 25;
		EBADF* = 9;
		ERANGE* = 34;
		ECANCELED* = 87;
		ETXTBSY* = 26;
		ENOMEM* = 12;
		EINPROGRESS* = 36;
		ENOTEMPTY* = 66;
		ENOTBLK* = 15;
		EPROTOTYPE* = 41;
		ENOMSG* = 83;
		ERPCMISMATCH* = 73;
		EALREADY* = 37;
		EADDRINUSE* = 48;
		ETIMEDOUT* = 60;
		ENEEDAUTH* = 81;
		ENODATA* = 89;
		EINTR* = 4;
		ENOLINK* = 95;
		ENOTCONN* = 57;
		EPERM* = 1;
		ESTALE* = 70;
		ENOTSOCK* = 38;
		ENOSR* = 90;
		EAUTH* = 80;
		ECHILD* = 10;
		EPIPE* = 32;
		ENOATTR* = 93;
		EBADMSG* = 88;
		ETOOMANYREFS* = 59;
		EPFNOSUPPORT* = 46;
		EPROCUNAVAIL* = 76;
		ENETRESET* = 52;
		EISDIR* = 21;
		EIDRM* = 82;
		ECONNABORTED* = 53;
		EHOSTUNREACH* = 65;
		EINVAL* = 22;
		ESHUTDOWN* = 58;
		EOVERFLOW* = 84;
		EBUSY* = 16;
		EPROCLIM* = 67;
		EPROTO* = 96;
		ENODEV* = 19;
		EROFS* = 30;
		E2BIG* = 7;
		EDEADLK* = 11;
		ENOTDIR* = 20;
		ECONNRESET* = 54;
		ENXIO* = 6;
		EBADRPC* = 72;
		ENAMETOOLONG* = 63;
		ELAST* = 98;
		ESOCKTNOSUPPORT* = 44;
		EADDRNOTAVAIL* = 49;
		ETIME* = 92;
		EPROTONOSUPPORT* = 43;
		ENOTRECOVERABLE* = 98;
		EIO* = 5;
		ENETUNREACH* = 51;
		EXDEV* = 18;
		EDQUOT* = 69;
		ENOSPC* = 28;
		ENOEXEC* = 8;
		EMSGSIZE* = 40;
		EFTYPE* = 79;
		EDOM* = 33;
		ENOSTR* = 91;
		EFBIG* = 27;
		ESRCH* = 3;
		EHOSTDOWN* = 64;
		ENOLCK* = 77;
		ENFILE* = 23;
		ENOSYS* = 78;
		ENOTSUP* = 86;
		EISCONN* = 56;
		EUSERS* = 68;
		ENETDOWN* = 50;
		ENOPROTOOPT* = 42;
		EMFILE* = 24;
		ELOOP* = 62;
		ENOBUFS* = 55;
		EFAULT* = 14;
		EAGAIN* = 35;
		EWOULDBLOCK* = EAGAIN;
		ECONNREFUSED* = 61;
		EEXIST* = 17;
		EPROGMISMATCH* = 75;
		ENOENT* = 2;
		EOPNOTSUPP* = 45;

		WAIT_ANY* = -1;
		WCONTINUED* = {4};
		WNOHANG* = {0};
		WUNTRACED* = {1};

		NAME_MAX* = 511;

		SEEK_SET* = 0;
		SEEK_CUR* = 1;
		SEEK_END* = 2;

		STDIN_FILENO* = 0;
		STDOUT_FILENO* = 1;
		STDERR_FILENO* = 2;

		P_tmpdir* = "/tmp";

		(* O_RDWR, O_NONBLOCK (intFlags) *)
		O_ALT_IO* = {18};
		O_NOCTTY* = {15};
		O_ACCMODE* = {0,1};
		O_EXLOCK* = {5};
		O_NONBLOCK* = {2};
		O_NDELAY* = O_NONBLOCK;
		O_ASYNC* = {6};
		O_EXCL* = {11};
		O_EXEC* = {26};
		O_REGULAR* = {25};
		O_SYNC* = {7};
		O_FSYNC* = O_SYNC;
		O_APPEND* = {3};
		O_RSYNC* = {17};
		O_DSYNC* = {16};
		O_RDWR* = {1};
		O_CLOEXEC* = {22};
		O_CREAT* = {9};
		O_NOSIGPIPE* = {24};
		O_SHLOCK* = {4};
		O_DIRECT* = {19};
		O_RDONLY* = {};
		O_TRUNC* = {10};
		O_WRONLY* = {0};
		O_SEARCH* = {23};
		O_NOFOLLOW* = {8};
		O_DIRECTORY* = {21};

		CLOCK_REALTIME* = 0;
		CLOCK_VIRTUAL* = 1;
		CLOCK_THREAD_CPUTIME_ID* = 20000000H;
		CLOCK_MONOTONIC* = 3;
		CLOCK_PROF* = 2;
		CLOCK_PROCESS_CPUTIME_ID* = 40000000H;

		RLIMIT_AS* = 10;
		RLIMIT_DATA* = 2;
		RLIMIT_CPU* = 0;
		RLIMIT_NPROC* = 7;
		RLIMIT_CORE* = 4;
		RLIMIT_RSS* = 5;
		RLIMIT_NOFILE* = 8;
		RLIMIT_VMEM* = RLIMIT_AS;
		RLIMIT_FSIZE* = 1;
		RLIMIT_MEMLOCK* = 6;
		RLIMIT_SBSIZE* = 9;
		RLIMIT_NTHR* = 11;
		RLIMIT_STACK* = 3;

		UTIME_NOW* = 1073741823;
		UTIME_OMIT* = 1073741822;

	TYPE
		StrArray* = POINTER TO ARRAY [untagged] OF PtrSTR;
		PtrSTR* = POINTER TO ARRAY [untagged] OF SHORTCHAR;

		(* PtrVoid, int, long, size_t, ssize_t, off_t, time_t, clock_t, sigjmp_buf *)
		(* mode_t, rlim_t, intFlags, sigset_t (set) *)
		PtrVoid* = INTEGER;
		int* = INTEGER;
		long* = INTEGER;
		ulong* = INTEGER;
		size_t* = INTEGER;
		ssize_t* = INTEGER;
		off_t* = LONGINT;
		clock_t* = INTEGER;
		clockid_t* = INTEGER;
		time_t* = LONGINT;
		mode_t* = SET;
		pid_t* = INTEGER;
		uid_t* = INTEGER;
		gid_t* = INTEGER;
		dev_t* = LONGINT;
		ino_t* = LONGINT;
		nlink_t* = INTEGER;
		blkcnt_t = LONGINT;
		blksize_t = INTEGER;
		int8_t* = SHORTCHAR;
		uint8_t* = SHORTCHAR;
		int16_t* = SHORTINT;
		uint16_t* = SHORTINT;
		int32_t* = INTEGER;
		uint32_t* = INTEGER;
		int64_t* = LONGINT;
		uint64_t* = LONGINT;
		wchar_t* = INTEGER;
		rlim_t* = LONGINT;
		sigjmp_buf* = ARRAY [untagged] 14 OF INTEGER;
		sigval_t* = INTEGER;
		lwpid_t* = INTEGER;
		intFlags* = SET;
		sigset_t* = ARRAY [untagged] 16 OF BYTE;
		PtrSigset_t* = POINTER [untagged] TO sigset_t;


		tm* = POINTER TO tmDesc;
		tmDesc* = RECORD [untagged]
			(* NOTE: check record size *)
			(* tm_year, tm_mon, tm_mday, tm_hour, tm_min, tm_sec, tm_wday [ , tm_gmtoff ] *)
			(* NetBSD 10.1 /usr/include/time.h *)
				tm_sec*: int; (* seconds after the minute [0-60] *)
				tm_min*: int; (* minutes after the hour [0-59] *)
				tm_hour*: int; (* hours since midnight [0-23] *)
				tm_mday*: int; (* day of the month [1-31] *)
				tm_mon*: int; (* months since January [0-11] *)
				tm_year*: int; (* years since 1900 *)
				tm_wday*: int; (* days since Sunday [0-6] *)
				tm_yday*: int; (* days since January 1 [0-365] *)
				tm_isdst*: int; (* Daylight Saving Time flag *)
				tm_gmtoff*: long; (* offset from UTC in seconds *)
				tm_zone*: PtrSTR; (* timezone abbreviation *)
		END;

		Ptrsiginfo_t* = POINTER TO siginfo_t;
		siginfo_t = RECORD [untagged]
			(* si_code, fault address *)
			(* NetBSD 10.1 /usr/include/sys/siginfo.h *)
				si_pad: ARRAY [untagged] 128 OF SHORTCHAR;
				si_signo*: int;
				si_code*: int;
				si_errno*: int;
				_reason*: RECORD [union]
					_rt: RECORD [untagged]
						_pid: pid_t;
						_uid: uid_t;
						_value: sigval_t (* si_value *)
					END;
					_child: RECORD [untagged]
						_pid: pid_t; (* si_pid *)
						_uid: uid_t; (* si_uid *)
						_status: int; (* si_status *)
						_utime: clock_t; (* si_utime *)
						_stime: clock_t (* si_stime *)
					END;
					_fault*: RECORD [untagged]
						_addr*: PtrVoid; (* si_addr *)
						_trap*: int; (* si_trap *)
						_trap2*: int; (* si_trap2 *)
						_trap3*: int (* si_trap3 *)
					END;
					_poll: RECORD [untagged]
						_band: long; (* si_band *)
						_fd: int (* si_fd *)
					END;
					_syscall: RECORD [untagged]
						_sysnum: int; (* si_sysnum *)
						_retval: ARRAY [untagged] 2 OF int; (* si_retval *)
						_error: int; (* si_error *)
						_args: ARRAY [untagged] 8 OF uint64_t (* si_args *)
					END;
					_ptrace_state: RECORD [untagged]
						_pe_report_event: int; (* si_pe_report_event *)
						_option: RECORD [union]
							_pe_other_pid: pid_t; (* si_pe_other_pid *)
							_pe_lwp: lwpid_t (* si_pe_lwp *)
						END;
					END
				END;
		END;

		Ptrucontext_t* = POINTER TO ucontext_t;
		ucontext_t = RECORD [untagged]
			(* IP, SP, FP *)
			(* NetBSD 10.1 /usr/include/sys/ucontext.h *)
				uc_flags: int (* unsigned int *);
				uc_link: Ptrucontext_t;
				uc_sigmask: sigset_t;
				uc_stack: stack_t;
				uc_mcontext-: mcontext_t;
				(* ... *)
		END;

		sa_sigaction_t* = PROCEDURE [ccall] (sig: int; siginfo: Ptrsiginfo_t; context: Ptrucontext_t);
		sigaction_t* = RECORD [untagged]
(*
			sa_sigaction*: sa_sigaction_t,
			sa_flags*: intFlags, sa_mask*: sigset_t
*)
			(* NetBSD 10.1 /usr/include/sys/signal.h *)
				sa_sigaction*: PROCEDURE [ccall] (sig: int; siginfo: Ptrsiginfo_t; ctx: Ptrucontext_t);
				sa_mask*: sigset_t;
				sa_flags*: intFlags;
		END;

		stack_t* = RECORD [untagged]
(*
			ss_sp*: PtrVoid, ss_size*: size_t, ss_flags*: intFlags
*)
			(* NetBSD 10.1 /usr/include/sys/sigtypes.h *)
				ss_sp*: PtrVoid; (* signal stack base *)
				ss_size*: size_t; (* signal stack length *)
				ss_flags*: intFlags; (* SS_DISABLE and/or SS_ONSTACK *)
		END;

		stat_t* = RECORD [untagged]
(*
			NOTE: check record size
			st_mode*: mode_t, st_size*: off_t, st_mtime*: time_t
*)
			(* NetBSD 10.1 /usr/include/sys/stat.h *)
				st_dev: dev_t; (* inode's device *)
				st_mode*: mode_t; (* inode protection mode *)
				st_ino: ino_t; (* inode's number *)
				st_nlink: nlink_t; (* number of hard links *)
				st_uid: uid_t; (* user ID of the file's owner *)
				st_gid: gid_t; (* group ID of the file's group *)
				st_rdev: dev_t; (* device type *)

				st_atim: timespec_t; (* time of last access *)
				st_mtim*: timespec_t; (* time of last data modification *)
				st_ctim: timespec_t; (* time of last file status change *)
				st_birthtim: timespec_t; (* time of creation *)

				st_size*: off_t; (* file size, in bytes *)
				st_blocks: blkcnt_t; (* blocks allocated for file *)
				st_blksize: blksize_t; (* optimal blocksize for I/O *)
				st_flags: uint32_t; (* user defined flags for file *)
				st_gen: uint32_t; (* file generation number *)
				st_spare: ARRAY [untagged] 2 OF uint32_t;
		END;

		PtrFILE* = PtrVoid;
		PtrDIR* = PtrVoid;

		PtrDirent* = POINTER TO Dirent;
		Dirent = RECORD [untagged]
(*
			d_name*: ARRAY [untagged] NAME_MAX + 1 OF SHORTCHAR
*)
			(* NetBSD 10.1 /usr/include/sys/dirent.h *)
				d_fileno*: ino_t; (* file number of entry *)
				d_reclen*: uint16_t; (* length of this record *)
				d_namlen*: uint16_t; (* length of string in d_name *)
				d_type*: uint8_t; (* file type, see below *)
				d_name*: ARRAY [untagged] 511 + 1 OF SHORTCHAR;
		END;

		timespec_t* = RECORD [untagged]
			(* NetBSD 10.1 /usr/include/sys/timespec.h *)
				tv_sec*: time_t; (* seconds *)
				tv_nsec*: long; (* and nanoseconds *)
		END;

		rlimit* = RECORD [untagged]
			rlim_cur*, rlim_max*: rlim_t
		END;

		TVP* = RECORD [untagged] access*, modification*: timespec_t END;

(*
	VAR
		stdin*: INTEGER;
		timezone*: INTEGER; (* or tm.tm_gmtoff *)
*)
(*
	PROCEDURE [ccall] __errno_location* (): PtrVoid;
*)
(*
	(* POSIX.1 *)
		PROCEDURE [ccall] stat* (path: PtrSTR; VAR sp: stat_t): int;
*)
	CONST
		(* NetBSD 10.1 /usr/include/i386/mcontext.h *)
		_NGREG* = 19;
		_REG_EBP* = 6;
		_REG_ESP* = 7;
		_REG_EIP* = 14;

	TYPE
		(* NetBSD 10.1 /usr/include/i386/mcontext.h *)
			__greg_t = int;
			__gregset_t = ARRAY [untagged] _NGREG OF __greg_t;

			__fpregset_t = RECORD [untagged]
				__fp_reg_set: RECORD [union]
					__fpchip_state: RECORD [untagged]
						__fp_state: ARRAY [untagged] 27 OF int
					END;
					__fp_xmm_state: RECORD [untagged]
						__fp_xmm: ARRAY [untagged] 512 OF SHORTCHAR
					END;
					__fp_fpregs: ARRAY [untagged] 128 OF int
				END;
				__fp_pad: ARRAY [untagged] 33 OF int
			END;

		(* NetBSD 10.1 /usr/include/i386/mcontext.h *)
			mcontext_t* = RECORD [untagged]
				__gregs-: __gregset_t;
				__fpregs: __fpregset_t;
				_mc_tlsbase: __greg_t
			END;

	PROCEDURE [ccall] __errno_location* ["__errno"] (): PtrVoid;

	(* POSIX.1 *)
		PROCEDURE [ccall] stat* ["__stat50"] (path: PtrSTR; VAR sb: stat_t): int;

		PROCEDURE [ccall] lseek* (fildes: int; offset: off_t; whence: int): off_t;

	(* XSH5 *)
		PROCEDURE [ccall] fseeko* (stream: PtrFILE; offset: off_t; whence: int): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] sigsetjmp* ["__sigsetjmp14"] (VAR env: sigjmp_buf; savemask: int): int;
		PROCEDURE [ccall] siglongjmp* ["__siglongjmp14"] (VAR env: sigjmp_buf; val: int);

	(* POSIX.1 *)
		PROCEDURE [ccall] sigemptyset* ["__sigemptyset14"] (set: PtrSigset_t): int;
		PROCEDURE [ccall] sigfillset* ["__sigfillset14"] (set: PtrSigset_t): int;
		PROCEDURE [ccall] sigaddset* ["__sigaddset14"] (set: PtrSigset_t; signo: int): int;
		PROCEDURE [ccall] sigprocmask* ["__sigprocmask14"] (how: int; set: PtrSigset_t; oset: PtrSigset_t): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] sigaction* ["__sigaction_siginfo"] (sig: int; VAR [nil] act: sigaction_t; VAR [nil] oact: sigaction_t): int;

	(* BSD *)
		PROCEDURE [ccall] sigaltstack* ["__sigaltstack14"] (VAR [nil] ss: stack_t; VAR [nil] oss: stack_t): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] clock_gettime* ["__clock_gettime50"] (clock_id: clockid_t; VAR tp: timespec_t): int;

	(* AT&T UNIX *)
		PROCEDURE [ccall] time* ["__time50"] (VAR [nil] tloc: time_t): time_t;

	(* AT&T UNIX *)
		PROCEDURE [ccall] gmtime* ["__gmtime50"] (VAR [nil] clock: time_t): tm;
		PROCEDURE [ccall] localtime* ["__locatime50"] (VAR [nil] clock: time_t): tm;
		PROCEDURE [ccall] mktime* ["__mktime50"] (VAR tm: tmDesc): time_t;

	(* POSIX.1 *)
		PROCEDURE [ccall] opendir* ["__opendir30"] (filename: PtrSTR): PtrDIR;
		PROCEDURE [ccall] readdir* ["__readdir30"] (dirp: PtrDIR): PtrDirent;


	(* ANSI C 89 *)
		PROCEDURE [ccall] clock* (): clock_t;

(*
	(* POSIX.1 *)
		PROCEDURE [ccall] clock_gettime* (clock_id: clockid_t; VAR tp: timespec_t): int;

	(* AT&T UNIX *)
		PROCEDURE [ccall] time* (VAR [nil] tloc: time_t): time_t;

	(* AT&T UNIX *)
		PROCEDURE [ccall] gmtime* (VAR [nil] clock: time_t): tm;
		PROCEDURE [ccall] localtime* (VAR [nil] clock: time_t): tm;
		PROCEDURE [ccall] mktime* (VAR tm: tmDesc): time_t;
*)

	PROCEDURE [ccall] mmap* (adr: PtrVoid; len: size_t; prot: intFlags; flags: intFlags; fd: int; offset: off_t): PtrVoid;
	(* BSD *)
		PROCEDURE [ccall] munmap* (adr: PtrVoid; len: size_t): int;
		PROCEDURE [ccall] mprotect* (adr: PtrVoid; len: size_t; prot: intFlags): int;

	PROCEDURE [ccall] calloc* (nmemb: size_t; size: size_t): PtrVoid;
(*	(* ANSI C 89 *)
		PROCEDURE [ccall] malloc* (size: size_t): PtrVoid;
*)
		PROCEDURE [ccall] free* (ptr: PtrVoid);


(*
	(* POSIX.1 *)
		PROCEDURE [ccall] sigsetjmp* (VAR env: sigjmp_buf; savemask: int): int;
		PROCEDURE [ccall] siglongjmp* (VAR env: sigjmp_buf; val: int);

	(* POSIX.1 *)
		PROCEDURE [ccall] sigemptyset* (set: PtrSigset_t): int;
		PROCEDURE [ccall] sigfillset* (set: PtrSigset_t): int;
		PROCEDURE [ccall] sigaddset* (set: PtrSigset_t; signo: int): int;
		PROCEDURE [ccall] sigprocmask* (how: int; set: PtrSigset_t; oset: PtrSigset_t): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] sigaction* (sig: int; VAR [nil] act: sigaction_t; VAR [nil] oact: sigaction_t): int;

	(* BSD *)
		PROCEDURE [ccall] sigaltstack* (VAR [nil] ss: stack_t; VAR [nil] oss: stack_t): int;
*)

	(* ANSI C 89 *)
		PROCEDURE [ccall] getenv* (s: PtrSTR): PtrSTR;

	(* ANSI C 89 *)
		PROCEDURE [ccall] fopen* (path, mode: PtrSTR): PtrFILE;
		PROCEDURE [ccall] fdopen* (fildes: int; mode: PtrSTR): PtrFILE;
		PROCEDURE [ccall] fclose* (stream: PtrFILE): int;
		PROCEDURE [ccall] fread* (ptr: PtrVoid; size: size_t; nmemb: size_t; stream: PtrFILE): size_t;
		PROCEDURE [ccall] fwrite* (ptr: PtrVoid; size: size_t; nmemb: size_t; stream: PtrFILE): size_t;
		PROCEDURE [ccall] fflush* (s: PtrFILE): int;
		PROCEDURE [ccall] printf0* ["printf"] (format: PtrSTR): int;
		PROCEDURE [ccall] printfs* ["printf"] (format, s: PtrSTR): int;
	(* ANSI C 89, XPG4 *)
		PROCEDURE [ccall] fseek* (stream: PtrFILE; offset: long; whence: int): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] fileno* (stream: PtrFILE): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] rename* (from, to: PtrSTR): int;
		PROCEDURE [ccall] mkdir* (path: PtrSTR; mode: mode_t): int;
(*
		PROCEDURE [ccall] opendir* (filename: PtrSTR): PtrDIR;
		PROCEDURE [ccall] readdir* (dirp: PtrDIR): PtrDirent;
*)
		PROCEDURE [ccall] closedir* (dirp: PtrDIR): int;
	(* ANSI C 89, XPG4.2 *)
		PROCEDURE [ccall] remove* (path: PtrSTR): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] getcwd* (buf: PtrSTR; size: size_t): PtrSTR;

	(* ANSI C 99 *)
		PROCEDURE [ccall] exit* (status: int);

	(* ANSI C 89 *)
		PROCEDURE [ccall] strftime* (buf: PtrSTR; maxsize: size_t; format: PtrSTR; timeptr: tm): size_t;

	(* XXX: use fread instead *)
		PROCEDURE [ccall] fgets* (str: PtrSTR; size: int; stream: PtrFILE): PtrSTR;

	(* POSIX.1 *)
		PROCEDURE [ccall] open* (path: PtrSTR; flags: intFlags; mode: mode_t): int;
		PROCEDURE [ccall] write* (d: int; buf: PtrVoid; nbytes: size_t): ssize_t;
		PROCEDURE [ccall] read* (d: int; buf: PtrVoid; nbytes: size_t): ssize_t;
		PROCEDURE [ccall] close* (d: int): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] chmod* (path: PtrSTR; mode: mode_t): int;
		PROCEDURE [ccall] fchmod* (fd: int; mode: mode_t): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] fork* (): pid_t;
		PROCEDURE [ccall] waitpid* (wpid: pid_t; VAR [nil] status: int; options: intFlags): pid_t;

	(* POSIX.1 *)
		PROCEDURE [ccall] execv* (path: PtrSTR; argv: POINTER [untagged] TO ARRAY [untagged] OF PtrSTR): int;
		PROCEDURE [ccall] execvp* (file: PtrSTR; argv: POINTER [untagged] TO ARRAY [untagged] OF PtrSTR): int;

	(* POSIX.2 *)
		PROCEDURE [ccall] system* (string: PtrSTR): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] sysconf* (name: int): long;

	(* POSIX.1 *)
		PROCEDURE [ccall] popen* (command, type: PtrSTR): PtrFILE;
		PROCEDURE [ccall] pclose* (stream: PtrFILE): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] getrlimit* (resource: int; VAR rlim: rlimit): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] utimensat* (fd: int; path: PtrSTR; VAR times: TVP; flag: intFlags): int;

	(* POSIX.1 *)
		PROCEDURE [ccall] readlink* (path: PtrSTR; buf: PtrSTR; bufsiz: size_t): ssize_t;

END NbsdLibc.

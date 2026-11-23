MODULE LinRt ["librt.so.1"];

	(*
		A. V. Shiryaev, 2013.12

		GNU/Linux
		32-bit
	*)

	IMPORT SYSTEM;

	CONST
		CLOCK_REALTIME* = 0;
		CLOCK_MONOTONIC* = 1;
	
	TYPE
		time_t* = SYSTEM.ADDRESS;

		timespec_t* = RECORD [untagged]
			tv_sec*: time_t; (* seconds *)
			tv_nsec*: SYSTEM.ADDRESS (* nanoseconds *)
		END;

		clockid_t* = INTEGER;

	PROCEDURE [ccall] clock_gettime* (clock_id: clockid_t; VAR tp: timespec_t): INTEGER;

END LinRt.

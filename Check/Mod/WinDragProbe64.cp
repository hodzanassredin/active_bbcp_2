MODULE CheckWinDragProbe64 ["drag64probe.dll"];
	IMPORT S := SYSTEM;

	PROCEDURE [ms64] IncomingProcess* (wnd: S.ADDRESS; x, y, mode, modifier, cancel: INTEGER; path: S.ADDRESS): INTEGER;

	PROCEDURE [ms64] BeginOutgoing* (x, y, modifier, cancel: INTEGER);

	PROCEDURE [ms64] EndOutgoing* (expected: INTEGER): INTEGER;

	PROCEDURE [ms64] LegacyFiles* (wnd, path: S.ADDRESS);

	PROCEDURE [ms64] Gesture* (sx, sy, tx, ty, modifier: INTEGER);
END CheckWinDragProbe64.

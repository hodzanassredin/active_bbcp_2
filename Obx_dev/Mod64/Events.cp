MODULE Obx_devEvents;
(** Тесты примеров событий: каждый пример зовётся командой Do и
	проверяет свой детерминированный итог. Примеры работают на задачах,
	поэтому тесты помечены TestProc… и идут отдельным процессом. *)

	IMPORT Tasks, UnitAssert,
		ObxEvent0, ObxEvent1, ObxEvent2, ObxEvent3, ObxEvent4, ObxEvent5;

	PROCEDURE TestProcSwap*;
	BEGIN
		ObxEvent0.Do;
		UnitAssert.EqualInt(2, ObxEvent0.First, "swap: first received 2");
		UnitAssert.EqualInt(1, ObxEvent0.Second, "swap: second received 1");
		UnitAssert.EqualInt(0, Tasks.Count(), "swap: no tasks left")
	END TestProcSwap;

	PROCEDURE TestProcBuffer*;
		VAR i: INTEGER;
	BEGIN
		ObxEvent1.Do;
		FOR i := 0 TO LEN(ObxEvent1.GotN) - 1 DO
			UnitAssert.EqualInt(i + 1, ObxEvent1.GotN[i], "buffer: order")
		END;
		UnitAssert.EqualInt(15, ObxEvent1.GotSum, "buffer: sum");
		UnitAssert.EqualInt(0, Tasks.Count(), "buffer: no tasks left")
	END TestProcBuffer;

	PROCEDURE TestProcCancel*;
	BEGIN
		ObxEvent2.Do;
		UnitAssert.True(ObxEvent2.Fast, "cancel: fast call replied");
		UnitAssert.True(~ObxEvent2.Slow, "cancel: slow call timed out");
		UnitAssert.EqualInt(1, ObxEvent2.Cancelled, "cancel: server saw one nack");
		UnitAssert.EqualInt(0, Tasks.Count(), "cancel: no tasks left")
	END TestProcCancel;

	PROCEDURE TestProcMulticast*;
	BEGIN
		ObxEvent3.Do;
		UnitAssert.EqualInt(15, ObxEvent3.SumA, "multicast: first sum");
		UnitAssert.EqualInt(15, ObxEvent3.SumB, "multicast: second sum");
		UnitAssert.EqualInt(5, ObxEvent3.CountA, "multicast: first count");
		UnitAssert.EqualInt(5, ObxEvent3.CountB, "multicast: second count");
		UnitAssert.EqualInt(0, Tasks.Count(), "multicast: no tasks left")
	END TestProcMulticast;

	PROCEDURE TestProcDeadline*;
	BEGIN
		ObxEvent4.Do;
		UnitAssert.True(ObxEvent4.Fast, "deadline: fast call replied");
		UnitAssert.True(~ObxEvent4.Slow, "deadline: slow call timed out");
		UnitAssert.EqualInt(2, ObxEvent4.Builds, "deadline: guard built twice");
		UnitAssert.EqualInt(0, Tasks.Count(), "deadline: no tasks left")
	END TestProcDeadline;

	PROCEDURE TestProcPhilosophers*;
		VAR i: INTEGER;
	BEGIN
		ObxEvent5.Do;
		FOR i := 0 TO LEN(ObxEvent5.Eaten) - 1 DO
			UnitAssert.EqualInt(2, ObxEvent5.Eaten[i], "philosophers: two meals each")
		END;
		UnitAssert.EqualInt(0, Tasks.Count(), "philosophers: no tasks left")
	END TestProcPhilosophers;

END Obx_devEvents.

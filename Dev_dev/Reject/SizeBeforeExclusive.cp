MODULE DevRejSize;
	IMPORT ACTIVE;
	TYPE
		Rec = RECORD n: INTEGER END;
		R = POINTER TO Rec;
	CONST
		s = SIZE(Rec);
	PROCEDURE [exclusive] (r: R) M, NEW;
	BEGIN
	END M;
END DevRejSize.

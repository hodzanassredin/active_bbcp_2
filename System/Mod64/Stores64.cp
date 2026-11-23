MODULE Stores64;
(**
	project	= "BlackBox"
	organization	= "blackboxframework.org/"
	contributors	= "BlackBox Framework Center"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

	For interoperability of 32-bit Stores with 64-bit Files.
**)

	IMPORT SYSTEM, Files64, Files, Stores, Kernel;

	TYPE
		Segment = POINTER TO RECORD (Files.File)
			container: Files64.File;
			org: LONGINT
		END;

		SegmentReader = POINTER TO RECORD (Files.Reader)
			base: Segment;
			rd: Files64.Reader
		END;

		SegmentWriter = POINTER TO RECORD (Files.Writer)
			base: Segment;
			wr: Files64.Writer
		END;
		
		Reader* = RECORD
			rider-: Files64.Reader;
			cancelled-: BOOLEAN;
			readAlien-: BOOLEAN;
			srd: POINTER TO Stores.Reader; (* only for ReadStore *)
			srdSeg: Segment
		END;
		
		Writer* = RECORD
			rider-: Files64.Writer;
			writtenStore-: Stores.Store;
			swr: POINTER TO Stores.Writer; (* only for WriteStore *)
			swrSeg: Segment
		END;


	(** Segment **)

	PROCEDURE NewSegment* (container: Files64.File; org: LONGINT): Files.File;
		VAR seg: Segment;
	BEGIN
		ASSERT(container # NIL, 20);
		ASSERT(org >= 0, 22);
		NEW(seg); seg.container := container; seg.org := org;
		RETURN seg
	END NewSegment;

	PROCEDURE (this: Segment) Close;
	BEGIN
		this.container := NIL
	END Close;

	PROCEDURE (this: Segment) Flush;
	BEGIN
		this.container.Flush
	END Flush;
	
	PROCEDURE (this: Segment) Length (): INTEGER;
	BEGIN
		RETURN SHORT(MIN(MAX(INTEGER), this.container.Length() - this.org))
	END Length;
	
	PROCEDURE (this: Segment) NewReader (old: Files.Reader): Files.Reader;
		VAR segRd: SegmentReader;
	BEGIN
		IF (old # NIL) & (old IS SegmentReader) THEN segRd := old(SegmentReader) ELSE NEW(segRd) END;
		IF segRd.base # this THEN
			segRd.base := this;
			segRd.rd := this.container.NewReader(NIL);
			segRd.rd.SetPos(segRd.base.org)
		END;
		segRd.eof := FALSE;
		RETURN segRd
	END NewReader;
	
	PROCEDURE (this: Segment) NewWriter (old: Files.Writer): Files.Writer;
		VAR segWr: SegmentWriter;
	BEGIN
		IF (old # NIL) & (old IS SegmentWriter) THEN segWr := old(SegmentWriter) ELSE NEW(segWr) END;
		IF segWr.base # this THEN
			segWr.base := this;
			segWr.wr := this.container.NewWriter(NIL);
			segWr.wr.SetPos(segWr.base.org + segWr.base.Length())
		END;
		RETURN segWr
	END NewWriter;
	
	PROCEDURE (this: Segment) Register (name: Files.Name; type: Files.Type; ask: BOOLEAN;
																OUT res: INTEGER);
	BEGIN HALT(20) (* a Segment cannot be registered *)
	END Register;

	PROCEDURE (this: SegmentReader) Base (): Files.File;
	BEGIN
		RETURN this.base
	END Base;

	PROCEDURE (this: SegmentReader) SetPos (pos: INTEGER);
		VAR seg: Segment;
	BEGIN
		seg := this.base;
		ASSERT(seg # NIL, 20);
		ASSERT(pos >= 0, 22); ASSERT(pos <= seg.Length(), 21);
		this.rd.SetPos(seg.org + pos)
	END SetPos;

	PROCEDURE (this: SegmentReader) Pos (): INTEGER;
	BEGIN
		ASSERT(this.base # NIL, 20);
		RETURN SHORT(this.rd.Pos() - this.base.org)
	END Pos;

	PROCEDURE (this: SegmentReader) ReadByte (OUT x: BYTE);
	BEGIN
		this.rd.ReadByte(x)
	END ReadByte;

	PROCEDURE (this: SegmentReader) ReadBytes (VAR x: ARRAY OF BYTE; beg, len: INTEGER);
	BEGIN
		this.rd.ReadBytes(x, beg, len)
	END ReadBytes;

	PROCEDURE (this: SegmentWriter) Base (): Files.File;
	BEGIN
		RETURN this.base
	END Base;

	PROCEDURE (this: SegmentWriter) SetPos (pos: INTEGER);
		VAR seg: Segment;
	BEGIN
		seg := this.base;
		ASSERT(seg # NIL, 20); (* ASSERT(~seg.Closed(), 25); *)
		ASSERT(pos >= 0, 22); ASSERT(pos <= seg.Length(), 21);
		this.wr.SetPos(seg.org + pos)
	END SetPos;

	PROCEDURE (this: SegmentWriter) Pos (): INTEGER;
	BEGIN
		ASSERT(this.base # NIL, 20);
		RETURN SHORT(this.wr.Pos() - this.base.org)
	END Pos;

	PROCEDURE (this: SegmentWriter) WriteByte (x: BYTE);
	BEGIN
		this.wr.WriteByte(x)
	END WriteByte;

	PROCEDURE (this: SegmentWriter) WriteBytes (IN x: ARRAY OF BYTE; beg, len: INTEGER);
	BEGIN
		this.wr.WriteBytes(x, beg, len)
	END WriteBytes;


	(** Reader **)

	PROCEDURE (VAR rd: Reader) ConnectTo* (f: Files64.File), NEW;
	BEGIN
		IF f = NIL THEN
			rd.rider := NIL
		ELSE
			rd.rider := f.NewReader(rd.rider);
			rd.rider.SetPos(0)
		END;
		rd.cancelled := FALSE;
		rd.readAlien := FALSE;
		rd.srd := NIL
	END ConnectTo;
	
	PROCEDURE (VAR rd: Reader) SetPos* (pos: LONGINT), NEW;
	BEGIN
		rd.rider.SetPos(pos)
	END SetPos;
	
	PROCEDURE (VAR rd: Reader) Pos* (): LONGINT, NEW;
	BEGIN
		RETURN rd.rider.Pos()
	END Pos;

	PROCEDURE (VAR rd: Reader) ReadBool* (OUT x: BOOLEAN), NEW;
		VAR b: BYTE;
	BEGIN
		rd.rider.ReadByte(b); x := b # 0
	END ReadBool;

	PROCEDURE (VAR rd: Reader) ReadSChar* (OUT x: SHORTCHAR), NEW;
	BEGIN
		rd.rider.ReadByte(SYSTEM.VAL(BYTE, x))
	END ReadSChar;

	PROCEDURE (VAR rd: Reader) ReadXChar* (OUT x: CHAR), NEW;
		VAR c: SHORTCHAR;
	BEGIN
		rd.rider.ReadByte(SYSTEM.VAL(BYTE, c)); x := c
	END ReadXChar;

	PROCEDURE (VAR rd: Reader) ReadChar* (OUT x: CHAR), NEW;
		VAR le: ARRAY 2 OF BYTE;	(* little endian, big endian *)
	BEGIN
		rd.rider.ReadBytes(le, 0, 2);
		x := CHR(le[0] MOD 256 + (le[1] MOD 256) * 256)
	END ReadChar;

	PROCEDURE (VAR rd: Reader) ReadByte* (OUT x: BYTE), NEW;
	BEGIN
		rd.rider.ReadByte(x)
	END ReadByte;

	PROCEDURE (VAR rd: Reader) ReadSInt* (OUT x: SHORTINT), NEW;
		VAR le, be: ARRAY 2 OF BYTE;	(* little endian, big endian *)
	BEGIN
		rd.rider.ReadBytes(le, 0, 2);
		IF Kernel.littleEndian THEN
			x := SYSTEM.VAL(SHORTINT, le)
		ELSE
			be[0] := le[1]; be[1] := le[0];
			x := SYSTEM.VAL(SHORTINT, be)
		END
	END ReadSInt;

	PROCEDURE (VAR rd: Reader) ReadXInt* (OUT x: INTEGER), NEW;
		VAR le, be: ARRAY 2 OF BYTE;	(* little endian, big endian *)
	BEGIN
		rd.rider.ReadBytes(le, 0, 2);
		IF Kernel.littleEndian THEN
			x := SYSTEM.VAL(SHORTINT, le)
		ELSE
			be[0] := le[1]; be[1] := le[0];
			x := SYSTEM.VAL(SHORTINT, be)
		END
	END ReadXInt;

	PROCEDURE (VAR rd: Reader) ReadInt* (OUT x: INTEGER), NEW;
		VAR le, be: ARRAY 4 OF BYTE;	(* little endian, big endian *)
	BEGIN
		rd.rider.ReadBytes(le, 0, 4);
		IF Kernel.littleEndian THEN
			x := SYSTEM.VAL(INTEGER, le)
		ELSE
			be[0] := le[3]; be[1] := le[2]; be[2] := le[1]; be[3] := le[0];
			x := SYSTEM.VAL(INTEGER, be)
		END
	END ReadInt;

	PROCEDURE (VAR rd: Reader) ReadLong* (OUT x: LONGINT), NEW;
		VAR le, be: ARRAY 8 OF BYTE;	(* little endian, big endian *)
	BEGIN
		rd.rider.ReadBytes(le, 0, 8);
		IF Kernel.littleEndian THEN
			x := SYSTEM.VAL(LONGINT, le)
		ELSE
			be[0] := le[7]; be[1] := le[6]; be[2] := le[5]; be[3] := le[4];
			be[4] := le[3]; be[5] := le[2]; be[6] := le[1]; be[7] := le[0];
			x := SYSTEM.VAL(LONGINT, be)
		END
	END ReadLong;

	PROCEDURE (VAR rd: Reader) ReadSReal* (OUT x: SHORTREAL), NEW;
		VAR le, be: ARRAY 4 OF BYTE;	(* little endian, big endian *)
	BEGIN
		rd.rider.ReadBytes(le, 0, 4);
		IF Kernel.littleEndian THEN
			x := SYSTEM.VAL(SHORTREAL, le)
		ELSE
			be[0] := le[3]; be[1] := le[2]; be[2] := le[1]; be[3] := le[0];
			x := SYSTEM.VAL(SHORTREAL, be)
		END
	END ReadSReal;

	PROCEDURE (VAR rd: Reader) ReadXReal* (OUT x: REAL), NEW;
		VAR le, be: ARRAY 4 OF BYTE;	(* little endian, big endian *)
	BEGIN
		rd.rider.ReadBytes(le, 0, 4);
		IF Kernel.littleEndian THEN
			x := SYSTEM.VAL(SHORTREAL, le)
		ELSE
			be[0] := le[3]; be[1] := le[2]; be[2] := le[1]; be[3] := le[0];
			x := SYSTEM.VAL(SHORTREAL, be)
		END
	END ReadXReal;

	PROCEDURE (VAR rd: Reader) ReadReal* (OUT x: REAL), NEW;
		VAR le, be: ARRAY 8 OF BYTE;	(* little endian, big endian *)
	BEGIN
		rd.rider.ReadBytes(le, 0, 8);
		IF Kernel.littleEndian THEN
			x := SYSTEM.VAL(REAL, le)
		ELSE
			be[0] := le[7]; be[1] := le[6]; be[2] := le[5]; be[3] := le[4];
			be[4] := le[3]; be[5] := le[2]; be[6] := le[1]; be[7] := le[0];
			x := SYSTEM.VAL(REAL, be)
		END
	END ReadReal;

	PROCEDURE (VAR rd: Reader) ReadSet* (OUT x: SET), NEW;
		VAR le, be: ARRAY 4 OF BYTE;	(* little endian, big endian *)
	BEGIN
		rd.rider.ReadBytes(le, 0, 4);
		IF Kernel.littleEndian THEN
			x := SYSTEM.VAL(SET, le)
		ELSE
			be[0] := le[3]; be[1] := le[2]; be[2] := le[1]; be[3] := le[0];
			x := SYSTEM.VAL(SET, be)
		END
	END ReadSet;

	PROCEDURE (VAR rd: Reader) ReadSString* (OUT x: ARRAY OF SHORTCHAR), NEW;
		VAR i: INTEGER; ch: SHORTCHAR;
	BEGIN
		i := 0; REPEAT rd.ReadSChar(ch); x[i] := ch; INC(i) UNTIL ch = 0X
	END ReadSString;

	PROCEDURE (VAR rd: Reader) ReadXString* (OUT x: ARRAY OF CHAR), NEW;
		VAR i: INTEGER; ch: CHAR;
	BEGIN
		i := 0; REPEAT rd.ReadXChar(ch); x[i] := ch; INC(i) UNTIL ch = 0X
	END ReadXString;

	PROCEDURE (VAR rd: Reader) ReadString* (OUT x: ARRAY OF CHAR), NEW;
		VAR i: INTEGER; ch: CHAR;
	BEGIN
		i := 0; REPEAT rd.ReadChar(ch); x[i] := ch; INC(i) UNTIL ch = 0X
	END ReadString;

	PROCEDURE (VAR rd: Reader) ReadStore* (OUT x: Stores.Store), NEW;
		VAR pos0, srdPos0: LONGINT; srd: POINTER TO Stores.Reader; seg: Segment;
	BEGIN
		pos0 := rd.Pos();
		srd := rd.srd;
		IF srd = NIL THEN
			srdPos0 := 0;
			seg := NewSegment(rd.rider.Base(), pos0)(Segment);
			NEW(srd);
			srd.ConnectTo(seg);
			rd.srd := srd; rd.srdSeg := seg
		ELSE
			srdPos0 := pos0 - rd.srdSeg.org;
			IF srdPos0 < MAX(INTEGER) THEN
				srd.SetPos(SHORT(srdPos0))
			ELSE
				HALT(20) (* stores need more than MAX(INTEGER) bytes *)
			END
		END;
		srd.ReadStore(x);
		rd.SetPos(pos0 + (srd.Pos() - srdPos0));
		rd.cancelled := srd.cancelled;
		rd.readAlien := srd.readAlien
	END ReadStore;


	(** Writer **)

	PROCEDURE (VAR wr: Writer) ConnectTo* (f: Files64.File), NEW;
	BEGIN
		IF f = NIL THEN
			wr.rider := NIL
		ELSE
			wr.rider := f.NewWriter(wr.rider);
			wr.rider.SetPos(f.Length())
		END;
		wr.writtenStore := NIL;
		wr.swr := NIL
	END ConnectTo;

	PROCEDURE (VAR wr: Writer) SetPos* (pos: LONGINT), NEW;
	BEGIN
		wr.rider.SetPos(pos)
	END SetPos;

	PROCEDURE (VAR wr: Writer) Pos* (): LONGINT, NEW;
	BEGIN
		RETURN wr.rider.Pos()
	END Pos;

	PROCEDURE (VAR wr: Writer) WriteBool* (x: BOOLEAN), NEW;
	BEGIN
		IF x THEN wr.rider.WriteByte(1) ELSE wr.rider.WriteByte(0) END
	END WriteBool;

	PROCEDURE (VAR wr: Writer) WriteSChar* (x: SHORTCHAR), NEW;
	BEGIN
		wr.rider.WriteByte(SYSTEM.VAL(BYTE, x))
	END WriteSChar;

	PROCEDURE (VAR wr: Writer) WriteXChar* (x: CHAR), NEW;
		VAR c: SHORTCHAR;
	BEGIN
		c := SHORT(x); wr.rider.WriteByte(SYSTEM.VAL(BYTE, c))
	END WriteXChar;

	PROCEDURE (VAR wr: Writer) WriteChar* (x: CHAR), NEW;
		TYPE a = ARRAY 2 OF BYTE;
		VAR le, be: a;	(* little endian, big endian *)
	BEGIN
		IF Kernel.littleEndian THEN
			le := SYSTEM.VAL(a, x)
		ELSE
			be := SYSTEM.VAL(a, x);
			le[0] := be[1]; le[1] := be[0]
		END;
		wr.rider.WriteBytes(le, 0, 2)
	END WriteChar;

	PROCEDURE (VAR wr: Writer) WriteByte* (x: BYTE), NEW;
	BEGIN
		wr.rider.WriteByte(x)
	END WriteByte;

	PROCEDURE (VAR wr: Writer) WriteSInt* (x: SHORTINT), NEW;
		TYPE a = ARRAY 2 OF BYTE;
		VAR le, be: a;	(* little endian, big endian *)
	BEGIN
		IF Kernel.littleEndian THEN
			le := SYSTEM.VAL(a, x)
		ELSE
			be := SYSTEM.VAL(a, x);
			le[0] := be[1]; le[1] := be[0]
		END;
		wr.rider.WriteBytes(le, 0, 2)
	END WriteSInt;

	PROCEDURE (VAR wr: Writer) WriteXInt* (x: INTEGER), NEW;
		TYPE a = ARRAY 2 OF BYTE;
		VAR y: SHORTINT; le, be: a;	(* little endian, big endian *)
	BEGIN
		y := SHORT(x);
		IF Kernel.littleEndian THEN
			le := SYSTEM.VAL(a, y)
		ELSE
			be := SYSTEM.VAL(a, y);
			le[0] := be[1]; le[1] := be[0]
		END;
		wr.rider.WriteBytes(le, 0, 2)
	END WriteXInt;

	PROCEDURE (VAR wr: Writer) WriteInt* (x: INTEGER), NEW;
		TYPE a = ARRAY 4 OF BYTE;
		VAR le, be: a;	(* little endian, big endian *)
	BEGIN
		IF Kernel.littleEndian THEN
			le := SYSTEM.VAL(a, x)
		ELSE
			be := SYSTEM.VAL(a, x);
			le[0] := be[3]; le[1] := be[2]; le[2] := be[1]; le[3] := be[0]
		END;
		wr.rider.WriteBytes(le, 0, 4)
	END WriteInt;

	PROCEDURE (VAR wr: Writer) WriteLong* (x: LONGINT), NEW;
		TYPE a = ARRAY 8 OF BYTE;
		VAR le, be: a;	(* little endian, big endian *)
	BEGIN
		IF Kernel.littleEndian THEN
			le := SYSTEM.VAL(a, x)
		ELSE
			be := SYSTEM.VAL(a, x);
			le[0] := be[7]; le[1] := be[6]; le[2] := be[5]; le[3] := be[4];
			le[4] := be[3]; le[5] := be[2]; le[6] := be[1]; le[7] := be[0]
		END;
		wr.rider.WriteBytes(le, 0, 8)
	END WriteLong;

	PROCEDURE (VAR wr: Writer) WriteSReal* (x: SHORTREAL), NEW;
		TYPE a = ARRAY 4 OF BYTE;
		VAR le, be: a;	(* little endian, big endian *)
	BEGIN
		IF Kernel.littleEndian THEN
			le := SYSTEM.VAL(a, x)
		ELSE
			be := SYSTEM.VAL(a, x);
			le[0] := be[3]; le[1] := be[2]; le[2] := be[1]; le[3] := be[0]
		END;
		wr.rider.WriteBytes(le, 0, 4)
	END WriteSReal;

	PROCEDURE (VAR wr: Writer) WriteXReal* (x: REAL), NEW;
		TYPE a = ARRAY 4 OF BYTE;
		VAR y: SHORTREAL; le, be: a;	(* little endian, big endian *)
	BEGIN
		y := SHORT(x);
		IF Kernel.littleEndian THEN
			le := SYSTEM.VAL(a, y)
		ELSE
			be := SYSTEM.VAL(a, y);
			le[0] := be[3]; le[1] := be[2]; le[2] := be[1]; le[3] := be[0]
		END;
		wr.rider.WriteBytes(le, 0, 4)
	END WriteXReal;

	PROCEDURE (VAR wr: Writer) WriteReal* (x: REAL), NEW;
		TYPE a = ARRAY 8 OF BYTE;
		VAR le, be: a;	(* little endian, big endian *)
	BEGIN
		IF Kernel.littleEndian THEN
			le := SYSTEM.VAL(a, x)
		ELSE
			be := SYSTEM.VAL(a, x);
			le[0] := be[7]; le[1] := be[6]; le[2] := be[5]; le[3] := be[4];
			le[4] := be[3]; le[5] := be[2]; le[6] := be[1]; le[7] := be[0]
		END;
		wr.rider.WriteBytes(le, 0, 8)
	END WriteReal;

	PROCEDURE (VAR wr: Writer) WriteSet* (x: SET), NEW;
		(* SIZE(SET) = 4 *)
		TYPE a = ARRAY 4 OF BYTE;
		VAR le, be: a;	(* little endian, big endian *)
	BEGIN
		IF Kernel.littleEndian THEN
			le := SYSTEM.VAL(a, x)
		ELSE
			be := SYSTEM.VAL(a, x);
			le[0] := be[3]; le[1] := be[2]; le[2] := be[1]; le[3] := be[0]
		END;
		wr.rider.WriteBytes(le, 0, 4)
	END WriteSet;

	PROCEDURE (VAR wr: Writer) WriteSString* (IN x: ARRAY OF SHORTCHAR), NEW;
		VAR i: INTEGER; ch: SHORTCHAR;
	BEGIN
		i := 0; ch := x[0]; WHILE ch # 0X DO wr.WriteSChar(ch); INC(i); ch := x[i] END;
		wr.WriteSChar(0X)
	END WriteSString;

	PROCEDURE (VAR wr: Writer) WriteXString* (IN x: ARRAY OF CHAR), NEW;
		VAR i: INTEGER; ch: CHAR;
	BEGIN
		i := 0; ch := x[0]; WHILE ch # 0X DO wr.WriteXChar(ch); INC(i); ch := x[i] END;
		wr.WriteSChar(0X)
	END WriteXString;

	PROCEDURE (VAR wr: Writer) WriteString* (IN x: ARRAY OF CHAR), NEW;
		VAR i: INTEGER; ch: CHAR;
	BEGIN
		i := 0; ch := x[0]; WHILE ch # 0X DO wr.WriteChar(ch); INC(i); ch := x[i] END;
		wr.WriteChar(0X)
	END WriteString;

	PROCEDURE (VAR wr: Writer) WriteVersion* (version: INTEGER), NEW;
	BEGIN
		wr.WriteByte(SHORT(SHORT(version)))
	END WriteVersion;

	PROCEDURE (VAR wr: Writer) WriteStore* (x: Stores.Store), NEW;
		VAR pos0, swrPos0: LONGINT; swr: POINTER TO Stores.Writer; seg: Segment;
	BEGIN
		pos0 := wr.Pos();
		swr := wr.swr;
		IF swr = NIL THEN
			swrPos0 := 0;
			seg := NewSegment(wr.rider.Base(), pos0)(Segment);
			NEW(swr);
			swr.ConnectTo(seg);
			wr.swr := swr; wr.swrSeg := seg
		ELSE
			swrPos0 := pos0 - wr.swrSeg.org;
			IF swrPos0 < MAX(INTEGER) THEN
				swr.SetPos(SHORT(swrPos0))
			ELSE
				HALT(20) (* stores need more than MAX(INTEGER) bytes *)
			END
		END;
		swr.WriteStore(x);
		wr.SetPos(pos0 + (swr.Pos() - swrPos0));
		wr.writtenStore := x
	END WriteStore;

END Stores64.

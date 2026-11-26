MODULE CryptoASN1;

	(** AUTHOR "Patrick Hunziker"; PURPOSE "ISO 8824/8825 standard tag/length/data encoding"; *)

(* ASN1(Abstract Syntax Notation One) is a standard that defines a formalism for the specification of abstract data types.

PROCEDURE Decode(ASN1Stream):Triplet produces a hierarchical tree with triple.next and triplet.child dependencies.*)

(*to do: correctly decode big integer numbers which do not fit into standard oberon types*)
(*to do for more general ASN1 implementation: bit strings of undefined length: see ftp://ftp.rsa.com/pub/pkcs/ascii/layman.asc; not needed for certifcates*)
(*refine standard OIDs as hex constants, for direct comparison*)
(*to do: encoding *)


	IMPORT Streams := CryptoAosStreams, AO := CryptoAosCompat;

	CONST
		Trace = TRUE;
		ASN1Universal* = 0;
		ASN1Application* = 1;
		ASN1Context* = 2;
		ASN1Private* = 3;
		Boolean* = 1H;
		Integer* = 2H;
		BitString* = 3H;
		String* = 4H;
		Null* = 5H;
		Oid* = 6H;
		UTF8* = 0CH;
		PrintableString* = 13H;
		TeletexString* = 14H;
		IA5String* = 16H;
		UTCTime* = 17H;
		BMPString* = 1EH;
		Sequence* = 30H;
		Set* = 31H;

	TYPE
		CharString* = POINTER TO ARRAY OF SHORTCHAR;


	TYPE
		Triplet* = POINTER TO EXTENSIBLE RECORD
			tag* : AO.SIGNED32;
			class* : AO.SIGNED32;
			length* : AO.SIGNED32;
			unusedbits* : AO.SIGNED32;
			constructed* : BOOLEAN;
			definite* : BOOLEAN;
			bvalue* : BOOLEAN;
			ivalue* : AO.SIGNED32;
			svalue* : POINTER TO ARRAY OF SHORTCHAR;
			child*, curchild* : Triplet;
			next* : Triplet;
			level* : AO.SIGNED32;
			(* TRUE, пока разбор не встретил ни одного нарушения:
			   обрезанные длины, неподдерживаемые формы, нехватка байт. *)
			ok* : BOOLEAN;
			(* Полная DER-запись элемента (заголовок и содержимое) для
			   проверки подписи сертификата. Заполняется, только если
			   поток поддерживает Pos/SetPos. *)
			raw* : POINTER TO ARRAY OF SHORTCHAR;
			rawLen* : AO.SIGNED32;
			start* : AO.SIGNED64
		END;


	VAR
		log: Streams.Writer;

	PROCEDURE (SELF: Triplet) AppendChild* (t : Triplet), NEW;

	BEGIN
		IF SELF.child = NIL THEN
			SELF.child := t;
			SELF.curchild := t
		ELSE
			SELF.curchild.next := t;
			SELF.curchild := t
		END
	END AppendChild;


	PROCEDURE PrintHexString* (w : Streams.Writer; IN s : ARRAY OF SHORTCHAR);
		VAR i: AO.SIZE; c: SHORTCHAR; hex : ARRAY 17 OF SHORTCHAR;
	BEGIN
		hex := "0123456789ABCDEF";
		FOR i := 0 TO LEN(s) - 1 DO
			c := s[i];
			w.Char(hex[ORD(c) DIV 16]);
			w.Char(hex[ORD(c) MOD 16]);
			w.Char(" ")
		END
	END PrintHexString;

	PROCEDURE PrintNumericString* (w : Streams.Writer; IN s : ARRAY OF SHORTCHAR);
		VAR i: AO.SIZE; c: SHORTCHAR;
	BEGIN
		w.String("  ");
		FOR i := 0 TO LEN(s) - 1 DO
			c := s[i];
			w.Int(ORD(c), 0);
			w.Char(".")
		END
	END PrintNumericString;

	PROCEDURE PrintString* (w : Streams.Writer; IN s : ARRAY OF SHORTCHAR);
		VAR i: AO.SIZE; c: SHORTCHAR;
	BEGIN
		FOR i := 0 TO LEN(s) - 1 DO
			c := s[i];
			IF (ORD(c) >= 20H) & (ORD(c) <= 7EH) THEN
				w.Char(c)
			ELSE
				w.Char(".")
			END
		END
	END PrintString;

	PROCEDURE (SELF: Triplet) Print* (w : Streams.Writer), NEW;
		VAR
			i: AO.SIGNED32;
	BEGIN
		w.Char(09X);
		FOR i := 0 TO SELF.level - 1 DO
			w.String("- ")
		END;
		w.Hex(SELF.tag, 4);
		w.String("H[");
		w.Int(SELF.length, 0);
		w.String("]");
		CASE SELF.tag OF
			Boolean :	       IF SELF.bvalue THEN
			w.String("true")
		ELSE
			w.String("false")
		END
		|       Integer :	       IF SELF.length <= 4 THEN
			w.Int(SELF.ivalue, 0)
		ELSE
			PrintHexString(w, SELF.svalue^)
		END
		|       Oid :	   PrintNumericString(w, SELF.svalue^);
		w.String("  ");
		PrintHexString(w, SELF.svalue^)
		|       PrintableString :	       PrintString(w, SELF.svalue^)
		|       UTCTime :	       PrintString(w, SELF.svalue^)
		|       BitString :	     w.Char("(");
		w.Int(SELF.unusedbits, 0);
		w.Char(")");
		IF SELF.svalue # NIL THEN
			PrintHexString(w, SELF.svalue^)
		END
		|       Sequence,Set :
		ELSE
			IF SELF.svalue # NIL THEN
				PrintHexString(w, SELF.svalue^)
			END
		END;
		w.Ln
	END Print;

	PROCEDURE Decode* (reader : Streams.Reader; level : AO.SIGNED32; VAR len : AO.SIZE): Triplet;
		(* Разбор DER без доверия к длинам: любое нарушение поднимает
		   t.ok := FALSE и останавливает чтение. Сетевые данные не могут
		   увести разбор за конец буфера или в бесконечный цикл. *)
		CONST
			MaxElementLength = 1024 * 1024;
		VAR
			t: Triplet;
			lengthbytes, consumed, bodyStart, i, got: AO.SIZE;
			c: SHORTCHAR;
			p0, p1: AO.SIGNED64;


		PROCEDURE Get (VAR ch : SHORTCHAR);
		BEGIN
			IF t.ok & (reader.Available() > 0) THEN
				reader.Char(ch);
				INC(consumed)
			ELSE
				t.ok := FALSE;
				ch := 0X
			END
		END Get;

		PROCEDURE GetBytes (VAR dst : ARRAY OF SHORTCHAR; ofs, count : AO.SIZE);
			VAR n : AO.SIZE;
		BEGIN
			IF t.ok & (count >= 0) & (count <= LEN(dst) - ofs) & (reader.Available() >= count) THEN
				n := 0;
				reader.Bytes(dst, ofs, count, n);
				INC(consumed, n);
				IF n # count THEN t.ok := FALSE END
			ELSE
				t.ok := FALSE
			END
		END GetBytes;

		PROCEDURE ReadChildren;
			VAR t1 : Triplet; len0 : AO.SIZE;
		BEGIN
			WHILE t.ok & (consumed - bodyStart < t.length) DO
				t1 := Decode(reader, level + 1, len0);
				INC(consumed, len0);
				IF ~t1.ok OR (consumed - bodyStart > t.length) THEN
					t.ok := FALSE
				END;
				t.AppendChild(t1)
			END;
			IF consumed - bodyStart # t.length THEN t.ok := FALSE END
		END ReadChildren;

	BEGIN
		NEW(t);
		t.level := level;
		t.ok := TRUE;
		len := 0;
		consumed := 0;
		p0 := reader.Pos();
		t.start := p0;
		Get(c);
		IF t.ok THEN
			IF Trace & (log # NIL) THEN
				log.Char("{");
				log.Hex(ORD(c), 0);
				log.Char("H");
				log.Char("}")
			END;
			t.class := ORD(c) DIV 64;
			t.constructed := ODD(ORD(c) DIV 32);
			IF ORD(c) MOD 32 # 31 THEN
				t.tag := ORD(c) MOD 64
			ELSE
				REPEAT
					Get(c);
					IF t.ok THEN
						t.tag := t.tag * 128 + ORD(c) MOD 128
					END
				UNTIL ~t.ok OR (ORD(c) DIV 128 # 1)
			END;
			Get(c);
			IF t.ok THEN
				IF ORD(c) < 128 THEN
					t.length := ORD(c);
					t.definite := TRUE
				ELSIF ORD(c) = 128 THEN
					t.definite := FALSE;
					t.ok := FALSE	(* indefinite length в DER запрещена *)
				ELSE
					lengthbytes := ORD(c) MOD 128;
					t.definite := TRUE;
					IF lengthbytes > 4 THEN
						t.ok := FALSE
					ELSE
						FOR i := 0 TO lengthbytes - 1 DO
							t.length := 256 * t.length;
							Get(c);
							IF t.ok THEN
								t.length := t.length + ORD(c)
							END
						END
					END
				END;
				IF t.ok & (t.length > MaxElementLength) THEN
					t.ok := FALSE
				END
			END
		END;
		IF t.ok THEN
			IF t.class = ASN1Context THEN
				(* Контекстные теги [n]: EXPLICIT/IMPLICIT. Различаем по
				   constructed, а не по номеру тега: [3] расширений и [2]
				   dNSName имеют тот же номер, что и BIT STRING. *)
				IF t.constructed THEN
					bodyStart := consumed;
					ReadChildren
				ELSE
					IF t.length > 0 THEN
						NEW(t.svalue, t.length);
						GetBytes(t.svalue^, 0, t.length)
					END
				END
			ELSE
				CASE t.tag OF
					Boolean :	   IF t.length # 1 THEN
						t.ok := FALSE
					ELSE
						Get(c);
						t.bvalue := c # 0X
					END
				|       Integer :	   IF t.length > 0 THEN
						NEW(t.svalue, t.length);
						FOR i := 0 TO t.length - 1 DO
							Get(c);
							IF t.ok THEN
								t.svalue[i] := c;
								IF t.length <= 4 THEN
									t.ivalue := 256 * t.ivalue + ORD(c)
								END
							END
						END
					END
				|       BitString :	     IF t.length = 0 THEN
						t.ok := FALSE	(* нулевая длина BIT STRING недопустима *)
					ELSE
						Get(c);
						IF t.ok THEN
							t.unusedbits := ORD(c);
							IF t.length > 0 THEN
								NEW(t.svalue, t.length - 1);
								GetBytes(t.svalue^, 0, t.length - 1)
							END
						END
					END
				|       Set, Sequence :    bodyStart := consumed;
					ReadChildren
				ELSE
					IF t.length > 0 THEN
						NEW(t.svalue, t.length);
						GetBytes(t.svalue^, 0, t.length)
					END
				END
			END
		END;
		IF t.ok THEN
			p1 := reader.Pos();
			IF (p0 >= 0) & (p1 > p0) & reader.CanSetPos() THEN
				IF p1 - p0 <= MaxElementLength THEN
					i := SHORT(p1 - p0);
					NEW(t.raw, i);
					reader.SetPos(p0);
					got := 0;
					reader.Bytes(t.raw^, 0, i, got);
					reader.SetPos(p1);
					IF got = i THEN
						t.rawLen := got
					ELSE
						t.ok := FALSE
					END
				END
			END
		END;
		len := consumed;
		IF Trace & (log # NIL) THEN
			CASE t.tag OF
				Integer :		       log.String("(Integer  ");
			log.Int(len, 4);
			log.Char(")");
			t.Print(log)
			|       BitString :		     IF t.constructed THEN
				log.String("(constrBitString ")
			ELSE
				log.String("(BitString ")
			END;
			log.Int(len, 4);
			log.Char(")");
			t.Print(log)
			|       String :			log.String("(String   ");
			log.Int(len, 4);
			log.Char(")");
			t.Print(log)
			|       Null :		  log.String("(Null     ");
			log.Int(len, 4);
			log.Char(")");
			t.Print(log)
			|       Oid :		   log.String("(OID");
			log.Int(len, 4);
			log.Char(")");
			t.Print(log)
			|       PrintableString :		       log.String("(Printable");
			log.Int(len, 4);
			log.Char(")");
			t.Print(log)
			|       UTCTime :		       log.String("(UTCTime");
			log.Int(len, 4);
			log.Char(")");
			t.Print(log)
			|       Set :
			|       Sequence :
			ELSE
				log.String("(t.tag=");
				log.Hex(t.tag, 4);
				log.Int(len, 4);
				log.Char(")");
				t.Print(log)
			END
		END;
		RETURN t
	END Decode;

BEGIN


END CryptoASN1.

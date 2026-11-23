MODULE StdRastersPng;
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch blackbox.oberon.org"
	contributors	= "Oberon microsystems, Ketmar Dark"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT SYSTEM, Files, Ports, StdInflate, StdRasters, Log;

	CONST
		Crc32Init = -1;
		defDPI = 96;

	TYPE
		ImageLoader* = POINTER TO RECORD (StdRasters.ImageLoader) END;
		
		FilesOffset = INTEGER;

		IDatReader = POINTER TO RECORD (StdInflate.Inflater)
			rd: Files.Reader;
			cleft: INTEGER;
			crc: INTEGER;
			nomore: BOOLEAN;
			crcIsWrong: BOOLEAN
		END;
		
		LineArrayPtr = POINTER TO ARRAY OF SHORTCHAR;

	VAR
		DEBUG_PNG_LOADER*: BOOLEAN;
		DEBUG_PNG_DEPACKER*: BOOLEAN;
		crc32Table: ARRAY 16 OF SET;

	PROCEDURE ColorsRGBA (r, g, b, a: INTEGER): INTEGER;
	BEGIN
		RETURN r + g * 100H + b * 10000H + a * 1000000H
	END ColorsRGBA;

	PROCEDURE ReadSChar (rd: Files.Reader; VAR c: SHORTCHAR);
		VAR x: BYTE;
	BEGIN rd.ReadByte(x); c := SHORT(CHR(x))
	END ReadSChar;

	PROCEDURE ReadSChars (rd: Files.Reader; VAR a: ARRAY OF SHORTCHAR; w, len: INTEGER);
		VAR x: BYTE;
	BEGIN
		ASSERT(w >= 0, 20);
		ASSERT(len > 0, 21);
		ASSERT(w + len <= LEN(a), 22);
		WHILE len > 0 DO
			rd.ReadByte(x);
			a[w] := SHORT(CHR(x));
			INC(w);
			DEC(len)
		END
	END ReadSChars;

	(******** crc32 ********)

	PROCEDURE Crc32Final (crc32: INTEGER): INTEGER;
	BEGIN RETURN ORD(-BITS(crc32))
	END Crc32Final;

	PROCEDURE Crc32SChar (VAR crc32: INTEGER; b: SHORTCHAR);
	VAR crc: INTEGER;
	BEGIN
		crc := crc32;
		crc := ORD(BITS(crc) / BITS(ORD(b)));
		crc := ORD(crc32Table[crc MOD 10H] / (BITS(ASH(crc, -4)) * {0..27}));
		crc := ORD(crc32Table[crc MOD 10H] / (BITS(ASH(crc, -4)) * {0..27}));
		crc32 := crc
	END Crc32SChar;

	PROCEDURE Crc32SChars (VAR crc32: INTEGER; IN arr: ARRAY OF SHORTCHAR; beg, len: INTEGER);
	VAR crc, b: INTEGER;
	BEGIN
		(* k8: don't bother
		ASSERT(beg >= 0, 20); ASSERT(len >= 0, 21); ASSERT(beg <= LEN(arr), 22); ASSERT(LEN(arr) - beg >= len, 23);
		*)
		crc := crc32;
		INC(len, beg);
		WHILE beg < len DO
			b := ORD(arr[beg]); INC(beg);
			crc := ORD(BITS(crc) / BITS(b));
			crc := ORD(crc32Table[crc MOD 10H] / (BITS(ASH(crc, -4)) * {0..27}));
			crc := ORD(crc32Table[crc MOD 10H] / (BITS(ASH(crc, -4)) * {0..27}))
		END;
		crc32 := crc
	END Crc32SChars;


	(******** big-endian integer readers ********)

	PROCEDURE ReadByteBE (rd: Files.Reader; VAR crc: INTEGER; OUT x: INTEGER);
	VAR bt: SHORTCHAR;
	BEGIN
		ReadSChar(rd, bt); Crc32SChar(crc, bt);
		x := ORD(bt)
	END ReadByteBE;

	PROCEDURE ReadWordBE (rd: Files.Reader; VAR crc: INTEGER; OUT x: INTEGER);
	VAR bt: ARRAY 2 OF SHORTCHAR;
	BEGIN
		ReadSChars(rd, bt, 0, 2); Crc32SChars(crc, bt, 0, 2);
		x := ASH(ORD(bt[0]), 8) + ORD(bt[1])
	END ReadWordBE;

	PROCEDURE ReadIntBE (rd: Files.Reader; VAR crc: INTEGER; OUT x: INTEGER);
	VAR bt: ARRAY 4 OF SHORTCHAR;
	BEGIN
		ReadSChars(rd, bt, 0, 4); Crc32SChars(crc, bt, 0, 4);
		x := ORD(BITS(ASH(ORD(bt[0]), 24)) +
						BITS(ASH(ORD(bt[1]), 16)) +
						BITS(ASH(ORD(bt[2]), 8)) +
						BITS(ORD(bt[3])))
	END ReadIntBE;


	(******** interlace tables ********)

	(* 3, 3, 2, 2, 1, 1, 0, 0 *)
	PROCEDURE WShift (pass: INTEGER): INTEGER;
	BEGIN ASSERT((pass >= 0) & (pass <= 7), 20); RETURN (7 - pass) DIV 2
	END WShift;

	(* 3, 3, 3, 2, 2, 1, 1, 0 *)
	PROCEDURE HShift (pass: INTEGER): INTEGER;
	BEGIN ASSERT((pass >= 0) & (pass <= 7), 20); RETURN MIN(3, (8 - pass) DIV 2)
	END HShift;

	(* 0, 0, 4, 0, 2, 0, 1, 0 *)
	PROCEDURE RowOfs (pass: INTEGER): INTEGER;
	BEGIN ASSERT((pass >= 0) & (pass <= 7), 20); RETURN ASH(ASH(1, (8 - pass) DIV 2), -1) MOD 8 * ((pass + 1) MOD 2)
	END RowOfs;

	(* 0, 4, 0, 2, 0, 1, 0, 0 *)
	PROCEDURE ColOfs (pass: INTEGER): INTEGER;
	BEGIN ASSERT((pass >= 0) & (pass <= 7), 20); RETURN ASH(ASH(1, (7 - pass) DIV 2), -1) * (pass MOD 2)
	END ColOfs;


	(******** idat byte stream ********)

	(* read as much as you can, return number of read bytes *)
	PROCEDURE (inf: IDatReader) ReadBuf- (VAR buf: ARRAY OF SHORTCHAR): INTEGER;
	VAR
		pos, left, rd, filecrc: INTEGER;
		sign: ARRAY 5 OF SHORTCHAR;
	BEGIN pos := 0; left := LEN(buf);
		WHILE ~inf.nomore & (left # 0) DO
			rd := MIN(inf.cleft, left);
			IF rd # 0 THEN
				ReadSChars(inf.rd, buf, pos, rd);
				IF inf.rd.eof THEN inf.nomore := TRUE; pos := 0
					; IF DEBUG_PNG_LOADER THEN Log.String("  WRONG!"); Log.Ln END
				ELSE
					Crc32SChars(inf.crc, buf, pos, rd);
					DEC(inf.cleft, rd); INC(pos, rd); DEC(left, rd)
				END
			ELSE	(* want a new idat chunk *)
				ReadIntBE(inf.rd, rd, filecrc);	(* CRC *)
				IF filecrc # Crc32Final(inf.crc) THEN
					IF DEBUG_PNG_LOADER THEN Log.String("  CRC IS WRONG!"); Log.Ln END;
					inf.nomore := TRUE; inf.crcIsWrong := TRUE
				ELSE
					ReadIntBE(inf.rd, rd, inf.cleft);	(* chunk size *)
					ReadSChars(inf.rd, sign, 0, 4); sign[4] := 0X;	(* chunk signature *)
					inf.crc := Crc32Init; Crc32SChars(inf.crc, sign, 0, 4);
					inf.nomore := inf.rd.eof OR (sign # "IDAT")
				END
			END
		END;
		RETURN pos
	END ReadBuf;

	PROCEDURE Round16To8 (v: INTEGER): INTEGER;
	VAR mv: INTEGER;
	BEGIN
		IF (v < 0) OR (v > 65535) THEN v := 1
		ELSE
			mv := v MOD 256; v := v DIV 256;
			IF v # 255 THEN INC(v, mv DIV 128) END
		END;
		RETURN v
	END Round16To8;


	(******** main decoder ********)

	PROCEDURE LoadPng* (rd: Files.Reader): StdRasters.Image;
	VAR
		sign: ARRAY 9 OF SHORTCHAR;
		seenidat: BOOLEAN;
		csize: INTEGER;
		ncofs: FilesOffset;
		f, width, height: INTEGER;
		img: StdRasters.Image;
		tR, tG, tB: INTEGER;
		compression, filter, bitdepth: INTEGER;
		colortype, interlace: INTEGER;
		seenplte: BOOLEAN;
		plte: ARRAY 256 OF Ports.Color;
		trns: ARRAY 256 OF INTEGER;
		sr, sg, sb: SHORTCHAR;
		dpiX, dpiY: INTEGER;
		gamma: INTEGER;
		idat: IDatReader;

		crc: INTEGER;
		checkCRC: BOOLEAN;

		PROCEDURE DecodeImage;
		VAR
			prevLine, currLine: LineArrayPtr;
			bytesPerPixel, bytesPerFullRow, bytesPerRowPass: INTEGER;
			pass: INTEGER;	(* current deinterlacing pass *)
			passwdt: INTEGER;	(* width of the current pass *)
			passxofs: INTEGER;	(* starting x offset (in pixels) for the start of each line in the destination buffer *)
			passyofs: INTEGER;	(* starting y offset (in pixels) for the first line in the destination buffer *)
			passxstep: INTEGER;	(* x step in the destination buffer, in pixels *)
			passystep: INTEGER;	(* y step in the destination buffer, in pixels *)
			curxofs, curxstep, curxval: INTEGER;

			rcount: INTEGER;
			curx, cury: INTEGER;
			clr: Ports.Color;
			dpos: INTEGER;
			tmpline: LineArrayPtr;

			(* get one pixel value; no converstion *)
			PROCEDURE GetPixelData (): INTEGER;
			VAR res: INTEGER;
			BEGIN
				CASE bitdepth OF
				| 1:
					IF curxstep = 8 THEN INC(curxofs); curxstep := 0; curxval := ORD(currLine[curxofs])
					ELSE INC(curxstep)
					END;
					res := curxval DIV 128; ASSERT((res = 0) OR (res = 1), 100);
					curxval := curxval * 2 MOD 256;
				| 2:
					IF curxstep = 4 THEN INC(curxofs); curxstep := 0; curxval := ORD(currLine[curxofs])
					ELSE INC(curxstep)
					END;
					res := curxval DIV 64; ASSERT((res >= 0) & (res <= 3), 100);
					curxval := curxval * 4 MOD 256;
				| 4:
					IF curxstep = 1 THEN INC(curxofs); curxstep := 0; curxval := ORD(currLine[curxofs])
					ELSE INC(curxstep)
					END;
					res := curxval DIV 16; ASSERT((res >= 0) & (res <= 15), 100);
					curxval := curxval * 16 MOD 256;
				| 8: res := ORD(currLine[curxofs]); INC(curxofs)
				| 16: res := ORD(currLine[curxofs]) * 256 + ORD(currLine[curxofs + 1]); INC(curxofs, 2)
				END;
				RETURN res
			END GetPixelData;

			PROCEDURE GetPixelValue (): Ports.Color;
			VAR
				pR, pG, pB, pA: INTEGER;
				res: Ports.Color;
			BEGIN
				CASE colortype OF
				| 0:	(* grayscale *)
					pR := GetPixelData();
					IF pR = tR THEN pA := 0 ELSE pA := 255 END;
					CASE bitdepth OF
					| 1: pR := pR * 255
					| 2:
						CASE pR OF
						| 0: (* ok *)
						| 1: pR := 055H
						| 2: pR := 0AAH
						| 3: pR := 0FFH
						END
					| 4: pR := pR * 16 + pR
					| 8: (* ok *)
					| 16: pR := Round16To8(pR)
					END;
					res := ColorsRGBA(pR, pR, pR, pA)
				| 2:	(* rgb *)
					pR := GetPixelData(); pG := GetPixelData(); pB := GetPixelData();
					IF (pR = tR) & (pG = tG) & (pB = tB) THEN pA := 0 ELSE pA := 255 END;
					IF bitdepth = 16 THEN
						pR := Round16To8(pR); pG := Round16To8(pG); pB := Round16To8(pB)
					END;
					res := ColorsRGBA(pR, pG, pB, pA)
				| 3:	(* indexed *)
					pR := GetPixelData();
					res := ORD(BITS(plte[pR]) + BITS(ASH(trns[pR], 24)))
				| 4:	(* grayscale with alpha *)
					pR := GetPixelData(); pA := GetPixelData();
					IF bitdepth = 16 THEN
						pR := Round16To8(pR); pA := Round16To8(pA)
					END;
					res := ColorsRGBA(pR, pR, pR, pA)
				| 6:	(* rgb with alpha *)
					pR := GetPixelData(); pG := GetPixelData(); pB := GetPixelData(); pA := GetPixelData();
					IF bitdepth = 16 THEN
						pR := Round16To8(pR); pG := Round16To8(pG); pB := Round16To8(pB); pA := Round16To8(pA)
					END;
					res := ColorsRGBA(pR, pG, pB, pA)
				END;
				RETURN res
			END GetPixelValue;

			PROCEDURE SetupPass;
			VAR rowofs, colofs: INTEGER;
			BEGIN
				DEC(pass);
				REPEAT
					INC(pass);
					rowofs := RowOfs(pass); colofs := ColOfs(pass);
				UNTIL (rowofs < height) & (colofs < width) OR (pass = 7);
				IF (pass = 7) & (interlace # 0) THEN pass := 69
				ELSE
					passwdt := WShift(pass);
					passwdt := ASH(width + ASH(1, passwdt) - 1 - colofs, -passwdt);
					bytesPerRowPass := ((passwdt * bytesPerPixel) * bitdepth + 8 - 1) DIV 8;
					passxofs := colofs;
					passyofs := rowofs;
					passxstep := ASH(1, WShift(pass));
					passystep := ASH(1, HShift(pass));
					(*curr = buffer+rowoffset*pitch+coloffset*bytesPerPixel;*)
					(*passpitch = pitch<<passheightshift[pass];*)
					IF DEBUG_PNG_LOADER THEN
						Log.String("pass:"); Log.Int(pass); Log.Ln;
						Log.String("passwdt:"); Log.Int(passwdt); Log.Ln;
						Log.String("passxofs:"); Log.Int(passxofs); Log.Ln;
						Log.String("passyofs:"); Log.Int(passyofs); Log.Ln;
						Log.String("passxstep:"); Log.Int(passxstep); Log.Ln;
						Log.String("passystep:"); Log.Int(passystep); Log.Ln;
						Log.String("bytesPerRowPass:"); Log.Int(bytesPerRowPass); Log.Ln
					END;
				END;
			END SetupPass;

			PROCEDURE UnfilterLineSub;
			VAR
				line: LineArrayPtr;
				bpp: INTEGER;
				idx: INTEGER;
			BEGIN
				line := currLine; bpp := bytesPerPixel;
				IF bitdepth = 16 THEN INC(bpp, bpp) END;
				FOR idx := 16 TO bytesPerRowPass + 16 - 1 DO
					line[idx] := SHORT(CHR((ORD(line[idx]) + ORD(line[idx - bpp])) MOD 256))
				END;
			END UnfilterLineSub;

			PROCEDURE UnfilterLineUp;
			VAR
				line, prev: LineArrayPtr;
				idx: INTEGER;
			BEGIN
				line := currLine; prev := prevLine;
				FOR idx := 16 TO bytesPerRowPass + 16 - 1 DO
					line[idx] := SHORT(CHR((ORD(line[idx]) + ORD(prev[idx])) MOD 256))
				END;
			END UnfilterLineUp;

			PROCEDURE UnfilterLineAverage;
			VAR
				line, prev: LineArrayPtr;
				bpp: INTEGER;
				idx: INTEGER;
			BEGIN
				line := currLine; prev := prevLine; bpp := bytesPerPixel;
				IF bitdepth = 16 THEN INC(bpp, bpp) END;
				FOR idx := 16 TO bytesPerRowPass + 16 - 1 DO
					line[idx] := SHORT(CHR((
														ORD(line[idx]) + (ORD(line[idx - bpp]) + ORD(prev[idx])) DIV 2
													) MOD 256))
				END;
			END UnfilterLineAverage;

			PROCEDURE Paeth (a, b, c: INTEGER): INTEGER;
			VAR p, pa, pb, pc: INTEGER;
			BEGIN
				p := a + b - c;
				pa := ABS(p - a); pb := ABS(p - b); pc := ABS(p - c);
				IF (pa <= pb) & (pa <= pc) THEN p := a
				ELSIF pb <= pc THEN p := b
				ELSE p := c
				END;
				RETURN p
			END Paeth;

			PROCEDURE UnfilterLinePaeth;
			VAR
				line, prev: LineArrayPtr;
				bpp: INTEGER;
				idx: INTEGER;
			BEGIN
				line := currLine; prev := prevLine; bpp := bytesPerPixel;
				IF bitdepth = 16 THEN INC(bpp, bpp) END;
				FOR idx := 16 TO bytesPerRowPass + 16 - 1 DO
					line[idx] := SHORT(CHR((
														ORD(line[idx]) + Paeth(ORD(line[idx - bpp]), ORD(prev[idx]), ORD(prev[idx - bpp]))
													) MOD 256))
				END;
			END UnfilterLinePaeth;

		BEGIN
			IF DEBUG_PNG_LOADER THEN Log.String("==== ==== ===="); Log.Ln END;
			CASE colortype OF
			| 2: bytesPerPixel := 3	(* rgb *)
			| 4: bytesPerPixel := 2	(* grayscale, alpha *)
			| 6: bytesPerPixel := 4	(* rgba *)
			ELSE bytesPerPixel := 1
			END;
			(*bytesPerFullRow := ((UP.width * bytesPerPixel) * UP.bitdepth + UP.bitdepth - 1) DIV UP.bitdepth;*)
			bytesPerFullRow := ((width * bytesPerPixel) * bitdepth + 8 - 1) DIV 8;
			IF DEBUG_PNG_LOADER THEN
				Log.String("bytesPerPixel:"); Log.Int(bytesPerPixel); Log.Ln;
				Log.String("bytesPerFullRow:"); Log.Int(bytesPerFullRow); Log.Ln
			END;
			NEW(prevLine, bytesPerFullRow + 32);
			NEW(currLine, bytesPerFullRow + 32);
			(*bytesPerRowOut := UP.width * bytesPerPixel;*)
			IF interlace # 0 THEN pass := 0 ELSE pass := 7 END;
			WHILE pass < 8 - interlace DO
				SetupPass;
				IF pass < 16 THEN
					(* clear first line *)
					FOR cury := 0 TO bytesPerRowPass + 31 - 1 DO currLine[cury] := 0X END;
					cury := passyofs;
					WHILE cury < height DO
						(* swap lines *)
						tmpline := currLine; currLine := prevLine; prevLine := tmpline;
						(* read filter *)
						rcount := idat.ReadSChars(currLine, 0, 1);
						IF (rcount # 1) OR idat.Error() OR idat.crcIsWrong THEN
							IF DEBUG_PNG_DEPACKER THEN
								Log.String("RF-ERR: tup:"); Log.Int(idat.totalUnpacked);
									Log.String("; rcount:"); Log.Int(rcount);
									Log.String("; bytesPerRowPass:"); Log.Int(bytesPerRowPass);
									Log.String("; idaterr:"); Log.Bool(idat.Error());
									Log.String("; crc is wrong:"); Log.Bool(idat.crcIsWrong);
									Log.Ln
							END;
							img := NIL;
							RETURN
						END;
						filter := ORD(currLine[0]);
						IF DEBUG_PNG_DEPACKER THEN
							Log.String("cury:"); Log.Int(cury); Log.String("; filter:"); Log.Int(filter); Log.Ln;
						END;
						IF filter > 4 THEN
							IF DEBUG_PNG_DEPACKER THEN
								Log.String("FL-ERR: tup:"); Log.Int(idat.totalUnpacked);
									Log.String("; rcount:"); Log.Int(rcount);
									Log.String("; bytesPerRowPass:"); Log.Int(bytesPerRowPass);
									Log.String("; idaterr:"); Log.Bool(idat.Error());
									Log.String("; crc is wrong:"); Log.Bool(idat.crcIsWrong);
									Log.Ln
							END;
							img := NIL;
							RETURN
						END;
						(* read line data *)
						currLine[0] := 0X;
						rcount := idat.ReadSChars(currLine, 16, bytesPerRowPass);
						IF (rcount # bytesPerRowPass) OR idat.Error() OR idat.crcIsWrong THEN
							IF DEBUG_PNG_DEPACKER THEN
								Log.String("RP-ERR: tup:"); Log.Int(idat.totalUnpacked);
									Log.String("; rcount:"); Log.Int(rcount);
									Log.String("; bytesPerRowPass:"); Log.Int(bytesPerRowPass);
									Log.String("; idaterr:"); Log.Bool(idat.Error());
									Log.String("; crc is wrong:"); Log.Bool(idat.crcIsWrong);
									Log.Ln
							END;
							img := NIL;
							RETURN
						END;
						(* restore original line data *)
						CASE filter OF
						| 0:	(* do nothing *)
						| 1: UnfilterLineSub
						| 2: UnfilterLineUp
						| 3: UnfilterLineAverage
						| 4: UnfilterLinePaeth
						END;
						(* unpack line data *)
						curxofs := 16; curxstep := 0; curxval := ORD(currLine[curxofs]);
						curx := 0; dpos := cury * width + passxofs;
						WHILE curx < passwdt DO
							clr := GetPixelValue();
							img.data[dpos] := clr;
							INC(dpos, passxstep);
							INC(curx)
						END;
						(* next line *)
						INC(cury, passystep)
					END;
					INC(pass)
				END
			END;
			IF DEBUG_PNG_DEPACKER THEN
				Log.String("DONE: err="); Log.Bool(idat.Error()); Log.Ln;
			END;
			IF ~idat.Eof() THEN
				(* this reads 0 bytes, but necessary to check the final Adler32 checksum of ZLib stream *)
				rcount := idat.ReadSChars(currLine, 0, 1);
				IF DEBUG_PNG_DEPACKER THEN
					Log.String("DONE-EOF: err="); Log.Bool(idat.Error()); Log.Ln;
				END;
				IF idat.Error() OR idat.crcIsWrong THEN img := NIL END
			END
		END DecodeImage;

		PROCEDURE FinishChunk (ncofs: FilesOffset; crc: INTEGER; check: BOOLEAN): BOOLEAN;
		VAR
			left: FilesOffset;
			b, filecrc: INTEGER;
		BEGIN
			IF rd.eof OR (ncofs < 0) OR (ncofs >= rd.Base().Length()) THEN RETURN FALSE
			ELSIF ~check THEN
				IF rd.Base().Length() - ncofs < 12 THEN RETURN FALSE
				ELSE rd.SetPos(ncofs + 4); RETURN TRUE
				END
			ELSE
				left := ncofs - rd.Pos();
				IF left < 0 THEN RETURN FALSE
				ELSE
					WHILE left # 0 DO ReadByteBE(rd, crc, b); DEC(left) END;
					ReadIntBE(rd, b, filecrc);
					RETURN ~rd.eof & (Crc32Final(crc) = filecrc)
				END
			END
		END FinishChunk;

	BEGIN
		(*rd.SetPos(0);*)
		IF rd.Base().Length() - rd.Pos() < 16 THEN RETURN NIL END;
		ReadSChars(rd, sign, 0, 8); sign[8] := 0X;
		IF sign # 89X+"PNG"+0DX+0AX+1AX+0AX THEN RETURN NIL END;
		(* first chunk must be IHDR *)
		ReadIntBE(rd, crc, csize); ReadSChars(rd, sign, 0, 4); sign[4] := 0X;
		IF rd.eof OR (sign # "IHDR") OR (csize # 13) THEN RETURN NIL END;
		crc := Crc32Init; Crc32SChars(crc, sign, 0, 4);
		ncofs := rd.Pos() + csize;	(* skip CRC *)
		(* parse IHDR chunk *)
		ReadIntBE(rd, crc, width); ReadIntBE(rd, crc, height);
		IF DEBUG_PNG_LOADER THEN Log.String("PNG: width:"); Log.Int(width); Log.String("; height:"); Log.Int(height); Log.Ln END;
		IF (width < 1) OR (width > 32768) OR (height < 1) OR (height > 32768) THEN RETURN NIL END;
		NEW(img); img.width := width; img.height := height;
		ReadByteBE(rd, crc, bitdepth);
		ReadByteBE(rd, crc, colortype);
		ReadByteBE(rd, crc, compression);
		ReadByteBE(rd, crc, filter);
		ReadByteBE(rd, crc, interlace);
		IF rd.eof THEN RETURN NIL END;
		IF DEBUG_PNG_LOADER THEN
			Log.String("  bitdepth:"); Log.Int(bitdepth); Log.Ln;
			Log.String("  colortype:"); Log.Int(colortype); Log.Ln;
			Log.String("  compression:"); Log.Int(compression); Log.Ln;
			Log.String("  filter:"); Log.Int(filter); Log.Ln;
			Log.String("  interlace:"); Log.Int(interlace); Log.Ln
		END;
		IF (compression # 0) OR (filter # 0) OR (interlace > 1) THEN RETURN NIL END;
		CASE colortype OF
		| 0:	(* grayscale *)
			CASE bitdepth OF
			| 1, 2, 4, 8, 16:	(* ok *)
			ELSE RETURN NIL
			END;
		| 2:	(* rgb *)
			CASE bitdepth OF
			| 8, 16:	(* ok *)
			ELSE RETURN NIL
			END;
		| 3:	(* indexed *)
			CASE bitdepth OF
			| 1, 2, 4, 8:	(* ok *)
			ELSE RETURN NIL
			END;
		| 4:	(* grayscale with alpha *)
			CASE bitdepth OF
			| 8, 16:	(* ok *)
			ELSE RETURN NIL
			END;
		| 6:	(* rgb with alpha *)
			CASE bitdepth OF
			| 8, 16:	(* ok *)
			ELSE RETURN NIL
			END;
		ELSE RETURN NIL
		END;
		seenidat := FALSE; seenplte := FALSE; tR := -1; dpiX := defDPI; dpiY := defDPI; gamma := 0;
		FOR f := 0 TO 255 DO trns[f] := 255 END;	(* default is fully opaque *)
		checkCRC := TRUE;
		WHILE ~rd.eof & ~seenidat DO
			IF rd.Base().Length() - ncofs <= 8 THEN RETURN NIL END;
			IF ~FinishChunk(ncofs, crc, checkCRC) THEN RETURN NIL END;
			ReadIntBE(rd, crc, csize);
			checkCRC := FALSE; crc := Crc32Init;
			ReadSChars(rd, sign, 0, 4);
			IF csize < 0 THEN RETURN NIL END;
			Crc32SChars(crc, sign, 0, 4);
			ncofs := rd.Pos() + csize;
			IF DEBUG_PNG_LOADER THEN Log.String(" CHUNK: "); Log.String(sign$); Log.String(" : size:"); Log.Int(csize); Log.Ln END;
			IF sign = "PLTE" THEN
				checkCRC := TRUE;
				IF seenplte THEN RETURN NIL END;	(* no more than one such chunk allowed *)
				IF (csize < 0) OR (csize MOD 3 # 0) THEN RETURN NIL END;	(* invalid size *)
				seenplte := TRUE;
				IF colortype = 3 THEN	(* we want chunk contents *)
					FOR f := 0 TO 255 DO plte[f] := -1 END;
					f := 0; WHILE (f # 256) & (csize # 0) DO
						ReadSChar(rd, sr); Crc32SChar(crc, sr);
						ReadSChar(rd, sg); Crc32SChar(crc, sg);
						ReadSChar(rd, sb); Crc32SChar(crc, sb);
						plte[f] := Ports.RGBColor(ORD(sr), ORD(sg), ORD(sb)); INC(f);
						DEC(csize, 3)
					END
				END
			ELSIF sign = "tRNS" THEN
				checkCRC := TRUE;
				CASE colortype OF
				| 0:	(* grayscale, 16-bit value *)
					IF csize # 2 THEN RETURN NIL END;
					ReadWordBE(rd, crc, tR);
					CASE bitdepth OF
					| 1: IF tR > 1 THEN tR := -1 ELSIF tR = 1 THEN tR := 255 END
					| 2:
						CASE tR OF
						| 0: (* ok *)
						| 1: tR := 055H
						| 2: tR := 0AAH
						| 3: tR := 0FFH
						ELSE tR := -1
						END
					| 4: IF tR > 15 THEN tR := -1 ELSE tR := tR * 16 + tR END
					| 8: IF tR > 255 THEN tR := -1 END
					ELSE END;
				| 2:	(* rgb, 3 16-bit values *)
					IF csize # 2 * 3 THEN RETURN NIL END;
					ReadWordBE(rd, crc, tR); ReadWordBE(rd, crc, tG); ReadWordBE(rd, crc, tB);
					IF bitdepth = 8 THEN
						IF (tR > 255) OR (tG > 255) OR (tB > 255) THEN tR := 1 END
					ELSE ASSERT(bitdepth = 16)
					END
				| 3:	(* byte table *)
					IF csize < 0 THEN RETURN NIL END;
					f := 0; WHILE (f # 256) & (csize # 0) DO
						ReadSChar(rd, sr); Crc32SChar(crc, sr);
						trns[f] := ORD(sr); INC(f);
						DEC(csize)
					END;
				END;
			ELSIF sign = "gAMA" THEN
				checkCRC := TRUE;
				IF csize >= 4 THEN
					ReadIntBE(rd, crc, gamma);
					IF DEBUG_PNG_LOADER THEN
						Log.String("  gamma:"); Log.Int(gamma DIV 100000); Log.String("."); Log.IntForm(gamma MOD 100000, 10, 0, '0', FALSE); Log.Ln;
					END;
					IF (gamma < 0) OR (gamma > 4 * 100000) THEN gamma := 0 END
				END
			ELSIF sign = "pHYs" THEN
				checkCRC := TRUE;
				IF csize >= 9 THEN
					ReadIntBE(rd, crc, dpiX); ReadIntBE(rd, crc, dpiY);
					ReadSChar(rd, sr); Crc32SChar(crc, sr);
					IF DEBUG_PNG_LOADER THEN
						Log.String("  dpiX:"); Log.Int(dpiX); Log.String("; dpiY"); Log.Int(dpiY); Log.String("; units:"); Log.Int(ORD(sr)); Log.Ln;
					END;
					IF sr = 01X THEN
						dpiX := MAX(0, SHORT(ENTIER(dpiX * 0.0254 + 0.5)));
						dpiY := MAX(0, SHORT(ENTIER(dpiY * 0.0254 + 0.5)));
						IF (dpiX = 0) OR (dpiY = 0) OR (dpiX > 8192) OR (dpiY > 8192) THEN
							dpiX := 0; dpiY := 0
						END;
					ELSE
						dpiX := 0; dpiY := 0
					END;
					IF DEBUG_PNG_LOADER THEN
						Log.String("  fin: dpiX:"); Log.Int(dpiX); Log.String("; dpiY"); Log.Int(dpiY); Log.Ln;
					END;
				END
			ELSIF sign = "IEND" THEN RETURN NIL
			ELSIF sign = "IDAT" THEN seenidat := TRUE
			END
		END;
		IF ~seenidat OR rd.eof THEN RETURN NIL END;
		IF (colortype = 3) & ~seenplte THEN RETURN NIL END;
		IF rd.Base().Length() - ncofs <= 8 THEN RETURN NIL END;
		NEW(img.data, width * height);
		img.width := width; img.height := height;
		img.dpiX := dpiX; img.dpiY := dpiY;
		NEW(idat);
		idat.rd := rd;
		idat.cleft := csize;
		idat.crc := crc;
		idat.nomore := FALSE;
		idat.Init(StdInflate.ZLib);
		DecodeImage;
		RETURN img
	END LoadPng;


	(* return Img.Image without data if the image is a PNG image, and sets rd position after the last image byte.  If there is an error, rd position is undefined. *)
	PROCEDURE ScanPng* (rd: Files.Reader): StdRasters.Image;
		VAR
			sign: ARRAY 9 OF SHORTCHAR;
			seenidat: BOOLEAN;
			csize: INTEGER;
			ncofs: FilesOffset;
			width, height: INTEGER;
			img: StdRasters.Image;
			compression, filter, bitdepth: INTEGER;
			colortype, interlace: INTEGER;
			seenplte: BOOLEAN;
			dpiX, dpiY: INTEGER;
			sr: SHORTCHAR;
			crc: INTEGER;
			checkCRC: BOOLEAN;

		PROCEDURE FinishChunk (ncofs: FilesOffset; crc: INTEGER; check: BOOLEAN): BOOLEAN;
		VAR
			left: FilesOffset;
			b, filecrc: INTEGER;
		BEGIN
			IF rd.eof OR (ncofs < 0) OR (ncofs >= rd.Base().Length()) THEN RETURN FALSE
			ELSIF ~check THEN
				IF rd.Base().Length() - ncofs < 12 THEN RETURN FALSE
				ELSE rd.SetPos(ncofs + 4); RETURN TRUE
				END
			ELSE
				left := ncofs - rd.Pos();
				IF left < 0 THEN RETURN FALSE
				ELSE
					WHILE left # 0 DO ReadByteBE(rd, crc, b); DEC(left) END;
					ReadIntBE(rd, b, filecrc);
					RETURN ~rd.eof & (Crc32Final(crc) = filecrc)
				END
			END
		END FinishChunk;

	BEGIN
		(*rd.SetPos(0);*)
		IF rd.Base().Length() - rd.Pos() < 16 THEN RETURN NIL END;
		ReadSChars(rd, sign, 0, 8); sign[8] := 0X;
		IF sign # 89X+"PNG"+0DX+0AX+1AX+0AX THEN RETURN NIL END;
		(* first chunk must be IHDR *)
		ReadIntBE(rd, crc, csize); ReadSChars(rd, sign, 0, 4); sign[4] := 0X;
		IF rd.eof OR (sign # "IHDR") OR (csize # 13) THEN RETURN NIL END;
		crc := Crc32Init; Crc32SChars(crc, sign, 0, 4);
		ncofs := rd.Pos() + csize;	(* skip CRC *)
		(* parse IHDR chunk *)
		ReadIntBE(rd, crc, width); ReadIntBE(rd, crc, height);
		IF DEBUG_PNG_LOADER THEN Log.String("PNG: width:"); Log.Int(width); Log.String("; height:"); Log.Int(height); Log.Ln END;
		IF (width < 1) OR (width > 32768) OR (height < 1) OR (height > 32768) THEN RETURN NIL END;
		NEW(img); img.width := width; img.height := height;
		ReadByteBE(rd, crc, bitdepth);
		ReadByteBE(rd, crc, colortype);
		ReadByteBE(rd, crc, compression);
		ReadByteBE(rd, crc, filter);
		ReadByteBE(rd, crc, interlace);
		IF rd.eof THEN RETURN NIL END;
		IF DEBUG_PNG_LOADER THEN
			Log.String("  bitdepth:"); Log.Int(bitdepth); Log.Ln;
			Log.String("  colortype:"); Log.Int(colortype); Log.Ln;
			Log.String("  compression:"); Log.Int(compression); Log.Ln;
			Log.String("  filter:"); Log.Int(filter); Log.Ln;
			Log.String("  interlace:"); Log.Int(interlace); Log.Ln
		END;
		IF (compression # 0) OR (filter # 0) OR (interlace > 1) THEN RETURN NIL END;
		CASE colortype OF
		| 0:	(* grayscale *)
			CASE bitdepth OF
			| 1, 2, 4, 8, 16:	(* ok *)
			ELSE RETURN NIL
			END;
		| 2:	(* rgb *)
			CASE bitdepth OF
			| 8, 16:	(* ok *)
			ELSE RETURN NIL
			END;
		| 3:	(* indexed *)
			CASE bitdepth OF
			| 1, 2, 4, 8:	(* ok *)
			ELSE RETURN NIL
			END;
		| 4:	(* grayscale with alpha *)
			CASE bitdepth OF
			| 8, 16:	(* ok *)
			ELSE RETURN NIL
			END;
		| 6:	(* rgb with alpha *)
			CASE bitdepth OF
			| 8, 16:	(* ok *)
			ELSE RETURN NIL
			END;
		ELSE RETURN NIL
		END;
		seenidat := FALSE; seenplte := FALSE; dpiX := 0; dpiY := 0;
		checkCRC := TRUE;
		WHILE ~rd.eof & ~seenidat DO
			IF rd.Base().Length() - ncofs <= 8 THEN RETURN NIL END;
			IF ~FinishChunk(ncofs, crc, checkCRC) THEN RETURN NIL END;
			ReadIntBE(rd, crc, csize);
			checkCRC := FALSE; crc := Crc32Init;
			ReadSChars(rd, sign, 0, 4);
			IF csize < 0 THEN RETURN NIL END;
			Crc32SChars(crc, sign, 0, 4);
			ncofs := rd.Pos() + csize;
			IF DEBUG_PNG_LOADER THEN Log.String(" CHUNK: "); Log.String(sign$); Log.String(" : size:"); Log.Int(csize); Log.Ln END;
			IF sign = "PLTE" THEN
				IF seenplte THEN RETURN NIL END;	(* no more than one such chunk allowed *)
				seenplte := TRUE;
			ELSIF sign = "pHYs" THEN
				checkCRC := TRUE;
				IF csize >= 9 THEN
					ReadIntBE(rd, crc, dpiX); ReadIntBE(rd, crc, dpiY);
					ReadSChar(rd, sr); Crc32SChar(crc, sr);
					IF DEBUG_PNG_LOADER THEN
						Log.String("  dpiX:"); Log.Int(dpiX); Log.String("; dpiY"); Log.Int(dpiY); Log.String("; units:"); Log.Int(ORD(sr)); Log.Ln;
					END;
					IF sr = 01X THEN
						dpiX := MAX(0, SHORT(ENTIER(dpiX * 0.0254 + 0.5)));
						dpiY := MAX(0, SHORT(ENTIER(dpiY * 0.0254 + 0.5)));
						IF (dpiX = 0) OR (dpiY = 0) OR (dpiX > 8192) OR (dpiY > 8192) THEN dpiX := 0; dpiY := 0 END;
					ELSE dpiX := 0; dpiY := 0
					END;
					IF DEBUG_PNG_LOADER THEN
						Log.String("  fin: dpiX:"); Log.Int(dpiX); Log.String("; dpiY"); Log.Int(dpiY); Log.Ln;
					END;
				END
			ELSIF sign = "IEND" THEN RETURN NIL
			ELSIF sign = "IDAT" THEN seenidat := TRUE
			END
		END;
		IF ~seenidat OR rd.eof THEN RETURN NIL END;
		IF (colortype = 3) & ~seenplte THEN RETURN NIL END;
		IF rd.Base().Length() - ncofs <= 8 THEN RETURN NIL END;
		img.dpiX := dpiX; img.dpiY := dpiY;
		RETURN img
	END ScanPng;

	(******** Loader ********)

	PROCEDURE (ldr: ImageLoader) Detect* (rd: Files.Reader): BOOLEAN;
	VAR buf: ARRAY 16 OF SHORTCHAR;
	BEGIN
		ReadSChars(rd, buf, 0, 16);
		RETURN ~rd.eof &
			(buf[0] = 89X) & (buf[1] = 50X) & (buf[2] = 4EX) & (buf[3] = 47X) &
			(buf[4] = 0DX) & (buf[5] = 0AX) & (buf[6] = 1AX)
	END Detect;

	PROCEDURE (ldr: ImageLoader) Load* (rd: Files.Reader): StdRasters.Image;
	BEGIN RETURN LoadPng(rd)
	END Load;

	PROCEDURE (ldr: ImageLoader) GetInfo* (rd: Files.Reader): StdRasters.Image;
	BEGIN RETURN ScanPng(rd)
	END GetInfo;

	PROCEDURE InitCRC;
	CONST poly = 0EDB88320H;
	VAR n, c, k: INTEGER;
	BEGIN
		(*
		crc32Table[0] := BITS(00000000H);
		crc32Table[1] := BITS(1DB71064H);
		crc32Table[2] := BITS(3B6E20C8H);
		crc32Table[3] := BITS(26D930ACH);
		crc32Table[4] := BITS(76DC4190H);
		crc32Table[5] := BITS(6B6B51F4H);
		crc32Table[6] := BITS(4DB26158H);
		crc32Table[7] := BITS(5005713CH);
		crc32Table[8] := BITS(0EDB88320H);
		crc32Table[9] := BITS(0F00F9344H);
		crc32Table[10] := BITS(0D6D6A3E8H);
		crc32Table[11] := BITS(0CB61B38CH);
		crc32Table[12] := BITS(9B64C2B0H);
		crc32Table[13] := BITS(86D3D2D4H);
		crc32Table[14] := BITS(0A00AE278H);
		crc32Table[15] := BITS(0BDBDF21CH);
		*)
		(* make exclusive-or pattern from polynomial (0EDB88320H).
			terms of polynomial defining this crc (except x^32)
			uint poly = 0; // polynomial exclusive-or pattern
			foreach (immutable n; [0,1,2,4,5,7,8,10,11,12,16,22,23,26]) poly |= 1u<<(31-n);
		*)
		n := 0;
		WHILE n < 16 DO
			c := n * 16;
			k := 8; WHILE k # 0 DO
				DEC(k);
				IF ODD(c) THEN c := ORD(BITS(SYSTEM.LSH(c, -1)) / BITS(poly))
				ELSE c := SYSTEM.LSH(c, -1)
				END
			END;
			crc32Table[n] := BITS(c);
			INC(n);
		END
	END InitCRC;


BEGIN
	InitCRC;
	DEBUG_PNG_LOADER := FALSE; DEBUG_PNG_DEPACKER := FALSE
END StdRastersPng.


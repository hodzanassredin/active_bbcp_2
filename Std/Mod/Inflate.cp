MODULE StdInflate;
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch blackbox.oberon.org"
	contributors	= "Oberon microsystems, Ketmar Dark"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	changes	= ""
	issues	= ""

	based on tinf — tiny inflate library (inflate, gzip, zlib)
	version 1.00 (with later improvements)
	streaming interface by Ketmar Dark

	Copyright (c) 2003 by Joergen Ibsen / Jibz
	All Rights Reserved

	http://www.ibsensoftware.com/

	This software is provided 'as-is', without any express
	or implied warranty.	In no event will the authors be
	held liable for any damages arising from the use of
	this software.

	Permission is granted to anyone to use this software
	for any purpose, including commercial applications,
	and to alter it and redistribute it freely, subject to
	the following restrictions:

	1. The origin of this software must not be
		misrepresented; you must not claim that you
		wrote the original software. If you use this
		software in a product, an acknowledgment in
		the product documentation would be appreciated
		but is not required.

	2. Altered source versions must be plainly marked
		as such, and must not be misrepresented as
		being the original software.

	3. This notice may not be removed or altered from
		any source distribution.

	This Ultra Pascal port was made by Ketmar // Invisible Vector
	ketmar@ketmar.no-ip.org
	the original code was heavily changed and uglified (sorry!)

	Adler-32 algorithm taken from the zlib source, which is
	Copyright (C) 1995-1998 Jean-loup Gailly and Mark Adler

**)


	CONST
		PKZIP_BUG_TOLERANCE = TRUE;

	CONST
		AdlerBase = 65521;
		AdlerNMax = (*5552*) 4102;	(* this should be totally safe *)

		RD_BUFFER_SIZE = 2048;

		(* current state *)
		ExpectZLibHeader = 0; (* expecting ZLib header *)
		ExpectBlock = 1; (* expecting new block *)
		RawBlock = 2; (* uncompressed block *)
		CompressedBlock = 3;
		EOF = 4; (* inf.ReadByte() returns false before block header *)
		Dead = 5; (* some error occured, throw exception on any read *)

		(* stream format *)
		ZLib* = 0; Deflate* = 1;

	TYPE
		Mode* = INTEGER;
		State = INTEGER;

		Tree = RECORD
			counts: ARRAY 16 OF INTEGER; (* ushort: counts of code length counts *)
			symbols: ARRAY 288 OF INTEGER; (* ushort: code -> symbol translation counts *)
			maxsym: INTEGER
		END;


		Inflater* = POINTER TO ABSTRACT RECORD
			(* state data *)
			bytesLeft: INTEGER;	(* bytes to copy both for compressed and for uncompressed blocks *)
			matchOfs: INTEGER;	(* match offset for inflated block *)
			lt: Tree;	(* dynamic length/symbol tree *)
			dt: Tree;	(* dynamic distance tree *)
			lastBlockWasFixed: BOOLEAN;	(* to avoid extra copies *)
			doingFinalBlock: BOOLEAN;	(* := false; *) (* stop on next processBlockHeader() *)
			state: State;	(* := State.ExpectZLibHeader; *)
			mode: Mode;	(* := Mode.ZLib; *)

			(* other data *)
			tag: INTEGER;	(* ushort *)
			bitcount: INTEGER;

			(* adler-32 *)
			a32s1, a32s2: INTEGER;
			nmaxLeft: INTEGER;	(* := Adler32.AdlerNMax; *)
			cura32: INTEGER;	(* unsigned *)

			(* dictionary *)
			dict: ARRAY 32768 OF SHORTCHAR;
			dictEnd: INTEGER;	(* current dict free byte *)

			rdbuf: ARRAY RD_BUFFER_SIZE OF SHORTCHAR;
			rbpos, rbused: INTEGER;
			rbeof: BOOLEAN;

			totalUnpacked-: INTEGER;
		END;


	VAR
		sltree: Tree;	(* fixed length/symbol tree *)
		sdtree: Tree;	(* fixed distance tree *)
		lengthBits: ARRAY 30 OF INTEGER;	(* extra bits and base tables for length codes *)
		lengthBase: ARRAY 30 OF INTEGER;
		distBits: ARRAY 30 OF INTEGER;	(* extra bits and base tables for distance codes *)
		distBase: ARRAY 30 OF INTEGER;
		clcidx: ARRAY 19 OF INTEGER;	(* special ordering of code length codes *)


	(******** Tree ********)


	PROCEDURE (VAR tree: Tree) Invalidate, NEW;
	BEGIN tree.maxsym := -1
	END Invalidate;

	PROCEDURE (VAR tree: Tree) Wipe, NEW;
		VAR f: INTEGER;
	BEGIN
		FOR f := 0 TO LEN(tree.counts) - 1 DO
			tree.counts[f] := 0
		END;
		tree.maxsym := -1
	END Wipe;

	PROCEDURE (VAR tree: Tree) Valid (): BOOLEAN, NEW;
	BEGIN RETURN (tree.maxsym >= 0) & (tree.maxsym <= 288)
	END Valid;


	(* given an array of code lengths, build a tree *)
	PROCEDURE (VAR tree: Tree) BuildTree (IN lengths: ARRAY OF INTEGER; beg, end: INTEGER), NEW;
		VAR
			offs: ARRAY 16 OF INTEGER; (* ushort *)
			f, l, available, numCodes, used: INTEGER;
	BEGIN
		ASSERT(beg < end, 20); ASSERT(beg >= 0, 21); ASSERT(end <= LEN(lengths), 22);
		(* clear code length count counts *)
		tree.Wipe;
		(* count number of codes for each non-zero length *)
		f := beg; WHILE f < end DO
			l := lengths[f];
			IF l # 0 THEN
				IF (l < 0) OR (l > 15) THEN tree.Invalidate; RETURN END;	(* error! *)
				tree.maxsym := f - beg;
				INC(tree.counts[l])
			END;
			INC(f)
		END;
		(* compute offset table for distribution sort *)
		available := 1; numCodes := 0;
		FOR f := 0 TO 15 DO
			used := tree.counts[f];
			(* Check length contains no more codes than available *)
			IF used > available THEN tree.Invalidate; RETURN END;	(* error! *)
			available := 2 * (available - used);
			offs[f] := numCodes;
			INC(numCodes, used)
		END;
		(* check all codes were used, or for the special case of only one code that it has length 1 *)
		IF ((numCodes > 1) & (available > 0)) OR
			((numCodes = 1) & (tree.counts[1] # 1))
		THEN
			tree.Invalidate; RETURN	(* error! *)
		END;
		(* fill in symbols sorted by code *)
		f := beg; WHILE f < end DO
			l := lengths[f];
			IF l # 0 THEN
				tree.symbols[offs[l]] := f - beg;
				INC(offs[l])
			END;
			INC(f)
		END;
		(* for the special case of only one code (which will be 0) add a code 1 which results in a symbol that is too large *)
		IF numCodes = 1 THEN
			tree.counts[1] := 2;
			tree.symbols[1] := tree.maxsym + 1
		END
	END BuildTree;


	(******** Inflater ********)

	(* read as much as you can, return number of read bytes *)
	PROCEDURE (inf: Inflater) ReadBuf- (VAR buf: ARRAY OF SHORTCHAR): INTEGER, NEW, ABSTRACT;

	PROCEDURE (inf: Inflater) Init* (mode: Mode), NEW;
	BEGIN
		ASSERT((mode = ZLib) OR (mode = Deflate), 20);
		inf.bytesLeft := 0;
		inf.matchOfs := 0;
		inf.lastBlockWasFixed := FALSE; inf.lt.Invalidate; inf.dt.Invalidate;
		inf.doingFinalBlock := FALSE;
		inf.mode := mode;
		inf.tag := 0;
		inf.bitcount := 0;
		inf.a32s1 := 0; inf.a32s2 := 0;
		inf.nmaxLeft := AdlerNMax;
		inf.cura32 := 0;
		inf.dictEnd := 0;
		inf.rbpos := 0; inf.rbused := 0; inf.rbeof := FALSE;
		inf.totalUnpacked := 0;
		IF mode = ZLib THEN
			inf.state := ExpectZLibHeader
		ELSE
			inf.state := ExpectBlock
		END
	END Init;

	PROCEDURE (inf: Inflater) PutByte (bt: SHORTCHAR), NEW;
	BEGIN
		inf.dict[inf.dictEnd] := bt;
		inf.dictEnd := (inf.dictEnd + 1) MOD LEN(inf.dict);
		IF inf.mode = ZLib THEN
			INC(inf.a32s1, ORD(bt));
			INC(inf.a32s2, inf.a32s1);
			IF (inf.a32s1 < 0) OR (inf.a32s2 < 0) THEN
				DEC(inf.a32s2, inf.a32s1);
				DEC(inf.a32s1, ORD(bt));
				inf.a32s1 := inf.a32s1 MOD AdlerBase;
				inf.a32s2 := inf.a32s2 MOD AdlerBase;
				INC(inf.a32s1, ORD(bt));
				INC(inf.a32s2, inf.a32s1)
			END
		END;
		INC(inf.totalUnpacked)
	END PutByte;

	PROCEDURE (inf: Inflater) FinishAdler32 (): INTEGER, NEW;
	BEGIN
		inf.a32s1 := inf.a32s1 MOD AdlerBase;
		inf.a32s2 := inf.a32s2 MOD AdlerBase;
		RETURN ASH(inf.a32s2, 16) + inf.a32s1
	END FinishAdler32;

	PROCEDURE (inf: Inflater) SetErrorState, NEW;
	BEGIN
		inf.state := Dead;
		inf.lastBlockWasFixed := FALSE; inf.lt.Invalidate; inf.dt.Invalidate;
		inf.doingFinalBlock := FALSE (* just in case too *)
	END SetErrorState;

	PROCEDURE (inf: Inflater) ReadByte (): SHORTCHAR, NEW;
		VAR res: SHORTCHAR;
	BEGIN
		IF inf.state # Dead THEN
			IF (inf.rbpos = inf.rbused) & ~inf.rbeof THEN
				inf.rbpos := 0;
				inf.rbused := inf.ReadBuf(inf.rdbuf);
				inf.rbeof := (inf.rbused <= 0)
			END;
			IF ~inf.rbeof THEN res := inf.rdbuf[inf.rbpos]; INC(inf.rbpos)
			ELSE inf.SetErrorState; res := 0X
			END
		ELSE res := 0X
		END;
		RETURN res
	END ReadByte;

	(* get one bit from source stream to `ubyte onebit` *)
	PROCEDURE (inf: Inflater) ReadBit (): BOOLEAN, NEW;
		VAR res: BOOLEAN;
	BEGIN
		DEC(inf.bitcount);
		IF inf.bitcount = -1 THEN
			inf.tag := ORD(inf.ReadByte());
			inf.bitcount := 7
		END;
		res := ODD(inf.tag); inf.tag := inf.tag DIV 2;
		RETURN res
	END ReadBit;

	(* read a num bit value from a stream and add base *)
	PROCEDURE (inf: Inflater) ReadBits (num, base: INTEGER): INTEGER, NEW;
		VAR val, mask: INTEGER;
	BEGIN val := 0; mask := 0;
		WHILE mask < num DO
			IF inf.ReadBit() THEN INC(val, ASH(1, mask)) END;
			INC(mask)
		END;
		RETURN val + base
	END ReadBits;

	PROCEDURE (inf: Inflater) ProcessZLibHeader, NEW;
		VAR cmf, flg: SHORTCHAR;
	BEGIN
		(* 7 bytes *)
		(* compression parameters *)
		cmf := inf.ReadByte();
		(* flags *)
		(* if (!inf.ReadByte(readBuf, flg)) throw new Exception("out of input data"); *)
		flg := inf.ReadByte();
		(* check format *)
		(* check checksum *)
		IF ~((256 * ORD(cmf) + ORD(flg)) MOD 31 = 0) THEN inf.SetErrorState; RETURN END;
		(* check method (only deflate allowed) *)
		IF ~(ORD(cmf) MOD 10H = 8) THEN inf.SetErrorState; RETURN END;
		(* check window size *)
		IF ~(ORD(cmf) DIV 16 <= 7) THEN inf.SetErrorState; RETURN END;
		(* there must be no preset dictionary *)
		IF ODD(ORD(flg) DIV 020H) THEN inf.SetErrorState; RETURN END;
		(* FYI: flg>>6 will give you compression level: *)
		(* 0 - compressor used fastest algorithm *)
		(* 1 - compressor used fast algorithm *)
		(* 2 - compressor used default algorithm *)
		(* 3 - compressor used maximum compression, slowest algorithm *)
		(* not that you can make any sane use of that info though... *)
		(* note that last 4 bytes is Adler32 checksum in big-endian format *)
		(* init Adler32 counters *)
		inf.a32s1 := 1; inf.a32s2 := 0;
		inf.nmaxLeft := AdlerNMax;
		(* ok, we can go now *)
		inf.state := ExpectBlock
	END ProcessZLibHeader;

	PROCEDURE (inf: Inflater) FinishZLibData, NEW;
		VAR a32: SET; bt: SHORTCHAR;
	BEGIN
		(* read Adler32 *)
		a32 := {};
		bt := inf.ReadByte(); a32 := a32 + BITS(ASH(ORD(bt), 24));
		bt := inf.ReadByte(); a32 := a32 + BITS(ASH(ORD(bt), 16));
		bt := inf.ReadByte(); a32 := a32 + BITS(ASH(ORD(bt), 8));
		bt := inf.ReadByte(); a32 := a32 + BITS(ORD(bt));
		IF ORD(a32) # inf.FinishAdler32() THEN inf.SetErrorState END	(* invalid checksum *)
	END FinishZLibData;

	PROCEDURE (inf: Inflater) DecodeSymbol (IN t: Tree): INTEGER, NEW;
		VAR base, ofs, len, c: INTEGER;
	BEGIN
		base := 0; ofs := 0;
		(*
			Get more bits while code index is above number of codes

			Rather than the actual code, we are computing the position of the
			code in the sorted order of codes, which is the index of the
			corresponding symbol.

			Conceptually, for each code length (level in the tree), there are
			counts[len] leaves on the left and internal nodes on the right.
			The index we have decoded so far is base + offs, and if that
			falls within the leaves we are done. Otherwise we adjust the range
			of offs and add one more bit to it.
		*)
		len := 1;
		WHILE len # 0 DO
			ofs := ofs * 2; IF inf.ReadBit() THEN INC(ofs) END;
			IF len > 15 THEN inf.SetErrorState; RETURN -1 END;	(* error *)
			c := t.counts[len];
			IF ofs < c THEN len := 0
			ELSE
				INC(base, c);
				DEC(ofs, c);
				INC(len)
			END
		END;
		IF (base + ofs < 0) OR (base + ofs >= 288) THEN inf.SetErrorState; RETURN -1 END;	(* error *)
		RETURN t.symbols[base + ofs]
	END DecodeSymbol;

	(* given a data stream, decode dynamic trees from it *)
	PROCEDURE (inf: Inflater) DecodeTrees, NEW;
	VAR
		codeTree: Tree;
		lengths: ARRAY 288 + 32 OF INTEGER;
		hlit, hdist, hclen, sym: INTEGER;
		f, num, length, bt: INTEGER;
	BEGIN
		inf.lastBlockWasFixed := FALSE; inf.lt.Invalidate; inf.dt.Invalidate;
		(* get 5 bits HLIT (257-286) *)
		hlit := inf.ReadBits(5, 257);
		(* get 5 bits HDIST (1-32) *)
		hdist := inf.ReadBits(5, 1);
		(* distances 31 and 32 are not technically allowed, but sometimes seen in the wild *)
		IF ~PKZIP_BUG_TOLERANCE THEN
			IF (hlit > 286) OR (hdist > 30) THEN inf.SetErrorState; RETURN END	(* error *)
		END;
		(* get 4 bits HCLEN (4-19) *)
		hclen := inf.ReadBits(4, 4);
		FOR f := 0 TO 18 DO lengths[f] := 0 END;
		(* read code lengths for code length alphabet *)
		f := 0; WHILE f < hclen DO
			lengths[clcidx[f]] := inf.ReadBits(3, 0); (* get 3 bits code length (0-7) *)
			INC(f)
		END;
		(* build code length tree *)
		codeTree.BuildTree(lengths, 0, 19);
		IF ~codeTree.Valid() THEN inf.SetErrorState; RETURN END;	(* error *)
		(* decode code lengths for the dynamic trees *)
		num := 0;
		WHILE num < hlit + hdist DO
			sym := inf.DecodeSymbol(codeTree);
			IF (sym < 0) OR (sym > codeTree.maxsym) THEN inf.SetErrorState; RETURN END;	(* error *)
			CASE sym OF
			| 16:	(* copy previous code length 3-6 times (read 2 bits) *)
				IF num = 0 THEN inf.SetErrorState; RETURN END;	(* error *)
				bt := lengths[num - 1];
				length := inf.ReadBits(2, 3)
			| 17:	(* repeat code length 0 for 3-10 times (read 3 bits) *)
				bt := 0;
				length := inf.ReadBits(3, 3)
			| 18:	(* repeat code length 0 for 11-138 times (read 7 bits) *)
				bt := 0;
				length := inf.ReadBits(7, 11)
			ELSE	(* values 0-15 represent the actual code lengths *)
				ASSERT((sym >= 0) & (sym < 16), 103); (* invalid tree symbol *)
				length := 1;
				bt := sym
			END;
			(* fill it *)
			IF length > hlit + hdist - num THEN inf.SetErrorState; RETURN END;	(* error *)
			WHILE length # 0 DO DEC(length); lengths[num] := bt; INC(num) END
		END;
		(* check if EOB symbol is present *)
		IF lengths[256] = 0 THEN inf.SetErrorState; RETURN END;	(* error *)
		(* build dynamic trees *)
		inf.lt.BuildTree(lengths, 0, hlit);
		IF ~inf.lt.Valid() THEN inf.SetErrorState; RETURN END;	(* error *)
		inf.dt.BuildTree(lengths, hlit, hlit + hdist);
		IF ~inf.dt.Valid() THEN inf.SetErrorState; RETURN END	(* error *)
	END DecodeTrees;

	(* can return zero-length slice (on state switch, for example) *)
	PROCEDURE (inf: Inflater) InflatedBlock (VAR btdest: ARRAY OF SHORTCHAR; beg, end: INTEGER): INTEGER, NEW;
	VAR
		btpos, sym: INTEGER;
		dist, length, offs: INTEGER;
		bt: SHORTCHAR;
	BEGIN ASSERT(beg < end, 20); ASSERT(beg >= 0, 21); ASSERT(end <= LEN(btdest), 22);
		btpos := beg;
		WHILE (btpos # end) & (inf.state # ExpectBlock) DO
			IF inf.bytesLeft # 0 THEN
				(* copying match; all checks already done *)
				bt := inf.dict[(inf.dictEnd + LEN(inf.dict) - inf.matchOfs) MOD LEN(inf.dict)];
				inf.PutByte(bt);
				btdest[btpos] := bt; INC(btpos);
				DEC(inf.bytesLeft)
			ELSE
				sym := inf.DecodeSymbol(inf.lt);
				IF (sym < 0) OR (inf.state = Dead) THEN inf.SetErrorState; RETURN -1 END;	(* error! *)
				IF sym = 256 THEN
					(* end of block, fix state *)
					inf.state := ExpectBlock
				ELSIF sym < 256 THEN
					(* normal *)
					bt := SHORT(CHR(sym));
					inf.PutByte(bt);
					btdest[btpos] := bt; INC(btpos)
				ELSE
					(* copy *)
					(* check sym is within range and distance tree is not empty *)
					IF (sym > inf.lt.maxsym) OR (sym - 257 > 28) THEN
						inf.SetErrorState; RETURN -1	(* error! *)
					END;
					DEC(sym, 257);
					(* possibly get more bits from length code *)
					length := inf.ReadBits(lengthBits[sym], lengthBase[sym]);
					dist := inf.DecodeSymbol(inf.dt);
					IF (dist < 0) OR (dist > 29) OR (dist > inf.dt.maxsym) THEN inf.SetErrorState; RETURN -1 END;	(* error! *)
					(* possibly get more bits from distance code *)
					offs := inf.ReadBits(distBits[dist], distBase[dist]);
					IF (offs < 0) OR (offs > 32768(*inf.dictEnd*)) THEN inf.SetErrorState; RETURN -1 END;	(* error! *)
					offs := offs MOD 32768;
					(* copy match *)
					inf.bytesLeft := length;
					inf.matchOfs := offs
				END
			END
		END;
		RETURN btpos - beg
	END InflatedBlock;

	(* can return zero-length slice (on state switch, for example) *)
	PROCEDURE (inf: Inflater) UncompressedBlock (VAR btdest: ARRAY OF SHORTCHAR; beg, end: INTEGER): INTEGER, NEW;
	VAR
		btpos: INTEGER;
		bt: SHORTCHAR;
	BEGIN ASSERT(beg < end, 20); ASSERT(beg >= 0, 21); ASSERT(end <= LEN(btdest), 22);
		btpos := beg;
		WHILE (btpos # end) & (inf.bytesLeft # 0) DO
			(* copying *)
			bt := inf.ReadByte();
			inf.PutByte(bt);
			btdest[btpos] := bt; INC(btpos);
			DEC(inf.bytesLeft)
		END;
		IF inf.bytesLeft = 0 THEN inf.state := ExpectBlock END;	(* end of block, fix state *)
		RETURN btpos - beg
	END UncompressedBlock;

	PROCEDURE (inf: Inflater) ReadU16 (): INTEGER, NEW;
		VAR b0, b1: SHORTCHAR;
	BEGIN
		b0 := inf.ReadByte(); b1 := inf.ReadByte();
		RETURN ORD(b0) + ASH(ORD(b1), 8)
	END ReadU16;

	PROCEDURE (inf: Inflater) ProcessRawHeader, NEW;
		VAR length, invlength: INTEGER;
	BEGIN
		length := inf.ReadU16();
		invlength := inf.ReadU16();	(* one's complement of length *)
		(* check length *)
		IF BITS(length) # (-BITS(invlength)) * {0..15} THEN
			inf.SetErrorState;
			RETURN (* error! *)
		END;
		inf.bitcount := 0;	(* make sure we start next block on a byte boundary *)
		inf.bytesLeft := length;
		inf.state := RawBlock
	END ProcessRawHeader;

	PROCEDURE (inf: Inflater) ProcessFixedHeader, NEW;
	BEGIN
		IF ~inf.lastBlockWasFixed THEN
			inf.lt := sltree; inf.dt := sdtree;
			inf.lastBlockWasFixed := TRUE
		END;
		inf.bytesLeft := 0;	(* force reading of symbol (just in case) *)
		inf.state := CompressedBlock
	END ProcessFixedHeader;

	PROCEDURE (inf: Inflater) ProcessDynamicHeader, NEW;
	BEGIN
		(* decode trees from stream *)
		inf.DecodeTrees;
		inf.bytesLeft := 0;	(* force reading of symbol (just in case) *)
		inf.state := CompressedBlock
	END ProcessDynamicHeader;

	(* set state to State.EOF on correct EOF *)
	PROCEDURE (inf: Inflater) ProcessBlockHeader, NEW;
	BEGIN
		IF inf.doingFinalBlock THEN
			inf.state := EOF;
			inf.doingFinalBlock := FALSE;
			IF inf.mode = ZLib THEN inf.FinishZLibData END
		ELSE
			inf.doingFinalBlock := inf.ReadBit();	(* final block flag *)
			(* read block type (2 bits) and fix state *)
			CASE inf.ReadBits(2, 0) OF
			| 0: inf.ProcessRawHeader	(* uncompressed block *)
			| 1: inf.ProcessFixedHeader	(* block with fixed huffman trees *)
			| 2: inf.ProcessDynamicHeader	(* block with dynamic huffman trees *)
			| 3: inf.SetErrorState	(* invalid block code *)
			END
		END
	END ProcessBlockHeader;

	PROCEDURE (inf: Inflater) CheckStreamHeader*, NEW;
	BEGIN
		IF (inf.mode = ZLib) & (inf.state = ExpectZLibHeader) THEN
			inf.ProcessZLibHeader
		END
	END CheckStreamHeader;

	(* returns length *)
	PROCEDURE (inf: Inflater) ReadSChars* (VAR btdest: ARRAY OF SHORTCHAR; beg, len: INTEGER): INTEGER, NEW;
		VAR st, rd, end: INTEGER;
	BEGIN
		ASSERT(len >= 0, 20); ASSERT(beg >= 0, 21);
		ASSERT(LEN(btdest) - beg >= len, 22);
		IF inf.state = EOF THEN RETURN 0 END;
		st := beg; end := beg + len;
		WHILE (beg < end) & (inf.state # EOF) & (inf.state # Dead) DO
			CASE inf.state OF
			| ExpectZLibHeader:
				inf.ProcessZLibHeader
			| ExpectBlock:
				inf.ProcessBlockHeader
			| RawBlock:
				rd := inf.UncompressedBlock(btdest, beg, end);
				INC(beg, rd)
			| CompressedBlock:
				rd := inf.InflatedBlock(btdest, beg, end);
				INC(beg, rd)
			END
		END;
		IF inf.state = Dead THEN beg := 0; st := 1 END;
		RETURN beg - st
	END ReadSChars;

	PROCEDURE (inf: Inflater) Eof* (): BOOLEAN, NEW;
	BEGIN RETURN inf.state = EOF
	END Eof;

	PROCEDURE (inf: Inflater) Error* (): BOOLEAN, NEW;
	BEGIN RETURN inf.state = Dead
	END Error;


	(******** data initialisers ********)

	(* fixed length/symbol tree *)
	PROCEDURE InitSLTree;
		VAR f: INTEGER;
	BEGIN
		sltree.Wipe;
		sltree.counts[7] := 24;
		sltree.counts[8] := 152;
		sltree.counts[9] := 112;
		FOR f := 0 TO 23 DO sltree.symbols[f] := 256 + f END;
		FOR f := 0 TO 143 DO sltree.symbols[24 + f] := f END;
		FOR f := 0 TO 7 DO sltree.symbols[24 + 144 + f] := 280 + f END;
		FOR f := 0 TO 111 DO sltree.symbols[24 + 144 + 8 + f] := 144 + f END;
		sltree.maxsym := 285
	END InitSLTree;

	(* fixed distance tree *)
	PROCEDURE InitSDTree;
		VAR f: INTEGER;
	BEGIN
		sdtree.Wipe;
		sdtree.counts[5] := 32;
		FOR f := 0 TO 31 DO sdtree.symbols[f] := f END;
		sdtree.maxsym := 29
	END InitSDTree;

	PROCEDURE FillBits (VAR bits: ARRAY OF INTEGER; delta: INTEGER);
		VAR f: INTEGER;
	BEGIN
		FOR f := 0 TO delta - 1 DO bits[f] := 0 END;
		FOR f := 0 TO 30 - delta - 1 DO bits[f + delta] := f DIV delta END;
	END FillBits;

	PROCEDURE FillBase (VAR base: ARRAY OF INTEGER; IN bits: ARRAY OF INTEGER; first: INTEGER);
		VAR f, sum: INTEGER;
	BEGIN
		sum := first;
		FOR f := 0 TO 29 DO base[f] := sum; INC(sum, ASH(1, bits[f])) END
	END FillBase;

	(* extra bits and base tables for length codes *)
	PROCEDURE BuildLengthBitsBase;
		VAR bits: ARRAY 30 OF INTEGER;
	BEGIN
		(* bits *)
		FillBits(lengthBits, 4);
		lengthBits[28] := 0; (* fix a special case *)
		(* base *)
		FillBits(bits, 4);
		FillBase(lengthBase, bits, 3);
		lengthBase[28] := 258 (* fix a special case *)
	END BuildLengthBitsBase;

	(* extra bits and base tables for distance codes *)
	PROCEDURE BuildExtraBitsBase;
	BEGIN
		(* bits *)
		FillBits(distBits, 2);
		(* base *)
		FillBase(distBase, distBits, 1)
	END BuildExtraBitsBase;

	PROCEDURE Init;
	VAR pos: INTEGER;
		PROCEDURE Put (v: INTEGER);
		BEGIN clcidx[pos] := v; INC(pos)
		END Put;
	BEGIN
		InitSLTree; InitSDTree; BuildLengthBitsBase; BuildExtraBitsBase;
		pos := 0;
		Put(16); Put(17); Put(18);
		Put(0); Put(8); Put(7); Put(9); Put(6);
		Put(10); Put(5); Put(11); Put(4); Put(12);
		Put(3); Put(13); Put(2); Put(14);
		Put(1); Put(15); ASSERT(pos = LEN(clcidx), 100)
	END Init;

BEGIN
	Init
END StdInflate.

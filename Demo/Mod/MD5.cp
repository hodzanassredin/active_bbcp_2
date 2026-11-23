MODULE DemoMD5;

IMPORT SYSTEM, Files, DevCommanders, TextModels, Log;

CONST modulus = 4294967296;
TYPE Block = ARRAY 64 OF BYTE;
VAR k: ARRAY 64 OF LONGINT; shifts: ARRAY 16 OF INTEGER;

(* Words are unsigned values held in LONGINT; SET implements bit operations. *)
PROCEDURE Bits(x: LONGINT): SET;
BEGIN
 IF x >= 2147483648 THEN DEC(x, modulus) END;
 RETURN SYSTEM.VAL(SET, SHORT(x))
END Bits;

PROCEDURE Word(x: SET): LONGINT;
 VAR n: LONGINT;
BEGIN
 n := SYSTEM.VAL(INTEGER, x);
 IF n < 0 THEN INC(n, modulus) END;
 RETURN n
END Word;

PROCEDURE Rotate(x: LONGINT; n: INTEGER): LONGINT;
 VAR p: LONGINT;
BEGIN
 p := ASH(1L, n);
 RETURN (x * p) MOD modulus + x DIV (modulus DIV p)
END Rotate;

PROCEDURE Transform(VAR state: ARRAY OF LONGINT; IN block: Block);
 VAR m: ARRAY 16 OF LONGINT; a, b, c, d, f, t: LONGINT;
  i, j, g, shift: INTEGER;
BEGIN
 FOR i := 0 TO 15 DO
  m[i] := 0;
  FOR j := 3 TO 0 BY -1 DO
   m[i] := m[i] * 256 + (block[4*i+j] MOD 256)
  END
 END;
 a := state[0]; b := state[1]; c := state[2]; d := state[3];
 FOR i := 0 TO 63 DO
  IF i < 16 THEN
   f := Word((Bits(b) * Bits(c)) + ((-Bits(b)) * Bits(d))); g := i
  ELSIF i < 32 THEN
   f := Word((Bits(d) * Bits(b)) + ((-Bits(d)) * Bits(c))); g := (5*i+1) MOD 16
  ELSIF i < 48 THEN
   f := Word(Bits(b) / Bits(c) / Bits(d)); g := (3*i+5) MOD 16
  ELSE
   f := Word(Bits(c) / (Bits(b) + (-Bits(d)))); g := (7*i) MOD 16
  END;
  shift := shifts[(i DIV 16)*4 + i MOD 4];
  t := d; d := c; c := b;
  b := (b + Rotate((a + f + k[i] + m[g]) MOD modulus, shift)) MOD modulus;
  a := t
 END;
 state[0] := (state[0]+a) MOD modulus; state[1] := (state[1]+b) MOD modulus;
 state[2] := (state[2]+c) MOD modulus; state[3] := (state[3]+d) MOD modulus
END Transform;

(* Programmatic entry point: directory path and file name. *)
PROCEDURE Sum*(IN path, name: ARRAY OF CHAR);

 VAR loc: Files.Locator; file: Files.File; rd: Files.Reader; filename: Files.Name;
  hex: ARRAY 17 OF CHAR; block: Block; state: ARRAY 4 OF LONGINT; digest: ARRAY 33 OF CHAR;
  size, remaining, n, i, j, v, pos: INTEGER; length, word: LONGINT;
BEGIN
 i := 0;
 WHILE (i < LEN(name)) & (name[i] # 0X) DO INC(i) END;
 IF (i = 0) OR (i >= LEN(filename)) THEN
  Log.String("MD5: invalid or too long file name"); Log.Ln; RETURN
 END;
 hex := "0123456789abcdef";
 filename := name$; loc := Files.dir.This(path);
 IF (loc = NIL) OR (loc.res # 0) THEN
  Log.String("MD5: cannot open directory"); Log.Ln; RETURN
 END;
 file := Files.dir.Old(loc, filename, Files.shared);
 IF file = NIL THEN Log.String("MD5: cannot open file: "); Log.String(name); Log.Ln; RETURN END;
 size := file.Length(); remaining := size; rd := file.NewReader(NIL);
 state[0] := 1732584193; state[1] := 4023233417;
 state[2] := 2562383102; state[3] := 271733878;
 WHILE remaining >= 64 DO
  pos := rd.Pos(); rd.ReadBytes(block, 0, 64);
  IF rd.eof OR (rd.Pos() - pos # 64) THEN
   file.Close(); Log.String("MD5: file read error"); Log.Ln; RETURN
  END;
  Transform(state, block); DEC(remaining, 64)
 END;
 n := remaining; pos := rd.Pos(); rd.ReadBytes(block, 0, n);
 IF rd.eof OR (rd.Pos() - pos # n) OR (file.Length() # size) THEN
  file.Close(); Log.String("MD5: file read error or size changed"); Log.Ln; RETURN
 END;
 file.Close();
 (* Append 1 bit, zero padding, and original bit length in little-endian order. *)
 block[n] := -128; INC(n);
 IF n > 56 THEN
  FOR i := n TO 63 DO block[i] := 0 END;
  Transform(state, block); n := 0
 END;
 FOR i := n TO 55 DO block[i] := 0 END;
 length := LONG(size) * 8;
 FOR i := 56 TO 63 DO
  v := SHORT(length MOD 256);
  IF v >= 128 THEN DEC(v, 256) END;
  block[i] := SHORT(SHORT(v));
  length := length DIV 256
 END;
 Transform(state, block);
 FOR i := 0 TO 3 DO
  word := state[i];
  FOR j := 0 TO 3 DO
   v := SHORT(word MOD 256); word := word DIV 256;
   digest[8*i+2*j] := hex[v DIV 16]; digest[8*i+2*j+1] := hex[v MOD 16]
  END
 END;
 digest[32] := 0X; Log.String(digest); Log.Ln
END Sum;

PROCEDURE Do*;
 VAR p: DevCommanders.Par; rd: TextModels.Reader;
  arg, path: ARRAY 4096 OF CHAR; name: Files.Name;
  n, first, last, slash, i, j: INTEGER;
BEGIN
 p := DevCommanders.par;
 IF p = NIL THEN Log.String("Usage: DemoMD5.Do <file path>"); Log.Ln; RETURN END;
 rd := p.text.NewReader(NIL); rd.SetPos(p.beg); n := 0;
 WHILE rd.Pos() < p.end DO
  rd.Read;
  IF n >= LEN(arg)-1 THEN Log.String("MD5: path too long"); Log.Ln; RETURN END;
  arg[n] := rd.char; INC(n)
 END;
 first := 0; last := n;
 WHILE (first < last) & (arg[first] <= " ") DO INC(first) END;
 WHILE (last > first) & (arg[last-1] <= " ") DO DEC(last) END;
 IF (first < last) & (arg[first] = '"') THEN
  IF (last-first < 2) OR (arg[last-1] # '"') THEN
   Log.String("MD5: unclosed quote"); Log.Ln; RETURN
  END;
  INC(first); DEC(last)
 END;
 IF first = last THEN Log.String("Usage: DemoMD5.Do <file path>"); Log.Ln; RETURN END;
 slash := first-1;
 FOR i := first TO last-1 DO
  IF (arg[i] = "/") OR (arg[i] = 5CX) THEN slash := i END
 END;
 IF last-slash-1 >= LEN(name) THEN Log.String("MD5: file name too long"); Log.Ln; RETURN END;
 j := 0;
 FOR i := first TO slash DO path[j] := arg[i]; INC(j) END;
 path[j] := 0X; j := 0;
 FOR i := slash+1 TO last-1 DO name[j] := arg[i]; INC(j) END;
 name[j] := 0X; Sum(path, name)
END Do;

BEGIN
 k[0] := 3614090360;
 k[1] := 3905402710;
 k[2] := 606105819;
 k[3] := 3250441966;
 k[4] := 4118548399;
 k[5] := 1200080426;
 k[6] := 2821735955;
 k[7] := 4249261313;
 k[8] := 1770035416;
 k[9] := 2336552879;
 k[10] := 4294925233;
 k[11] := 2304563134;
 k[12] := 1804603682;
 k[13] := 4254626195;
 k[14] := 2792965006;
 k[15] := 1236535329;
 k[16] := 4129170786;
 k[17] := 3225465664;
 k[18] := 643717713;
 k[19] := 3921069994;
 k[20] := 3593408605;
 k[21] := 38016083;
 k[22] := 3634488961;
 k[23] := 3889429448;
 k[24] := 568446438;
 k[25] := 3275163606;
 k[26] := 4107603335;
 k[27] := 1163531501;
 k[28] := 2850285829;
 k[29] := 4243563512;
 k[30] := 1735328473;
 k[31] := 2368359562;
 k[32] := 4294588738;
 k[33] := 2272392833;
 k[34] := 1839030562;
 k[35] := 4259657740;
 k[36] := 2763975236;
 k[37] := 1272893353;
 k[38] := 4139469664;
 k[39] := 3200236656;
 k[40] := 681279174;
 k[41] := 3936430074;
 k[42] := 3572445317;
 k[43] := 76029189;
 k[44] := 3654602809;
 k[45] := 3873151461;
 k[46] := 530742520;
 k[47] := 3299628645;
 k[48] := 4096336452;
 k[49] := 1126891415;
 k[50] := 2878612391;
 k[51] := 4237533241;
 k[52] := 1700485571;
 k[53] := 2399980690;
 k[54] := 4293915773;
 k[55] := 2240044497;
 k[56] := 1873313359;
 k[57] := 4264355552;
 k[58] := 2734768916;
 k[59] := 1309151649;
 k[60] := 4149444226;
 k[61] := 3174756917;
 k[62] := 718787259;
 k[63] := 3951481745;
 shifts[0] := 7;
 shifts[1] := 12;
 shifts[2] := 17;
 shifts[3] := 22;
 shifts[4] := 5;
 shifts[5] := 9;
 shifts[6] := 14;
 shifts[7] := 20;
 shifts[8] := 4;
 shifts[9] := 11;
 shifts[10] := 16;
 shifts[11] := 23;
 shifts[12] := 6;
 shifts[13] := 10;
 shifts[14] := 15;
 shifts[15] := 21;
END DemoMD5.

MODULE CryptoPSS;
(** PURPOSE "RSASSA-PSS (RFC 8017) с солью длины хеша"; *)

	IMPORT B := CryptoAosBigNumbers, Hashes := CryptoHashes, RSA := CryptoRSA,
		U := CryptoUtils, AO := CryptoAosCompat;

	CONST
		MaxModulusBytes = 512;
		MaxHash = 64;

	PROCEDURE ToFixed (b : B.BigNumber; VAR out : ARRAY OF SHORTCHAR;
			ofs, k : AO.SIZE): BOOLEAN;
		VAR raw : ARRAY MaxModulusBytes OF SHORTCHAR;
			n, i : AO.SIZE;
	BEGIN
		IF (b = NIL) OR (ofs < 0) OR (k < 0) OR (ofs + k > LEN(out)) THEN
			RETURN FALSE
		END;
		n := 4 * b.len;
		IF (n > k) OR (n > LEN(raw)) THEN
			RETURN FALSE
		END;
		B.GetBinaryValue(b, raw, 0);
		FOR i := 0 TO k - 1 DO
			out[ofs + i] := 0X
		END;
		FOR i := 0 TO n - 1 DO
			out[ofs + k - n + i] := raw[i]
		END;
		RETURN TRUE
	END ToFixed;

	PROCEDURE HashSeg (IN hashName : ARRAY OF SHORTCHAR;
			IN a : ARRAY OF SHORTCHAR; aLen : AO.SIZE;
			IN b : ARRAY OF SHORTCHAR; bLen : AO.SIZE;
			IN c : ARRAY OF SHORTCHAR; cLen : AO.SIZE;
			VAR out : ARRAY OF SHORTCHAR): AO.SIZE;
		VAR h : Hashes.Hash;
	BEGIN
		h := Hashes.NewHash(hashName);
		IF h = NIL THEN
			RETURN 0
		END;
		h.Initialize;
		IF aLen > 0 THEN h.Update(a, 0, aLen) END;
		IF bLen > 0 THEN h.Update(b, 0, bLen) END;
		IF cLen > 0 THEN h.Update(c, 0, cLen) END;
		h.GetHash(out, 0);
		RETURN h.size
	END HashSeg;

	PROCEDURE MGF1 (IN hashName : ARRAY OF SHORTCHAR;
			IN seed : ARRAY OF SHORTCHAR; seedLen : AO.SIZE;
			VAR mask : ARRAY OF SHORTCHAR; maskLen : AO.SIZE): BOOLEAN;
		VAR
			h : Hashes.Hash;
			counter : ARRAY 4 OF SHORTCHAR;
			out : ARRAY MaxHash OF SHORTCHAR;
			pos, n, i, c : AO.SIZE;
	BEGIN
		pos := 0;
		c := 0;
		WHILE pos < maskLen DO
			h := Hashes.NewHash(hashName);
			IF h = NIL THEN RETURN FALSE END;
			h.Initialize;
			h.Update(seed, 0, seedLen);
			U.IntToBufferBE(c, counter, 0);
			h.Update(counter, 0, 4);
			h.GetHash(out, 0);
			n := h.size;
			IF n > maskLen - pos THEN n := maskLen - pos END;
			FOR i := 0 TO n - 1 DO
				mask[pos + i] := out[i]
			END;
			INC(pos, n);
			INC(c)
		END;
		RETURN TRUE
	END MGF1;

	(* EMSA-PSS-ENCODE: EM длины emLen, соль длины hLen. *)
	PROCEDURE Encode (IN hashName, digest : ARRAY OF SHORTCHAR; digestLen : AO.SIZE;
			emLen, zeroBits : AO.SIZE; VAR em : ARRAY OF SHORTCHAR;
			VAR emOut : AO.SIZE): BOOLEAN;
		VAR
			hLen, dbLen, psLen, i : AO.SIZE;
			salt, h, db, dbMask, mprime : ARRAY 2 * MaxHash + 8 OF SHORTCHAR;
	BEGIN
		hLen := HashSeg(hashName, em, 0, em, 0, em, 0, h);
		IF hLen = 0 THEN RETURN FALSE END;
		IF emLen < hLen + hLen + 2 THEN RETURN FALSE END;
		IF ~U.RandomBytesOk(salt, 0, hLen) THEN RETURN FALSE END;

		(* M' = 0x00 * 8 || mHash || salt *)
		FOR i := 0 TO 7 DO mprime[i] := 0X END;
		FOR i := 0 TO digestLen - 1 DO mprime[8 + i] := digest[i] END;
		FOR i := 0 TO hLen - 1 DO mprime[8 + digestLen + i] := salt[i] END;
		hLen := HashSeg(hashName, mprime, 8 + digestLen + hLen, em, 0, em, 0, h);

		dbLen := emLen - hLen - 1;
		psLen := dbLen - hLen - 1;
		FOR i := 0 TO psLen - 1 DO db[i] := 0X END;
		db[psLen] := 1X;
		FOR i := 0 TO hLen - 1 DO db[psLen + 1 + i] := salt[i] END;
		IF ~MGF1(hashName, h, hLen, dbMask, dbLen) THEN RETURN FALSE END;
		FOR i := 0 TO dbLen - 1 DO
			em[i] := SHORT(CHR(ORD(BITS(ORD(db[i])) / BITS(ORD(dbMask[i])))))
		END;
		IF zeroBits > 0 THEN
			em[0] := SHORT(CHR(ORD(BITS(ORD(em[0])) * BITS(ASH(255, -zeroBits)))))
		END;
		FOR i := 0 TO hLen - 1 DO em[dbLen + i] := h[i] END;
		em[emLen - 1] := SHORT(CHR(0BCH));
		emOut := emLen;
		RETURN TRUE
	END Encode;

	PROCEDURE SignHash* (IN hashName, digest : ARRAY OF SHORTCHAR; digestLen : AO.SIZE;
			key : RSA.Key; VAR sig : ARRAY OF SHORTCHAR; sigOfs : AO.SIZE;
			VAR sigLen : AO.SIZE);
		VAR
			em, padded : ARRAY MaxModulusBytes OF SHORTCHAR;
			emLen, k, i, bits, zeroBits : AO.SIZE;
			clear, signed : B.BigNumber;
	BEGIN
		sigLen := 0;
		IF (key = NIL) OR ~key.private THEN RETURN END;
		k := 4 * key.modulus.len;
		bits := key.modulus.BitSize();
		IF (k = 0) OR (k > MaxModulusBytes) OR (bits < 2) THEN RETURN END;
		emLen := (bits - 1 + 7) DIV 8;
		IF (emLen <= 0) OR (emLen > k) THEN RETURN END;
		zeroBits := 8 * emLen - (bits - 1);
		IF ~Encode(hashName, digest, digestLen, emLen, zeroBits, em, emLen) THEN RETURN END;
		FOR i := 0 TO k - 1 DO padded[i] := 0X END;
		FOR i := 0 TO emLen - 1 DO padded[k - emLen + i] := em[i] END;
		B.AssignBin(clear, padded, 0, k);
		signed := key.Decrypt(clear);
		IF ToFixed(signed, sig, sigOfs, k) THEN
			sigLen := k
		END
	END SignHash;

	PROCEDURE VerifyHash* (IN hashName, digest : ARRAY OF SHORTCHAR; digestLen : AO.SIZE;
			key : RSA.Key; IN sig : ARRAY OF SHORTCHAR; sigOfs, sigLen : AO.SIZE): BOOLEAN;
		VAR
			got, em, db, dbMask, h, hp, salt : ARRAY MaxModulusBytes OF SHORTCHAR;
			mprime : ARRAY 2 * MaxHash + 8 OF SHORTCHAR;
			k, emLen, bits, hLen, dbLen, i, psLen, saltLen, zeroBits : AO.SIZE;
			enc, recovered : B.BigNumber;
			ok : BOOLEAN;
	BEGIN
		IF (key = NIL) OR (sigLen <= 0) OR (sigOfs < 0) OR (sigOfs + sigLen > LEN(sig)) THEN
			RETURN FALSE
		END;
		k := 4 * key.modulus.len;
		bits := key.modulus.BitSize();
		IF (k = 0) OR (k > MaxModulusBytes) OR (bits < 2) OR (sigLen > k) THEN
			RETURN FALSE
		END;
		emLen := (bits - 1 + 7) DIV 8;
		IF (emLen <= 0) OR (emLen > k) THEN RETURN FALSE END;
		B.AssignBin(enc, sig, sigOfs, sigLen);
		IF enc.GEQ(key.modulus) THEN RETURN FALSE END;
		recovered := key.Encrypt(enc);
		IF ~ToFixed(recovered, got, 0, k) THEN RETURN FALSE END;
		(* Слева EM дополняется нулями до длины модуля. *)
		FOR i := 0 TO k - emLen - 1 DO
			IF got[i] # 0X THEN RETURN FALSE END
		END;
		FOR i := 0 TO emLen - 1 DO em[i] := got[k - emLen + i] END;
		IF ORD(em[emLen - 1]) # 0BCH THEN RETURN FALSE END;

		hLen := HashSeg(hashName, em, 0, em, 0, em, 0, hp);
		IF (hLen = 0) OR (emLen < hLen + hLen + 2) THEN RETURN FALSE END;
		dbLen := emLen - hLen - 1;
		FOR i := 0 TO hLen - 1 DO h[i] := em[dbLen + i] END;
		IF ~MGF1(hashName, h, hLen, dbMask, dbLen) THEN RETURN FALSE END;
		FOR i := 0 TO dbLen - 1 DO
			db[i] := SHORT(CHR(ORD(BITS(ORD(em[i])) / BITS(ORD(dbMask[i])))))
		END;
		(* Старшие zeroBits бита db[0] обнуляются (RFC 8017, 9.1.2). *)
		zeroBits := 8 * emLen - (bits - 1);
		db[0] := SHORT(CHR(ORD(BITS(ORD(db[0])) * BITS(ASH(255, -zeroBits)))));
		(* db = 0x00... || 0x01 || salt, соль длины hLen. *)
		saltLen := hLen;
		psLen := emLen - hLen - saltLen - 2;
		FOR i := 0 TO psLen - 1 DO
			IF db[i] # 0X THEN RETURN FALSE END
		END;
		IF db[psLen] # 1X THEN RETURN FALSE END;
		FOR i := 0 TO saltLen - 1 DO
			salt[i] := db[psLen + 1 + i]
		END;

		FOR i := 0 TO 7 DO mprime[i] := 0X END;
		FOR i := 0 TO digestLen - 1 DO mprime[8 + i] := digest[i] END;
		FOR i := 0 TO saltLen - 1 DO mprime[8 + digestLen + i] := salt[i] END;
		hLen := HashSeg(hashName, mprime, 8 + digestLen + saltLen, em, 0, em, 0, hp);
		ok := U.EqualBytesCT(h, hp, 0, 0, hLen);
		RETURN ok
	END VerifyHash;

END CryptoPSS.

MODULE CryptoPKCS1;
(**
	Implementation of the PKCS#1 encryption scheme for RSA.
	See RFC 2437.
	Timothée Martiel, 2014
*)

	IMPORT AosBigNumbers := CryptoAosBigNumbers, CryptoRSA, CryptoUtils, (*CONV FROM KernelLog*)
StdLog, AO := CryptoAosCompat;

	CONST
		Trace = FALSE;
		MaxModulusBytes = 1024;
		(* DER-кодировка DigestInfo из RFC 8017, раздел 9.2, для трёх
		   хешей, которые есть в подсистеме. *)
		DigestInfoMD5 = "3020300C06082A864886F70D020505000410";
		DigestInfoSHA1 = "3021300906052B0E03021A05000414";
		DigestInfoSHA256 = "3031300D060960864801650304020105000420";

	PROCEDURE Encrypt* (IN src : ARRAY OF SHORTCHAR; srcOfs, srcLen : AO.SIZE; key : CryptoRSA.Key; blockType : AO.SIGNED32; VAR dest : ARRAY OF SHORTCHAR; destOfs : AO.SIZE; VAR destLen : AO.SIZE);
		VAR
			k, padd: AO.SIZE;
			counter, ofs: AO.SIZE;
			encryptionBlock: POINTER TO ARRAY OF SHORTCHAR;
			clear, cipher: AosBigNumbers.BigNumber;
			nonzero: BOOLEAN;


	BEGIN
		ASSERT(key # NIL);
		IF (blockType < 0) OR (blockType > 2) THEN
			destLen := 0;
			RETURN
		END;
		k := 4 * key.modulus.len;
		IF k < 12 THEN
			destLen := 0;
			RETURN
		END;
		IF srcLen > k - 11 THEN
			destLen := 0;
			RETURN
		END;
		IF LEN(src) < srcOfs + srcLen THEN
			destLen := 0;
			RETURN
		END;
		IF LEN(dest) < destOfs + k THEN
			destLen := 0;
			RETURN
		END;
		NEW(encryptionBlock, k);
		ofs := 0;
		encryptionBlock[ofs] := 0X;
		INC(ofs);
		encryptionBlock[ofs] := SHORT(CHR(blockType));
		INC(ofs);
		padd := k - srcLen - 3;
		ASSERT(padd >= 8);
		CASE blockType OF
			0 :	     FOR counter := ofs TO ofs + padd - 1 DO
			encryptionBlock[counter] := 0X
		END
		|       1 :	     FOR counter := ofs TO ofs + padd - 1 DO
			encryptionBlock[counter] := 0FFX
		END
		|       2 :	     REPEAT
			IF ~CryptoUtils.RandomBytesOk(encryptionBlock^, ofs, padd) THEN
				destLen := 0;
				RETURN
			END;
			nonzero := TRUE;
			FOR counter := ofs TO ofs + padd - 1 DO
				nonzero := nonzero & (encryptionBlock[counter] # 0X)
			END
		UNTIL nonzero
		END;
		INC(ofs, padd);
		encryptionBlock[ofs] := 0X;
		INC(ofs);
		FOR counter := 0 TO srcLen - 1 DO
			encryptionBlock[ofs + counter] := src[srcOfs + counter];
			IF Trace THEN
				AO.LogHex(ORD(src[srcOfs + counter]), (-2));
				StdLog.String(' ')
			END
		END;
		IF Trace THEN
			StdLog.Ln;
			StdLog.String("PKCS#1:");
			StdLog.Ln;
			AO.LogBuffer(encryptionBlock^, 0, k)
		END;
		AosBigNumbers.AssignBin(clear, encryptionBlock^, 0, k);
		cipher := key.Encrypt(clear);
		AosBigNumbers.GetBinaryValue(cipher, dest, destOfs);
		destLen := k
	END Encrypt;

	PROCEDURE Decrypt* (IN src : ARRAY OF SHORTCHAR; srcOfs, srcLen : AO.SIZE; key : CryptoRSA.Key; blockType : AO.SIGNED32; VAR dest : ARRAY OF SHORTCHAR; destOfs : AO.SIZE; VAR destLen : AO.SIZE);
		VAR
			k, ofs, counter, n: AO.SIZE;
			decryptedBlock: POINTER TO ARRAY OF SHORTCHAR;
			clear, cipher: AosBigNumbers.BigNumber;


	BEGIN
		destLen := 0;
		IF (key = NIL) OR (key.modulus = NIL) THEN
			RETURN
		END;
		k := 4 * key.modulus.len;
		IF (k = 0) OR (k > MaxModulusBytes) OR (srcLen # k)
			OR (srcOfs < 0) OR (srcOfs + srcLen > LEN(src)) THEN
			RETURN
		END;
		AosBigNumbers.AssignBin(cipher, src, srcOfs, srcLen);
		clear := key.Decrypt(cipher);
		IF clear = NIL THEN
			RETURN
		END;
		NEW(decryptedBlock, k);
		FOR counter := 0 TO k - 1 DO
			decryptedBlock[counter] := 0X
		END;
		n := 4 * clear.len;
		IF n > k THEN
			RETURN
		END;
		AosBigNumbers.GetBinaryValue(clear, decryptedBlock^, k - n);
		IF decryptedBlock[0] # 0X THEN
			RETURN
		END;
		IF decryptedBlock[1] # SHORT(CHR(blockType)) THEN
			RETURN
		END;
		ofs := 2;
		CASE blockType OF
			0 :	     WHILE (ofs < k) & (decryptedBlock[ofs] = 0X) DO INC(ofs) END;
			IF ofs >= k THEN RETURN END
		|       1 :	     WHILE (ofs < k) & (decryptedBlock[ofs] = 0FFX) DO INC(ofs) END;
			IF ofs >= k THEN RETURN END;
			IF decryptedBlock[ofs] # 0X THEN RETURN END;
			INC(ofs)
		|       2 :	     WHILE (ofs < k) & (decryptedBlock[ofs] # 0X) DO INC(ofs) END;
			IF ofs >= k THEN RETURN END;
			INC(ofs)
		END;
		IF (ofs > k) OR (k - ofs > LEN(dest) - destOfs) THEN
			RETURN
		END;
		FOR counter := 0 TO k - ofs - 1 DO
			dest[destOfs + counter] := decryptedBlock[ofs + counter]
		END;
		destLen := k - ofs
	END Decrypt;

	(* EM = 00 || 01 || FF...FF || 00 || DigestInfo, RFC 8017 9.2. *)
	PROCEDURE BuildEM (IN hashName, digest : ARRAY OF SHORTCHAR; digestLen, k : AO.SIZE;
			VAR em : ARRAY OF SHORTCHAR; VAR emLen : AO.SIZE): BOOLEAN;
		VAR
			prefix : ARRAY 32 OF SHORTCHAR;
			prefixLen, tLen, i : AO.SIZE;


	BEGIN
		IF hashName = "CryptoMD5" THEN
			prefixLen := 18;
			CryptoUtils.Hex2Bin(DigestInfoMD5, 0, prefix, 0, prefixLen)
		ELSIF hashName = "CryptoSHA1" THEN
			prefixLen := 15;
			CryptoUtils.Hex2Bin(DigestInfoSHA1, 0, prefix, 0, prefixLen)
		ELSIF hashName = "CryptoSHA256" THEN
			prefixLen := 19;
			CryptoUtils.Hex2Bin(DigestInfoSHA256, 0, prefix, 0, prefixLen)
		ELSE
			RETURN FALSE
		END;
		tLen := prefixLen + digestLen;
		IF (k > LEN(em)) OR (k < tLen + 11) THEN
			RETURN FALSE
		END;
		em[0] := 0X;
		em[1] := 1X;
		i := 2;
		WHILE i < k - tLen - 1 DO
			em[i] := 0FFX;
			INC(i)
		END;
		em[k - tLen - 1] := 0X;
		FOR i := 0 TO prefixLen - 1 DO
			em[k - tLen + i] := prefix[i]
		END;
		FOR i := 0 TO digestLen - 1 DO
			em[k - digestLen + i] := digest[i]
		END;
		emLen := k;
		RETURN TRUE
	END BuildEM;

	(* Число в k байт с ведущими нулями: подпись и модуль должны
	   сравниваться побайтно при одной длине. *)
	PROCEDURE ToFixed (b : AosBigNumbers.BigNumber; VAR out : ARRAY OF SHORTCHAR;
			ofs, k : AO.SIZE): BOOLEAN;
		VAR
			raw : ARRAY MaxModulusBytes OF SHORTCHAR;
			n, i : AO.SIZE;


	BEGIN
		IF (b = NIL) OR (ofs < 0) OR (k < 0) OR (ofs + k > LEN(out)) THEN
			RETURN FALSE
		END;
		n := 4 * b.len;
		IF (n > k) OR (n > LEN(raw)) THEN
			RETURN FALSE
		END;
		AosBigNumbers.GetBinaryValue(b, raw, 0);
		FOR i := 0 TO k - 1 DO
			out[ofs + i] := 0X
		END;
		FOR i := 0 TO n - 1 DO
			out[ofs + k - n + i] := raw[i]
		END;
		RETURN TRUE
	END ToFixed;

	(* Подпись PKCS#1 v1.5: EM строится из digest, затем EM^d mod n.
	   Для секретного показателя используется CRT (Key.Decrypt). *)
	PROCEDURE SignHash* (IN hashName, digest : ARRAY OF SHORTCHAR; digestLen : AO.SIZE;
			key : CryptoRSA.Key; VAR sig : ARRAY OF SHORTCHAR; sigOfs : AO.SIZE;
			VAR sigLen : AO.SIZE);
		VAR
			em : ARRAY MaxModulusBytes OF SHORTCHAR;
			emLen, k : AO.SIZE;
			clear, signed : AosBigNumbers.BigNumber;


	BEGIN
		sigLen := 0;
		IF (key = NIL) OR ~key.private THEN
			RETURN
		END;
		k := 4 * key.modulus.len;
		IF (k = 0) OR (k > MaxModulusBytes) THEN
			RETURN
		END;
		IF ~BuildEM(hashName, digest, digestLen, k, em, emLen) THEN
			RETURN
		END;
		AosBigNumbers.AssignBin(clear, em, 0, k);
		signed := key.Decrypt(clear);
		IF ToFixed(signed, sig, sigOfs, k) THEN
			sigLen := k
		END
	END SignHash;

	(* Проверка PKCS#1 v1.5. Сравнение EM с ожидаемым — постоянного
	   времени: подпись и digest не должны утекать по таймингу. *)
	PROCEDURE VerifyHash* (IN hashName, digest : ARRAY OF SHORTCHAR; digestLen : AO.SIZE;
			key : CryptoRSA.Key; IN sig : ARRAY OF SHORTCHAR; sigOfs, sigLen : AO.SIZE): BOOLEAN;
		VAR
			em, got : ARRAY MaxModulusBytes OF SHORTCHAR;
			emLen, k : AO.SIZE;
			enc, recovered : AosBigNumbers.BigNumber;


	BEGIN
		IF (key = NIL) OR (sigLen <= 0) OR (sigOfs < 0) OR (sigOfs + sigLen > LEN(sig)) THEN
			RETURN FALSE
		END;
		k := 4 * key.modulus.len;
		IF (k = 0) OR (k > MaxModulusBytes) OR (sigLen > k) THEN
			RETURN FALSE
		END;
		IF ~BuildEM(hashName, digest, digestLen, k, em, emLen) THEN
			RETURN FALSE
		END;
		AosBigNumbers.AssignBin(enc, sig, sigOfs, sigLen);
		IF enc.GEQ(key.modulus) THEN
			RETURN FALSE
		END;
		recovered := key.Encrypt(enc);
		IF ~ToFixed(recovered, got, 0, k) THEN
			RETURN FALSE
		END;
		RETURN CryptoUtils.EqualBytesCT(got, em, 0, 0, k)
	END VerifyHash;

END CryptoPKCS1.

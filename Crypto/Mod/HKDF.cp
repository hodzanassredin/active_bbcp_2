MODULE CryptoHKDF;
(** AUTHOR "Anton Shelin"; PURPOSE "HKDF (RFC 5869)"; *)

	IMPORT HMAC := CryptoHMAC, AO := CryptoAosCompat;

	(* HKDF-Extract: PRK = HMAC-Hash(salt, IKM). *)
	PROCEDURE Extract* (IN hashmod : ARRAY OF SHORTCHAR;
			IN salt : ARRAY OF SHORTCHAR; saltLen : AO.SIZE;
			IN ikm : ARRAY OF SHORTCHAR; ikmLen : AO.SIZE;
			OUT prk : ARRAY OF SHORTCHAR);
		VAR h : HMAC.HMac;
	BEGIN
		NEW(h);
		h.Init(hashmod);
		h.Initialize(salt, saltLen);
		h.Update(ikm, 0, ikmLen);
		h.GetMac(prk, 0)
	END Extract;

	(* HKDF-Expand: T(i) = HMAC-Hash(PRK, T(i-1) || info || i).
	   Возвращает FALSE, если запрошено больше 255 блоков хеша или
	   выход не помещается в okm. *)
	PROCEDURE Expand* (IN hashmod : ARRAY OF SHORTCHAR;
			IN prk : ARRAY OF SHORTCHAR; prkLen : AO.SIZE;
			IN info : ARRAY OF SHORTCHAR; infoLen : AO.SIZE;
			VAR okm : ARRAY OF SHORTCHAR; okmLen : AO.SIZE): BOOLEAN;
		VAR
			h : HMAC.HMac;
			t : ARRAY 64 OF SHORTCHAR;
			counter : ARRAY 1 OF SHORTCHAR;
			tLen, out, n, i, j : AO.SIZE;
	BEGIN
		IF (okmLen < 0) OR (okmLen > LEN(okm)) THEN
			RETURN FALSE
		END;
		NEW(h);
		h.Init(hashmod);
		IF okmLen > 255 * h.size THEN
			RETURN FALSE
		END;
		tLen := 0;
		out := 0;
		i := 1;
		WHILE out < okmLen DO
			h.Initialize(prk, prkLen);
			IF tLen > 0 THEN
				h.Update(t, 0, tLen)
			END;
			IF infoLen > 0 THEN
				h.Update(info, 0, infoLen)
			END;
			counter[0] := SHORT(CHR(i));
			h.Update(counter, 0, 1);
			h.GetMac(t, 0);
			tLen := h.size;

			n := tLen;
			IF n > okmLen - out THEN
				n := okmLen - out
			END;
			FOR j := 0 TO n - 1 DO
				okm[out + j] := t[j]
			END;
			INC(out, n);
			INC(i)
		END;
		RETURN TRUE
	END Expand;

	(* Extract и Expand одним вызовом. *)
	PROCEDURE HKDF* (IN hashmod : ARRAY OF SHORTCHAR;
			IN salt : ARRAY OF SHORTCHAR; saltLen : AO.SIZE;
			IN ikm : ARRAY OF SHORTCHAR; ikmLen : AO.SIZE;
			IN info : ARRAY OF SHORTCHAR; infoLen : AO.SIZE;
			VAR okm : ARRAY OF SHORTCHAR; okmLen : AO.SIZE): BOOLEAN;
		VAR
			prk : ARRAY 64 OF SHORTCHAR;
			h : HMAC.HMac;
	BEGIN
		NEW(h);
		h.Init(hashmod);
		Extract(hashmod, salt, saltLen, ikm, ikmLen, prk);
		RETURN Expand(hashmod, prk, h.size, info, infoLen, okm, okmLen)
	END HKDF;

END CryptoHKDF.

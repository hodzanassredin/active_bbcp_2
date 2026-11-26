MODULE CryptoECDSA;
(** PURPOSE "ECDSA на P-256/P-384 с детерминированным k (RFC 6979)"; *)

	IMPORT EC := CryptoEC, HMAC := CryptoHMAC, U := CryptoUtils;

	(* Хеш выбирается по кривой: SHA-256 для P-256, SHA-384 для P-384.
	   Длина хеша совпадает с длиной порядка, поэтому bits2int и
	   bits2octets из RFC 6979 сводятся к чтению байтов и одному
	   вычитанию порядка. *)

	PROCEDURE HashName (c : EC.Curve; VAR name : ARRAY OF SHORTCHAR);
	BEGIN
		IF c.nbits = 256 THEN
			name := "CryptoSHA256"
		ELSE
			name := "CryptoSHA384"
		END
	END HashName;

	PROCEDURE Mac (c : EC.Curve; IN key : ARRAY OF SHORTCHAR; keyLen : INTEGER;
			IN data : ARRAY OF SHORTCHAR; dataLen : INTEGER;
			VAR out : ARRAY OF SHORTCHAR);
		VAR h : HMAC.HMac;
			name : ARRAY 32 OF SHORTCHAR;
	BEGIN
		HashName(c, name);
		NEW(h);
		h.Init(name);
		h.Initialize(key, keyLen);
		h.Update(data, 0, dataLen);
		h.GetMac(out, 0)
	END Mac;

	(* K = HMAC_K(V || sep || x || h). Пустые x или h не добавляются;
	   при вызове из шага повтора передаются нулевые длины. *)
	PROCEDURE Rekey (c : EC.Curve; IN K : ARRAY OF SHORTCHAR; rlen : INTEGER;
			IN V : ARRAY OF SHORTCHAR; sep : SHORTCHAR;
			IN x : ARRAY OF SHORTCHAR; xLen : INTEGER;
			IN h : ARRAY OF SHORTCHAR; hLen : INTEGER;
			VAR Kout : ARRAY OF SHORTCHAR);
		VAR hh : HMAC.HMac;
			name : ARRAY 32 OF SHORTCHAR;
			one : ARRAY 1 OF SHORTCHAR;
	BEGIN
		HashName(c, name);
		NEW(hh);
		hh.Init(name);
		one[0] := sep;
		hh.Initialize(K, rlen);
		hh.Update(V, 0, rlen);
		hh.Update(one, 0, 1);
		IF xLen > 0 THEN
			hh.Update(x, 0, xLen)
		END;
		IF hLen > 0 THEN
			hh.Update(h, 0, hLen)
		END;
		hh.GetMac(Kout, 0)
	END Rekey;

	PROCEDURE SameBytes (IN a, b : ARRAY OF SHORTCHAR; len : INTEGER): BOOLEAN;
	BEGIN
		RETURN U.EqualBytesCT(a, b, 0, 0, len)
	END SameBytes;

	(* Подпись: hash — готовое значение хеша длиной 4*n байт.
	   k детерминирован: тот же ключ и то же сообщение дают ту же
	   подпись (RFC 6979). *)
	PROCEDURE Sign* (c : EC.Curve; IN priv : EC.BigInt;
			IN hash : ARRAY OF SHORTCHAR; hashLen : INTEGER;
			VAR r, s : EC.BigInt): BOOLEAN;
		VAR
			rlen, i : INTEGER;
			x, hmod, K, V, T : ARRAY 64 OF SHORTCHAR;
			e, k, rx, ry, kinv, t : EC.BigInt;
			ok : BOOLEAN;
	BEGIN
		rlen := 4 * c.n;
		IF (hashLen # rlen) OR ~EC.OrderInRange(c, priv) THEN
			RETURN FALSE
		END;

		EC.OrderBytes(c, priv, x, 0);
		IF ~EC.OrderFromBytes(c, hash, 0, hashLen, e) THEN
			RETURN FALSE
		END;
		EC.ReduceOrder(c, e, e);
		EC.OrderBytes(c, e, hmod, 0);

		FOR i := 0 TO rlen - 1 DO
			V[i] := 1X;
			K[i] := 0X;
			T[i] := 0X
		END;
		Rekey(c, K, rlen, V, 0X, x, rlen, hmod, rlen, K);
		Mac(c, K, rlen, V, rlen, V);
		Rekey(c, K, rlen, V, 1X, x, rlen, hmod, rlen, K);
		Mac(c, K, rlen, V, rlen, V);

		LOOP
			Mac(c, K, rlen, V, rlen, V);
			FOR i := 0 TO rlen - 1 DO
				T[i] := V[i]
			END;
			IF EC.OrderFromBytes(c, T, 0, rlen, k) & EC.OrderInRange(c, k) THEN
				EC.ScalarMulBase(c, k, rx, ry, ok);
				IF ok THEN
					EC.ReduceOrder(c, rx, r);
					IF ~EC.OrderIsZero(c, r) THEN
						EC.OrderMul(c, r, priv, t);
						EC.OrderAdd(c, e, t, t);
						EC.OrderInv(c, k, kinv);
						EC.OrderMul(c, kinv, t, s);
						IF ~EC.OrderIsZero(c, s) THEN
							RETURN TRUE
						END
					END
				END
			END;
			(* Новое k по RFC 6979: K = HMAC_K(V || 0x00), V = HMAC_K(V). *)
			Rekey(c, K, rlen, V, 0X, x, 0, hmod, 0, K);
			Mac(c, K, rlen, V, rlen, V)
		END
	END Sign;

	(* Проверка подписи; Q проверяется на принадлежность кривой. *)
	PROCEDURE Verify* (c : EC.Curve; IN qx, qy : EC.BigInt;
			IN hash : ARRAY OF SHORTCHAR; hashLen : INTEGER;
			IN r, s : EC.BigInt): BOOLEAN;
		VAR
			e, w, u1, u2, x, y, rx : EC.BigInt;
			ok : BOOLEAN;
			rb, rxb : ARRAY 64 OF SHORTCHAR;
	BEGIN
		IF (hashLen # 4 * c.n) OR ~EC.Validate(c, qx, qy) THEN
			RETURN FALSE
		END;
		IF ~EC.OrderInRange(c, r) OR ~EC.OrderInRange(c, s) THEN
			RETURN FALSE
		END;
		IF ~EC.OrderFromBytes(c, hash, 0, hashLen, e) THEN
			RETURN FALSE
		END;
		EC.ReduceOrder(c, e, e);
		EC.OrderInv(c, s, w);
		EC.OrderMul(c, e, w, u1);
		EC.OrderMul(c, r, w, u2);
		EC.ScalarMulAdd(c, u1, qx, qy, u2, x, y, ok);
		IF ~ok THEN
			RETURN FALSE
		END;
		EC.ReduceOrder(c, x, rx);
		EC.OrderBytes(c, r, rb, 0);
		EC.OrderBytes(c, rx, rxb, 0);
		RETURN SameBytes(rb, rxb, 4 * c.n)
	END Verify;

END CryptoECDSA.

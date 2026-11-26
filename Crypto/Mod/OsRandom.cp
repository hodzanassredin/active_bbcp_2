MODULE CryptoOsRandom;
(**
	Энтропия операционной системы для ключей и ГСЧ.

	Читает /dev/urandom через LinLibc. Возвращает FALSE, только если
	источник недоступен или прочитано меньше запрошенного; вызывающий
	код обязан в этом случае отказаться от работы с ключом, а не
	подставлять предсказуемые байты.

	Модуль Linux-специфичен: Crypto собирается только для Linux amd64
	(в Windows-сборку подсистема не входит).
*)

	IMPORT SYSTEM, Libc := LinLibc, AO := CryptoAosCompat;

	CONST
		Urandom = "/dev/urandom";

	PROCEDURE Fill* (VAR buf : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE) : BOOLEAN;
		VAR
			f : Libc.PtrFILE;
			got : Libc.size_t;
			ok : BOOLEAN;
	BEGIN
		ok := FALSE;
		IF len = 0 THEN
			RETURN TRUE
		END;
		IF (ofs < 0) OR (len < 0) OR (ofs + len > LEN(buf)) THEN
			RETURN FALSE
		END;
		f := Libc.fopen(Urandom, "rb");
		IF f # Libc.NULL THEN
			got := Libc.fread(SYSTEM.ADR(buf[ofs]), 1, len, f);
			IF got = len THEN
				ok := TRUE
			END;
			ok := (Libc.fclose(f) = 0) & ok
		END;
		RETURN ok
	END Fill;

END CryptoOsRandom.

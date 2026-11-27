MODULE Actor_devTls;
(** Тесты списка допущенных узлов ActorNetTls.

	Рукопожатия здесь не проверяются: проверяется сам список. Отзывом
	считается уход узла, который был в прежнем списке; запись того же
	списка счётчик не двигает, а вернувшийся узел снова допущен (иначе
	закрытая когда-то связь не открылась бы никогда). *)

	IMPORT ActorNetTls, CommStreams, UnitAssert;

	PROCEDURE Set (OUT m: ActorNetTls.Member; IN name: ARRAY OF CHAR; b: BYTE);
		VAR i: INTEGER;
	BEGIN
		m.name := name$;
		FOR i := 0 TO LEN(m.fp) - 1 DO m.fp[i] := b END
	END Set;

	PROCEDURE Revoked (net: ActorNetTls.Net): LONGINT;
		VAR sent, read, refused, forged, rejected, revoked: LONGINT;
	BEGIN
		ActorNetTls.Stats(net, sent, read, refused, forged, rejected, revoked);
		RETURN revoked
	END Revoked;

	PROCEDURE TestRevoke*;
	(* Список: отзыв только ушедшего, повтор списка не считает, вернувшийся
	   допущен, повторный уход снова считается. *)
		VAR net: ActorNetTls.Net; res: INTEGER; m: ARRAY 3 OF ActorNetTls.Member;
	BEGIN
		ActorNetTls.New("127.0.0.1:1", net, res);
		UnitAssert.EqualInt(CommStreams.done, res, "сеть");
		Set(m[0], "G0", 10); Set(m[1], "G1", 11); Set(m[2], "G2", 12);
		ActorNetTls.Peer(net, "G1", "127.0.0.1:2");
		ActorNetTls.Peer(net, "G2", "127.0.0.1:3");
		net.SetMembers(m, 3);
		UnitAssert.EqualInt(0, Revoked(net), "первый список никого не отзывает");
		net.SetMembers(m, 2);	(* G2 ушёл *)
		UnitAssert.EqualInt(1, Revoked(net), "уход G2 отозван");
		net.SetMembers(m, 2);
		UnitAssert.EqualInt(1, Revoked(net), "повтор списка не отзывает");
		net.SetMembers(m, 3);	(* G2 вернулся *)
		net.SetMembers(m, 1);	(* ушли G1 и G2 *)
		UnitAssert.EqualInt(3, Revoked(net), "повторный уход G1 и G2 отозван")
	END TestRevoke;

	PROCEDURE TestFileThenList*;
	(* Как в прогоне: сначала полный список сертификатов (14 имён), потом
	   состав из трёх, потом без G1. Уход G1 обязан быть отозван. *)
		VAR net: ActorNetTls.Net; res: INTEGER; m: ARRAY 16 OF ActorNetTls.Member;
	BEGIN
		ActorNetTls.New("127.0.0.1:21", net, res);
		Set(m[0], "G0", 1); Set(m[1], "G1", 2); Set(m[2], "G2", 3); Set(m[3], "R0", 4);
		Set(m[4], "R1", 5); Set(m[5], "R2", 6); Set(m[6], "K0", 7); Set(m[7], "K1", 8);
		Set(m[8], "K2", 9); Set(m[9], "S0", 10); Set(m[10], "S1", 11); Set(m[11], "S2", 12);
		Set(m[12], "S3", 13); Set(m[13], "S4", 14);
		ActorNetTls.Peer(net, "G1", "127.0.0.1:22");
		ActorNetTls.Peer(net, "G2", "127.0.0.1:23");
		net.SetMembers(m, 14);
		net.SetMembers(m, 2);	(* ушли все, кроме G0 и G1: G2 отозван *)
		UnitAssert.EqualInt(1, Revoked(net), "уход G2 из полного списка отозван");
		net.SetMembers(m, 3);	(* G2 вернулся *)
		net.SetMembers(m, 2);	(* G1 ушёл *)
		UnitAssert.EqualInt(2, Revoked(net), "уход G1 после возврата G2 отозван")
	END TestFileThenList;

	PROCEDURE TestEmpty*;
	(* Пустой список: все связи закрыты, но отзывом считается только
	   прежний состав. *)
		VAR net: ActorNetTls.Net; res: INTEGER; m: ARRAY 2 OF ActorNetTls.Member;
	BEGIN
		ActorNetTls.New("127.0.0.1:11", net, res);
		Set(m[0], "G0", 10); Set(m[1], "G1", 11);
		ActorNetTls.Peer(net, "G1", "127.0.0.1:12");
		net.SetMembers(m, 2);
		net.SetMembers(m, 0);
		UnitAssert.EqualInt(1, Revoked(net), "пустой список отозвал G1");
		net.SetMembers(m, 0);
		UnitAssert.EqualInt(1, Revoked(net), "пустой список повторно не отзывает")
	END TestEmpty;

END Actor_devTls.

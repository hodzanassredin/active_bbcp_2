MODULE CryptoAosTCP;

	(**
	Version: 16 August 2023 (see Crypto/Docu/Crypto.md)
	
	AUTHOR: Shelin Anton, 2023
	PURPOSE: BB TCP CommStreams aos wrapper
	*)

	IMPORT StdLog, CryptoAosCommStreams, AO := CryptoAosCompat, Streams := CryptoAosStreams, CommStreams;

	CONST
		protocol = "CommTCP";

		NilPort* = 0;

		(** Error codes *)
		Ok* = 0;

		(** TCP connection states *)

		Closed* = 0;
		Listen* = 1;
		Established* = 4;
		Unused* = 11; (* no real state, only used in this implementation *)

	TYPE
		Connection* = POINTER TO EXTENSIBLE RECORD(Streams.Connection)
			state*: AO.SIGNED8;
			r: Streams.Reader;
			w: Streams.Writer;
			s: CommStreams.Stream;
			l: CommStreams.Listener;
			client : BOOLEAN;
			localAdr*, remoteAdr*: POINTER TO ARRAY OF CHAR;
			timeoutSeconds* : INTEGER;
		END;

	PROCEDURE (c: Connection) Open* (localAdr, remoteAdr: ARRAY OF CHAR; VAR res: INTEGER), NEW, EXTENSIBLE;
		VAR i: INTEGER;
	BEGIN
		NEW(c.localAdr, LEN(localAdr)); ;
		FOR i := 0 TO LEN(localAdr) - 1 DO c.localAdr[i] := localAdr[i] END;

		NEW(c.remoteAdr, LEN(remoteAdr));
		FOR i := 0 TO LEN(remoteAdr) - 1 DO c.remoteAdr[i] := remoteAdr[i] END;

		c.client := LEN(remoteAdr$) # 0;
		ASSERT((c.state = Unused));
		IF c.client THEN
			StdLog.String("Opening stream to " + remoteAdr$);StdLog.Ln;
			CommStreams.NewStream(protocol, localAdr, remoteAdr, c.s, res);
			IF res = 0 THEN
				ASSERT(c.s # NIL);
				CryptoAosCommStreams.OpenReader(c.r, c.s, 0, c.timeoutSeconds);
				CryptoAosCommStreams.OpenWriter(c.w, c.s, 0);
				c.receiver := c.r.receive;
				c.sender := c.w.send;
				c.state := Established;
			END;
		ELSE	
			StdLog.String("Listening on port" + localAdr$);StdLog.Ln;
			CommStreams.NewListener(protocol, localAdr, c.l, res);
			IF res = 0 THEN
				ASSERT(c.l # NIL);
				c.state := Listen;
			END;
		END;
	END Open;

	(** Accept a client waiting on a listening connection. Never blocks*)
	PROCEDURE (c: Connection) Accept* (VAR client: Connection; VAR res: INTEGER), NEW, EXTENSIBLE;
	BEGIN
		res := (-1);
		c.l.Accept(client.s);
		IF client.s # NIL THEN
			StdLog.String("Accepted TCP client");
			CryptoAosCommStreams.OpenReader(client.r, client.s, 0, c.timeoutSeconds);
			CryptoAosCommStreams.OpenWriter(client.w, client.s, 0);
			client.receiver := client.r.receive;
			client.sender := client.w.send;
			client.state := Established;
			res := Ok;
		END;

	END Accept;

	PROCEDURE (s: Connection) Close* (), EXTENSIBLE;
	BEGIN
		IF s.s # NIL THEN s.s.Close(); END;
		s.state := Closed;
	END Close;

	PROCEDURE (SELF: Connection) State* (): AO.SIGNED32, NEW;
	BEGIN
		IF (SELF.state = Established) & (SELF.s # NIL) & (~SELF.s.IsConnected()) THEN
			SELF.Close
		END;
		RETURN SELF.state
	END State;

	PROCEDURE (SELF: Connection) Init*, NEW, EXTENSIBLE;
	BEGIN
		SELF.state := Unused;
		SELF.timeoutSeconds := 10;
	END Init;

	PROCEDURE (s: Connection) IsConnected* (): BOOLEAN, NEW, EXTENSIBLE;
	BEGIN
		RETURN s.s.IsConnected();
	END IsConnected;

	PROCEDURE (s: Connection) Alive* (): BOOLEAN, NEW;
	(** Пост: TRUE — поток соединения есть и не оборван; FALSE — соединение
		уже закрыто или не открывалось. Не разыменовывает NIL при Close. *)
	BEGIN
		RETURN (s.s # NIL) & s.s.IsConnected()
	END Alive;

	PROCEDURE GetConnection* (): Connection;
		VAR con: Connection;
	BEGIN
		NEW(con); con.Init();
		RETURN con
	END GetConnection;

END CryptoAosTCP.

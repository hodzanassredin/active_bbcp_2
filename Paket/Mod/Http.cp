MODULE PaketHttp;
(**
	project	= "BlackBox 2.0, new module"
	organization	= "BlackBox Cross-Platform Team"
	contributors	= "Ivan Denisov, Anton Shelin"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		CommStreams, Log, Files, Services, Strings, Views, Ports,
		Fonts, Properties, Dialog, Models, StdLog;

	CONST
		protocol = "CommTCP";
		protocolTLS = "CryptoTLSStream";
		bufferSize = 1048576; (* income data buffer *)
		debug = FALSE;
		timeOut = 60; (* Время ожидания пакетов при разрыве *)
		
		firstHeader = 0;
		completingNormal = 1;
		completingChunked = 2;
		startingChunked = 3;
		error = 4;
		redirected = 5;
		
		timeoutError* = 21;
		unknownHeaderError* = 22;
		relocationError* = 23;
		
		silent* = TRUE;
		progress* = FALSE;
	
	TYPE
		Widget = POINTER TO RECORD (Views.View)
			d: Downloading
		END;
		Downloading* = POINTER TO RECORD
			file*: Files.File;
			ready-, uploading-, silent*: BOOLEAN;
			widget: Widget;
			totalSize-, downloaded-, error-: INTEGER;
			created-, begin-, finish-: LONGINT;
			onFinish*: PROCEDURE (f: Files.File; data: ARRAY OF CHAR);
			url-, data*, info*: ARRAY 256 OF CHAR;
		END;
		Reader = POINTER TO RECORD (Services.Action)
			buf: POINTER TO ARRAY OF BYTE;
			download: Downloading;
			wr: Files.Writer;
			rpos, len, state: INTEGER;
			start, last: LONGINT;
			ready: BOOLEAN;
			s: CommStreams.Stream;
		END;
		Writer = POINTER TO RECORD (Services.Action)
			buf: POINTER TO ARRAY OF BYTE;
			pos: INTEGER;
			s: CommStreams.Stream;
			r : Reader;
		END;
		Stream = POINTER TO RECORD
			s: CommStreams.Stream;
			address: ARRAY 50 OF CHAR;
			r: Reader;
			w: Writer;
		END;
		Msg = RECORD (Models.Message)
			consumed: BOOLEAN
		END;
		
	VAR
		font*: Fonts.Font;

	PROCEDURE NewDownloading* (url: ARRAY OF CHAR): Downloading;
		VAR download: Downloading;
	BEGIN
		NEW(download);
		download.file := Files.dir.Temp();
		download.ready := FALSE;
		download.uploading := FALSE;
		download.downloaded := 0;
		download.totalSize := 0;
		download.url := url$;
		download.onFinish := NIL;
		download.error := 0;
		download.silent := FALSE;
		NEW(download.widget);
		download.widget.d :=download;
		download.created := Services.Ticks();
		RETURN download
	END NewDownloading;

	PROCEDURE WriteString (r: ARRAY OF CHAR; VAR b: ARRAY OF BYTE; pos: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO LEN(r) - 2 DO
			IF r[i] < 100X THEN
				b[i + pos] := SHORT(SHORT(ORD(r[i])))
			END
		END
	END WriteString;

	PROCEDURE (s: Stream) Connect (host: ARRAY OF CHAR; https: BOOLEAN), NEW;
		VAR res: INTEGER;
	BEGIN
		IF https THEN
			s.address := host$ + ":443";
			CommStreams.NewStream(protocolTLS, "0.0.0.0:0", s.address, s.s, res);
			IF res # 0 THEN
				StdLog.String("Cant connect to https server. Probably you need to install Crypto module."); StdLog.Ln;
			END;
		ELSE
			s.address := host$ + ":80";
			CommStreams.NewStream(protocol, "0.0.0.0:0", s.address, s.s, res);
		END;
		
		IF (s.s # NIL) & (res = 0) THEN
			NEW(s.r);
			NEW(s.w);

			s.r.s := s.s;
			s.w.s := s.s;
			s.w.r := s.r
		END
	END Connect;

	PROCEDURE (s: Stream) Send (a: ARRAY OF CHAR), NEW;
	BEGIN
		NEW(s.w.buf, LEN(a) - 1);
		WriteString(a, s.w.buf, 0);
		s.w.pos := 0;
		Services.DoLater(s.w, Services.now)
	END Send;

	PROCEDURE (w: Writer) Do;
		VAR written: INTEGER;
	BEGIN
		ASSERT(w.s # NIL, 21);
		IF (w.buf # NIL) THEN
			IF w.pos = 0 THEN
				w.s.WriteBytes(w.buf, 0, LEN(w.buf), written);
				w.pos := w.pos + written
			ELSE
				w.s.WriteBytes(w.buf, w.pos, LEN(w.buf) - w.pos, written);
				w.pos := w.pos + written
			END
		END;
		IF w.pos = LEN(w.buf) THEN
			Services.RemoveAction(w);
			Services.DoLater(w.r, Services.now)
		ELSE
			(* Log.String("> "); (* Для отладки проблем с отправкой *) *)
			Services.DoLater(w, Services.now)
		END
	END Do;


	PROCEDURE (d: Downloading) Start*, NEW;
		VAR stream: Stream; host, par: ARRAY 255 OF CHAR;
			res, i, j, shift: INTEGER; msg: Msg;
			title: ARRAY 64 OF CHAR;
			https: BOOLEAN;
	BEGIN
		(* Анализ URL, опеделение хоста, имени файла, если необходимо *)
		ASSERT(LEN(d.url) > 1);
		Strings.Extract(d.url, 0, 7, host);
		IF host$ = "http://" THEN 
			shift := 7; 
			https := FALSE;
		ELSIF host$ = "https:/" THEN 
			shift := 8; 
			https := TRUE;
		ELSE 
			shift := 0;
			https := FALSE;
		END;
		i := 0;
		WHILE (d.url[i + shift] # "/") & (i < LEN(d.url$)) DO
			host[i] := d.url[i + shift];
			INC(i)
		END;
		host[i] := 0X;
		INC(i);
		j := 0;
		WHILE (i < LEN(d.url$) - shift) DO
			par[j] := d.url[i + shift];
			INC(i);
			INC(j)
		END;
		par[j] := 0X;

		IF ~ d.silent THEN
			d.info := "Connecting with: " + host;
			msg.consumed := FALSE;
			Views.Omnicast(msg); (* Проверяем открыто ли окно *)
			IF ~ msg.consumed THEN
				IF LEN(d.url$) > 40 THEN
					Strings.Extract(d.url$, LEN(d.url$) - 40, 40, title)
				ELSE
					title := d.url$
				END;
				Views.OpenAux(d.widget, title$);
				Views.Update(d.widget, Views.keepFrames)
			END
		END;

		(* Cоздание потока и его окружения *)
		NEW(stream);
		stream.Connect(host, https);
		
		IF stream.s # NIL THEN
			stream.r.download := d;
			stream.r.wr := stream.r.download.file.NewWriter(NIL);
			stream.Send("GET /"+par$+" HTTP/1.1"+0DX+0AX+
				"Host: "+host$+0DX+0AX+0DX+0AX);
		ELSE
			IF ~ d.silent THEN
				Dialog.GetOK("Can not connect to: " + host + ". Aborting!", "","", "", {Dialog.ok}, res)
			END
		END;
	END Start;

	PROCEDURE Save (f: Files.File; name: ARRAY OF CHAR);
	VAR res: INTEGER;
	BEGIN
		f.Register(name$, "", Files.dontAsk, res)
	END Save;

	PROCEDURE Download* (IN url, savePath, saveName: ARRAY OF CHAR;
		silent: BOOLEAN);
		VAR name: ARRAY 255 OF CHAR; i, shift: INTEGER;
			d: Downloading;
	BEGIN
		IF saveName$ = "" THEN
			shift := 0;
			FOR i := 0 TO LEN(url$) - 1 DO IF url[i] = "/" THEN shift := i + 1 END END;
			i := 0;
			WHILE url[i + shift] # 0X DO name[i] := url[shift + i]; INC(i) END;
			name[i] := 0X
		ELSE
			name := saveName$
		END;
		d := NewDownloading(url);
		d.onFinish := Save;
		d.data := name$;
		d.silent := silent;
		d.file := Files.dir.New(Files.dir.This(savePath), Files.dontAsk);
		d.Start;
	END Download;
	
	PROCEDURE LogHead(in: ARRAY OF CHAR);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE i < LEN(in) DO
			IF (in[i] = 0DX) OR (in[i] = 0X) THEN
				Log.Ln;
				INC(i)
			ELSE
				Log.Char(in[i])
			END;
			INC(i)
		END
	END LogHead;
	

	PROCEDURE (r: Reader) Do;
		VAR	
			read, i, j, k, pos, res, dataStarts, headerStarts, blockCounter: INTEGER;
			head: POINTER TO ARRAY OF CHAR;
			len, tmp: ARRAY 256 OF CHAR; chunkSizeL: LONGINT; speed: REAL;
			foundEnd, blockDone: BOOLEAN;
			
			PROCEDURE MultyPack;
			BEGIN
				WHILE ~ blockDone DO
					j := 0;
					i := headerStarts + 2;
					WHILE r.buf[i] # ORD(0DX) DO
						IF debug THEN Log.Char(CHR(r.buf[i])) END;
						len[j] := CHR(r.buf[i]); INC(i); INC(j)
					END; len[j] := 0X;
					IF debug THEN Log.Ln END;
					dataStarts := i + 2;
					Strings.ToUpper(len, len);
					Strings.StringToLInt(len$ + "H" + 0X, chunkSizeL, res);
					r.len := SHORT(chunkSizeL);
					r.rpos := 0;
					IF debug THEN Log.String("Должен записать: "); Log.Int(r.len); Log.Ln END;
					
					IF dataStarts + r.len > read THEN
						(* Покет в буфере не полностью *)
						IF debug THEN
							Log.String("Записываем хвост и ждем "); Log.Int(r.len); Log.Ln
						END;
					  r.wr.WriteBytes(r.buf, dataStarts, read - dataStarts);
						INC(r.rpos, read - dataStarts);
						blockDone := TRUE;
					ELSIF dataStarts + r.len = read THEN
						(* Пакет полностью и ничего больше нет *)
						r.wr.WriteBytes(r.buf, dataStarts, r.len);
						r.rpos := r.len;
						IF debug THEN Log.String("Ждем заголовок следующего блока"); Log.Ln END;
						r.state := startingChunked;
						blockDone := TRUE;
					ELSE
						(* Кроме пакета в буфере еще есть данные *)
						headerStarts := dataStarts + r.len;
						r.wr.WriteBytes(r.buf, dataStarts, r.len);
						r.rpos := r.len;
						IF (read - headerStarts = 2) & (CHR(r.buf[headerStarts]) = 0DX) &
							(CHR(r.buf[headerStarts + 1]) = 0AX) THEN
								r.ready := TRUE;
								blockDone := TRUE
						ELSE
					  	IF debug THEN
								Log.String("Еще данных "); Log.Int(read - headerStarts); Log.Ln
							END
						END
					END
				END
			END MultyPack;
			
	BEGIN
		IF r.s # NIL THEN
			IF r.buf = NIL THEN
				NEW(r.buf, bufferSize);
				r.state := firstHeader;
				r.start := Services.Ticks();
				r.download.begin := r.start;
				r.last := Services.Ticks()
			END;
			
			IF ~ r.ready THEN
			
				r.s.ReadBytes(r.buf, 0, LEN(r.buf), read);
				IF debug THEN Log.String("Принял: "); Log.Int(read); Log.Ln END;

				IF read # 0 THEN
					r.last := Services.Ticks(); (* Обновить время последнего приема *)
					
					IF (r.state = firstHeader) OR (r.state = startingChunked) THEN

						(* Вычисляем местоположение конца заголовка в буфере  *)
						i := 0;
						foundEnd := FALSE;
						WHILE (i < read) & ~ foundEnd DO
							foundEnd :=
								(CHR(r.buf[i]) = 0DX) &
								(CHR(r.buf[i + 1]) = 0AX) &
								(CHR(r.buf[i + 2]) = 0DX) &
								(CHR(r.buf[i + 3]) = 0AX);
							INC(i)
						END;
						ASSERT(foundEnd, 21);
						dataStarts := i + 3;
						(* Копируем заголовок в строку из буфера *)
						NEW(head, i);
						FOR i := 0 TO LEN(head) - 2 DO
							head[i] := CHR(r.buf[i])
						END;
						head[LEN(head) - 1] := 0X;
						
						IF debug THEN LogHead(head) END;
						
						IF r.state = firstHeader THEN
							(* Обработка заголовка *)
							Strings.Find(head, "HTTP/1.1 200 OK", 0, pos);
							
							IF pos = 0 THEN
							
								(* Вычисляем тип передачи данных  *)
								Strings.Find(head, "Content-Length:", 0, pos);
								IF pos = - 1 THEN
									Strings.Find(head, "content-length:", 0, pos);
								END;
								IF pos # - 1 THEN
									r.state := completingNormal;
									IF debug THEN Log.String("Начало нормальной передачи"); Log.Ln END;
									(* Вычисляем длину данных, которые необходимо принять *)
									k := pos + 16;
									j := 0;
									WHILE CHR(r.buf[k]) # 0DX DO
										len[j] := CHR(r.buf[k]); INC(k); INC(j)
									END; len[j] := 0X;
									Strings.StringToInt(len, r.len, res);
									IF debug THEN
										Log.String("Должен принять: "); Log.Int(r.len); Log.Ln
									END;
								ELSE
								
									Strings.Find(head, "Transfer-Encoding: chunked", 0, pos);
									IF pos # - 1 THEN
										IF debug THEN Log.String("Начало блочной передачи"); Log.Ln END;
										blockCounter := 0;
										(* Первый кусок порционной передачи  *)
										r.state := completingChunked;
										(* Вычисляем длину данных, которые необходимо принять *)
										j := 0;
										i := dataStarts;
										WHILE r.buf[i] # ORD(0DX) DO
											len[j] := CHR(r.buf[i]); INC(i); INC(j)
										END; len[j] := 0X;
										dataStarts := i + 2;
										
										Strings.ToUpper(len, len);
										Strings.StringToLInt(len$ + "H" + 0X, chunkSizeL, res);
										r.len := SHORT(chunkSizeL);
										IF debug THEN
											Log.String("Должен принять: "); Log.Int(r.len); Log.Ln
										END;
									ELSE
										Log.String("Unknown header!"); Log.Ln;
										Log.String(head); Log.Ln;
										r.state := error;
										r.download.error := unknownHeaderError;
										r.ready := TRUE
									END
								END
							ELSE
								Strings.Find(head, "Location: ", 0, pos);
									IF pos # - 1 THEN
										i := pos + 10;
										j := 0;
										WHILE (head[i] # 0DX) & (head[i] # 0X) DO
											len[j] := head[i]; INC(i); INC(j);
										END;
										len[j] := 0X;
										IF ~ r.download.silent THEN
											r.download.info := "Redirected to: " + len;
											IF debug THEN  Log.String(r.download.info); Log.Ln END;
											Views.Update(r.download.widget, Views.keepFrames)
										END;
										r.download.url := len$;
										r.download.Start;
										r.len := 0;
										r.state := redirected;
										r.ready := TRUE
									ELSE
										Strings.Find(head, "HTTP/1.1 404 Not Found", 0, pos);
										IF pos # - 1 THEN
											IF ~ r.download.silent THEN
												r.download.info := "Error 404: file not found.";
												Views.Update(r.download.widget, Views.keepFrames)
											END;
											r.download.error := relocationError;
											r.state := error;
											r.ready := TRUE
										ELSE
											IF debug THEN Log.String("Unknown header!"); Log.Ln;
											Log.String(head); Log.Ln END;
											r.state := error;
											r.download.error := unknownHeaderError;
											r.ready := TRUE
										END
									END
							END
							
						ELSIF r.state = startingChunked THEN
							IF debug THEN Log.String("Начинаю новый блок"); Log.Ln END;
							(* Новый кусок порционной передачи  *)
							r.state := completingChunked;
							INC(blockCounter);
							(* Вычисляем длину данных, которые необходимо принять *)
							j := 0;
							i := 2;
							WHILE r.buf[i] # ORD(0DX) DO
								len[j] := CHR(r.buf[i]); INC(i); INC(j)
							END; len[j] := 0X;
							dataStarts := i + 2;
							
							Strings.ToUpper(len, len);
							Strings.StringToLInt(len$ + "H" + 0X, chunkSizeL, res);
							r.len := SHORT(chunkSizeL);
							IF debug THEN Log.String("Должен принять: "); Log.Int(r.len); Log.Ln END;
							r.ready := r.len = 0;
						END;

						(* записываем в файл *)
						IF r.len # 0 THEN
							IF read - dataStarts <= r.len THEN
								r.wr.WriteBytes(r.buf, dataStarts, read - dataStarts);
								IF debug THEN
									Log.String("Записал: "); Log.Int(read - dataStarts); Log.Ln
								END;
								r.rpos := read - dataStarts;
								IF (r.len = r.rpos) & (r.state = completingNormal) THEN
									r.ready := TRUE
								ELSE
									IF debug THEN
										Log.String("Осталось 1: "); Log.Int(r.len - r.rpos); Log.Ln
									END;
								END
							ELSE
								IF debug THEN
									Log.String("В первом блоке несколько пакетов "); Log.Ln
								END;
								headerStarts := dataStarts + r.len;
								r.wr.WriteBytes(r.buf, dataStarts, r.len);
								r.rpos := r.len;
								blockDone := FALSE;
								MultyPack() (* Разбор остатков пакета *)
							END
						END

					ELSE (* completing *)
						
						IF r.rpos + read > r.len THEN
							headerStarts := r.len - r.rpos;
							r.wr.WriteBytes(r.buf, 0, headerStarts);
							r.rpos := r.len;
							blockDone := FALSE;	
							MultyPack() (* Разбор остатков пакета *)
						ELSE
							r.wr.WriteBytes(r.buf, 0, read);
							IF r.rpos + read = r.len THEN
								r.rpos := r.len;
								IF r.state = completingNormal THEN
									IF debug THEN
										Log.String("Нормальное завершение передачи"); Log.Ln
									END;
									r.ready := TRUE
								ELSIF r.state = completingChunked THEN
									IF debug THEN
										Log.String("Ждем заголовок следующего блока"); Log.Ln
									END;
									r.state := startingChunked
								END
							ELSE
								INC(r.rpos, read)
							END
						END
					END
				END
			END
		ELSE
			Log.String("reader: stream = NIL"); Log.Ln
		END;
		
		(* Статистика *)
		IF r.len > 0 THEN
			r.download.downloaded := r.rpos;
			r.download.totalSize := r.len;
			speed := r.rpos / (Services.Ticks() - r.start) * 1000;
			IF ~ r.download.silent THEN
				Strings.RealToStringForm(speed / 1024, 8, 7, - 1, " ", r.download.info);
				r.download.info := "Speed:" + r.download.info + " KB/s.";
				Views.Update(r.download.widget, Views.keepFrames)
			END
		ELSE
			IF ~ r.download.silent THEN
				Strings.IntToString(SHORT(ENTIER((Services.Ticks() - r.last) / 1000)), tmp);
				r.download.info := "Waiting ... " + tmp + " s";
				Views.Update(r.download.widget, Views.keepFrames)
			END
		END;
		
		(* Финализация по завершению *)
		IF r.ready THEN
			r.s.Close;
			IF r.state # redirected THEN
				r.download.finish := Services.Ticks();
				r.download.ready := TRUE;
				r.download.downloaded := r.download.totalSize;
				IF ~ r.download.silent THEN
					Strings.RealToStringForm((r.download.finish - r.download.begin) / 1000,
						8, 7, - 3, " ", tmp);
					r.download.info := r.download.info + ". Loaded in " + tmp + " s.";
					Views.Update(r.download.widget, Views.keepFrames)
				END;
				IF r.state # error THEN
					r.download.onFinish(r.download.file, r.download.data)
				END;
			END;
			
			Services.RemoveAction(r);
		ELSIF (Services.Ticks() - r.last) / 1000 < timeOut THEN
			Services.DoLater(r, Services.now)
		ELSE
			IF ~ r.download.silent THEN
				r.download.info := "Wait timeout.";
				Views.Update(r.download.widget, Views.keepFrames)
			END;
			r.download.finish := Services.Ticks();
			r.download.error := timeoutError;
			r.s.Close();
			Services.RemoveAction(r)
		END
	END Do;

	PROCEDURE (a: Widget) HandlePropMsg- (VAR msg: Properties.Message);
		CONST w = 100 * Ports.mm; h = 10 * Ports.mm;
	BEGIN
		WITH msg: Properties.SizePref DO
			IF msg.w = Views.undefined THEN msg.w := w; msg.h := h END
		| msg: Properties.ResizePref DO
			msg.horFitToWin := TRUE; msg.verFitToWin := TRUE
		| msg: Properties.FocusPref DO
			msg.setFocus := TRUE
		ELSE END
	END HandlePropMsg;
	
	PROCEDURE (v: Widget) HandleModelMsg (VAR msg: Models.Message);
	BEGIN
		WITH msg: Msg DO
			msg.consumed := TRUE
		ELSE END
	END HandleModelMsg;

	PROCEDURE (v: Widget) Restore (f: Views.Frame; l_, t_, r_, b_: INTEGER);
	VAR width, height, w: INTEGER; tmp: ARRAY 128 OF CHAR;
	BEGIN
		v.context.GetSize(width, height);
		height := 50 * f.dot;
		
		IF (v.d # NIL) & (v.d.totalSize > 0) THEN
			w := SHORT(ENTIER(v.d.downloaded / v.d.totalSize * (width - 2 * f.dot)));
			IF w > 2 * f.dot THEN
				f.DrawRect(2 * f.dot, 14 * f.dot,
					SHORT(ENTIER(v.d.downloaded / v.d.totalSize * (width - 2 * f.dot))),
					height - 2 * f.dot, -1, Ports.RGBColor(100, 100, 250))
			END;
			f.DrawString(2 * f.dot, 10 * f.dot, Ports.black, v.d.info, font);
			Strings.IntToString(SHORT(ENTIER(v.d.downloaded / v.d.totalSize * 100)), tmp);
			f.DrawString(7 * f.dot, 30 * f.dot, Ports.white, tmp + " %", font)
		END
	END Restore;

BEGIN
	font := Fonts.dir.This("Verdana", 9  * Ports.point, {}, Fonts.normal)

END PaketHttp.


DevDebug.UnloadThis PaketObxHttp PaketDocTools PaketHttp
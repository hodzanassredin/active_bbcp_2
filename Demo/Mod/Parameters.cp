MODULE DemoParameters;

IMPORT DevCommanders, TextModels, Log;

PROCEDURE Do*;
	VAR
		p: DevCommanders.Par; 
		pars: ARRAY 1024 OF CHAR; n: INTEGER; rd: TextModels.Reader;
BEGIN
	p := DevCommanders.par;
	IF p # NIL THEN
		DevCommanders.par := NIL;
		rd := p.text.NewReader(NIL);
		rd.SetPos(p.beg);
		n := 0;
		rd.Read;
		WHILE (~rd.eot) & (rd.Pos() <= p.end) & (n < LEN(pars) - 1) DO			
			pars[n] := rd.char;
			INC(n);
			rd.Read;
		END;
		pars[n] := 0X;
		Log.String(pars); Log.Ln;
	END
END Do;

END DemoParameters.

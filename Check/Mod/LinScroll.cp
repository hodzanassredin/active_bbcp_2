MODULE CheckLinScroll;

	IMPORT TextModels, TextViews, Views;

	PROCEDURE Open*;
		VAR text: ARRAY 8201 OF CHAR; i,j,k: INTEGER; view: TextViews.View;
	BEGIN
		k := 0;
		FOR i := 0 TO 199 DO
			FOR j := 0 TO 39 DO text[k] := CHR(ORD("a")+j MOD 26); INC(k) END;
			text[k] := TextModels.line; INC(k)
		END; text[k] := 0X;
		view := TextViews.dir.New(TextModels.dir.NewFromString(text));
		Views.OpenAux(view,"GTK4 wheel");
	END Open;

END CheckLinScroll.

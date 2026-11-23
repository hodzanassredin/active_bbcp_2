MODULE XhtmlEntitySets;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	references	= "http://www.w3.org/TR/xhtml1/DTD/HTMLlat1x.ent,
		    http://www.w3.org/TR/xhtml1/DTD/HTMLspecialx.ent,
		    http://www.w3.org/TR/xhtml1/DTD/HTMLsymbolx.ent"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

**)

	PROCEDURE MapCharToEntity* (ch: CHAR; OUT s: ARRAY OF CHAR);
	BEGIN
		CASE ch OF
		| 0022X: s := "&quot;"
		| 0026X: s := "&amp;"
		| 0027X: s := "&apos;"
		| 003CX: s := "&lt;"
		| 003EX: s := "&gt;"
		ELSE s[0] := ch; s[1] := 0X
		END
	END MapCharToEntity;

END XhtmlEntitySets.

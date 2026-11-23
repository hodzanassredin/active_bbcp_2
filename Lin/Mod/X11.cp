MODULE LinX11 ["libX11.so.6"];

	IMPORT S := SYSTEM;

	PROCEDURE [ccall16] XDefaultRootWindow* (display: S.ADDRESS): S.ADDRESS;
	PROCEDURE [ccall16] XTranslateCoordinates* (display,source,destination: S.ADDRESS;
		x,y: INTEGER; OUT rootX,rootY: INTEGER; OUT child: S.ADDRESS): INTEGER;

END LinX11.

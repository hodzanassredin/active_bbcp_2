Stores64

DEFINITION Stores64;

	IMPORT Files, Files64, Stores;

	TYPE
		Reader = RECORD 
			rider-: Files64.Reader;
			cancelled-, readAlien-: BOOLEAN;
			(VAR rd: Reader) ConnectTo (f: Files64.File), NEW;
			(VAR rd: Reader) Pos (): LONGINT, NEW;
			(VAR rd: Reader) ReadBool (OUT x: BOOLEAN), NEW;
			(VAR rd: Reader) ReadByte (OUT x: BYTE), NEW;
			(VAR rd: Reader) ReadChar (OUT x: CHAR), NEW;
			(VAR rd: Reader) ReadInt (OUT x: INTEGER), NEW;
			(VAR rd: Reader) ReadLong (OUT x: LONGINT), NEW;
			(VAR rd: Reader) ReadReal (OUT x: REAL), NEW;
			(VAR rd: Reader) ReadSChar (OUT x: SHORTCHAR), NEW;
			(VAR rd: Reader) ReadSInt (OUT x: SHORTINT), NEW;
			(VAR rd: Reader) ReadSReal (OUT x: SHORTREAL), NEW;
			(VAR rd: Reader) ReadSString (OUT x: ARRAY OF SHORTCHAR), NEW;
			(VAR rd: Reader) ReadSet (OUT x: SET), NEW;
			(VAR rd: Reader) ReadStore (OUT x: Stores.Store), NEW;
			(VAR rd: Reader) ReadString (OUT x: ARRAY OF CHAR), NEW;
			(VAR rd: Reader) ReadXChar (OUT x: CHAR), NEW;
			(VAR rd: Reader) ReadXInt (OUT x: INTEGER), NEW;
			(VAR rd: Reader) ReadXReal (OUT x: REAL), NEW;
			(VAR rd: Reader) ReadXString (OUT x: ARRAY OF CHAR), NEW;
			(VAR rd: Reader) SetPos (pos: LONGINT), NEW
		END;

		Writer = RECORD 
			rider-: Files64.Writer;
			writtenStore-: Stores.Store;
			(VAR wr: Writer) ConnectTo (f: Files64.File), NEW;
			(VAR wr: Writer) Pos (): LONGINT, NEW;
			(VAR wr: Writer) SetPos (pos: LONGINT), NEW;
			(VAR wr: Writer) WriteBool (x: BOOLEAN), NEW;
			(VAR wr: Writer) WriteByte (x: BYTE), NEW;
			(VAR wr: Writer) WriteChar (x: CHAR), NEW;
			(VAR wr: Writer) WriteInt (x: INTEGER), NEW;
			(VAR wr: Writer) WriteLong (x: LONGINT), NEW;
			(VAR wr: Writer) WriteReal (x: REAL), NEW;
			(VAR wr: Writer) WriteSChar (x: SHORTCHAR), NEW;
			(VAR wr: Writer) WriteSInt (x: SHORTINT), NEW;
			(VAR wr: Writer) WriteSReal (x: SHORTREAL), NEW;
			(VAR wr: Writer) WriteSString (IN x: ARRAY OF SHORTCHAR), NEW;
			(VAR wr: Writer) WriteSet (x: SET), NEW;
			(VAR wr: Writer) WriteStore (x: Stores.Store), NEW;
			(VAR wr: Writer) WriteString (IN x: ARRAY OF CHAR), NEW;
			(VAR wr: Writer) WriteVersion (version: INTEGER), NEW;
			(VAR wr: Writer) WriteXChar (x: CHAR), NEW;
			(VAR wr: Writer) WriteXInt (x: INTEGER), NEW;
			(VAR wr: Writer) WriteXReal (x: REAL), NEW;
			(VAR wr: Writer) WriteXString (IN x: ARRAY OF CHAR), NEW
		END;

	PROCEDURE NewSegment (container: Files64.File; org: LONGINT): Files.File;

END Stores64.

This module provides Reader and Writer objects similar to Stores.Reader and Stores.Writer but for 64-bit files (Files64).

Stores64 read and write operations can be applied to the full file space (MAX(LONGINT)) except for the following limitation. The file space for stores must fit within MAX(INTEGER) bytes because for compatibility reasons the file format being used is the same as for Stores.Store. In order to avoid this limitation, a separate ConnectTo operation (typically followed by SetPos) can be executed. This resets internal data structures and restarts checking the store size limitation with the next WriteStore/ReadStore operation. Stores read resp. written after different ConnectTo operations do not refer to each other but are treated as if they are in different files.

For the documentation of Reader and Writer operations see Stores.

PROCEDURE NewSegment (container: Files64.File; org: LONGINT): Files.File;
Creates and returns a 32-bit file segment backed by a 64-bit container file starting at position org. The returned file segment can be used for connecting a Stores.Reader or Stores.Writer, for example. Segments are also used internally for handling ReadStore and WriteStore. A segment cannot be registered. 

Pre
container # NIL	20
~container.Closed()	21
org >= 0	22

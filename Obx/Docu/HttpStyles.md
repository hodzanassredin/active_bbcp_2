# One HTTP client in three styles

The task is the same: `GET http://host:port/path`, parse the status line and the headers, receive a
body by `Content-Length` or by `Transfer-Encoding: chunked`, follow one redirect from `Location`,
return the code and the body. The same client is written three times. Next to them stands
`PaketHttp.Reader.Do` from the original system: this is what a network client looked like before
tasks.

| Module | Where | What moves the parse |
|---|---|---|
| `ObxHttpTask` | `Obx/Mod64/HttpTask.cp` | a task, sequential code |
| `ObxHttpCodec` | `Obx/Mod/HttpCodec.cp` | `Services.Action`, decoder layers |
| `ObxHttpCodecTask` | `Obx/Mod64/HttpCodecTask.cp` | a task feeds the same decoders |
| `PaketHttp` | `Paket/Mod/Http.cp` | `Services.Action`, one automaton |

The layer interface lives separately, in `Obx/Mod/Codec.cp`:

```
CONST
	done* = 0; more* = 1; error* = 2;

TYPE
	Decoder* = ABSTRACT RECORD END;

PROCEDURE (VAR d: Decoder) Decode* (IN buf: ARRAY OF BYTE;
		beg, len: INTEGER; OUT used, res: INTEGER), NEW, ABSTRACT;
```

`more` means "the unit is not whole, nothing was eaten"; `used = 0`. `done` means "the unit is
parsed, `used` bytes were eaten"; the description of the unit — an offset and a length of a slice of
the same buffer — lies in the decoder fields. `error` means "the stream is corrupt". The decoder
remembers how far it has already searched for a separator, so a repeated call with a larger window
does not start the search anew.

## Three key places

The task reads the response with ordinary nested procedures. `ReadChunk` calls `ReadLine` and
`ReadExact`, they call `ReadByte`, `ReadByte` calls `Fill`, and only at the bottom sits the wait:

```
PROCEDURE (VAR r: Reader) Fill (): BOOLEAN;
BEGIN
	AsyncIO.Read(r.s, r.buf, 0, LEN(r.buf), r.end);
	IF r.end <= 0 THEN r.beg := 0; r.end := 0; RETURN FALSE END;
	r.beg := 0;
	RETURN TRUE
END Fill;

PROCEDURE ReadChunk (VAR r: Reader; VAR body: ARRAY OF BYTE; at: INTEGER;
		OUT n: INTEGER; OUT last: BOOLEAN; OUT err: INTEGER): BOOLEAN;
	VAR line: ARRAY 64 OF CHAR;
BEGIN
	n := 0; last := FALSE;
	IF ~r.ReadLine(line) THEN err := errClosed; RETURN FALSE END;
	IF ~ParseHex(line, n) THEN err := errChunk; RETURN FALSE END;
	IF n = 0 THEN
		IF ~r.ReadExact(junk, 0, 2) THEN err := errClosed; RETURN FALSE END;
		last := TRUE; RETURN TRUE
	END;
	IF at + n > LEN(body) THEN err := errBuffer; RETURN FALSE END;
	IF ~r.ReadExact(body, at, n) THEN err := errClosed; RETURN FALSE END;
	IF ~r.ReadExact(junk, 0, 2) THEN err := errClosed; RETURN FALSE END;
	RETURN TRUE
END ReadChunk;
```

The state of the parse is in local variables of the task: `kind`, `length`, `loc`, and the position
in the `Reader` buffer. There is no step number. While there is no data, the task sleeps in
`AsyncIO.Read`; the main loop polls the descriptor.

The decoder does not know who feeds it. Its driver keeps a buffer, the fields of three decoders and
one field of layer order, `phase`. `Drive` calls a layer, looks at `used` and `res`, moves the
window and repeats:

```
CASE p.phase OF
	phaseHeader:
		p.hd.Decode(p.buf, p.beg, p.end - p.beg, used, res);
		IF res = ObxCodec.more THEN
			IF used # 0 THEN p.Fail(errContract) END;
			RETURN
		ELSIF res # ObxCodec.done THEN
			IF p.hd.badStatus THEN p.Fail(errStatus) ELSE p.Fail(errHeader) END;
			RETURN
		END;
		...
		CASE p.hd.kind OF
			kindNone: p.finished := TRUE; RETURN
		| kindLength: p.bd.Reset(p.hd.length); p.phase := phaseLength
		| kindChunked: p.cd.Reset; p.phase := phaseChunk
		END
| phaseLength: ...
| phaseChunk: ...
END
```

The layers: the response header up to the empty line, the body by length, a `chunked` piece. Between
calls live the buffer and the decoder fields; the layer order is the small `phase` field of the
driver. The driver is a `Services.Action`: while there is no data, the action is put back into the
main loop queue.

The third module takes the same `ObxHttpCodec.Parser` and `ObxHttpCodec.HeaderDecoder`, and a task
feeds them:

```
LOOP
	IF j.p.err # errNone THEN EXIT END;
	IF j.p.redirect OR j.p.finished THEN EXIT END;
	AsyncIO.Read(s, x, 0, LEN(x), n);
	IF n > 0 THEN j.p.Feed(x, n); j.p.Drive
	ELSIF ~s.IsConnected() THEN j.p.Eof
	END
END;
```

The parse does not depend on who feeds it. The task waits for the socket; the action spins in the
main loop.

`PaketHttp.Reader.Do` is built differently. It is one procedure with explicit states `firstHeader`,
`completingNormal`, `completingChunked`, `startingChunked`, `error`, `redirected`; the nested
`MultyPack` parses the rest of the packet. Header, length, chunk and the write to a file are
intertwined in one body, and the buffer is 1 MiB per connection.

## Structural comparison

Parse lines are counted without empty lines and comments. For `PaketHttp` the text of `Reader.Do` is
taken; for `ObxHttpTask` — `ReadByte`, `ReadLine`, `ReadExact`, the parsers of the status line and
headers, `ReadHead`, `ReadChunk`, `ReadBody`; for `ObxHttpCodec` — `HeaderDecoder`, `BodyDecoder`,
`ChunkDecoder`, `Drive` and the auxiliary parsers; `ObxHttpCodecTask` adds no new parse.

| Variant | Parse lines | Explicit states | Dependencies | Stack per connection | Buffer |
|---|---|---|---|---|---|
| `PaketHttp.Reader.Do` | 286 (counted) | 6 constants, 3 fields (counted) | `Services.Action`, `CommStreams`, `Views`, `Files` | none | 1 MiB per connection (counted) |
| `ObxHttpTask` | 208 (counted) | 0 constants, 2 position fields (counted) | tasks, `AsyncIO`, amd64 | 64 KiB in the example; 256 KiB by default; minimum 9 KiB (measured) | 4 KiB of read buffer on the stack, 8 KiB of body |
| `ObxHttpCodec` | 292 (counted) | 6 named: 4 phases and 2 chunk states; 9 fields (counted) | `Services.Action`; no tasks, `Async*`, `Monitors`, `ACTIVE` | none | 8 KiB input and 8 KiB body in the record |
| `ObxHttpCodecTask` | 0 new (counted) | 0 new (counted) | tasks, `AsyncIO`, `ObxHttpCodec` | 64 KiB; minimum 6 KiB (measured) | 2 KiB window, 16 KiB `Parser` in the record |

`ObxHttpCodec` imports neither `Tasks` nor `Async*` nor `Monitors` and uses neither `ACTIVE` nor
`[exclusive]` nor `AWAIT`; this is checked against the import list. The driver relies on
`Services.Action`, a mechanism of the original BlackBox. None of the three modules writes
`ACTIVE.AWAIT`, `[exclusive]` or `^`: they do not need the language extension. A task comes from the
`Tasks` library, a decoder from `Services.Action`.

The stack minimum is measured on the path "connect, send the request, wait for the first byte": an
`ObxHttpTask` task runs with 9 KiB, an `ObxHttpCodecTask` task with 6 KiB. A smaller stack traps
before the connection counter grows. This is the minimum of this example and this path, not a
general promise of `Tasks`.

## Measured comparison

Conditions: a local server on 127.0.0.1, 1000 simultaneous connections that hold the response; and
1000 sequential requests by `Content-Length`. One machine, one process, five runs, load average
0.9–2.0. The command:

```sh
tools/iso.sh tools/test-http-examples.sh --measure
```

| Variant | RSS per connection | Virtual memory | Tasks per 1000 | Allocations per request | Heap per request | 1000 sequential requests |
|---|---|---|---|---|---|---|
| `ObxHttpTask` | 21 KiB (measured) | 77 KiB at a 64 KiB stack; 270 KiB at 256 KiB (measured) | 1000 (measured) | 6 (measured) | 737 bytes (measured) | 1160 ms (measured) |
| `ObxHttpCodec` | 19 KiB (measured) | 19 KiB (measured) | 0; 1004 actions (measured) | 4 (measured) | 1744 bytes (measured) | 72 ms (measured) |
| `ObxHttpCodecTask` | 27 KiB (measured) | 87 KiB (measured) | 1000 (measured) | 6 (measured) | 1833 bytes (measured) | 1170 ms (measured) |
| `PaketHttp` | not measured | not measured | 0 tasks, one action per download | not measured | not measured | not measured |

Allocations are counted by `Kernel.AllocCount` before and after 1000 requests; the heap is counted
by `Kernel.Allocated` right after the loop. A full collection after the loop gives no growth: the
counter falls by 60 KiB of service blocks, there is no leak.

`PaketHttp` is not measured: its only entry `Download` writes the result to a `Files.File` and
touches `Views` for progress. In the console without a working file locator there is no observable
result. Its line count and the 1 MiB buffer are counted from the source.

The time of `ObxHttpTask` and `ObxHttpCodecTask` includes the wait of the main loop: `Get` runs
`Services.actionHook` and `AsyncIO.Poll(1)`, so a sequential request pays one millisecond of polling.
`ObxHttpCodec` in the same experiment does not wait and puts the action back without a pause, and
1000 sequential requests pass faster. This is a property of the waiting method, not of the parse:
with hundreds of simultaneous connections the task pauses overlap.

## What is lost and what is gained

A task gives ordinary sequential code: `ReadLine`, `ReadExact`, `ReadChunk` read from top to bottom,
the state lies in local variables, the wait point is visible at the bottom. The price is a stack per
connection and the scheduler: 64 KiB of virtual memory in the example, 256 KiB by default, 1000 task
records in the heap for 1000 connections. Simultaneous connections come without rewriting the code.

A decoder starts neither a task nor a stack: 19 KiB per idle connection and 4 allocations per
request. It runs on the original BlackBox and needs neither tasks nor their scheduler. The price is an
explicit automaton: layers, a `phase` field, the `more/done/error` contract, checking `used`, and
more parse lines than the task. While the response has not arrived, the action occupies the main
loop; a blocking call such as `CommTCP.NewStream` stops the loop for the whole connection.

`ObxHttpCodecTask` joins both: the parse is the same as the decoder's, the wait is the task's. The
price is a task stack plus the `Parser` record in the heap; the gain is the familiar sequential code
of the driver.

`PaketHttp` keeps one 1 MiB buffer and writes the body to a file; its automaton parses the header and
the chunk in one pass and is therefore shorter than the layered one, but the step state, position
and length lie in fields, and only the owner of `Downloading` sees the errors.

## Where the decoder is inconvenient

A body of unknown length. Our length layer returns `more` until the whole body has been gathered,
and by the contract `more` eats nothing. Hence the connection buffer must hold the whole body. A body
of 1 MiB needs a buffer of 1 MiB per connection; this returns the memory the decoder saved on the
stack.

Streaming a large body. To give the body away in pieces, a layer must eat part of a unit and return
a slice on every call. The `more` contract with `used = 0` does not allow this. A different contract
(partial consumption) or a task with a stack buffer is needed. `chunked` in our form is bounded by
the size of one chunk, not of the whole body.

The wait in the code calls `CommTCP.NewStream` directly on the main stack. On a local host this is
unnoticeable; resolving a name or connecting to a distant host stops the main loop. A task moves
this work to `AsyncWork`; a decoder does not.

## How to check

```sh
tools/iso.sh tools/test-http-examples.sh            # 39 checks of the three clients
tools/iso.sh tools/test-http-examples.sh --mutate   # three mutations must be caught
tools/iso.sh tools/test-http-examples.sh --measure  # memory and time
tools/iso.sh tools/unit.sh Obx_devHttpCodec --compile "ObxCodec ObxHttpCodec"
```

The script starts a local server on 127.0.0.1 and runs `Content-Length`, `chunked` from three
pieces, a redirect, a response one byte at a time with pauses, a cut in the middle of the header, in
the middle of a chunk length and in the middle of the body, garbage instead of the status line and a
non-hexadecimal chunk length. Every path goes through all three clients, the results are compared
with each other and with the reference.

The mutations: the header eats one extra byte; the body layer returns `more` with `used = 1`;
`ReadExact` does not finish a short read. The first is caught on `/len`, the second on `/slow`, the
third on `/cutbody`.

The unit test `Obx_devHttpCodec` does not touch the network: it feeds the same buffer in two pieces
at every possible cut position and compares the result with the parse of the whole buffer. Separately,
every cut position must give `errClosed`. The test is registered in `tools/tests.txt`.

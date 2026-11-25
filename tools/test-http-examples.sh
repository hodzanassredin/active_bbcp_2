#!/bin/sh
# Три HTTP-клиента на одном наборе путей: задача, Services.Action с
# кодеками и задача с теми же кодеками. Плюс отказные пути и мутации.
#
#   tools/iso.sh tools/test-http-examples.sh
#   tools/iso.sh tools/test-http-examples.sh --mutate
#
# Сервер — python3 из стандартной библиотеки, только 127.0.0.1 и свободный
# порт; наружу сеть не ходит. Пути: ответ по Content-Length, chunked из
# трёх кусков, одна переадресация, вторая переадресация (отказ), ответ по
# одному байту, обрыв посреди заголовка, посреди длины куска, посреди тела,
# мусор вместо строки состояния, нешестнадцатеричная длина куска.
#
# --mutate дополнительно портит код тремя способами и требует, чтобы прогон
# это заметил: заголовок съедает лишний байт; more возвращает used # 0;
# ReadExact не дочитывает при коротком чтении. Исходники возвращаются.
set -u
. "$(dirname "$0")/env.sh"

mutate=0; measure=0
case "${1:-}" in
	--mutate) mutate=1;;
	--measure) measure=1;;
esac

[ -x "$INTERP" ] || { echo "test-http: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }

mods="ObxCodec ObxHttpCodec ObxHttpTask ObxHttpCodecTask"
[ "$measure" -eq 1 ] && mods="$mods Obx_devHttpBench"
"$BB/tools/go64.sh" $mods >/dev/null 2>&1 || {
	echo "test-http: компиляция клиентов не удалась" >&2
	"$BB/tools/go64.sh" $mods >&2
	exit 1
}

tmp=$(mktemp -d)
srvpid=""; holdpid=""
restore=""

cleanup() {
	[ -n "$srvpid" ] && kill "$srvpid" 2>/dev/null
	[ -n "$holdpid" ] && kill "$holdpid" 2>/dev/null
	IFS=' '
	for f in $restore; do
		[ -f "$tmp/bak.$(basename "$f")" ] && cp "$tmp/bak.$(basename "$f")" "$f"
	done
	rm -rf "$tmp"
}
trap cleanup EXIT INT TERM

cat > "$tmp/server.py" <<'PY'
import http.server, socketserver, sys, time

LEN_BODY = b'hello, world!'
CHUNKS = (b'alpha', b'beta', b'gamma')

class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def raw(self, data, pause=0.0):
        self.close_connection = True
        if pause <= 0.0:
            self.wfile.write(data)
            self.wfile.flush()
            return
        for b in data:
            self.wfile.write(bytes([b]))
            self.wfile.flush()
            time.sleep(pause)

    def do_GET(self):
        path = self.path.split('?')[0]
        port = self.server.server_address[1]
        if path == '/len':
            self.raw(b'HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\n' + LEN_BODY)
        elif path == '/chunked':
            body = b''
            for c in CHUNKS:
                body += b'%x\r\n' % len(c) + c + b'\r\n'
            body += b'0\r\n\r\n'
            self.raw(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n' + body)
        elif path == '/redir':
            loc = ('http://127.0.0.1:%d/len' % port).encode()
            self.raw(b'HTTP/1.1 302 Found\r\nLocation: ' + loc +
                     b'\r\nContent-Length: 0\r\n\r\n')
        elif path == '/redir2':
            loc = ('http://127.0.0.1:%d/redir' % port).encode()
            self.raw(b'HTTP/1.1 302 Found\r\nLocation: ' + loc +
                     b'\r\nContent-Length: 0\r\n\r\n')
        elif path == '/slow':
            self.raw(b'HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\n' + LEN_BODY, 0.002)
        elif path == '/slowchunked':
            body = b''
            for c in CHUNKS:
                body += b'%x\r\n' % len(c) + c + b'\r\n'
            body += b'0\r\n\r\n'
            self.raw(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n' + body, 0.002)
        elif path == '/cutheader':
            self.raw(b'HTTP/1.1 200 OK\r\nContent-Len')
        elif path == '/cutbody':
            self.raw(b'HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n12345')
        elif path == '/cutchunklen':
            self.raw(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4')
        elif path == '/cutchunkdata':
            self.raw(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nab')
        elif path == '/garbage':
            self.raw(b'NOT-HTTP blah\r\nContent-Length: 0\r\n\r\n')
        elif path == '/badchunk':
            self.raw(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\nabc\r\n0\r\n\r\n')
        else:
            self.raw(b'HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n')

class S(socketserver.TCPServer):
    allow_reuse_address = True

srv = S(('127.0.0.1', 0), H)
print(srv.server_address[1], flush=True)
srv.serve_forever()
PY

python3 -I "$tmp/server.py" > "$tmp/port" 2> "$tmp/server.err" &
srvpid=$!
i=0
while [ ! -s "$tmp/port" ]; do
	i=$((i + 1))
	if [ $i -gt 100 ]; then
		echo "test-http: сервер не поднялся" >&2
		cat "$tmp/server.err" >&2
		exit 1
	fi
	sleep 0.05
done
port=$(cat "$tmp/port")
base="http://127.0.0.1:$port"

expect() {
	case "$1" in
		/len)		echo "status=200 err=0 len=13 body=68656c6c6f2c20776f726c6421";;
		/chunked)	echo "status=200 err=0 len=14 body=616c7068616265746167616d6d61";;
		/redir)		echo "status=200 err=0 len=13 body=68656c6c6f2c20776f726c6421";;
		/redir2)	echo "status=0 err=8 len=0 body=";;
		/slow)		echo "status=200 err=0 len=13 body=68656c6c6f2c20776f726c6421";;
		/slowchunked)	echo "status=200 err=0 len=14 body=616c7068616265746167616d6d61";;
		/cutheader)	echo "status=0 err=7 len=0 body=";;
		/cutbody)	echo "status=0 err=7 len=0 body=";;
		/cutchunklen)	echo "status=0 err=7 len=0 body=";;
		/cutchunkdata)	echo "status=0 err=7 len=0 body=";;
		/garbage)	echo "status=0 err=3 len=0 body=";;
		/badchunk)	echo "status=0 err=6 len=0 body=";;
		*)		echo "status=404 err=0 len=0 body=";;
	esac
}

pass=0
fail=0
out=""

run_client() {	# модуль, путь; результат в out, TRUE — совпало
	out=$(printf '%s.Do\n' "$1" | HTTP_URL="$base$2" timeout -k 5 60 "$BB/tools/run64.sh" 2>&1)
	rc=$?
	if [ $rc -ne 0 ]; then
		out="rc=$rc $out"
		return 1
	fi
	out=$(printf '%s\n' "$out" | grep '^status=' | tail -1)
	[ "$out" = "$(expect "$2")" ]
}

if [ "$measure" -eq 1 ]; then
	cat > "$tmp/hold.py" <<'PY'
import asyncio

async def handle(reader, writer):
    try:
        await reader.readuntil(b'\r\n\r\n')
        await asyncio.sleep(30)
    except Exception:
        pass
    finally:
        writer.close()

async def main():
    srv = await asyncio.start_server(handle, '127.0.0.1', 0, backlog=2048)
    print(srv.sockets[0].getsockname()[1], flush=True)
    async with srv:
        await srv.serve_forever()

asyncio.run(main())
PY
	python3 -I "$tmp/hold.py" > "$tmp/hport" 2> "$tmp/hold.err" &
	holdpid=$!
	i=0
	while [ ! -s "$tmp/hport" ]; do
		i=$((i + 1))
		if [ $i -gt 100 ]; then echo "test-http: hold-сервер не поднялся" >&2; exit 1; fi
		sleep 0.05
	done
	hport=$(cat "$tmp/hport")
	hbase="http://127.0.0.1:$hport"

	bench_one() {	# подпись, команда
		i=0; vals=""; last=""
		while [ $i -lt 5 ]; do
			last=$(printf '%s\n' "$2" | HTTP_URL="$base/len" HTTP_N=1000 \
				timeout -k 5 180 "$BB/tools/run64.sh" 2>&1)
			ms=$(printf '%s\n' "$last" | sed -n 's/^bench .* ms=\([0-9]*\).*/\1/p' | tail -1)
			if [ -n "$ms" ]; then vals="$vals $ms"; else echo "FAIL bench $1: нет строки bench" >&2; fail=$((fail + 1)); fi
			i=$((i + 1))
		done
		med=$(printf '%s\n' $vals | sort -n | sed -n '3p')
		line=$(printf '%s\n' "$last" | grep '^bench ' | tail -1)
		echo "BENCH $1 median_ms=$med runs=$vals load=$(cut -d' ' -f1-3 /proc/loadavg)"
		echo "  $line"
	}

	bench_one task Obx_devHttpBench.Task
	bench_one codec Obx_devHttpBench.Codec
	bench_one codecTask Obx_devHttpBench.CodecTask

	measure_one() {	# подпись, команда, N, стек ("" — по умолчанию примера)
		label=$1; cmd=$2; n=$3; st=$4
		printf '%s.Measure\n' "$cmd" | (
			cd "$INSTALL" || exit 1
			export LD_LIBRARY_PATH="$INSTALL" HTTP_URL="$hbase/hold" HTTP_N="$n"
			[ -n "$st" ] && export HTTP_STACK="$st"
			exec ./blackbox-console
		) > "$tmp/m.out" 2>&1 &
		mpid=$!
		i=0
		while ! grep -q '^ready ' "$tmp/m.out" 2>/dev/null; do
			i=$((i + 1))
			if [ $i -gt 400 ]; then
				echo "FAIL measure $label: нет ready" >&2
				kill "$mpid" 2>/dev/null; wait "$mpid" 2>/dev/null
				fail=$((fail + 1)); return 1
			fi
			sleep 0.05
		done
		rss=$(awk '/^VmRSS:/ {print $2}' "/proc/$mpid/status" 2>/dev/null)
		vsz=$(awk '/^VmSize:/ {print $2}' "/proc/$mpid/status" 2>/dev/null)
		line=$(grep '^ready ' "$tmp/m.out" | tail -1)
		i=0
		while kill -0 "$mpid" 2>/dev/null && [ $i -lt 300 ]; do sleep 0.1; i=$((i + 1)); done
		kill "$mpid" 2>/dev/null; wait "$mpid" 2>/dev/null
		echo "MEASURE $label n=$n stack=${st:-example} rss_kb=${rss:-?} vsize_kb=${vsz:-?} $line"
		MEASURE_RSS="$rss"; MEASURE_VSZ="$vsz"
	}

	for spec in "task:ObxHttpTask.Measure" "codec:ObxHttpCodec.Measure" "codecTask:ObxHttpCodecTask.Measure"; do
		label=${spec%%:*}; cmd=${spec#*:}
		measure_one "$label" "$cmd" 0 ""; base_rss=$MEASURE_RSS; base_vsz=$MEASURE_VSZ
		measure_one "$label" "$cmd" 1000 ""; full_rss=$MEASURE_RSS; full_vsz=$MEASURE_VSZ
		per=$(awk -v a="$base_rss" -v b="$full_rss" 'BEGIN { if (a ~ /^[0-9]+$/ && b ~ /^[0-9]+$/) printf "%d", (b - a) / 1000; else print "?" }')
		perv=$(awk -v a="$base_vsz" -v b="$full_vsz" 'BEGIN { if (a ~ /^[0-9]+$/ && b ~ /^[0-9]+$/) printf "%d", (b - a) / 1000; else print "?" }')
echo "MEM $label rss0_kb=$base_rss rss1000_kb=$full_rss per_conn_kb=$per vsize0_kb=$base_vsz vsize1000_kb=$full_vsz per_conn_vsize_kb=$perv"
	done

	measure_one task-defaultstack ObxHttpTask.Measure 1000 0
	measure_one task-stack64k ObxHttpTask.Measure 1000 65536

	echo "test-http: замеры закончены, провалов $fail"
	[ "$fail" -eq 0 ]
	exit $?
fi

PATH_LIST="/len /chunked /redir /redir2 /slow /slowchunked /cutheader /cutbody /cutchunklen /cutchunkdata /garbage /badchunk /missing"
MODULES="ObxHttpTask ObxHttpCodec ObxHttpCodecTask"

for path in $PATH_LIST; do
	for mod in $MODULES; do
		if run_client "$mod" "$path"; then
			pass=$((pass + 1))
		else
			fail=$((fail + 1))
			echo "FAIL $mod $path: ожидалось <$(expect "$path")>, получено <$out>" >&2
		fi
	done
	echo "OK   $path"
done

echo "test-http: проверок $((pass + fail)), совпало $pass, провалов $fail"

# --- мутации --------------------------------------------------------------

caught=0; missed=0
backup() {
	cp "$1" "$tmp/bak.$(basename "$1")"
	restore="$restore $1"
}
recompile() {
	"$BB/tools/go64.sh" "$@" >/dev/null 2>&1
}

check_mutation() {	# имя, модуль, путь; TRUE — прогон не совпал с эталоном
	if run_client "$2" "$3"; then
		echo "MISS $1: прогон совпал с эталоном, мутация не поймана" >&2
		missed=$((missed + 1))
	else
		echo "CAUGHT $1: <$out>"
		caught=$((caught + 1))
	fi
}

if [ "$mutate" -eq 1 ]; then
	src="$BB/Obx/Mod/HttpCodec.cp"
	backup "$src"
	python3 -I - "$src" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = '\t\tused := d.unitLen; res := ObxCodec.done'
new = '\t\tused := d.unitLen + 1; res := ObxCodec.done'
assert old in s
open(p, 'w').write(s.replace(old, new, 1))
PY
	if recompile ObxHttpCodec ObxHttpCodecTask; then
		check_mutation "заголовок съедает лишний байт" ObxHttpCodec /len
	else
		echo "MISS заголовок съедает лишний байт: не компилируется" >&2
		missed=$((missed + 1))
	fi
	cp "$tmp/bak.HttpCodec.cp" "$src"; recompile ObxHttpCodec ObxHttpCodecTask

	python3 -I - "$src" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
a = s.index('PROCEDURE (VAR d: BodyDecoder) Decode')
b = s.index('END Decode;', a)
part = s[a:b]
old = '\t\tELSE\n\t\t\tres := ObxCodec.more\n\t\tEND'
new = '\t\tELSE\n\t\t\tused := 1; res := ObxCodec.more\n\t\tEND'
assert old in part
s = s[:a] + part.replace(old, new, 1) + s[b:]
open(p, 'w').write(s)
PY
	if recompile ObxHttpCodec ObxHttpCodecTask; then
		check_mutation "more с used # 0" ObxHttpCodec /slow
	else
		echo "MISS more с used # 0: не компилируется" >&2
		missed=$((missed + 1))
	fi
	cp "$tmp/bak.HttpCodec.cp" "$src"; recompile ObxHttpCodec ObxHttpCodecTask

	src="$BB/Obx/Mod64/HttpTask.cp"
	backup "$src"
	python3 -I - "$src" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
a = s.index('PROCEDURE (VAR r: Reader) ReadExact')
b = s.index('END ReadExact;', a)
part = s[a:b]
old = '\t\t\t\tIF ~r.Fill() THEN RETURN FALSE END'
new = '\t\t\t\tIF ~r.Fill() THEN RETURN TRUE END'
assert old in part
s = s[:a] + part.replace(old, new, 1) + s[b:]
open(p, 'w').write(s)
PY
	if recompile ObxHttpTask; then
		check_mutation "ReadExact не дочитывает" ObxHttpTask /cutbody
	else
		echo "MISS ReadExact не дочитывает: не компилируется" >&2
		missed=$((missed + 1))
	fi
	cp "$tmp/bak.HttpTask.cp" "$src"; recompile ObxHttpTask

	echo "test-http: мутаций поймано $caught, пропущено $missed"
	[ "$missed" -eq 0 ] || fail=$((fail + missed))
fi

[ "$fail" -eq 0 ]

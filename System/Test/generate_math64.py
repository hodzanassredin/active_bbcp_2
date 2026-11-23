#!/usr/bin/env python3
"""Generate deterministic Math regression vectors from Python's binary64 math library."""
import math
import random
from pathlib import Path
random.seed(1994)
ROOT = Path(__file__).resolve().parents[2]
cases = []
def literal(x):
	if math.isnan(x): return 'Value(2146959360, 0)'
	if math.isinf(x): return '-INF' if x < 0 else 'INF'
	if x == 0 and math.copysign(1, x) < 0: return 'Value(MIN(INTEGER), 0)'
	text = repr(float(x)).replace('e', 'E')
	if 'E' in text and '.' not in text: text = text.replace('E', '.0E')
	return text
def add(name, args, expected):
	label = name + '.large' if name in ('Sin','Cos') and abs(args[0]) >= 1e10 else name
	cases.append(f'Test("{label}", Math.{name}({", ".join(literal(x) for x in args)}), {literal(expected)});')
common = [-10., -1., -.5, -1e-20, -0., 0., 1e-20, .5, 1., 10.]
spec = {
'Sqrt': (math.sqrt, [0., 1e-300, .25, 2., 1e300, math.inf]),
'Exp': (math.exp, common + [-745., -700., 700., math.inf, -math.inf]),
'Ln': (math.log, [1e-300, .125, 1., 2., 1e300, math.inf]),
'Log': (math.log10, [1e-300, .125, 1., 10., 1e300, math.inf]),
'Sin': (math.sin, common + [math.pi, math.pi/2, 1e10]),
'Cos': (math.cos, common + [math.pi, math.pi/2, 1e10]),
'Tan': (math.tan, common + [math.pi/4, 1e5]),
'ArcSin': (math.asin, [-1., -.999, -.5, -0., 0., .5, .999, 1.]),
'ArcCos': (math.acos, [-1., -.999, -.5, 0., .5, .999, 1.]),
'ArcTan': (math.atan, common + [-math.inf, math.inf, 1e300]),
'Sinh': (math.sinh, common + [-710., 710., math.inf, -math.inf]),
'Cosh': (math.cosh, common + [-710., 710., math.inf, -math.inf]),
'Tanh': (math.tanh, common + [-1000., 1000., math.inf, -math.inf]),
'ArcSinh': (math.asinh, common + [-1e300, 1e300, math.inf, -math.inf]),
'ArcCosh': (math.acosh, [1., 1.+2**-52, 1.1, 2., 1e300, math.inf]),
'ArcTanh': (math.atanh, [-.999999999, -.5, -1e-20, -0., 0., 1e-20, .5, .999999999]),
'Floor': (lambda x: float(math.floor(x)), common + [-2.5, 2.5, 2**52]),
'Ceiling': (lambda x: float(math.ceil(x)), common + [-2.5, 2.5, 2**52]),
'Round': (lambda x: float(math.floor(x+.5)), common + [-2.5, 2.5, 2**52]),
'Trunc': (lambda x: float(math.trunc(x)), common + [-2.5, 2.5, 2**52]),
'Frac': (lambda x: math.modf(x)[0], common + [-2.5, 2.5, 2**52]),
'Mod1': (lambda x: x-math.floor(x), common + [-2.5, 2.5, 2**52]),
'Sign': (lambda x: math.copysign(1., x) if x else x, common + [-math.inf, math.inf]),
'Mantissa': (lambda x: math.frexp(x)[0]*2 if x else x, common + [5e-324, 1e-300, 1e300]),
}
for name,(f,xs) in spec.items():
	for x in xs: add(name, [x], f(x))
	if name not in ('Floor','Ceiling','Round','Trunc','Frac','Mod1','Sign'):
		for _ in range(20):
			x = random.uniform(-.99,.99) if name in ('ArcSin','ArcCos','ArcTanh') else random.uniform(.01,10.) if name in ('Sqrt','Ln','Log','ArcCosh') else random.uniform(-10.,10.)
			if name=='ArcCosh': x += 1
			add(name,[x],f(x))
for x in [.125,.5,1.,2.,10.,1e100,math.inf]:
	for y in [-2.,-.5,.5,2.,3.]: add('Power',[x,y],math.pow(x,y))
for y in [-math.inf,-1.,-0.,0.,1.,math.inf]:
	for x in [-math.inf,-1.,-0.,0.,1.,math.inf]: add('ArcTan2',[y,x],math.atan2(y,x))
for x in [-2.,-0.,0.,2.,math.inf]:
	for y in [-1.,-0.,0.,1.]: add('CopySign',[x,y],math.copysign(x,y))
# Integer and output-parameter procedures need separate call signatures.
for x in [-2.,-.5,1.,2.]:
	for n in [-20,-3,-1,0,1,3,20]:
		cases.append(f'Test("IntPower", Math.IntPower({literal(x)}, {n}), {literal(x**n)});')
for x in [0.,-0.,5e-324,1e-300,-10.56,1.,1e300]:
	e = math.frexp(x)[1]-1 if x else 0
	cases.append(f'Test("Exponent", Math.Exponent({literal(x)}), {e}.0);')
for n in [-1074,-1022,-1,0,1,1023]:
	cases.append(f'Test("Real", Math.Real(1., {n}), {literal(math.ldexp(1.,n))});')
for x in common:
	cases.append(f'Math.SinCos({literal(x)}, s, c); Test("SinCos.sin", s, {literal(math.sin(x))}); Test("SinCos.cos", c, {literal(math.cos(x))});')
for name in ('Floor','Ceiling','Round','Trunc'):
	for x in [-math.inf,math.inf]: add(name,[x],x)
for x,n,want in [(0.,0,1.),(-1.,-2147483648,1.),(1e300,2,math.inf),(2.,-1074,5e-324)]:
	cases.append(f'Test("IntPower", Math.IntPower({literal(x)}, {n if n!=-2147483648 else "MIN(INTEGER)"}), {literal(want)});')
for x,y,w in [(0.,-1.,math.inf),(0.,1.,0.),(math.inf,-1.,0.),(2.,math.inf,math.inf),(.5,math.inf,0.),(2.,-math.inf,0.),(.5,-math.inf,math.inf),(2.,0.,1.)]: add('Power',[x,y],w)
for name in ('Floor','Ceiling','Round','Trunc','Frac','Mod1'):
	f = spec[name][0]
	for _ in range(30):
		x = random.uniform(-10000,10000); add(name,[x],f(x))
for x in [-1.,1.]: add('ArcTanh',[x],math.copysign(math.inf,x))
for x in [math.inf,-math.inf]:
	cases.append(f'Test("Exponent.special", Math.Exponent({literal(x)}), 2147483647.);')
	cases.append(f'Test("Real.special", Math.Real(Math.Mantissa({literal(x)}), MAX(INTEGER)), {literal(x)});')
for name in ('Sin','Tan','ArcSin','ArcTan','Sinh','Tanh','ArcSinh','ArcTanh','Trunc','Sign','Mantissa'):
	cases.append(f'Test("{name}.negativezero", Bool(Math.SignBit(Math.{name}(Value(MIN(INTEGER), 0)))), 1.);')
cases.append('Test("CopySign.negativezero", Bool(Math.SignBit(Math.CopySign(0., -1.))), 1.);')
cases += ['Test("Pi", Math.Pi(), 3.141592653589793);', 'Test("Eps", Math.Eps(), 2.220446049250313E-16);']
header='''MODULE CheckMath64;

	IMPORT S := SYSTEM, Math;

	VAR count*, failures*: INTEGER; name*: ARRAY 40 OF CHAR; actual*, expected*: REAL;
		errors*: ARRAY 32 OF RECORD name*: ARRAY 40 OF CHAR; actual*, expected*: REAL END;

	PROCEDURE Value (high, low: INTEGER): REAL;
		VAR x: REAL;
	BEGIN S.PUT(S.ADR(x), low); S.PUT(S.ADR(x)+4, high); RETURN x END Value;

	PROCEDURE NaN (x: REAL): BOOLEAN;
		VAR hi, lo: INTEGER;
	BEGIN
		S.GET(S.ADR(x)+4, hi); S.GET(S.ADR(x), lo);
		RETURN (hi DIV 1048576 MOD 2048 = 2047) & ((hi MOD 1048576 # 0) OR (lo # 0))
	END NaN;

	PROCEDURE Bool (x: BOOLEAN): REAL;
	BEGIN IF x THEN RETURN 1. ELSE RETURN 0. END END Bool;

	PROCEDURE Test (IN label: ARRAY OF CHAR; got, want: REAL);
		VAR ok: BOOLEAN;
	BEGIN
		INC(count);
		IF NaN(want) THEN ok := NaN(got)
		ELSIF NaN(got) THEN ok := FALSE
		ELSIF ABS(want) = INF THEN ok := got = want
		ELSIF want = 0 THEN ok := got = 0.
		ELSIF (label = "Sin.large") OR (label = "Cos.large") THEN ok := ABS(got - want) < 2.0E-11
		ELSIF (label = "Sin") OR (label = "Cos") THEN ok := ABS(got - want) < 3.0E-14 * ABS(want) + 1.0E-16
		ELSE ok := ABS((got - want) / want) < 3.0E-14
		END;
		IF ~ok THEN
			IF failures = 0 THEN name := label$; actual := got; expected := want END;
			IF failures < LEN(errors) THEN
				errors[failures].name := label$; errors[failures].actual := got; errors[failures].expected := want
			END;
			INC(failures)
		END
	END Test;
'''
chunks=[]
for i in range(0,len(cases),60):
	chunks.append(f'\n\tPROCEDURE Group{i//60};\n\t\tVAR s, c: REAL;\n\tBEGIN\n'+''.join('\t\t'+c+'\n' for c in cases[i:i+60])+f'\tEND Group{i//60};\n')
tail='''
	PROCEDURE Run*;
		VAR i: INTEGER; x: REAL;
	BEGIN
		count := 0; failures := 0; name := "";
'''+''.join(f'\t\tGroup{i};\n' for i in range(len(chunks)))+'''		Test("IsNaN.inf", Bool(Math.IsNaN(INF)), 0.);
		Test("IsNaN.finite", Bool(Math.IsNaN(1.)), 0.);
		Test("SignBit.negativezero", Bool(Math.SignBit(Value(MIN(INTEGER), 0))), 1.);
		Test("SignBit.positivezero", Bool(Math.SignBit(0.)), 0.);
		FOR i := -1074 TO 1023 DO
			x := Math.Real(1., i);
			Test("Exponent.power2", Math.Exponent(x), i);
			Test("Mantissa.power2", Math.Mantissa(x), 1.);
			Test("Real.roundtrip", Math.Real(Math.Mantissa(-x), Math.Exponent(-x)), -x)
		END;
		FOR i := 0 TO 99 DO
			x := Math.IntPower(1., MIN(INTEGER)); Test("IntPower.MIN", x, 1.);
			Test("x87 balance", Math.Sqrt(4.), 2.)
		END
	END Run;

END CheckMath64.
'''
(ROOT/'Check/Mod/Math64.cp').write_text(header+''.join(chunks)+tail)
print(len(cases), 'vectors generated')

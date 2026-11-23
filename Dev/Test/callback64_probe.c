/* Test-only DLL. Production programs do not import it. */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"callback/unwind check failed: %d\n",__LINE__); ExitProcess(__LINE__); } } while(0)
typedef uint64_t (*Callback6)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static DWORD64 *reg(CONTEXT *c, unsigned n) { return &c->Rax+n; }
static void check_unwind(void *fn, CONTEXT *state, CONTEXT *original, unsigned offset) {
 CONTEXT c=*state; DWORD64 image, frame; void *data;
 PRUNTIME_FUNCTION f=RtlLookupFunctionEntry((DWORD64)fn+offset,&image,NULL);
 CHECK(f!=NULL); c.Rip=(DWORD64)fn+offset;
 RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,c.Rip,f,&c,&data,&frame,NULL);
 CHECK(c.Rip==0x12345678); CHECK(c.Rsp==original->Rsp+8);
 unsigned regs[]={3,5,6,7,12,13,14,15};
 for(unsigned i=0;i<8;i++) CHECK(*reg(&c,regs[i])==*reg(original,regs[i]));
 CHECK(!memcmp(&c.Xmm6,&original->Xmm6,10*sizeof(M128A)));
}
__declspec(dllexport) void Probe(void *fn) {
 unsigned char stack[4096] __attribute__((aligned(16))); CONTEXT c={0},original;
 unsigned char *p=fn; unsigned off=0;
 c.Rsp=(DWORD64)(stack+3072)-8; *(DWORD64*)c.Rsp=0x12345678;
 for(unsigned i=0;i<16;i++) if(i!=4) *reg(&c,i)=0x100000000ULL+i*0x100;
 for(unsigned i=6;i<16;i++) memset(&c.Xmm0+i,0xa0+i,sizeof(M128A));
 original=c;
 check_unwind(fn,&c,&original,off);
 unsigned regs[]={5,3,6,7,12,13,14,15};
 for(unsigned i=0;i<8;i++) {
  if(regs[i]>=8) { CHECK(p[off]==0x41); off++; }
  CHECK(p[off++]==0x50+regs[i]%8);
  c.Rsp-=8; *(DWORD64*)c.Rsp=*reg(&c,regs[i]);
  check_unwind(fn,&c,&original,off);
 }
 CHECK(!memcmp(p+off,"\x48\x81\xec",3)); c.Rsp-=*(DWORD*)(p+off+3); off+=7;
 check_unwind(fn,&c,&original,off);
 CHECK(!memcmp(p+off,"\x48\x8d\xac\x24",4)); c.Rbp=c.Rsp+*(DWORD*)(p+off+4); off+=8;
 check_unwind(fn,&c,&original,off);
 for(unsigned i=6;i<16;i++) {
  CHECK(p[off++]==0xf3); if(i>=8) CHECK(p[off++]==0x44);
  CHECK(p[off++]==0x0f); CHECK(p[off++]==0x7f);
  CHECK(p[off++]==0x84+(i%8)*8); CHECK(p[off++]==0x24);
  DWORD disp=*(DWORD*)(p+off); off+=4;
  memcpy((void*)(c.Rsp+disp),&c.Xmm0+i,sizeof(M128A));
  check_unwind(fn,&c,&original,off);
 }
 /* Full body: dynamic CP arguments below the fixed frame. */
 c.Rsp-=48; check_unwind(fn,&c,&original,off+1);
 c.Rsp+=48;
 /* Exercise every boundary in the restore sequence and canonical epilogue. */
 unsigned ep=off;
 while(ep<off+64 && p[ep]!=0xe8) ep++;
 CHECK(ep<off+64); ep+=5;
 memset(&c.Xmm6,0,10*sizeof(M128A));
 for(unsigned i=6;i<16;i++) {
  check_unwind(fn,&c,&original,ep);
  CHECK(p[ep++]==0xf3); if(i>=8) CHECK(p[ep++]==0x44);
  CHECK(p[ep++]==0x0f); CHECK(p[ep++]==0x6f);
  CHECK(p[ep++]==0x84+(i%8)*8); CHECK(p[ep++]==0x24);
  DWORD disp=*(DWORD*)(p+ep); ep+=4;
  memcpy(&c.Xmm0+i,(void*)(c.Rsp+disp),sizeof(M128A));
 }
 check_unwind(fn,&c,&original,ep);
 CHECK(!memcmp(p+ep,"\xd9\xac\x24",3)); ep+=7;
 check_unwind(fn,&c,&original,ep);
 CHECK(!memcmp(p+ep,"\x0f\xae\x94\x24",4)); ep+=8;
 check_unwind(fn,&c,&original,ep);
 CHECK(!memcmp(p+ep,"\x48\x81\xc4",3)); c.Rsp+=*(DWORD*)(p+ep+3); ep+=7;
 for(int i=7;i>=0;i--) {
  check_unwind(fn,&c,&original,ep);
  if(regs[i]>=8) CHECK(p[ep++]==0x41);
  CHECK(p[ep++]==0x58+regs[i]%8);
  *reg(&c,regs[i])=*(DWORD64*)c.Rsp; c.Rsp+=8;
 }
 check_unwind(fn,&c,&original,ep); CHECK(p[ep]==0xc3);
 unsigned short oldcw, expectedcw=0x037f, gotcw;
 unsigned oldmx, expectedmx=0x3f80, gotmx;
 __asm__ volatile("fnstcw %0" : "=m"(oldcw));
 __asm__ volatile("stmxcsr %0" : "=m"(oldmx));
 __asm__ volatile("fldcw %0" : : "m"(expectedcw));
 __asm__ volatile("ldmxcsr %0" : : "m"(expectedmx));
 CHECK(((Callback6)fn)(0x100000001ULL,2,3,4,0x200000005ULL,6)==0x300000015ULL);
 __asm__ volatile("fnstcw %0" : "=m"(gotcw));
 __asm__ volatile("stmxcsr %0" : "=m"(gotmx));
 __asm__ volatile("fldcw %0" : : "m"(oldcw));
 __asm__ volatile("ldmxcsr %0" : : "m"(oldmx));
 CHECK(gotcw==expectedcw && gotmx==expectedmx);
 puts("Windows callback and prologue unwind OK");
}
__declspec(dllexport) void Walk(void *callback) {
 CONTEXT c; DWORD64 base,frame; void *data; unsigned count=0;
 RtlCaptureContext(&c);
 for(unsigned i=0;i<12;i++) {
  PRUNTIME_FUNCTION f=RtlLookupFunctionEntry(c.Rip,&base,NULL); CHECK(f!=NULL);
  int found=base+f->BeginAddress==(DWORD64)callback;
  RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,c.Rip,f,&c,&data,&frame,NULL);
  count++;
  if(found) { CHECK(count>=3); puts("Live CP frame and callback unwind OK"); return; }
 }
 CHECK(0);
}

__declspec(dllexport) void FaultWalk(CONTEXT *fault, void *callback) {
 CONTEXT c=*fault; DWORD64 base,frame; void *data;
 for(unsigned i=0;i<20;i++) {
  PRUNTIME_FUNCTION f=RtlLookupFunctionEntry(c.Rip,&base,NULL); CHECK(f!=NULL);
  int found=base+f->BeginAddress==(DWORD64)callback;
  RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,c.Rip,f,&c,&data,&frame,NULL);
  if(found) { puts("Exception context unwind through CP callback OK"); return; }
 }
 CHECK(0);
}

/* Separate native Windows process: publish and inspect the i386 clipboard formats. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"clipboard native check failed: %d (%s), error %lu\n",__LINE__,#x,GetLastError()); ExitProcess(80);} }while(0)
static void open_clip(HWND w) { for(int i=0;i<100;i++){if(OpenClipboard(w))return;Sleep(10);} CHECK(0); }
static void put(UINT format,const void *data,SIZE_T size) {
	HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,size);CHECK(h);void *p=GlobalLock(h);CHECK(p);memcpy(p,data,size);GlobalUnlock(h);CHECK(SetClipboardData(format,h));
}
static void save(UINT format,const char *name) {
	HANDLE h=GetClipboardData(format);CHECK(h);SIZE_T n=GlobalSize(h);void*p=GlobalLock(h);CHECK(p);FILE*f=fopen(name,"wb");CHECK(f);CHECK(fwrite(p,1,n,f)==n);fclose(f);GlobalUnlock(h);
}
static void load(UINT format,const char *name) {
	FILE*f=fopen(name,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);void*p=malloc(n);CHECK(p);CHECK(fread(p,1,n,f)==(size_t)n);fclose(f);put(format,p,n);free(p);
}
int main(int argc,char **argv) {
	CHECK(argc==3);const char *op=argv[1],*mode=argv[2];
	UINT rtf=RegisterClipboardFormatW(L"Rich Text Format"),native=RegisterClipboardFormatW(L"BlackBox Data"),info=RegisterClipboardFormatW(L"BlackBox Info");
	HWND w=CreateWindowExW(0,L"STATIC",L"Native clipboard test",0,0,0,0,0,0,0,0,0);CHECK(w);
	if(!strcmp(op,"keep")){
		puts("Clipboard native keeper ready");fflush(stdout);
		MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
	}
	open_clip(w);
	if(!strcmp(op,"set")) {
	CHECK(EmptyClipboard());
	if(!strcmp(mode,"unicode")){const wchar_t s[]=L"\x0412\x043d\x0435\x0448\x043d\x0438\x0439\r\ntext\tend";put(CF_UNICODETEXT,s,sizeof(s));}
	else if(!strcmp(mode,"ansi")){const char s[]="ANSI caf\xe9\r\nx";put(CF_TEXT,s,sizeof(s));}
	else if(!strcmp(mode,"rtf")){const char s[]="{\\rtf1\\ansi\\ansicpg1252{\\fonttbl{\\f0 Arial;}}\\f0\\b Bold\\b0  \\u1046?\\par tail}";put(rtf,s,sizeof(s));}
	else if((!strcmp(mode,"native")||!strcmp(mode,"native-only"))){load(native,"native.dat");if(!strcmp(mode,"native"))load(info,"info.dat");}
	else {
	HDC dc=CreateMetaFileW(NULL);CHECK(dc);SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,100,50,NULL);Rectangle(dc,0,0,100,50);
	HMETAFILE mf=CloseMetaFile(dc);CHECK(mf);METAFILEPICT mp={MM_ANISOTROPIC,1000,500,mf};put(CF_METAFILEPICT,&mp,sizeof(mp));
	}
	} else {
	CHECK(IsClipboardFormatAvailable(native));
	CHECK(IsClipboardFormatAvailable(info));
	CHECK(IsClipboardFormatAvailable(CF_METAFILEPICT));
	HGLOBAL h=GetClipboardData(CF_METAFILEPICT);CHECK(h);METAFILEPICT*mp=GlobalLock(h);CHECK(mp);CHECK(mp->hMF);CHECK(GetMetaFileBitsEx(mp->hMF,0,NULL)>0);GlobalUnlock(h);
	if(strcmp(mode,"picture")){
	CHECK(IsClipboardFormatAvailable(CF_UNICODETEXT));CHECK(IsClipboardFormatAvailable(CF_TEXT));CHECK(IsClipboardFormatAvailable(rtf));
	h=GetClipboardData(CF_UNICODETEXT);const wchar_t*p=GlobalLock(h);CHECK(p);CHECK(!wcscmp(p,L"BlackBox \x0416\r\nline"));GlobalUnlock(h);
	h=GetClipboardData(rtf);const char*s=GlobalLock(h);CHECK(s);CHECK(strstr(s,"\\rtf"));CHECK(strstr(s,"\\b"));GlobalUnlock(h);
	save(native,"native.dat");save(info,"info.dat");
	}
	}
	CHECK(CloseClipboard());DestroyWindow(w);puts("Native clipboard OK");return 0;
}

/* Private file/URI associations and child processes for Dialog external-call tests. */
#define _WIN32_WINNT 0x0600
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#define CHECK(x) do { if(!(x)){fwprintf(stderr,L"external64 native check failed at %d, error %lu\n",__LINE__,GetLastError());ExitProcess(110);} }while(0)

static WCHAR scheme[128], output[1024];

static void marker(const WCHAR *name,const WCHAR *value,BOOL append){
	WCHAR path[1200];swprintf(path,1200,L"%ls\\%ls",output,name);
	FILE *f=_wfopen(path,append?L"ab":L"wb");CHECK(f);
	int n=WideCharToMultiByte(CP_UTF8,0,value,-1,NULL,0,NULL,NULL);CHECK(n>0);
	char *utf8=malloc(n);CHECK(utf8);CHECK(WideCharToMultiByte(CP_UTF8,0,value,-1,utf8,n,NULL,NULL));
	CHECK(fwrite(utf8,1,n-1,f)==(size_t)n-1);free(utf8);CHECK(!fclose(f));
}

static void reg_value(const WCHAR *suffix,const WCHAR *name,const WCHAR *value){
	WCHAR path[1024];swprintf(path,1024,L"%ls",suffix);HKEY key;
	CHECK(RegCreateKeyExW(HKEY_CLASSES_ROOT,path,0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL)==ERROR_SUCCESS);
	CHECK(RegSetValueExW(key,name,0,REG_SZ,(const BYTE*)value,(DWORD)(wcslen(value)+1)*2)==ERROR_SUCCESS);
	CHECK(RegCloseKey(key)==ERROR_SUCCESS);
}

static void association(BOOL install){
	WCHAR ext[140],progid[160],key[256],exe[1024],command[1200];
	swprintf(ext,140,L".%ls",scheme);swprintf(progid,160,L"%ls.Document",scheme);
	if(install){
		CHECK(GetModuleFileNameW(NULL,exe,1024));swprintf(command,1200,L"\"%ls\" --record \"%%1\"",exe);
		reg_value(ext,NULL,progid);reg_value(progid,NULL,L"External64 test document");
		swprintf(key,256,L"%ls\\shell\\open\\command",progid);reg_value(key,NULL,command);
		reg_value(scheme,NULL,L"URL:External64 test");reg_value(scheme,L"URL Protocol",L"");
		swprintf(key,256,L"%ls\\shell\\open\\command",scheme);reg_value(key,NULL,command);
	}else{
		const WCHAR *names[]={ext,progid,scheme};
		for(int i=0;i<3;i++){swprintf(key,256,L"%ls",names[i]);LONG r=RegDeleteTreeW(HKEY_CLASSES_ROOT,key);CHECK(r==ERROR_SUCCESS||r==ERROR_FILE_NOT_FOUND);}
	}
	SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,NULL,NULL);
}

int wmain(int argc,WCHAR **argv){
	CHECK(GetEnvironmentVariableW(L"BB_EXT_SCHEME",scheme,128));
	CHECK(GetEnvironmentVariableW(L"BB_EXT_OUTPUT",output,1024));CHECK(argc>=2);
	if(!wcscmp(argv[1],L"--setup"))association(TRUE);
	else if(!wcscmp(argv[1],L"--cleanup"))association(FALSE);
	else if(!wcscmp(argv[1],L"--record")){
		CHECK(argc==3);marker(wcsstr(argv[2],L"://")?L"uri.marker":L"file.marker",argv[2],FALSE);
	}else if(!wcscmp(argv[1],L"--run")){
		CHECK(argc==3);marker(L"run.marker",argv[2],FALSE);
	}else if(!wcscmp(argv[1],L"--hidden")){
		STARTUPINFOW si={0};GetStartupInfoW(&si);CHECK(si.dwFlags&STARTF_USESHOWWINDOW);CHECK(si.wShowWindow==SW_HIDE);
		marker(L"hidden.marker",L"hidden",FALSE);
	}else CHECK(0);
	return 0;
}

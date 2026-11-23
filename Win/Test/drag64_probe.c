/* Test-only native OLE peer. Production BlackBox has no dependency on this DLL. */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"drag64 native check failed: %d error %lu\n",__LINE__,GetLastError());ExitProcess(110);}}while(0)

typedef struct Data {IDataObject iface; LONG refs; UINT format; STGMEDIUM medium;} Data;
typedef struct Source {IDropSource iface; LONG refs;} Source;
typedef struct Target {IDropTarget iface; LONG refs; DWORD effect;} Target;
static HWND peer;
static Target target;
static LONG received;
static DWORD received_effect;
static volatile LONG feedback_effect;
static DWORD drag_button=MK_LBUTTON;
static HGLOBAL global_copy(HGLOBAL source) {
	SIZE_T n=GlobalSize(source);HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,n);CHECK(h);
	void*s=GlobalLock(source),*d=GlobalLock(h);CHECK(s&&d);memcpy(d,s,n);GlobalUnlock(h);GlobalUnlock(source);return h;
}
static HRESULT STDMETHODCALLTYPE data_qi(IDataObject*self,REFIID iid,void**out){if(!out)return E_POINTER;*out=NULL;if(IsEqualIID(iid,&IID_IUnknown)||IsEqualIID(iid,&IID_IDataObject)){*out=self;IDataObject_AddRef(self);return S_OK;}return E_NOINTERFACE;}
static ULONG STDMETHODCALLTYPE data_add(IDataObject*self){return ++((Data*)self)->refs;}
static ULONG STDMETHODCALLTYPE data_release(IDataObject*self){Data*d=(Data*)self;ULONG n=--d->refs;if(!n){ReleaseStgMedium(&d->medium);free(d);}return n;}
static HRESULT STDMETHODCALLTYPE data_query(IDataObject*self,FORMATETC*f){Data*d=(Data*)self;return f&&f->cfFormat==d->format&&f->dwAspect==DVASPECT_CONTENT&&f->lindex==-1&&(f->tymed&d->medium.tymed)?S_OK:DV_E_FORMATETC;}
static HRESULT STDMETHODCALLTYPE data_get(IDataObject*self,FORMATETC*f,STGMEDIUM*m){Data*d=(Data*)self;if(!m)return E_POINTER;memset(m,0,sizeof(*m));if(FAILED(data_query(self,f)))return DV_E_FORMATETC;m->tymed=d->medium.tymed;m->hGlobal=global_copy(d->medium.hGlobal);if(m->tymed==TYMED_MFPICT){METAFILEPICT*p=GlobalLock(m->hGlobal);p->hMF=CopyMetaFileW(p->hMF,NULL);CHECK(p->hMF);GlobalUnlock(m->hGlobal);}return S_OK;}
static HRESULT STDMETHODCALLTYPE data_here(IDataObject*s,FORMATETC*f,STGMEDIUM*m){(void)s;(void)f;(void)m;return E_NOTIMPL;}
static HRESULT STDMETHODCALLTYPE data_canon(IDataObject*s,FORMATETC*f,FORMATETC*g){(void)s;if(!f||!g)return E_POINTER;*g=*f;g->ptd=NULL;return DATA_S_SAMEFORMATETC;}
static HRESULT STDMETHODCALLTYPE data_set(IDataObject*s,FORMATETC*f,STGMEDIUM*m,BOOL r){(void)s;(void)f;(void)m;(void)r;return E_NOTIMPL;}
static HRESULT STDMETHODCALLTYPE data_enum(IDataObject*s,DWORD dir,IEnumFORMATETC**out){Data*d=(Data*)s;FORMATETC f={(CLIPFORMAT)d->format,NULL,DVASPECT_CONTENT,-1,d->medium.tymed};return dir==DATADIR_GET?SHCreateStdEnumFmtEtc(1,&f,out):E_NOTIMPL;}
static HRESULT STDMETHODCALLTYPE data_advise(IDataObject*s,FORMATETC*f,DWORD a,IAdviseSink*k,DWORD*c){(void)s;(void)f;(void)a;(void)k;if(c)*c=0;return OLE_E_ADVISENOTSUPPORTED;}
static HRESULT STDMETHODCALLTYPE data_unadvise(IDataObject*s,DWORD c){(void)s;(void)c;return OLE_E_ADVISENOTSUPPORTED;}
static HRESULT STDMETHODCALLTYPE data_enumadvise(IDataObject*s,IEnumSTATDATA**out){(void)s;if(out)*out=NULL;return OLE_E_ADVISENOTSUPPORTED;}
static IDataObjectVtbl data_vt={data_qi,data_add,data_release,data_get,data_here,data_query,data_canon,data_set,data_enum,data_advise,data_unadvise,data_enumadvise};
static Data*make_data(int mode,const WCHAR*path){
	Data*d=calloc(1,sizeof(*d));CHECK(d);d->iface.lpVtbl=&data_vt;d->refs=1;d->medium.tymed=TYMED_HGLOBAL;
	const WCHAR text[]=L"External drag \x0416";const char rtf[]="{\\rtf1\\ansi\\b Drag\\b0  \\u1046?}";SIZE_T n=sizeof(text);
	d->format=CF_UNICODETEXT;
	if(mode==1){d->format=RegisterClipboardFormatW(L"Rich Text Format");n=sizeof(rtf);}
	if(mode==2){d->format=CF_HDROP;n=sizeof(DROPFILES)+(wcslen(path)+2)*sizeof(WCHAR);}
	if(mode==3){d->format=RegisterClipboardFormatW(L"Unsupported Drag64 Format");}
	if(mode==4){d->format=CF_METAFILEPICT;d->medium.tymed=TYMED_MFPICT;n=sizeof(METAFILEPICT);}
	d->medium.hGlobal=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,n);CHECK(d->medium.hGlobal);void*p=GlobalLock(d->medium.hGlobal);CHECK(p);
	if(mode==1)memcpy(p,rtf,n);
	else if(mode==2){DROPFILES*f=p;f->pFiles=sizeof(*f);f->fWide=TRUE;memcpy((char*)p+sizeof(*f),path,(wcslen(path)+1)*2);}
	else if(mode==4){METAFILEPICT*f=p;HDC dc=CreateMetaFileW(NULL);CHECK(dc);SetMapMode(dc,8);SetWindowExtEx(dc,100,50,NULL);Rectangle(dc,0,0,100,50);f->mm=8;f->xExt=1000;f->yExt=500;f->hMF=CloseMetaFile(dc);CHECK(f->hMF);}
	else memcpy(p,text,sizeof(text));
	GlobalUnlock(d->medium.hGlobal);return d;
}
static HRESULT STDMETHODCALLTYPE source_qi(IDropSource*self,REFIID iid,void**out){if(!out)return E_POINTER;*out=NULL;if(IsEqualIID(iid,&IID_IUnknown)||IsEqualIID(iid,&IID_IDropSource)){*out=self;IDropSource_AddRef(self);return S_OK;}return E_NOINTERFACE;}
static ULONG STDMETHODCALLTYPE source_add(IDropSource*self){return ++((Source*)self)->refs;}
static ULONG STDMETHODCALLTYPE source_release(IDropSource*self){return --((Source*)self)->refs;}
static HRESULT STDMETHODCALLTYPE source_continue(IDropSource*self,BOOL escape,DWORD key){(void)self;return escape?DRAGDROP_S_CANCEL:!(key&drag_button)?DRAGDROP_S_DROP:S_OK;}
static HRESULT STDMETHODCALLTYPE source_feedback(IDropSource*self,DWORD effect){(void)self;InterlockedExchange(&feedback_effect,(LONG)effect);return DRAGDROP_S_USEDEFAULTCURSORS;}
static IDropSourceVtbl source_vt={source_qi,source_add,source_release,source_continue,source_feedback};

static void inspect_data(IDataObject*data){
	IDataObject*qi=NULL;CHECK(SUCCEEDED(IDataObject_QueryInterface(data,&IID_IDataObject,(void**)&qi)));CHECK(qi);IDataObject_Release(qi);
	IEnumFORMATETC*en=NULL,*clone=NULL;CHECK(IDataObject_EnumFormatEtc(data,DATADIR_GET,&en)==S_OK);CHECK(en);
	FORMATETC fmt[2];struct {ULONG count;DWORD canary;} fetched={0,0x12345678};
	CHECK(IEnumFORMATETC_Next(en,2,fmt,&fetched.count)==S_OK);CHECK(fetched.count==2&&fetched.canary==0x12345678);
	CHECK(IEnumFORMATETC_Clone(en,&clone)==S_OK);CHECK(IEnumFORMATETC_Next(clone,1,fmt,NULL)==S_OK);IEnumFORMATETC_Release(clone);
	CHECK(IEnumFORMATETC_Reset(en)==S_OK);unsigned count=0;
	while(IEnumFORMATETC_Next(en,1,fmt,&fetched.count)==S_OK){
		CHECK(fmt[0].ptd==NULL&&fmt[0].dwAspect==DVASPECT_CONTENT&&fmt[0].lindex==-1);CHECK(IDataObject_QueryGetData(data,fmt)==S_OK);
		STGMEDIUM m={0};CHECK(IDataObject_GetData(data,fmt,&m)==S_OK);CHECK(m.tymed==fmt[0].tymed);CHECK(m.hGlobal);
		if(fmt[0].cfFormat==CF_UNICODETEXT){const WCHAR*p=GlobalLock(m.hGlobal);CHECK(p&&!wcscmp(p,L"DRAG"));GlobalUnlock(m.hGlobal);}
		ReleaseStgMedium(&m);count++;
	}
	CHECK(count==6);CHECK(IEnumFORMATETC_Reset(en)==S_OK);CHECK(IEnumFORMATETC_Skip(en,99)==S_FALSE);CHECK(IEnumFORMATETC_Next(en,1,fmt,&fetched.count)==S_FALSE);CHECK(!fetched.count);IEnumFORMATETC_Release(en);
	FORMATETC f={CF_UNICODETEXT,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM m={0};
	CHECK(IDataObject_GetData(data,&f,&m)==S_OK);ReleaseStgMedium(&m);CHECK(IDataObject_GetData(data,&f,&m)==S_OK);ReleaseStgMedium(&m);
	f.tymed=TYMED_ISTREAM;CHECK(FAILED(IDataObject_QueryGetData(data,&f)));
}
static HRESULT STDMETHODCALLTYPE target_qi(IDropTarget*self,REFIID iid,void**out){if(!out)return E_POINTER;*out=NULL;if(IsEqualIID(iid,&IID_IUnknown)||IsEqualIID(iid,&IID_IDropTarget)){*out=self;IDropTarget_AddRef(self);return S_OK;}return E_NOINTERFACE;}
static ULONG STDMETHODCALLTYPE target_add(IDropTarget*self){return ++((Target*)self)->refs;}
static ULONG STDMETHODCALLTYPE target_release(IDropTarget*self){return --((Target*)self)->refs;}
static DWORD choose(DWORD allowed,DWORD key){if((key&(MK_CONTROL|MK_SHIFT))==(MK_CONTROL|MK_SHIFT))return 0;if(key&MK_CONTROL)return allowed&DROPEFFECT_COPY;if(key&MK_SHIFT)return allowed&DROPEFFECT_MOVE;return allowed&DROPEFFECT_MOVE?DROPEFFECT_MOVE:allowed&DROPEFFECT_COPY;}
static HRESULT STDMETHODCALLTYPE target_enter(IDropTarget*self,IDataObject*d,DWORD key,POINTL point,DWORD*effect){(void)point;inspect_data(d);((Target*)self)->effect=*effect;*effect=choose(*effect,key);return S_OK;}
static HRESULT STDMETHODCALLTYPE target_over(IDropTarget*self,DWORD key,POINTL point,DWORD*effect){(void)self;(void)point;*effect=choose(*effect,key);return S_OK;}
static HRESULT STDMETHODCALLTYPE target_leave(IDropTarget*self){(void)self;return S_OK;}
static HRESULT STDMETHODCALLTYPE target_drop(IDropTarget*self,IDataObject*d,DWORD key,POINTL point,DWORD*effect){(void)self;(void)point;inspect_data(d);*effect=choose(*effect,key);received_effect=*effect;InterlockedIncrement(&received);return S_OK;}
static IDropTargetVtbl target_vt={target_qi,target_add,target_release,target_enter,target_over,target_leave,target_drop};

typedef struct Driver {int x,y,mod,cancel;} Driver;
static Driver driver;
static HANDLE thread;
static DWORD WINAPI drive(void*arg){
	Driver*d=arg;Sleep(200);SetCursorPos(d->x,d->y);
	for(int i=0;i<100;i++){
		SetCursorPos(d->x,d->y);Sleep(20);
		if(i>20&&InterlockedCompareExchange(&feedback_effect,0,0))break;
	}
	SetCursorPos(d->x,d->y);Sleep(60);
	if(d->cancel){keybd_event(VK_ESCAPE,1,0,0);Sleep(100);}
	SetCursorPos(d->x,d->y);mouse_event(d->mod==3?MOUSEEVENTF_RIGHTUP:MOUSEEVENTF_LEFTUP,0,0,0,0);Sleep(50);
	if(d->mod==3){Sleep(200);keybd_event(VK_HOME,0,0,0);keybd_event(VK_HOME,0,KEYEVENTF_KEYUP,0);Sleep(40);keybd_event(VK_RETURN,0,0,0);keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);}
	return 0;
}
static HWND dummy(void){HWND w=CreateWindowExW(WS_EX_TOPMOST,L"STATIC",L"Drag64 native peer",WS_POPUP|WS_VISIBLE|SS_NOTIFY,5,5,90,70,NULL,NULL,NULL,NULL);CHECK(w);return w;}
static void begin(int x,int y,int mod,int cancel){driver=(Driver){x,y,mod,cancel};feedback_effect=0;drag_button=mod==3?MK_RBUTTON:MK_LBUTTON;SetCursorPos(30,30);if(mod==1)keybd_event(VK_LCONTROL,29,0,0);if(mod==2)keybd_event(VK_LSHIFT,42,0,0);mouse_event(mod==3?MOUSEEVENTF_RIGHTDOWN:MOUSEEVENTF_LEFTDOWN,0,0,0,0);Sleep(40);MSG msg;while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}BYTE keys[256];GetKeyboardState(keys);if(mod==1)keys[VK_CONTROL]=keys[VK_LCONTROL]=0x80;if(mod==2)keys[VK_SHIFT]=keys[VK_LSHIFT]=0x80;SetKeyboardState(keys);thread=CreateThread(NULL,0,drive,&driver,0,NULL);CHECK(thread);}
static void finish(void){CHECK(WaitForSingleObject(thread,3000)==WAIT_OBJECT_0);CloseHandle(thread);thread=NULL;
	if(driver.cancel)keybd_event(VK_ESCAPE,1,KEYEVENTF_KEYUP,0);
	if(driver.mod==1)keybd_event(VK_LCONTROL,29,KEYEVENTF_KEYUP,0);
	if(driver.mod==2)keybd_event(VK_LSHIFT,42,KEYEVENTF_KEYUP,0);
	BYTE keys[256];GetKeyboardState(keys);
	keys[VK_ESCAPE]=keys[VK_CONTROL]=keys[VK_LCONTROL]=keys[VK_SHIFT]=keys[VK_LSHIFT]=0;SetKeyboardState(keys);
}
__declspec(dllexport) int Incoming(HWND wnd,int x,int y,int mode,int mod,int cancel,const WCHAR*path){
	(void)wnd;Data*d=make_data(mode,path);Source source={{&source_vt},1};HWND w=dummy();begin(x,y,mod,cancel);DWORD effect=0;HRESULT hr=DoDragDrop(&d->iface,&source.iface,DROPEFFECT_COPY|DROPEFFECT_MOVE,&effect);finish();DestroyWindow(w);IDataObject_Release(&d->iface);CHECK(source.refs==1);CHECK(hr==DRAGDROP_S_DROP||hr==DRAGDROP_S_CANCEL);return (int)effect;
}
__declspec(dllexport) void BeginOutgoing(int x,int y,int mod,int cancel){
	CHECK(!peer);peer=dummy();target=(Target){{&target_vt},1,0};CHECK(RegisterDragDrop(peer,&target.iface)==S_OK);received=0;received_effect=0;begin(x?x:40,y?y:40,mod,cancel);
}
__declspec(dllexport) int EndOutgoing(int expected){finish();CHECK(RevokeDragDrop(peer)==S_OK);DestroyWindow(peer);peer=NULL;CHECK(target.refs==1);CHECK(received==(expected?1:0));CHECK((int)received_effect==expected);puts("Native OLE drag peer OK");return 1;}

__declspec(dllexport) void LegacyFiles(HWND wnd,const WCHAR*path){
	Data*d=make_data(2,path);HGLOBAL h=d->medium.hGlobal;d->medium.hGlobal=NULL;d->medium.tymed=TYMED_NULL;
	SendMessageW(wnd,WM_DROPFILES,(WPARAM)h,0);IDataObject_Release(&d->iface);
}

__declspec(dllexport) void Gesture(int sx,int sy,int tx,int ty,int mod){
	CHECK(!thread);driver=(Driver){tx,ty,mod,0};feedback_effect=0;
	SetCursorPos(sx,sy);
	if(mod==1)keybd_event(VK_LCONTROL,29,0,0);
	if(mod==2)keybd_event(VK_LSHIFT,42,0,0);
	Sleep(40);MSG msg;
	while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
	BYTE keys[256];GetKeyboardState(keys);
	if(mod==1)keys[VK_CONTROL]=keys[VK_LCONTROL]=0x80;
	SetKeyboardState(keys);
	thread=CreateThread(NULL,0,drive,&driver,0,NULL);CHECK(thread);
	mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0);
	do{
		while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
		MsgWaitForMultipleObjects(1,&thread,FALSE,20,QS_ALLINPUT);
	}while(WaitForSingleObject(thread,0)==WAIT_TIMEOUT);
	while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
	finish();
}

static DWORD wait_process(HANDLE process){
	DWORD started=GetTickCount();MSG msg;
	while(WaitForSingleObject(process,0)==WAIT_TIMEOUT){
		CHECK(GetTickCount()-started<30000);
		while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
		MsgWaitForMultipleObjects(1,&process,FALSE,20,QS_ALLINPUT);
	}
	DWORD code;CHECK(GetExitCodeProcess(process,&code));return code;
}
__declspec(dllexport) int IncomingProcess(HWND wnd,int x,int y,int mode,int mod,int cancel,const WCHAR*path){
	WCHAR exe[MAX_PATH],command[2048];
	CHECK(GetModuleFileNameW(GetModuleHandleW(L"drag64probe.dll"),exe,MAX_PATH));
	WCHAR*name=wcsrchr(exe,L'\\');CHECK(name);wcscpy(name+1,L"drag64peer.exe");
	swprintf(command,2048,L"\"%ls\" incoming %llu %d %d %d %d %d \"%ls\"",exe,(unsigned long long)(UINT_PTR)wnd,x,y,mode,mod,cancel,path?path:L"");
	STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof(si);
	CHECK(CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi));CloseHandle(pi.hThread);
	DWORD result=wait_process(pi.hProcess);CloseHandle(pi.hProcess);CHECK(result<=2);return (int)result;
}
int wmain(int argc,WCHAR**argv){
	CHECK(SUCCEEDED(OleInitialize(NULL)));
	CHECK(argc==9&&!wcscmp(argv[1],L"incoming"));
	int effect=Incoming((HWND)(UINT_PTR)wcstoull(argv[2],NULL,10),_wtoi(argv[3]),_wtoi(argv[4]),_wtoi(argv[5]),_wtoi(argv[6]),_wtoi(argv[7]),argv[8]);
	OleUninitialize();return effect;
}

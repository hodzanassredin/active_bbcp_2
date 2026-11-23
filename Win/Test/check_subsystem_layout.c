/* Independent Windows x64 ABI expectations used by CheckWinSubsystem64. */
#include <windows.h>
#include <commdlg.h>
#include <prsht.h>
#include <oleauto.h>
#include <rpc.h>
#include <stddef.h>
#define SIZE(t,n) _Static_assert(sizeof(t) == (n), #t " size")
#define OFFSET(t,f,n) _Static_assert(offsetof(t,f) == (n), #t "." #f " offset")
SIZE(DEVMODEW,220); SIZE(DOCINFOW,40); SIZE(PRINTDLGW,120); SIZE(OPENFILENAMEW,152);
SIZE(VARIANT,24); SIZE(SAFEARRAY,32); SIZE(STATSTG,80); SIZE(FORMATETC,32); SIZE(STGMEDIUM,24);
SIZE(PROPSHEETHEADERW,96); SIZE(PROPSHEETPAGEW,96); SIZE(RPC_MESSAGE,80);
SIZE(MIDL_STUB_MESSAGE,400); SIZE(MIDL_STUB_DESC,152);
OFFSET(PRINTDLGW,hInstance,56); OFFSET(DEVMODEW,dmFields,72); OFFSET(VARIANT,lVal,8);
OFFSET(SAFEARRAY,pvData,16); OFFSET(STATSTG,clsid,56); OFFSET(DOCINFOW,fwType,32);
OFFSET(MIDL_STUB_MESSAGE,MaxCount,104); OFFSET(MIDL_STUB_MESSAGE,pfnAllocate,120);
SIZE(CONTEXT,1232); OFFSET(CONTEXT,ContextFlags,48); OFFSET(CONTEXT,Rsp,152);
OFFSET(CONTEXT,Rip,248); OFFSET(CONTEXT,VectorRegister,768);

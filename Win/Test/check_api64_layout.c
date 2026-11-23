#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <winspool.h>
_Static_assert(sizeof(SYSTEM_INFO) == 48, "SYSTEM_INFO");
_Static_assert(sizeof(MEMORYSTATUS) == 56, "MEMORYSTATUS");
_Static_assert(sizeof(MEMORY_BASIC_INFORMATION) == 48, "MEMORY_BASIC_INFORMATION");
_Static_assert(sizeof(EXCEPTION_RECORD) == 152, "EXCEPTION_RECORD");
_Static_assert(sizeof(EXCEPTION_POINTERS) == 16, "EXCEPTION_POINTERS");
_Static_assert(sizeof(COPYDATASTRUCT) == 24, "COPYDATASTRUCT");
_Static_assert(sizeof(MEASUREITEMSTRUCT) == 32, "MEASUREITEMSTRUCT");
_Static_assert(sizeof(DRAWITEMSTRUCT) == 64, "DRAWITEMSTRUCT");
_Static_assert(sizeof(DELETEITEMSTRUCT) == 32, "DELETEITEMSTRUCT");
_Static_assert(sizeof(COMPAREITEMSTRUCT) == 56, "COMPAREITEMSTRUCT");
_Static_assert(sizeof(NMHDR) == 24, "NMHDR");
_Static_assert(sizeof(MENUITEMINFOA) == 80, "MENUITEMINFOA");
_Static_assert(sizeof(MENUITEMINFOW) == 80, "MENUITEMINFOW");
_Static_assert(sizeof(SHFILEOPSTRUCTW) == 56, "SHFILEOPSTRUCTW");
_Static_assert(sizeof(SHELLEXECUTEINFOA) == 112, "SHELLEXECUTEINFOA");
_Static_assert(sizeof(APPBARDATA) == 48, "APPBARDATA");
_Static_assert(sizeof(PRINTER_INFO_2W) == 136, "PRINTER_INFO_2W");
_Static_assert(sizeof(TOKEN_PRIVILEGES) == 16, "TOKEN_PRIVILEGES");
_Static_assert(sizeof(TOKEN_GROUPS) == 24, "TOKEN_GROUPS");
_Static_assert(sizeof(CRITICAL_SECTION) == 40, "CRITICAL_SECTION");
_Static_assert(sizeof(CRITICAL_SECTION_DEBUG) == 48, "CRITICAL_SECTION_DEBUG");
_Static_assert(sizeof(IMAGE_OPTIONAL_HEADER32) == 224, "IMAGE_OPTIONAL_HEADER32");
_Static_assert(sizeof(IMAGE_OPTIONAL_HEADER64) == 240, "IMAGE_OPTIONAL_HEADER64");
_Static_assert(sizeof(IMAGE_NT_HEADERS32) == 248, "IMAGE_NT_HEADERS32");
_Static_assert(sizeof(IMAGE_NT_HEADERS64) == 264, "IMAGE_NT_HEADERS64");
_Static_assert(sizeof(IMAGE_TLS_DIRECTORY) == 40, "IMAGE_TLS_DIRECTORY");
_Static_assert(sizeof(IMAGE_EXPORT_DIRECTORY) == 40, "IMAGE_EXPORT_DIRECTORY");
_Static_assert(sizeof(IMAGE_IMPORT_DESCRIPTOR) == 20, "IMAGE_IMPORT_DESCRIPTOR");
_Static_assert(sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY) == 12, "IMAGE_RUNTIME_FUNCTION_ENTRY");
_Static_assert(sizeof(IMAGE_LOAD_CONFIG_DIRECTORY) == 112, "IMAGE_LOAD_CONFIG_DIRECTORY");
_Static_assert(sizeof(NOTIFYICONDATAA) == 528, "NOTIFYICONDATAA");
_Static_assert(sizeof(NOTIFYICONDATAW) == 976, "NOTIFYICONDATAW");
#include <stddef.h>
_Static_assert(offsetof(SYSTEM_INFO,dwActiveProcessorMask)==24,"SYSTEM_INFO mask");
_Static_assert(offsetof(EXCEPTION_RECORD,ExceptionInformation)==32,"EXCEPTION_RECORD info");
_Static_assert(offsetof(MENUITEMINFOW,dwItemData)==48,"MENUITEMINFO data");
_Static_assert(offsetof(MENUITEMINFOW,hbmpItem)==72,"MENUITEMINFO bitmap");
_Static_assert(offsetof(IMAGE_OPTIONAL_HEADER64,ImageBase)==24,"PE32+ base");
_Static_assert(offsetof(IMAGE_IMPORT_DESCRIPTOR,FirstThunk)==16,"Import thunk RVA");
_Static_assert(offsetof(SHFILEOPSTRUCTW,pFrom)==16,"SHFILEOPSTRUCT source");
_Static_assert(offsetof(APPBARDATA,lParam)==40,"APPBARDATA parameter");
_Static_assert(offsetof(NOTIFYICONDATAW,guidItem)==952,"NOTIFYICONDATA GUID");

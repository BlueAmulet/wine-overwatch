@ stdcall CommandLineToArgvW(wstr ptr) shell32.CommandLineToArgvW
@ stub CreateRandomAccessStreamOnFile
@ stub CreateRandomAccessStreamOverStream
@ stub CreateStreamOverRandomAccessStream
@ stdcall -private DllCanUnloadNow() shell32.DllCanUnloadNow
@ stub DllGetActivationFactory
@ stdcall -private DllGetClassObject(ptr ptr ptr) shell32.DllGetClassObject
@ stdcall GetCurrentProcessExplicitAppUserModelID(ptr) shell32.GetCurrentProcessExplicitAppUserModelID
@ stdcall GetDpiForMonitor(long long ptr ptr)
@ stub GetDpiForShellUIComponent
@ stdcall GetProcessDpiAwareness(long ptr)
@ stub GetProcessReference
@ stub GetScaleFactorForDevice
@ stub GetScaleFactorForMonitor
@ stub IStream_Copy
@ stdcall IStream_Read(ptr ptr long) shlwapi.IStream_Read
@ stub IStream_ReadStr
@ stdcall IStream_Reset(ptr) shlwapi.IStream_Reset
@ stdcall IStream_Size(ptr ptr) shlwapi.IStream_Size
@ stdcall IStream_Write(ptr ptr long) shlwapi.IStream_Write
@ stub IStream_WriteStr
@ stdcall IUnknown_AtomicRelease(long) shlwapi.IUnknown_AtomicRelease
@ stdcall IUnknown_GetSite(ptr ptr ptr) shlwapi.IUnknown_GetSite
@ stdcall IUnknown_QueryService(ptr ptr ptr ptr) shlwapi.IUnknown_QueryService
@ stdcall IUnknown_Set(ptr ptr) shlwapi.IUnknown_Set
@ stdcall IUnknown_SetSite(ptr ptr) shlwapi.IUnknown_SetSite
@ stdcall IsOS(long) shlwapi.IsOS
@ stub RegisterScaleChangeEvent
@ stub RegisterScaleChangeNotifications
@ stub RevokeScaleChangeNotifications
@ stdcall SHAnsiToAnsi(str ptr long) shlwapi.SHAnsiToAnsi
@ stdcall SHAnsiToUnicode(str ptr long) shlwapi.SHAnsiToUnicode
@ stdcall SHCopyKeyA(long str long long) shlwapi.SHCopyKeyA
@ stdcall SHCopyKeyW(long wstr long long) shlwapi.SHCopyKeyW
@ stdcall SHCreateMemStream(ptr long) shlwapi.SHCreateMemStream
@ stdcall SHCreateStreamOnFileA(str long ptr) shlwapi.SHCreateStreamOnFileA
@ stdcall SHCreateStreamOnFileEx(wstr long long long ptr ptr) shlwapi.SHCreateStreamOnFileEx
@ stdcall SHCreateStreamOnFileW(wstr long ptr) shlwapi.SHCreateStreamOnFileW
@ stdcall SHCreateThread(ptr ptr long ptr) shlwapi.SHCreateThread
@ stdcall SHCreateThreadRef(ptr ptr) shlwapi.SHCreateThreadRef
@ stub SHCreateThreadWithHandle
@ stdcall SHDeleteEmptyKeyA(long ptr) shlwapi.SHDeleteEmptyKeyA
@ stdcall SHDeleteEmptyKeyW(long ptr) shlwapi.SHDeleteEmptyKeyW
@ stdcall SHDeleteKeyA(long str) shlwapi.SHDeleteKeyA
@ stdcall SHDeleteKeyW(long wstr) shlwapi.SHDeleteKeyW
@ stdcall SHDeleteValueA(long  str  str) shlwapi.SHDeleteValueA
@ stdcall SHDeleteValueW(long wstr wstr) shlwapi.SHDeleteValueW
@ stdcall SHEnumKeyExA(long long str ptr) shlwapi.SHEnumKeyExA
@ stdcall SHEnumKeyExW(long long wstr ptr) shlwapi.SHEnumKeyExW
@ stdcall SHEnumValueA(long long str ptr ptr ptr ptr) shlwapi.SHEnumValueA
@ stdcall SHEnumValueW(long long wstr ptr ptr ptr ptr) shlwapi.SHEnumValueW
@ stdcall SHGetThreadRef(ptr) shlwapi.SHGetThreadRef
@ stdcall SHGetValueA( long str str ptr ptr ptr ) shlwapi.SHGetValueA
@ stdcall SHGetValueW( long wstr wstr ptr ptr ptr ) shlwapi.SHGetValueW
@ stdcall SHOpenRegStream2A(long str str long) shlwapi.SHOpenRegStream2A
@ stdcall SHOpenRegStream2W(long wstr wstr long) shlwapi.SHOpenRegStream2W
@ stdcall SHOpenRegStreamA(long str str long) shlwapi.SHOpenRegStreamA
@ stdcall SHOpenRegStreamW(long wstr wstr long) shlwapi.SHOpenRegStreamW
@ stdcall SHQueryInfoKeyA(long ptr ptr ptr ptr) shlwapi.SHQueryInfoKeyA
@ stdcall SHQueryInfoKeyW(long ptr ptr ptr ptr) shlwapi.SHQueryInfoKeyW
@ stdcall SHQueryValueExA(long str ptr ptr ptr ptr) shlwapi.SHQueryValueExA
@ stdcall SHQueryValueExW(long wstr ptr ptr ptr ptr) shlwapi.SHQueryValueExW
@ stdcall SHRegDuplicateHKey(long) shlwapi.SHRegDuplicateHKey
@ stdcall SHRegGetIntW(ptr wstr long) shlwapi.SHRegGetIntW
@ stdcall SHRegGetPathA(long str str ptr long) shlwapi.SHRegGetPathA
@ stdcall SHRegGetPathW(long wstr wstr ptr long) shlwapi.SHRegGetPathW
@ stdcall SHRegGetValueA( long str str long ptr ptr ptr ) shlwapi.SHRegGetValueA
@ stub SHRegGetValueFromHKCUHKLM
@ stdcall SHRegGetValueW( long wstr wstr long ptr ptr ptr ) shlwapi.SHRegGetValueW
@ stdcall SHRegSetPathA(long str str str long) shlwapi.SHRegSetPathA
@ stdcall SHRegSetPathW(long wstr wstr wstr long) shlwapi.SHRegSetPathW
@ stdcall SHReleaseThreadRef() shlwapi.SHReleaseThreadRef
@ stdcall SHSetThreadRef(ptr) shlwapi.SHSetThreadRef
@ stdcall SHSetValueA(long  str  str long ptr long) shlwapi.SHSetValueA
@ stdcall SHSetValueW(long wstr wstr long ptr long) shlwapi.SHSetValueW
@ stdcall SHStrDupA(str ptr) shlwapi.SHStrDupA
@ stdcall SHStrDupW(wstr ptr) shlwapi.SHStrDupW
@ stdcall SHUnicodeToAnsi(wstr ptr ptr) shlwapi.SHUnicodeToAnsi
@ stdcall SHUnicodeToUnicode(wstr ptr long) shlwapi.SHUnicodeToUnicode
@ stdcall SetCurrentProcessExplicitAppUserModelID(wstr) shell32.SetCurrentProcessExplicitAppUserModelID
@ stdcall SetProcessDpiAwareness(long)
@ stub SetProcessReference
@ stub UnregisterScaleChangeEvent

1   stdcall -ordinal ParseURLA(str ptr)
2   stdcall -ordinal ParseURLW(wstr ptr)
3   stdcall -noname PathFileExistsDefExtA(str long)
4   stdcall -noname PathFileExistsDefExtW(wstr long)
5   stdcall -noname PathFindOnPathExA(str ptr long)
6   stdcall -noname PathFindOnPathExW(wstr ptr long)
7   stdcall -ordinal SHAllocShared(ptr long long)
8   stdcall -ordinal SHLockShared(long long)
9   stdcall -ordinal SHUnlockShared(ptr)
10  stdcall -ordinal SHFreeShared(long long)
11  stdcall -noname SHMapHandle(long long long long long)
12  stdcall -ordinal SHCreateMemStream(ptr long)
13  stdcall -noname RegisterDefaultAcceptHeaders(ptr ptr)
14  stdcall -ordinal GetAcceptLanguagesA(ptr ptr)
15  stdcall -ordinal GetAcceptLanguagesW(ptr ptr)
16  stdcall -ordinal SHCreateThread(ptr ptr long ptr)
17  stdcall -noname SHWriteDataBlockList(ptr ptr)
18  stdcall -noname SHReadDataBlockList(ptr ptr)
19  stdcall -noname SHFreeDataBlockList(ptr)
20  stdcall -noname SHAddDataBlock(ptr ptr)
21  stdcall -noname SHRemoveDataBlock(ptr long)
22  stdcall -noname SHFindDataBlock(ptr long)
23  stdcall -noname SHStringFromGUIDA(ptr ptr long)
24  stdcall -noname SHStringFromGUIDW(ptr ptr long)
25  stdcall -noname IsCharAlphaWrapW(long) user32.IsCharAlphaW
26  stdcall -noname IsCharUpperWrapW(long) user32.IsCharUpperW
27  stdcall -noname IsCharLowerWrapW(long) user32.IsCharLowerW
28  stdcall -noname IsCharAlphaNumericWrapW(long) user32.IsCharAlphaNumericW
29  stdcall -ordinal IsCharSpaceW(long)
30  stdcall -noname IsCharBlankW(long)
31  stdcall -noname IsCharPunctW(long)
32  stdcall -noname IsCharCntrlW(ptr)
33  stdcall -noname IsCharDigitW(long)
34  stdcall -noname IsCharXDigitW(long)
35  stdcall -noname GetStringType3ExW(ptr long ptr)
36  stdcall -noname AppendMenuWrapW(long long long wstr) user32.AppendMenuW
37  stdcall -noname CallWindowProcWrapW(ptr long long long long) user32.CallWindowProcW
38  stdcall -noname CharLowerWrapW(wstr) user32.CharLowerW
39  stdcall -noname CharLowerBuffWrapW(wstr long) user32.CharLowerBuffW
40  stdcall -noname CharNextWrapW(wstr) user32.CharNextW
41  stdcall -noname CharPrevWrapW(wstr wstr) user32.CharPrevW
42  stdcall -noname CharToOemWrapW(wstr) user32.CharToOemW
43  stdcall -noname CharUpperWrapW(wstr) user32.CharUpperW
44  stdcall -noname CharUpperBuffWrapW(wstr long) user32.CharUpperBuffW
45  stdcall -noname CompareStringWrapW(long long wstr long wstr long) kernel32.CompareStringW
46  stdcall -noname CopyAcceleratorTableWrapW(long ptr long) user32.CopyAcceleratorTableW
47  stdcall -noname CreateAcceleratorTableWrapW(ptr long) user32.CreateAcceleratorTableW
48  stdcall -noname CreateDCWrapW(wstr wstr wstr ptr) gdi32.CreateDCW
49  stdcall -noname CreateDialogParamWrapW(long ptr long ptr long) user32.CreateDialogParamW
50  stdcall -noname CreateDirectoryWrapW(wstr ptr) kernel32.CreateDirectoryW
51  stdcall -noname CreateEventWrapW(ptr long long wstr) kernel32.CreateEventW
52  stdcall -noname CreateFileWrapW(wstr long long ptr long long long) kernel32.CreateFileW
53  stdcall -noname CreateFontIndirectWrapW(ptr) gdi32.CreateFontIndirectW
54  stdcall -noname CreateICWrapW(wstr wstr wstr ptr) gdi32.CreateICW
55  stdcall -noname CreateWindowExWrapW(long wstr wstr long long long long long long long long ptr) user32.CreateWindowExW
56  stdcall -noname DefWindowProcWrapW(long long long long) user32.DefWindowProcW
57  stdcall -noname DeleteFileWrapW(wstr) kernel32.DeleteFileW
58  stdcall -noname DialogBoxIndirectParamWrapW(long ptr long ptr long) user32.DialogBoxIndirectParamW
59  stdcall -noname DialogBoxParamWrapW(long wstr long ptr long) user32.DialogBoxParamW
60  stdcall -noname DispatchMessageWrapW(ptr) user32.DispatchMessageW
61  stdcall -noname DrawTextWrapW(long wstr long ptr long) user32.DrawTextW
62  stdcall -noname EnumFontFamiliesWrapW(long wstr ptr long) gdi32.EnumFontFamiliesW
63  stdcall -noname EnumFontFamiliesExWrapW(long ptr ptr long long) gdi32.EnumFontFamiliesExW
64  stdcall -noname EnumResourceNamesWrapW(long wstr ptr long) kernel32.EnumResourceNamesW
65  stdcall -noname FindFirstFileWrapW(wstr ptr) kernel32.FindFirstFileW
66  stdcall -noname FindResourceWrapW(long wstr wstr) kernel32.FindResourceW
67  stdcall -noname FindWindowWrapW(wstr wstr) user32.FindWindowW
68  stdcall -noname FormatMessageWrapW(long ptr long long ptr long ptr) kernel32.FormatMessageW
69  stdcall -noname GetClassInfoWrapW(long wstr ptr) user32.GetClassInfoW
70  stdcall -noname GetClassLongWrapW(long long) user32.GetClassLongW
71  stdcall -noname GetClassNameWrapW(long ptr long) user32.GetClassNameW
72  stdcall -noname GetClipboardFormatNameWrapW(long ptr long) user32.GetClipboardFormatNameW
73  stdcall -noname GetCurrentDirectoryWrapW(long ptr) kernel32.GetCurrentDirectoryW
74  stdcall -noname GetDlgItemTextWrapW(long long wstr long) user32.GetDlgItemTextW
75  stdcall -noname GetFileAttributesWrapW(wstr) kernel32.GetFileAttributesW
76  stdcall -noname GetFullPathNameWrapW(wstr long ptr ptr) kernel32.GetFullPathNameW
77  stdcall -noname GetLocaleInfoWrapW(long long ptr long) kernel32.GetLocaleInfoW
78  stdcall -noname GetMenuStringWrapW(long long ptr long long) user32.GetMenuStringW
79  stdcall -noname GetMessageWrapW(ptr long long long) user32.GetMessageW
80  stdcall -noname GetModuleFileNameWrapW(long ptr long) kernel32.GetModuleFileNameW
81  stdcall -noname GetSystemDirectoryWrapW(ptr long) kernel32.GetSystemDirectoryW
82  stdcall -noname SearchPathWrapW(wstr wstr wstr long ptr ptr) kernel32.SearchPathW
83  stdcall -noname GetModuleHandleWrapW(wstr) kernel32.GetModuleHandleW
84  stdcall -noname GetObjectWrapW(long long ptr) gdi32.GetObjectW
85  stdcall -noname GetPrivateProfileIntWrapW(wstr wstr long wstr) kernel32.GetPrivateProfileIntW
86  stdcall -noname GetProfileStringWrapW(wstr wstr wstr ptr long) kernel32.GetProfileStringW
87  stdcall -noname GetPropWrapW(long wstr) user32.GetPropW
88  stdcall -noname GetStringTypeExWrapW(long long wstr long ptr) kernel32.GetStringTypeExW
89  stdcall -noname GetTempFileNameWrapW(wstr wstr long ptr) kernel32.GetTempFileNameW
90  stdcall -noname GetTempPathWrapW(long ptr) kernel32.GetTempPathW
91  stdcall -noname GetTextExtentPoint32WrapW(long wstr long ptr) gdi32.GetTextExtentPoint32W
92  stdcall -noname GetTextFaceWrapW(long long ptr) gdi32.GetTextFaceW
93  stdcall -noname GetTextMetricsWrapW(long ptr) gdi32.GetTextMetricsW
94  stdcall -noname GetWindowLongWrapW(long long) user32.GetWindowLongW
95  stdcall -noname GetWindowTextWrapW(long ptr long) user32.GetWindowTextW
96  stdcall -noname GetWindowTextLengthWrapW(long) user32.GetWindowTextLengthW
97  stdcall -noname GetWindowsDirectoryWrapW(ptr long) kernel32.GetWindowsDirectoryW
98  stdcall -noname InsertMenuWrapW(long long long long ptr) user32.InsertMenuW
99  stdcall -noname IsDialogMessageWrapW(long ptr) user32.IsDialogMessageW
100 stdcall -noname LoadAcceleratorsWrapW(long wstr) user32.LoadAcceleratorsW
101 stdcall -noname LoadBitmapWrapW(long wstr) user32.LoadBitmapW
102 stdcall -noname LoadCursorWrapW(long wstr) user32.LoadCursorW
103 stdcall -noname LoadIconWrapW(long wstr) user32.LoadIconW
104 stdcall -noname LoadImageWrapW(long wstr long long long long) user32.LoadImageW
105 stdcall -noname LoadLibraryExWrapW(wstr long long) kernel32.LoadLibraryExW
106 stdcall -noname LoadMenuWrapW(long wstr) user32.LoadMenuW
107 stdcall -noname LoadStringWrapW(long long ptr long) user32.LoadStringW
108 stdcall -noname MessageBoxIndirectWrapW(ptr) user32.MessageBoxIndirectW
109 stdcall -noname ModifyMenuWrapW(long long long long ptr) user32.ModifyMenuW
110 stdcall -noname GetCharWidth32WrapW(long long long long) gdi32.GetCharWidth32W
111 stdcall -noname GetCharacterPlacementWrapW(long wstr long long ptr long) gdi32.GetCharacterPlacementW
112 stdcall -noname CopyFileWrapW(wstr wstr long) kernel32.CopyFileW
113 stdcall -noname MoveFileWrapW(wstr wstr) kernel32.MoveFileW
114 stdcall -noname OemToCharWrapW(ptr ptr) user32.OemToCharW
115 stdcall -noname OutputDebugStringWrapW(wstr) kernel32.OutputDebugStringW
116 stdcall -noname PeekMessageWrapW(ptr long long long long) user32.PeekMessageW
117 stdcall -noname PostMessageWrapW(long long long long) user32.PostMessageW
118 stdcall -noname PostThreadMessageWrapW(long long long long) user32.PostThreadMessageW
119 stdcall -noname RegCreateKeyWrapW(long wstr ptr) advapi32.RegCreateKeyW
120 stdcall -noname RegCreateKeyExWrapW(long wstr long ptr long long ptr ptr ptr) advapi32.RegCreateKeyExW
121 stdcall -noname RegDeleteKeyWrapW(long wstr) advapi32.RegDeleteKeyW
122 stdcall -noname RegEnumKeyWrapW(long long ptr long) advapi32.RegEnumKeyW
123 stdcall -noname RegEnumKeyExWrapW(long long ptr ptr ptr ptr ptr ptr) advapi32.RegEnumKeyExW
124 stdcall -noname RegOpenKeyWrapW(long wstr ptr) advapi32.RegOpenKeyW
125 stdcall -noname RegOpenKeyExWrapW(long wstr long long ptr) advapi32.RegOpenKeyExW
126 stdcall -noname RegQueryInfoKeyWrapW(long ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) advapi32.RegQueryInfoKeyW
127 stdcall -noname RegQueryValueWrapW(long wstr ptr ptr) advapi32.RegQueryValueW
128 stdcall -noname RegQueryValueExWrapW(long wstr ptr ptr ptr ptr) advapi32.RegQueryValueExW
129 stdcall -noname RegSetValueWrapW(long wstr long ptr long) advapi32.RegSetValueW
130 stdcall -noname RegSetValueExWrapW(long wstr long long ptr long) advapi32.RegSetValueExW
131 stdcall -noname RegisterClassWrapW(ptr) user32.RegisterClassW
132 stdcall -noname RegisterClipboardFormatWrapW(wstr) user32.RegisterClipboardFormatW
133 stdcall -noname RegisterWindowMessageWrapW(wstr) user32.RegisterWindowMessageW
134 stdcall -noname RemovePropWrapW(long wstr) user32.RemovePropW
135 stdcall -noname SendDlgItemMessageWrapW(long long long long long) user32.SendDlgItemMessageW
136 stdcall -noname SendMessageWrapW(long long long long) user32.SendMessageW
137 stdcall -noname SetCurrentDirectoryWrapW(wstr) kernel32.SetCurrentDirectoryW
138 stdcall -noname SetDlgItemTextWrapW(long long wstr) user32.SetDlgItemTextW
139 stdcall -noname SetMenuItemInfoWrapW(long long long ptr) user32.SetMenuItemInfoW
140 stdcall -noname SetPropWrapW(long wstr long) user32.SetPropW
141 stdcall -noname SetWindowLongWrapW(long long long) user32.SetWindowLongW
142 stdcall -noname SetWindowsHookExWrapW(long long long long) user32.SetWindowsHookExW
143 stdcall -noname SetWindowTextWrapW(long wstr) user32.SetWindowTextW
144 stdcall -noname StartDocWrapW(long ptr) gdi32.StartDocW
145 stdcall -noname SystemParametersInfoWrapW(long long ptr long) user32.SystemParametersInfoW
146 stdcall -noname TranslateAcceleratorWrapW(long long ptr) user32.TranslateAcceleratorW
147 stdcall -noname UnregisterClassWrapW(wstr long) user32.UnregisterClassW
148 stdcall -noname VkKeyScanWrapW(long) user32.VkKeyScanW
149 stdcall -noname WinHelpWrapW(long wstr long long) user32.WinHelpW
150 stdcall -noname wvsprintfWrapW(ptr wstr ptr) user32.wvsprintfW
151 stdcall -ordinal StrCmpNCA(str ptr long)
152 stdcall -ordinal StrCmpNCW(wstr wstr long)
153 stdcall -ordinal StrCmpNICA(long long long)
154 stdcall -ordinal StrCmpNICW(wstr wstr long)
155 stdcall -ordinal StrCmpCA(str str)
156 stdcall -ordinal StrCmpCW(wstr wstr)
157 stdcall -ordinal StrCmpICA(str str)
158 stdcall -ordinal StrCmpICW(wstr wstr)
159 stdcall -noname CompareStringAltW(long long wstr long wstr long) kernel32.CompareStringW
160 stdcall -noname SHAboutInfoA(ptr long)
161 stdcall -noname SHAboutInfoW(ptr long)
162 stdcall -noname SHTruncateString(str long)
163 stdcall -noname IUnknown_QueryStatus(ptr ptr long ptr ptr)
164 stdcall -noname IUnknown_Exec(ptr ptr long long ptr ptr)
165 stdcall -noname SHSetWindowBits(long long long long)
166 stdcall -noname SHIsEmptyStream(ptr)
167 stdcall -noname SHSetParentHwnd(long ptr)
168 stdcall -ordinal ConnectToConnectionPoint(ptr ptr long ptr ptr ptr)
169 stdcall -ordinal IUnknown_AtomicRelease(long)
170 stdcall -noname PathSkipLeadingSlashesA(str)
171 stdcall -noname SHIsSameObject(ptr ptr)
172 stdcall -ordinal IUnknown_GetWindow(ptr ptr)
173 stdcall -noname IUnknown_SetOwner(ptr ptr)
174 stdcall -ordinal IUnknown_SetSite(ptr ptr)
175 stdcall -noname IUnknown_GetClassID(ptr ptr)
176 stdcall -ordinal IUnknown_QueryService(ptr ptr ptr ptr)
177 stdcall -noname SHLoadMenuPopup(ptr wstr)
178 stdcall -noname SHPropagateMessage(ptr long long long long)
179 stdcall -noname SHMenuIndexFromID(long long)
180 stdcall -noname SHRemoveAllSubMenus(long)
181 stdcall -noname SHEnableMenuItem(long long long)
182 stdcall -noname SHCheckMenuItem(long long long)
183 stdcall -noname SHRegisterClassA(ptr)
184 stdcall -ordinal IStream_Read(ptr ptr long) SHIStream_Read
185 stdcall -ordinal SHMessageBoxCheckA(ptr str str long long str)
186 stdcall -noname SHSimulateDrop(ptr ptr long ptr ptr)
187 stdcall -noname SHLoadFromPropertyBag(ptr ptr)
188 stdcall -noname IUnknown_TranslateAcceleratorOCS(ptr ptr long)
189 stdcall -noname IUnknown_OnFocusOCS(ptr ptr)
190 stdcall -noname IUnknown_HandleIRestrict(ptr ptr ptr ptr ptr)
191 stdcall -ordinal SHMessageBoxCheckW(ptr wstr wstr long long wstr)
192 stdcall -noname SHGetMenuFromID(ptr long)
193 stdcall -noname SHGetCurColorRes()
194 stdcall -noname SHWaitForSendMessageThread(ptr long)
195 stdcall -noname SHIsExpandableFolder(ptr ptr)
196 stdcall -noname SHVerbExistsNA(str ptr ptr long)
197 stdcall -noname SHFillRectClr(long ptr long)
198 stdcall -noname SHSearchMapInt(ptr ptr long long)
199 stdcall -ordinal IUnknown_Set(ptr ptr)
200 stdcall -noname MayQSForward(ptr ptr ptr long ptr ptr)
201 stdcall -noname MayExecForward(ptr long ptr long long ptr ptr)
202 stdcall -noname IsQSForward(ptr long ptr)
203 stdcall -ordinal SHStripMneumonicA(str)
204 stdcall -ordinal SHIsChildOrSelf(long long)
205 stdcall -noname SHGetValueGoodBootA(long str str ptr ptr ptr)
206 stdcall -noname SHGetValueGoodBootW(long wstr wstr ptr ptr ptr)
207 stub -noname IContextMenu_Invoke
208 stdcall -noname FDSA_Initialize(long long ptr ptr long)
209 stdcall -noname FDSA_Destroy(ptr)
210 stdcall -noname FDSA_InsertItem(ptr long ptr)
211 stdcall -noname FDSA_DeleteItem(ptr long)
212 stdcall -ordinal IStream_Write(ptr ptr long) SHIStream_Write
213 stdcall -ordinal IStream_Reset(ptr)
214 stdcall -ordinal IStream_Size(ptr ptr)
215 stdcall -ordinal SHAnsiToUnicode(str ptr long)
216 stdcall -noname SHAnsiToUnicodeCP(long str ptr long)
217 stdcall -ordinal SHUnicodeToAnsi(wstr ptr ptr)
218 stdcall -noname SHUnicodeToAnsiCP(long wstr ptr long)
219 stdcall QISearch(long long long long)
220 stdcall -noname SHSetDefaultDialogFont(ptr long)
221 stdcall -noname SHRemoveDefaultDialogFont(ptr)
222 stdcall -noname SHGlobalCounterCreate(long)
223 stdcall -noname SHGlobalCounterGetValue(long)
224 stdcall -noname SHGlobalCounterIncrement(long)
225 stdcall -ordinal SHStripMneumonicW(wstr)
226 stub -noname ZoneCheckPathA
227 stub -noname ZoneCheckPathW
228 stub -noname ZoneCheckUrlA
229 stub -noname ZoneCheckUrlW
230 stub -noname ZoneCheckUrlExA
231 stdcall -noname ZoneCheckUrlExW(wstr ptr long long long long long long)
232 stub -noname ZoneCheckUrlExCacheA
233 stub -noname ZoneCheckUrlExCacheW
234 stub -noname ZoneCheckHost
235 stub -noname ZoneCheckHostEx
236 stdcall -noname SHPinDllOfCLSID(ptr)
237 stdcall -noname SHRegisterClassW(ptr)
238 stdcall -noname SHUnregisterClassesA(ptr ptr long)
239 stdcall -noname SHUnregisterClassesW(ptr ptr long)
240 stdcall -noname SHDefWindowProc(long long long long)
241 stdcall -noname StopWatchMode()
242 stdcall -noname StopWatchFlush()
243 stdcall -noname StopWatchA(long str long long long)
244 stdcall -noname StopWatchW(long wstr long long long)
245 stdcall -noname StopWatch_TimerHandler(ptr ptr long ptr)
246 stub -noname StopWatch_CheckMsg
247 stdcall -noname StopWatch_MarkFrameStart(str)
248 stub -noname StopWatch_MarkSameFrameStart
249 stdcall -noname StopWatch_MarkJavaStop(wstr ptr long)
250 stdcall -noname GetPerfTime()
251 stub -noname StopWatch_DispatchTime
252 stdcall -noname StopWatch_SetMsgLastLocation(long)
253 stub -noname StopWatchExA
254 stub -noname StopWatchExW
255 stub -noname EventTraceHandler
256 stdcall -ordinal IUnknown_GetSite(ptr ptr ptr)
257 stdcall -noname SHCreateWorkerWindowA(long ptr long long ptr long)
258 stub -noname SHRegisterWaitForSingleObject
259 stub -noname SHUnregisterWait
260 stdcall -noname SHQueueUserWorkItem(long long long long long long long)
261 stub -noname SHCreateTimerQueue
262 stub -noname SHDeleteTimerQueue
263 stdcall -noname SHSetTimerQueueTimer(long ptr ptr long long str long)
264 stub -noname SHChangeTimerQueueTimer
265 stub -noname SHCancelTimerQueueTimer
266 stdcall -noname SHRestrictionLookup(long wstr ptr ptr)
267 stdcall -noname SHWeakQueryInterface(long long long long)
268 stdcall -noname SHWeakReleaseInterface(long long)
269 stdcall -noname GUIDFromStringA(str ptr)
270 stdcall -noname GUIDFromStringW(wstr ptr)
271 stdcall -noname SHGetRestriction(wstr wstr wstr)
272 stub -noname SHSetThreadPoolLimits
273 stub -noname SHTerminateThreadPool
274 stub -noname RegisterGlobalHotkeyW
275 stub -noname RegisterGlobalHotkeyA
276 stdcall -ordinal WhichPlatform()
277 stub -noname SHDialogBox
278 stdcall -noname SHCreateWorkerWindowW(long long long long long long)
279 stdcall -noname SHInvokeDefaultCommand(ptr ptr ptr)
280 stdcall -ordinal SHRegGetIntW(ptr wstr long)
281 stdcall -noname SHPackDispParamsV(ptr ptr long ptr)
282 varargs -noname SHPackDispParams(ptr ptr long)
283 stdcall -noname IConnectionPoint_InvokeWithCancel(ptr long long long long)
284 stdcall -noname IConnectionPoint_SimpleInvoke(ptr long ptr)
285 stdcall -noname IConnectionPoint_OnChanged(ptr long)
286 varargs -noname IUnknown_CPContainerInvokeParam(ptr ptr long ptr long)
287 stdcall -noname IUnknown_CPContainerOnChanged(ptr long)
288 stub -noname IUnknown_CPContainerInvokeIndirect
289 stdcall -noname PlaySoundWrapW(wstr long long)
290 stub -noname SHMirrorIcon
291 stdcall -noname SHMessageBoxCheckExA(ptr ptr ptr ptr ptr long str)
292 stdcall -noname SHMessageBoxCheckExW(ptr ptr ptr ptr ptr long wstr)
293 stub -noname SHCancelUserWorkItems
294 stdcall -noname SHGetIniStringW(wstr wstr ptr long wstr)
295 stdcall -noname SHSetIniStringW(wstr ptr wstr wstr)
296 stub -noname CreateURLFileContentsW
297 stub -noname CreateURLFileContentsA
298 stdcall -noname WritePrivateProfileStringWrapW(wstr wstr wstr wstr) kernel32.WritePrivateProfileStringW
299 stdcall -noname ExtTextOutWrapW(long long long long ptr wstr long ptr) gdi32.ExtTextOutW
300 stdcall -noname CreateFontWrapW(long long long long long long long long long long long long long wstr) gdi32.CreateFontW
301 stdcall -noname DrawTextExWrapW(long wstr long ptr long ptr) user32.DrawTextExW
302 stdcall -noname GetMenuItemInfoWrapW(long long long ptr) user32.GetMenuItemInfoW
303 stdcall -noname InsertMenuItemWrapW(long long long ptr) user32.InsertMenuItemW
304 stdcall -noname CreateMetaFileWrapW(wstr) gdi32.CreateMetaFileW
305 stdcall -noname CreateMutexWrapW(ptr long wstr) kernel32.CreateMutexW
306 stdcall -noname ExpandEnvironmentStringsWrapW(wstr ptr long) kernel32.ExpandEnvironmentStringsW
307 stdcall -noname CreateSemaphoreWrapW(ptr long long wstr) kernel32.CreateSemaphoreW
308 stdcall -noname IsBadStringPtrWrapW(ptr long) kernel32.IsBadStringPtrW
309 stdcall -noname LoadLibraryWrapW(wstr) kernel32.LoadLibraryW
310 stdcall -noname GetTimeFormatWrapW(long long ptr wstr ptr long) kernel32.GetTimeFormatW
311 stdcall -noname GetDateFormatWrapW(long long ptr wstr ptr long) kernel32.GetDateFormatW
312 stdcall -noname GetPrivateProfileStringWrapW(wstr wstr wstr ptr long wstr) kernel32.GetPrivateProfileStringW
313 stdcall -noname SHGetFileInfoWrapW(ptr long ptr long long)
314 stdcall -noname RegisterClassExWrapW(ptr) user32.RegisterClassExW
315 stdcall -noname GetClassInfoExWrapW(long wstr ptr) user32.GetClassInfoExW
316 stub -noname IShellFolder_GetDisplayNameOf
317 stub -noname IShellFolder_ParseDisplayName
318 stdcall -noname DragQueryFileWrapW(long long wstr long)
319 stdcall -noname FindWindowExWrapW(long long wstr wstr) user32.FindWindowExW
320 stdcall -noname RegisterMIMETypeForExtensionA(str str)
321 stdcall -noname RegisterMIMETypeForExtensionW(wstr wstr)
322 stdcall -noname UnregisterMIMETypeForExtensionA(str)
323 stdcall -noname UnregisterMIMETypeForExtensionW(wstr)
324 stdcall -noname RegisterExtensionForMIMETypeA(str str)
325 stdcall -noname RegisterExtensionForMIMETypeW(wstr wstr)
326 stdcall -noname UnregisterExtensionForMIMETypeA(str)
327 stdcall -noname UnregisterExtensionForMIMETypeW(wstr)
328 stdcall -noname GetMIMETypeSubKeyA(str ptr long)
329 stdcall -noname GetMIMETypeSubKeyW(wstr ptr long)
330 stdcall -noname MIME_GetExtensionA(str ptr long)
331 stdcall -noname MIME_GetExtensionW(wstr ptr long)
332 stdcall -noname CallMsgFilterWrapW(ptr long) user32.CallMsgFilterW
333 stdcall -noname SHBrowseForFolderWrapW(ptr)
334 stdcall -noname SHGetPathFromIDListWrapW(ptr ptr)
335 stdcall -noname ShellExecuteExWrapW(ptr)
336 stdcall -noname SHFileOperationWrapW(ptr)
337 stdcall -noname ExtractIconExWrapW(wstr long ptr ptr long) user32.PrivateExtractIconExW
338 stdcall -noname SetFileAttributesWrapW(wstr long) kernel32.SetFileAttributesW
339 stdcall -noname GetNumberFormatWrapW(long long wstr ptr ptr long) kernel32.GetNumberFormatW
340 stdcall -noname MessageBoxWrapW(long wstr wstr long) user32.MessageBoxW
341 stdcall -noname FindNextFileWrapW(long ptr) kernel32.FindNextFileW
342 stdcall -noname SHInterlockedCompareExchange(ptr ptr ptr)
343 stdcall -noname SHRegGetCLSIDKeyA(ptr str long long ptr)
344 stdcall -noname SHRegGetCLSIDKeyW(ptr wstr long long ptr)
345 stdcall -ordinal SHAnsiToAnsi(str ptr long)
346 stdcall -ordinal SHUnicodeToUnicode(wstr ptr long)
347 stdcall -noname RegDeleteValueWrapW(long wstr) advapi32.RegDeleteValueW
348 stub -noname SHGetFileDescriptionW
349 stub -noname SHGetFileDescriptionA
350 stdcall -noname GetFileVersionInfoSizeWrapW(wstr ptr)
351 stdcall -noname GetFileVersionInfoWrapW(wstr long long ptr)
352 stdcall -noname VerQueryValueWrapW(ptr wstr ptr ptr)
353 stdcall -ordinal SHFormatDateTimeA(ptr ptr str long)
354 stdcall -ordinal SHFormatDateTimeW(ptr ptr wstr long)
355 stdcall -noname IUnknown_EnableModeless(ptr long)
356 stdcall -noname CreateAllAccessSecurityAttributes(ptr ptr long)
357 stdcall -noname SHGetNewLinkInfoWrapW(wstr wstr wstr long long)
358 stdcall -noname SHDefExtractIconWrapW(wstr long long ptr ptr long)
359 stdcall -noname OpenEventWrapW(long long wstr) kernel32.OpenEventW
360 stdcall -noname RemoveDirectoryWrapW(wstr) kernel32.RemoveDirectoryW
361 stdcall -noname GetShortPathNameWrapW(wstr ptr long) kernel32.GetShortPathNameW
362 stdcall -noname GetUserNameWrapW(ptr ptr) advapi32.GetUserNameW
363 stdcall -noname SHInvokeCommand(ptr ptr ptr long)
364 stdcall -noname DoesStringRoundTripA(str ptr long)
365 stdcall -noname DoesStringRoundTripW(wstr ptr long)
366 stdcall -noname RegEnumValueWrapW(long long ptr ptr ptr ptr ptr ptr) advapi32.RegEnumValueW
367 stdcall -noname WritePrivateProfileStructWrapW(wstr wstr ptr long wstr) kernel32.WritePrivateProfileStructW
368 stdcall -noname GetPrivateProfileStructWrapW(wstr wstr ptr long wstr) kernel32.GetPrivateProfileStructW
369 stdcall -noname CreateProcessWrapW(wstr wstr ptr ptr long long ptr wstr ptr ptr) kernel32.CreateProcessW
370 stdcall -noname ExtractIconWrapW(long wstr long)
371 stdcall -noname DdeInitializeWrapW(ptr ptr long long) user32.DdeInitializeW
372 stdcall -noname DdeCreateStringHandleWrapW(long ptr long) user32.DdeCreateStringHandleW
373 stdcall -noname DdeQueryStringWrapW(long ptr wstr long long) user32.DdeQueryStringW
374 stub -noname SHCheckDiskForMediaA
375 stub -noname SHCheckDiskForMediaW
376 stdcall -noname MLGetUILanguage() kernel32.GetUserDefaultUILanguage
377 stdcall -ordinal MLLoadLibraryA(str long long)
378 stdcall -ordinal MLLoadLibraryW(wstr long long)
379 stub -noname Shell_GetCachedImageIndexWrapW
380 stub -noname Shell_GetCachedImageIndexWrapA
381 stub -noname AssocCopyVerbs
382 stdcall -noname ZoneComputePaneSize(ptr)
383 stub -noname ZoneConfigureW
384 stub -noname SHRestrictedMessageBox
385 stub -noname SHLoadRawAccelerators
386 stub -noname SHQueryRawAccelerator
387 stub -noname SHQueryRawAcceleratorMsg
388 varargs -ordinal ShellMessageBoxWrapW(long long wstr wstr long)
389 stdcall -noname GetSaveFileNameWrapW(ptr)
390 stdcall -noname WNetRestoreConnectionWrapW(long wstr)
391 stdcall -noname WNetGetLastErrorWrapW(ptr ptr long ptr long)
392 stdcall -noname EndDialogWrap(ptr ptr) user32.EndDialog
393 stdcall -noname CreateDialogIndirectParamWrapW(long ptr long ptr long) user32.CreateDialogIndirectParamW
394 stdcall -noname SHChangeNotifyWrap(long long ptr ptr)
395 stub -noname MLWinHelpA
396 stub -noname MLHtmlHelpA
397 stub -noname MLWinHelpW
398 stub -noname MLHtmlHelpW
399 stdcall -noname StrCpyNXA(ptr str long)
400 stdcall -noname StrCpyNXW(ptr wstr long)
401 stdcall -noname PageSetupDlgWrapW(ptr)
402 stdcall -noname PrintDlgWrapW(ptr)
403 stdcall -noname GetOpenFileNameWrapW(ptr)
404 stdcall -noname IShellFolder_EnumObjects(ptr ptr long ptr) SHIShellFolder_EnumObjects
405 stdcall -noname MLBuildResURLA(str ptr long str ptr long)
406 stdcall -noname MLBuildResURLW(wstr ptr long wstr ptr long)
407 stub -noname AssocMakeProgid
408 stub -noname AssocMakeShell
409 stub -noname AssocMakeApplicationByKeyW
410 stub -noname AssocMakeApplicationByKeyA
411 stub -noname AssocMakeFileExtsToApplicationW
412 stub -noname AssocMakeFileExtsToApplicationA
413 stdcall -noname SHGetMachineInfo(long)
414 stub -noname SHHtmlHelpOnDemandW
415 stub -noname SHHtmlHelpOnDemandA
416 stdcall -noname SHWinHelpOnDemandW(long wstr long ptr long)
417 stdcall -noname SHWinHelpOnDemandA(long str long ptr long)
418 stdcall -ordinal MLFreeLibrary(long)
419 stdcall -noname SHFlushSFCacheWrap()
420 stub @ # CMemStream::Commit, don't forward
421 stub -noname SHLoadPersistedDataObject
422 stdcall -noname SHGlobalCounterCreateNamedA(str long)
423 stdcall -noname SHGlobalCounterCreateNamedW(wstr long)
424 stdcall -noname SHGlobalCounterDecrement(long)
425 stdcall -noname DeleteMenuWrap(ptr long long) user32.DeleteMenu
426 stdcall -noname DestroyMenuWrap(long) user32.DestroyMenu
427 stdcall -noname TrackPopupMenuWrap(long long long long long long ptr) user32.TrackPopupMenu
428 stdcall -noname TrackPopupMenuExWrap(long long long long long ptr) user32.TrackPopupMenuEx
429 stdcall -noname MLIsMLHInstance(long)
430 stdcall -noname MLSetMLHInstance(long long)
431 stdcall -noname MLClearMLHInstance(long)
432 stdcall -ordinal SHSendMessageBroadcastA(long long long)
433 stdcall -ordinal SHSendMessageBroadcastW(long long long)
434 stdcall -noname SendMessageTimeoutWrapW(long long long long long long ptr) user32.SendMessageTimeoutW
435 stdcall -noname CLSIDFromProgIDWrap(wstr ptr) ole32.CLSIDFromProgID
436 stdcall -noname CLSIDFromStringWrap(wstr ptr)
437 stdcall -ordinal IsOS(long)
438 stub -noname SHLoadRegUIStringA
439 stdcall -noname SHLoadRegUIStringW(ptr wstr ptr long)
440 stdcall -noname SHGetWebFolderFilePathA(str ptr long)
441 stdcall -noname SHGetWebFolderFilePathW(wstr ptr long)
442 stdcall -noname GetEnvironmentVariableWrapW(wstr ptr long) kernel32.GetEnvironmentVariableW
443 stdcall -noname SHGetSystemWindowsDirectoryA(ptr long) kernel32.GetSystemWindowsDirectoryA
444 stdcall -noname SHGetSystemWindowsDirectoryW(ptr long) kernel32.GetSystemWindowsDirectoryW
445 stdcall -noname PathFileExistsAndAttributesA(str ptr)
446 stdcall -noname PathFileExistsAndAttributesW(wstr ptr)
447 stub -noname FixSlashesAndColonA
448 stdcall -noname FixSlashesAndColonW(wstr)
449 stub -noname NextPathA
450 stub -noname NextPathW
451 stub -noname CharUpperNoDBCSA
452 stub -noname CharUpperNoDBCSW
453 stub -noname CharLowerNoDBCSA
454 stub -noname CharLowerNoDBCSW
455 stdcall -noname PathIsValidCharA(long long)
456 stdcall -noname PathIsValidCharW(long long)
457 stdcall -noname GetLongPathNameWrapW(wstr ptr long) kernel32.GetLongPathNameW
458 stdcall -noname GetLongPathNameWrapA(str ptr long) kernel32.GetLongPathNameA
459 stdcall -noname SHExpandEnvironmentStringsA(str ptr long) kernel32.ExpandEnvironmentStringsA
460 stdcall -noname SHExpandEnvironmentStringsW(wstr ptr long) kernel32.ExpandEnvironmentStringsW
461 stdcall -noname SHGetAppCompatFlags(long)
462 stdcall -ordinal UrlFixupW(wstr wstr long)
463 stdcall -noname SHExpandEnvironmentStringsForUserA(ptr str ptr long) userenv.ExpandEnvironmentStringsForUserA
464 stdcall -noname SHExpandEnvironmentStringsForUserW(ptr wstr ptr long) userenv.ExpandEnvironmentStringsForUserW
465 stub -noname PathUnExpandEnvStringsForUserA
466 stub -noname PathUnExpandEnvStringsForUserW
467 stub -ordinal SHRunIndirectRegClientCommand
468 stub -noname RunIndirectRegCommand
469 stub -noname RunRegCommand
470 stub -noname IUnknown_ProfferServiceOld
471 stdcall -noname SHCreatePropertyBagOnRegKey(long wstr long ptr ptr)
472 stub -noname SHCreatePropertyBagOnProfileSelection
473 stub -noname SHGetIniStringUTF7W
474 stub -noname SHSetIniStringUTF7W
475 stdcall -noname GetShellSecurityDescriptor(ptr long)
476 stdcall -noname SHGetObjectCompatFlags(ptr ptr)
477 stub -noname SHCreatePropertyBagOnMemory
478 stdcall -noname IUnknown_TranslateAcceleratorIO(ptr ptr)
479 stdcall -noname IUnknown_UIActivateIO(ptr long ptr)
480 stdcall -noname UrlCrackW(wstr long long ptr) wininet.InternetCrackUrlW
481 stdcall -noname IUnknown_HasFocusIO(ptr)
482 stub -noname SHMessageBoxHelpA
483 stub -noname SHMessageBoxHelpW
484 stdcall -noname IUnknown_QueryServiceExec(ptr ptr ptr long long long ptr)
485 stub -noname MapWin32ErrorToSTG
486 stub -noname ModeToCreateFileFlags
487 stdcall -ordinal SHLoadIndirectString(wstr ptr long ptr)
488 stub -noname SHConvertGraphicsFile
489 stdcall -noname GlobalAddAtomWrapW(wstr) kernel32.GlobalAddAtomW
490 stdcall -noname GlobalFindAtomWrapW(wstr) kernel32.GlobalFindAtomW
491 stdcall -noname SHGetShellKey(long long long)
492 stub -noname PrettifyFileDescriptionW
493 stub -noname SHPropertyBag_ReadType
494 stub -noname SHPropertyBag_ReadStr
495 stub -noname SHPropertyBag_WriteStr
496 stdcall -noname SHPropertyBag_ReadLONG(ptr wstr ptr)
497 stub -noname SHPropertyBag_WriteLONG
498 stub -noname SHPropertyBag_ReadBOOLOld
499 stub -noname SHPropertyBag_WriteBOOL

505 stub -noname SHPropertyBag_ReadGUID
506 stub -noname SHPropertyBag_WriteGUID
507 stub -noname SHPropertyBag_ReadDWORD
508 stub -noname SHPropertyBag_WriteDWORD
509 stdcall -noname IUnknown_OnFocusChangeIS(ptr ptr long)
510 stub -noname SHLockSharedEx
511 stub -noname PathFileExistsDefExtAndAttributesW
512 stub -ordinal IStream_ReadPidl
513 stub -ordinal IStream_WritePidl
514 stdcall -noname IUnknown_ProfferService(ptr ptr ptr ptr)
515 stdcall -ordinal SHGetViewStatePropertyBag(ptr wstr long ptr ptr)
516 stdcall -noname SKGetValueW(long wstr wstr ptr ptr ptr)
517 stdcall -noname SKSetValueW(long wstr wstr long ptr long)
518 stdcall -noname SKDeleteValueW(long wstr wstr)
519 stdcall -noname SKAllocValueW(long wstr wstr ptr ptr ptr)
520 stub -noname SHPropertyBag_ReadBSTR
521 stub -noname SHPropertyBag_ReadPOINTL
522 stub -noname SHPropertyBag_WritePOINTL
523 stub -noname SHPropertyBag_ReadRECTL
524 stub -noname SHPropertyBag_WriteRECTL
525 stub -noname SHPropertyBag_ReadPOINTS
526 stub -noname SHPropertyBag_WritePOINTS
527 stub -noname SHPropertyBag_ReadSHORT
528 stub -noname SHPropertyBag_WriteSHORT
529 stub -noname SHPropertyBag_ReadInt
530 stub -noname SHPropertyBag_WriteInt
531 stub -noname SHPropertyBag_ReadStream
532 stub -noname SHPropertyBag_WriteStream
533 stub -noname SHGetPerScreenResName
534 stub -noname SHPropertyBag_ReadBOOL
535 stub -noname SHPropertyBag_Delete
536 stub -noname IUnknown_QueryServicePropertyBag
537 stub -noname SHBoolSystemParametersInfo
538 stdcall -noname IUnknown_QueryServiceForWebBrowserApp(ptr ptr ptr)
539 stub -noname IUnknown_ShowBrowserBar
540 stub -noname SHInvokeCommandOnContextMenu
541 stub -noname SHInvokeCommandsOnContextMen
542 stdcall -noname GetUIVersion()
543 stdcall -noname CreateColorSpaceWrapW(ptr) gdi32.CreateColorSpaceW
544 stub -noname QuerySourceCreateFromKey
545 stub -noname SHForwardContextMenuMsg
546 stub -noname IUnknown_DoContextMenuPopup

548 stub -noname SHAreIconsEqual
549 stdcall -noname SHCoCreateInstanceAC(ptr ptr long ptr ptr)
550 stub -noname GetTemplateInfoFromHandle
551 stub -noname IShellFolder_CompareIDs

@ stdcall AssocCreate(int128 ptr ptr)
@ stdcall AssocGetPerceivedType(wstr ptr ptr ptr)
@ stdcall AssocIsDangerous(wstr)
@ stdcall AssocQueryKeyA(long long str str ptr)
@ stdcall AssocQueryKeyW(long long wstr wstr ptr)
@ stdcall AssocQueryStringA(long long str str ptr ptr)
@ stdcall AssocQueryStringByKeyA(long long ptr str ptr ptr)
@ stdcall AssocQueryStringByKeyW(long long ptr wstr ptr ptr)
@ stdcall AssocQueryStringW(long long wstr wstr ptr ptr)
@ stdcall ChrCmpIA(long long)
@ stdcall ChrCmpIW(long long)
@ stdcall ColorAdjustLuma(long long long)
@ stdcall ColorHLSToRGB(long long long)
@ stdcall ColorRGBToHLS(long ptr ptr ptr)
@ stdcall DelayLoadFailureHook(str str) kernel32.DelayLoadFailureHook
@ stdcall -private DllGetVersion(ptr)
@ stdcall GetMenuPosFromID(ptr long)
@ stdcall HashData (ptr long ptr long)
@ stdcall IntlStrEqWorkerA(long str str long) StrIsIntlEqualA
@ stdcall IntlStrEqWorkerW(long wstr wstr long) StrIsIntlEqualW
@ stdcall IsCharSpaceA(long)
@ stdcall IsInternetESCEnabled()
@ stdcall PathAddBackslashA (str)
@ stdcall PathAddBackslashW (wstr)
@ stdcall PathAddExtensionA (str str)
@ stdcall PathAddExtensionW (wstr wstr)
@ stdcall PathAppendA (str str)
@ stdcall PathAppendW (wstr wstr)
@ stdcall PathBuildRootA (ptr long)
@ stdcall PathBuildRootW (ptr long)
@ stdcall PathCanonicalizeA (ptr str)
@ stdcall PathCanonicalizeW (ptr wstr)
@ stdcall PathCombineA (ptr str str)
@ stdcall PathCombineW (ptr wstr wstr)
@ stdcall PathCommonPrefixA(str str ptr)
@ stdcall PathCommonPrefixW(wstr wstr ptr)
@ stdcall PathCompactPathA(long str long)
@ stdcall PathCompactPathExA(ptr str long long)
@ stdcall PathCompactPathExW(ptr wstr long long)
@ stdcall PathCompactPathW(long wstr long)
@ stdcall PathCreateFromUrlA(str ptr ptr long)
@ stdcall PathCreateFromUrlW(wstr ptr ptr long)
@ stdcall PathCreateFromUrlAlloc(wstr ptr long)
@ stdcall PathFileExistsA (str)
@ stdcall PathFileExistsW (wstr)
@ stdcall PathFindExtensionA (str)
@ stdcall PathFindExtensionW (wstr)
@ stdcall PathFindFileNameA (str)
@ stdcall PathFindFileNameW (wstr)
@ stdcall PathFindNextComponentA (str)
@ stdcall PathFindNextComponentW (wstr)
@ stdcall PathFindOnPathA (str ptr)
@ stdcall PathFindOnPathW (wstr ptr)
@ stdcall PathFindSuffixArrayA(str ptr long)
@ stdcall PathFindSuffixArrayW(wstr ptr long)
@ stdcall PathGetArgsA (str)
@ stdcall PathGetArgsW (wstr)
@ stdcall PathGetCharTypeA(long)
@ stdcall PathGetCharTypeW(long)
@ stdcall PathGetDriveNumberA (str)
@ stdcall PathGetDriveNumberW (wstr)
@ stdcall PathIsContentTypeA(str str)
@ stdcall PathIsContentTypeW(wstr wstr)
@ stdcall PathIsDirectoryA(str)
@ stdcall PathIsDirectoryEmptyA(str)
@ stdcall PathIsDirectoryEmptyW(wstr)
@ stdcall PathIsDirectoryW(wstr)
@ stdcall PathIsFileSpecA(str)
@ stdcall PathIsFileSpecW(wstr)
@ stdcall PathIsLFNFileSpecA(str)
@ stdcall PathIsLFNFileSpecW(wstr)
@ stdcall PathIsNetworkPathA(str)
@ stdcall PathIsNetworkPathW(wstr)
@ stdcall PathIsPrefixA(str str)
@ stdcall PathIsPrefixW(wstr wstr)
@ stdcall PathIsRelativeA (str)
@ stdcall PathIsRelativeW (wstr)
@ stdcall PathIsRootA(str)
@ stdcall PathIsRootW(wstr)
@ stdcall PathIsSameRootA(str str)
@ stdcall PathIsSameRootW(wstr wstr)
@ stdcall PathIsSystemFolderA(str long)
@ stdcall PathIsSystemFolderW(wstr long)
@ stdcall PathIsUNCA (str)
@ stdcall PathIsUNCServerA(str)
@ stdcall PathIsUNCServerShareA(str)
@ stdcall PathIsUNCServerShareW(wstr)
@ stdcall PathIsUNCServerW(wstr)
@ stdcall PathIsUNCW(wstr)
@ stdcall PathIsURLA(str)
@ stdcall PathIsURLW(wstr)
@ stdcall PathMakePrettyA(str)
@ stdcall PathMakePrettyW(wstr)
@ stdcall PathMakeSystemFolderA(str)
@ stdcall PathMakeSystemFolderW(wstr)
@ stdcall PathMatchSpecA  (str str)
@ stdcall PathMatchSpecW  (wstr wstr)
@ stdcall PathParseIconLocationA (str)
@ stdcall PathParseIconLocationW (wstr)
@ stdcall PathQuoteSpacesA (str)
@ stdcall PathQuoteSpacesW (wstr)
@ stdcall PathRelativePathToA(ptr str long str long)
@ stdcall PathRelativePathToW(ptr wstr long wstr long)
@ stdcall PathRemoveArgsA(str)
@ stdcall PathRemoveArgsW(wstr)
@ stdcall PathRemoveBackslashA (str)
@ stdcall PathRemoveBackslashW (wstr)
@ stdcall PathRemoveBlanksA(str)
@ stdcall PathRemoveBlanksW(wstr)
@ stdcall PathRemoveExtensionA(str)
@ stdcall PathRemoveExtensionW(wstr)
@ stdcall PathRemoveFileSpecA (str)
@ stdcall PathRemoveFileSpecW (wstr)
@ stdcall PathRenameExtensionA(str str)
@ stdcall PathRenameExtensionW(wstr wstr)
@ stdcall PathSearchAndQualifyA(str ptr long)
@ stdcall PathSearchAndQualifyW(wstr ptr long)
@ stdcall PathSetDlgItemPathA (long long ptr)
@ stdcall PathSetDlgItemPathW (long long ptr)
@ stdcall PathSkipRootA(str)
@ stdcall PathSkipRootW(wstr)
@ stdcall PathStripPathA(str)
@ stdcall PathStripPathW(wstr)
@ stdcall PathStripToRootA(str)
@ stdcall PathStripToRootW(wstr)
@ stdcall PathUnExpandEnvStringsA(str ptr long)
@ stdcall PathUnExpandEnvStringsW(wstr ptr long)
@ stdcall PathUndecorateA(str)
@ stdcall PathUndecorateW(wstr)
@ stdcall PathUnmakeSystemFolderA(str)
@ stdcall PathUnmakeSystemFolderW(wstr)
@ stdcall PathUnquoteSpacesA (str)
@ stdcall PathUnquoteSpacesW (wstr)
@ stdcall SHAutoComplete(ptr long)
@ stdcall SHCopyKeyA(long str long long)
@ stdcall SHCopyKeyW(long wstr long long)
@ stdcall SHCreateShellPalette(long)
@ stdcall SHCreateStreamOnFileA(str long ptr)
@ stdcall SHCreateStreamOnFileEx(wstr long long long ptr ptr)
@ stdcall SHCreateStreamOnFileW(wstr long ptr)
@ stdcall SHCreateStreamWrapper(ptr ptr long ptr)
@ stdcall SHCreateThreadRef(ptr ptr)
@ stdcall SHDeleteEmptyKeyA(long ptr)
@ stdcall SHDeleteEmptyKeyW(long ptr)
@ stdcall SHDeleteKeyA(long str)
@ stdcall SHDeleteKeyW(long wstr)
@ stdcall SHDeleteOrphanKeyA(long str)
@ stdcall SHDeleteOrphanKeyW(long wstr)
@ stdcall SHDeleteValueA(long  str  str)
@ stdcall SHDeleteValueW(long wstr wstr)
@ stdcall SHEnumKeyExA(long long str ptr)
@ stdcall SHEnumKeyExW(long long wstr ptr)
@ stdcall SHEnumValueA(long long str ptr ptr ptr ptr)
@ stdcall SHEnumValueW(long long wstr ptr ptr ptr ptr)
@ stdcall SHGetInverseCMAP ( ptr long )
@ stdcall SHGetThreadRef (ptr)
@ stdcall SHGetValueA ( long str str ptr ptr ptr )
@ stdcall SHGetValueW ( long wstr wstr ptr ptr ptr )
@ stdcall SHIsLowMemoryMachine(long)
@ stdcall SHOpenRegStream2A(long str str long)
@ stdcall SHOpenRegStream2W(long wstr wstr long)
@ stdcall SHOpenRegStreamA(long str str long)
@ stdcall SHOpenRegStreamW(long wstr wstr long)
@ stdcall SHQueryInfoKeyA(long ptr ptr ptr ptr)
@ stdcall SHQueryInfoKeyW(long ptr ptr ptr ptr)
@ stdcall SHQueryValueExA(long str ptr ptr ptr ptr)
@ stdcall SHQueryValueExW(long wstr ptr ptr ptr ptr)
@ stdcall SHRegCloseUSKey(ptr)
@ stdcall SHRegCreateUSKeyA(str long long ptr long)
@ stdcall SHRegCreateUSKeyW(wstr long long ptr long)
@ stdcall SHRegDeleteEmptyUSKeyA(long str long)
@ stdcall SHRegDeleteEmptyUSKeyW(long wstr long)
@ stdcall SHRegDeleteUSValueA(long str long)
@ stdcall SHRegDeleteUSValueW(long wstr long)
@ stdcall SHRegDuplicateHKey (long)
@ stdcall SHRegEnumUSKeyA(long long str ptr long)
@ stdcall SHRegEnumUSKeyW(long long wstr ptr long)
@ stdcall SHRegEnumUSValueA(long long ptr ptr ptr ptr ptr long)
@ stdcall SHRegEnumUSValueW(long long ptr ptr ptr ptr ptr long)
@ stdcall SHRegGetBoolUSValueA(str str long long)
@ stdcall SHRegGetBoolUSValueW(wstr wstr long long)
@ stdcall SHRegGetPathA(long str str ptr long)
@ stdcall SHRegGetPathW(long wstr wstr ptr long)
@ stdcall SHRegGetUSValueA ( str str ptr ptr ptr long ptr long )
@ stdcall SHRegGetUSValueW ( wstr wstr ptr ptr ptr long ptr long )
@ stdcall SHRegGetValueA ( long str str long ptr ptr ptr ) advapi32.RegGetValueA
@ stdcall SHRegGetValueW ( long wstr wstr long ptr ptr ptr ) advapi32.RegGetValueW
@ stdcall SHRegOpenUSKeyA ( str long long long long )
@ stdcall SHRegOpenUSKeyW ( wstr long long long long )
@ stdcall SHRegQueryInfoUSKeyA ( long ptr ptr ptr ptr long )
@ stdcall SHRegQueryInfoUSKeyW ( long ptr ptr ptr ptr long )
@ stdcall SHRegQueryUSValueA ( long str ptr ptr ptr long ptr long )
@ stdcall SHRegQueryUSValueW ( long wstr ptr ptr ptr long ptr long )
@ stdcall SHRegSetPathA(long str str str long)
@ stdcall SHRegSetPathW(long wstr wstr wstr long)
@ stdcall SHRegSetUSValueA ( str str long ptr long long)
@ stdcall SHRegSetUSValueW ( wstr wstr long ptr long long)
@ stdcall SHRegWriteUSValueA (long str long ptr long long)
@ stdcall SHRegWriteUSValueW (long wstr long ptr long long)
@ stdcall SHRegisterValidateTemplate(wstr long)
@ stdcall SHReleaseThreadRef()
@ stdcall SHSetThreadRef (ptr)
@ stdcall SHSetValueA (long  str  str long ptr long)
@ stdcall SHSetValueW (long wstr wstr long ptr long)
@ stdcall SHSkipJunction(ptr ptr)
@ stdcall SHStrDupA (str ptr)
@ stdcall SHStrDupW (wstr ptr)
@ stdcall StrCSpnA (str str)
@ stdcall StrCSpnIA (str str)
@ stdcall StrCSpnIW (wstr wstr)
@ stdcall StrCSpnW (wstr wstr)
@ stdcall StrCatBuffA (str str long)
@ stdcall StrCatBuffW (wstr wstr long)
@ stdcall StrCatChainW (ptr long long wstr)
@ stdcall StrCatW (ptr wstr)
@ stdcall StrChrA (str long)
@ stdcall StrChrIA (str long)
@ stdcall StrChrIW (wstr long)
@ stdcall StrChrNW(wstr long long)
@ stdcall StrChrW (wstr long)
@ stdcall StrCmpIW (wstr wstr)
@ stdcall StrCmpLogicalW(wstr wstr)
@ stdcall StrCmpNA (str str long)
@ stdcall StrCmpNIA (str str long)
@ stdcall StrCmpNIW (wstr wstr long)
@ stdcall StrCmpNW (wstr wstr long)
@ stdcall StrCmpW (wstr wstr)
@ stdcall StrCpyNW (ptr wstr long)
@ stdcall StrCpyW (ptr wstr)
@ stdcall StrDupA (str)
@ stdcall StrDupW (wstr)
@ stdcall StrFormatByteSize64A(int64 ptr long)
@ stdcall StrFormatByteSizeA(long ptr long)
@ stdcall StrFormatByteSizeW(int64 ptr long)
@ stdcall StrFormatKBSizeA(int64 str long)
@ stdcall StrFormatKBSizeW(int64 wstr long)
@ stdcall StrFromTimeIntervalA(ptr long long long)
@ stdcall StrFromTimeIntervalW(ptr long long long)
@ stdcall StrIsIntlEqualA(long str str long)
@ stdcall StrIsIntlEqualW(long wstr wstr long)
@ stdcall StrNCatA(str str long)
@ stdcall StrNCatW(wstr wstr long)
@ stdcall StrPBrkA(str str)
@ stdcall StrPBrkW(wstr wstr)
@ stdcall StrRChrA (str str long)
@ stdcall StrRChrIA (str str long)
@ stdcall StrRChrIW (wstr wstr long)
@ stdcall StrRChrW (wstr wstr long)
@ stdcall StrRStrIA (str str str)
@ stdcall StrRStrIW (wstr wstr wstr)
@ stdcall StrRetToBSTR(ptr ptr ptr)
@ stdcall StrRetToBufA(ptr ptr ptr long)
@ stdcall StrRetToBufW(ptr ptr ptr long)
@ stdcall StrRetToStrA(ptr ptr ptr)
@ stdcall StrRetToStrW(ptr ptr ptr)
@ stdcall StrSpnA (str str)
@ stdcall StrSpnW (wstr wstr)
@ stdcall StrStrA(str str)
@ stdcall StrStrIA(str str)
@ stdcall StrStrIW(wstr wstr)
@ stdcall StrStrNW(wstr wstr long)
@ stdcall StrStrNIW(wstr wstr long)
@ stdcall StrStrW(wstr wstr)
@ stdcall StrToInt64ExA(str long ptr)
@ stdcall StrToInt64ExW(wstr long ptr)
@ stdcall StrToIntA(str)
@ stdcall StrToIntExA(str long ptr)
@ stdcall StrToIntExW(wstr long ptr)
@ stdcall StrToIntW(wstr)
@ stdcall StrTrimA(str str)
@ stdcall StrTrimW(wstr wstr)
@ stdcall UrlApplySchemeA(str ptr ptr long)
@ stdcall UrlApplySchemeW(wstr ptr ptr long)
@ stdcall UrlCanonicalizeA(str ptr ptr long)
@ stdcall UrlCanonicalizeW(wstr ptr ptr long)
@ stdcall UrlCombineA(str str ptr ptr long)
@ stdcall UrlCombineW(wstr wstr ptr ptr long)
@ stdcall UrlCompareA(str str long)
@ stdcall UrlCompareW(wstr wstr long)
@ stdcall UrlCreateFromPathA(str ptr ptr long)
@ stdcall UrlCreateFromPathW(wstr ptr ptr long)
@ stdcall UrlEscapeA(str ptr ptr long)
@ stdcall UrlEscapeW(wstr ptr ptr long)
@ stdcall UrlGetLocationA(str)
@ stdcall UrlGetLocationW(wstr)
@ stdcall UrlGetPartA(str ptr ptr long long)
@ stdcall UrlGetPartW(wstr ptr ptr long long)
@ stdcall UrlHashA(str ptr long)
@ stdcall UrlHashW(wstr ptr long)
@ stdcall UrlIsA(str long)
@ stdcall UrlIsNoHistoryA(str)
@ stdcall UrlIsNoHistoryW(wstr)
@ stdcall UrlIsOpaqueA(str)
@ stdcall UrlIsOpaqueW(wstr)
@ stdcall UrlIsW(wstr long)
@ stdcall UrlUnescapeA(str ptr ptr long)
@ stdcall UrlUnescapeW(wstr ptr ptr long)
@ stdcall _SHGetInstanceExplorer(ptr)
@ varargs wnsprintfA(ptr long str)
@ varargs wnsprintfW(ptr long wstr)
@ stdcall wvnsprintfA(ptr long str ptr)
@ stdcall wvnsprintfW(ptr long wstr ptr)

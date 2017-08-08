/*
 * 				Shell Library Functions
 *
 * Copyright 1998 Marcus Meissner
 * Copyright 2002 Eric Pouech
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <ctype.h>
#include <assert.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winreg.h"
#include "winuser.h"
#include "shlwapi.h"
#include "ddeml.h"

#include "shell32_main.h"
#include "pidl.h"
#include "shresdef.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(exec);

extern HANDLE CDECL __wine_create_default_token(BOOL admin);

static const WCHAR wszOpen[] = {'o','p','e','n',0};
static const WCHAR wszExe[] = {'.','e','x','e',0};
static const WCHAR wszILPtr[] = {':','%','p',0};
static const WCHAR wszShell[] = {'\\','s','h','e','l','l','\\',0};
static const WCHAR wszFolder[] = {'F','o','l','d','e','r',0};
static const WCHAR wszEmpty[] = {0};

#define SEE_MASK_CLASSALL (SEE_MASK_CLASSNAME | SEE_MASK_CLASSKEY)

typedef UINT_PTR (*SHELL_ExecuteW32)(const WCHAR *lpCmd, WCHAR *env, BOOL shWait,
			    const SHELLEXECUTEINFOW *sei, LPSHELLEXECUTEINFOW sei_out);

static inline BOOL isSpace(WCHAR c)
{
    return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v';
}

/***********************************************************************
 *	SHELL_ArgifyW [Internal]
 *
 * this function is supposed to expand the escape sequences found in the registry
 * some diving reported that the following were used:
 * + %1, %2...  seem to report to parameter of index N in ShellExecute pmts
 *	%1 file
 *	%2 printer
 *	%3 driver
 *	%4 port
 * %I address of a global item ID (explorer switch /idlist)
 * %L seems to be %1 as long filename followed by the 8+3 variation
 * %S ???
 * %* all following parameters (see batfile)
 *
 */
static BOOL SHELL_ArgifyW(WCHAR* out, int len, const WCHAR* fmt, const WCHAR* lpFile, LPITEMIDLIST pidl, LPCWSTR args, DWORD* out_len)
{
    WCHAR   xlpFile[1024];
    BOOL    done = FALSE;
    BOOL    found_p1 = FALSE;
    PWSTR   res = out;
    PCWSTR  cmd;
    DWORD   used = 0;

    TRACE("%p, %d, %s, %s, %p, %p\n", out, len, debugstr_w(fmt),
          debugstr_w(lpFile), pidl, args);

    while (*fmt)
    {
        if (*fmt == '%')
        {
            switch (*++fmt)
            {
            case '\0':
            case '%':
                used++;
                if (used < len)
                    *res++ = '%';
                break;

            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '0':
            case '*':
                if (args)
                {
                    if (*fmt == '*')
                    {
                        used++;
                        if (used < len)
                            *res++ = '"';
                        while(*args)
                        {
                            used++;
                            if (used < len)
                                *res++ = *args++;
                            else
                                args++;
                        }
                        used++;
                        if (used < len)
                            *res++ = '"';
                    }
                    else
                    {
                        while(*args && !isSpace(*args))
                        {
                            used++;
                            if (used < len)
                                *res++ = *args++;
                            else
                                args++;
                        }

                        while(isSpace(*args))
                            ++args;
                    }
                    break;
                }
                /* else fall through */
            case '1':
                if (!done || (*fmt == '1'))
                {
                    /*FIXME Is the call to SearchPathW() really needed? We already have separated out the parameter string in args. */
                    if (SearchPathW(NULL, lpFile, wszExe, sizeof(xlpFile)/sizeof(WCHAR), xlpFile, NULL))
                        cmd = xlpFile;
                    else
                        cmd = lpFile;

                    used += strlenW(cmd);
                    if (used < len)
                    {
                        strcpyW(res, cmd);
                        res += strlenW(cmd);
                    }
                }
                found_p1 = TRUE;
                break;

            /*
             * IE uses this a lot for activating things such as windows media
             * player. This is not verified to be fully correct but it appears
             * to work just fine.
             */
            case 'l':
            case 'L':
		if (lpFile) {
		    used += strlenW(lpFile);
		    if (used < len)
		    {
			strcpyW(res, lpFile);
			res += strlenW(lpFile);
		    }
		}
		found_p1 = TRUE;
                break;

            case 'i':
            case 'I':
		if (pidl) {
		    INT chars = 0;
		    /* %p should not exceed 8, maybe 16 when looking forward to 64bit.
		     * allowing a buffer of 100 should more than exceed all needs */
		    WCHAR buf[100];
		    LPVOID  pv;
		    HGLOBAL hmem = SHAllocShared(pidl, ILGetSize(pidl), 0);
		    pv = SHLockShared(hmem, 0);
		    chars = sprintfW(buf, wszILPtr, pv);
		    if (chars >= sizeof(buf)/sizeof(WCHAR))
			ERR("pidl format buffer too small!\n");
		    used += chars;
		    if (used < len)
		    {
			strcpyW(res,buf);
			res += chars;
		    }
		    SHUnlockShared(pv);
		}
                found_p1 = TRUE;
                break;

	    default:
                /*
                 * Check if this is an env-variable here...
                 */

                /* Make sure that we have at least one more %.*/
                if (strchrW(fmt, '%'))
                {
                    WCHAR   tmpBuffer[1024];
                    PWSTR   tmpB = tmpBuffer;
                    WCHAR   tmpEnvBuff[MAX_PATH];
                    DWORD   envRet;

                    while (*fmt != '%')
                        *tmpB++ = *fmt++;
                    *tmpB++ = 0;

                    TRACE("Checking %s to be an env-var\n", debugstr_w(tmpBuffer));

                    envRet = GetEnvironmentVariableW(tmpBuffer, tmpEnvBuff, MAX_PATH);
                    if (envRet == 0 || envRet > MAX_PATH)
                    {
                        used += strlenW(tmpBuffer);
                        if (used < len)
                        {
                            strcpyW( res, tmpBuffer );
                            res += strlenW(tmpBuffer);
                        }
                    }
                    else
                    {
                        used += strlenW(tmpEnvBuff);
                        if (used < len)
                        {
                            strcpyW( res, tmpEnvBuff );
                            res += strlenW(tmpEnvBuff);
                        }
                    }
                }
                done = TRUE;
                break;
            }
            /* Don't skip past terminator (catch a single '%' at the end) */
            if (*fmt != '\0')
            {
                fmt++;
            }
        }
        else
        {
            used ++;
            if (used < len) 
                *res++ = *fmt++;
            else
                fmt++;
        }
    }

    used ++;
    if (res - out < len)
        *res = '\0';
    else
        out[len-1] = '\0';

    TRACE("used %i of %i space\n",used,len);
    if (out_len)
        *out_len = used;

    return found_p1;
}

static HRESULT SHELL_GetPathFromIDListForExecuteW(LPCITEMIDLIST pidl, LPWSTR pszPath, UINT uOutSize)
{
    STRRET strret;
    IShellFolder* desktop;

    HRESULT hr = SHGetDesktopFolder(&desktop);

    if (SUCCEEDED(hr)) {
	hr = IShellFolder_GetDisplayNameOf(desktop, pidl, SHGDN_FORPARSING, &strret);

	if (SUCCEEDED(hr))
	    StrRetToStrNW(pszPath, uOutSize, &strret, pidl);

	IShellFolder_Release(desktop);
    }

    return hr;
}

/*************************************************************************
 *	SHELL_ExecuteW [Internal]
 *
 */
static UINT_PTR SHELL_ExecuteW(const WCHAR *lpCmd, WCHAR *env, BOOL shWait,
			    const SHELLEXECUTEINFOW *psei, LPSHELLEXECUTEINFOW psei_out)
{
    static WCHAR runasW[] = {'r','u','n','a','s',0};
    HANDLE token = NULL;
    STARTUPINFOW  startup;
    PROCESS_INFORMATION info;
    UINT_PTR retval = SE_ERR_NOASSOC;
    UINT gcdret = 0;
    WCHAR curdir[MAX_PATH];
    DWORD dwCreationFlags;
    const WCHAR *lpDirectory = NULL;

    TRACE("Execute %s from directory %s\n", debugstr_w(lpCmd), debugstr_w(psei->lpDirectory));

    /* make sure we don't fail the CreateProcess if the calling app passes in
     * a bad working directory */
    if (psei->lpDirectory && psei->lpDirectory[0])
    {
        DWORD attr = GetFileAttributesW(psei->lpDirectory);
        if (attr != INVALID_FILE_ATTRIBUTES && attr & FILE_ATTRIBUTE_DIRECTORY)
            lpDirectory = psei->lpDirectory;
    }

    /* ShellExecute specifies the command from psei->lpDirectory
     * if present. Not from the current dir as CreateProcess does */
    if( lpDirectory )
        if( ( gcdret = GetCurrentDirectoryW( MAX_PATH, curdir)))
            if( !SetCurrentDirectoryW( lpDirectory))
                ERR("cannot set directory %s\n", debugstr_w(lpDirectory));
    ZeroMemory(&startup,sizeof(STARTUPINFOW));
    startup.cb = sizeof(STARTUPINFOW);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = psei->nShow;
    dwCreationFlags = CREATE_UNICODE_ENVIRONMENT;
    if (!(psei->fMask & SEE_MASK_NO_CONSOLE))
        dwCreationFlags |= CREATE_NEW_CONSOLE;

    /* Spawning a process with runas verb means that the process should be
     * executed with admin rights. This function ignores the manifest data,
     * and allows programs to elevate rights on-demand. On Windows a complex
     * RPC menchanism is used, using CreateProcessAsUser would fail because
     * it can only be used to drop rights. */
    if (psei->lpVerb && !strcmpiW(psei->lpVerb, runasW))
    {
        if (!(token = __wine_create_default_token(TRUE)))
            ERR("Failed to create admin token\n");
    }

    if (CreateProcessAsUserW(token, NULL, (LPWSTR)lpCmd, NULL, NULL, FALSE,
                             dwCreationFlags, env, lpDirectory, &startup, &info))
    {
        /* Give 30 seconds to the app to come up, if desired. Probably only needed
           when starting app immediately before making a DDE connection. */
        if (shWait)
            if (WaitForInputIdle( info.hProcess, 30000 ) == WAIT_FAILED)
                WARN("WaitForInputIdle failed: Error %d\n", GetLastError() );
        retval = 33;
        if (psei->fMask & SEE_MASK_NOCLOSEPROCESS)
            psei_out->hProcess = info.hProcess;
        else
            CloseHandle( info.hProcess );
        CloseHandle( info.hThread );
    }
    else if ((retval = GetLastError()) >= 32)
    {
        TRACE("CreateProcess returned error %ld\n", retval);
        retval = ERROR_BAD_FORMAT;
    }

    if (token) CloseHandle(token);

    TRACE("returning %lu\n", retval);

    psei_out->hInstApp = (HINSTANCE)retval;
    if( gcdret )
        if( !SetCurrentDirectoryW( curdir))
            ERR("cannot return to directory %s\n", debugstr_w(curdir));

    return retval;
}


/***********************************************************************
 *           SHELL_BuildEnvW	[Internal]
 *
 * Build the environment for the new process, adding the specified
 * path to the PATH variable. Returned pointer must be freed by caller.
 */
static void *SHELL_BuildEnvW( const WCHAR *path )
{
    static const WCHAR wPath[] = {'P','A','T','H','=',0};
    WCHAR *strings, *new_env;
    WCHAR *p, *p2;
    int total = strlenW(path) + 1;
    BOOL got_path = FALSE;

    if (!(strings = GetEnvironmentStringsW())) return NULL;
    p = strings;
    while (*p)
    {
        int len = strlenW(p) + 1;
        if (!strncmpiW( p, wPath, 5 )) got_path = TRUE;
        total += len;
        p += len;
    }
    if (!got_path) total += 5;  /* we need to create PATH */
    total++;  /* terminating null */

    if (!(new_env = HeapAlloc( GetProcessHeap(), 0, total * sizeof(WCHAR) )))
    {
        FreeEnvironmentStringsW( strings );
        return NULL;
    }
    p = strings;
    p2 = new_env;
    while (*p)
    {
        int len = strlenW(p) + 1;
        memcpy( p2, p, len * sizeof(WCHAR) );
        if (!strncmpiW( p, wPath, 5 ))
        {
            p2[len - 1] = ';';
            strcpyW( p2 + len, path );
            p2 += strlenW(path) + 1;
        }
        p += len;
        p2 += len;
    }
    if (!got_path)
    {
        strcpyW( p2, wPath );
        strcatW( p2, path );
        p2 += strlenW(p2) + 1;
    }
    *p2 = 0;
    FreeEnvironmentStringsW( strings );
    return new_env;
}


/***********************************************************************
 *           SHELL_TryAppPathW	[Internal]
 *
 * Helper function for SHELL_FindExecutable
 * @param lpResult - pointer to a buffer of size MAX_PATH
 * On entry: szName is a filename (probably without path separators).
 * On exit: if szName found in "App Path", place full path in lpResult, and return true
 */
static BOOL SHELL_TryAppPathW( LPCWSTR szName, LPWSTR lpResult, WCHAR **env)
{
    static const WCHAR wszKeyAppPaths[] = {'S','o','f','t','w','a','r','e','\\','M','i','c','r','o','s','o','f','t','\\','W','i','n','d','o','w','s',
	'\\','C','u','r','r','e','n','t','V','e','r','s','i','o','n','\\','A','p','p',' ','P','a','t','h','s','\\',0};
    static const WCHAR wPath[] = {'P','a','t','h',0};
    HKEY hkApp = 0;
    WCHAR buffer[1024];
    LONG len;
    LONG res;
    BOOL found = FALSE;

    if (env) *env = NULL;
    strcpyW(buffer, wszKeyAppPaths);
    strcatW(buffer, szName);
    res = RegOpenKeyExW(HKEY_LOCAL_MACHINE, buffer, 0, KEY_READ, &hkApp);
    if (res) goto end;

    len = MAX_PATH*sizeof(WCHAR);
    res = RegQueryValueW(hkApp, NULL, lpResult, &len);
    if (res) goto end;
    found = TRUE;

    if (env)
    {
        DWORD count = sizeof(buffer);
        if (!RegQueryValueExW(hkApp, wPath, NULL, NULL, (LPBYTE)buffer, &count) && buffer[0])
            *env = SHELL_BuildEnvW( buffer );
    }

end:
    if (hkApp) RegCloseKey(hkApp);
    return found;
}

/*************************************************************************
 *	SHELL_FindExecutableByVerb [Internal]
 *
 * called from SHELL_FindExecutable or SHELL_execute_class
 * in/out:
 *      classname a buffer, big enough, to get the key name to do actually the
 *              command   "WordPad.Document.1\\shell\\open\\command"
 *              passed as "WordPad.Document.1"
 * in:
 *      lpVerb the operation on it (open)
 *      commandlen the size of command buffer (in bytes)
 * out:
 *      command a buffer, to store the command to do the
 *              operation on the file
 *      key a buffer, big enough, to get the key name to do actually the
 *              command "WordPad.Document.1\\shell\\open\\command"
 *              Can be NULL
 */
static UINT SHELL_FindExecutableByVerb(LPCWSTR lpVerb, LPWSTR key, LPWSTR classname, LPWSTR command, LONG commandlen)
{
    static const WCHAR wCommand[] = {'\\','c','o','m','m','a','n','d',0};
    HKEY hkeyClass;
    WCHAR verb[MAX_PATH];

    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, classname, 0, 0x02000000, &hkeyClass))
        return SE_ERR_NOASSOC;
    if (!HCR_GetDefaultVerbW(hkeyClass, lpVerb, verb, sizeof(verb)/sizeof(verb[0])))
        return SE_ERR_NOASSOC;
    RegCloseKey(hkeyClass);

    /* Looking for ...buffer\shell\<verb>\command */
    strcatW(classname, wszShell);
    strcatW(classname, verb);
    strcatW(classname, wCommand);

    if (RegQueryValueW(HKEY_CLASSES_ROOT, classname, command,
                       &commandlen) == ERROR_SUCCESS)
    {
	commandlen /= sizeof(WCHAR);
        if (key) strcpyW(key, classname);
#if 0
        LPWSTR tmp;
        WCHAR param[256];
	LONG paramlen = sizeof(param);
        static const WCHAR wSpace[] = {' ',0};

        /* FIXME: it seems all Windows version don't behave the same here.
         * the doc states that this ddeexec information can be found after
         * the exec names.
         * on Win98, it doesn't appear, but I think it does on Win2k
         */
	/* Get the parameters needed by the application
	   from the associated ddeexec key */
	tmp = strstrW(classname, wCommand);
	tmp[0] = '\0';
	strcatW(classname, wDdeexec);
	if (RegQueryValueW(HKEY_CLASSES_ROOT, classname, param,
				     &paramlen) == ERROR_SUCCESS)
	{
	    paramlen /= sizeof(WCHAR);
            strcatW(command, wSpace);
            strcatW(command, param);
            commandlen += paramlen;
	}
#endif

	command[commandlen] = '\0';

	return 33; /* FIXME see SHELL_FindExecutable() */
    }

    return SE_ERR_NOASSOC;
}

/*************************************************************************
 *	SHELL_FindExecutable [Internal]
 *
 * Utility for code sharing between FindExecutable and ShellExecute
 * in:
 *      lpFile the name of a file
 *      lpVerb the operation on it (open)
 * out:
 *      lpResult a buffer, big enough :-(, to store the command to do the
 *              operation on the file
 *      key a buffer, big enough, to get the key name to do actually the
 *              command (it'll be used afterwards for more information
 *              on the operation)
 */
static UINT SHELL_FindExecutable(LPCWSTR lpPath, LPCWSTR lpFile, LPCWSTR lpVerb,
                                 LPWSTR lpResult, int resultLen, LPWSTR key, WCHAR **env, LPITEMIDLIST pidl, LPCWSTR args)
{
    static const WCHAR wWindows[] = {'w','i','n','d','o','w','s',0};
    static const WCHAR wPrograms[] = {'p','r','o','g','r','a','m','s',0};
    static const WCHAR wExtensions[] = {'e','x','e',' ','p','i','f',' ','b','a','t',' ','c','m','d',' ','c','o','m',0};
    WCHAR *extension = NULL; /* pointer to file extension */
    WCHAR classname[256];     /* registry name for this file type */
    LONG  classnamelen = sizeof(classname); /* length of above */
    WCHAR command[1024];     /* command from registry */
    WCHAR wBuffer[256];      /* Used to GetProfileString */
    UINT  retval = SE_ERR_NOASSOC;
    WCHAR *tok;              /* token pointer */
    WCHAR xlpFile[256];      /* result of SearchPath */
    DWORD attribs;           /* file attributes */

    TRACE("%s\n", debugstr_w(lpFile));

    if (!lpResult)
        return ERROR_INVALID_PARAMETER;

    xlpFile[0] = '\0';
    lpResult[0] = '\0'; /* Start off with an empty return string */
    if (key) *key = '\0';

    /* trap NULL parameters on entry */
    if (!lpFile)
    {
        WARN("(lpFile=%s,lpResult=%s): NULL parameter\n",
             debugstr_w(lpFile), debugstr_w(lpResult));
        return ERROR_FILE_NOT_FOUND; /* File not found. Close enough, I guess. */
    }

    if (SHELL_TryAppPathW( lpFile, lpResult, env ))
    {
        TRACE("found %s via App Paths\n", debugstr_w(lpResult));
        return 33;
    }

    if (SearchPathW(lpPath, lpFile, wszExe, sizeof(xlpFile)/sizeof(WCHAR), xlpFile, NULL))
    {
        TRACE("SearchPathW returned non-zero\n");
        lpFile = xlpFile;
        /* The file was found in the application-supplied default directory (or the system search path) */
    }
    else if (lpPath && SearchPathW(NULL, lpFile, wszExe, sizeof(xlpFile)/sizeof(WCHAR), xlpFile, NULL))
    {
        TRACE("SearchPathW returned non-zero\n");
        lpFile = xlpFile;
        /* The file was found in one of the directories in the system-wide search path */
    }

    attribs = GetFileAttributesW(lpFile);
    if (attribs!=INVALID_FILE_ATTRIBUTES && (attribs&FILE_ATTRIBUTE_DIRECTORY))
    {
       strcpyW(classname, wszFolder);
    }
    else
    {
        /* Did we get something? Anything? */
        if (xlpFile[0]==0)
        {
            TRACE("Returning SE_ERR_FNF\n");
            return SE_ERR_FNF;
        }
        /* First thing we need is the file's extension */
        extension = strrchrW(xlpFile, '.'); /* Assume last "." is the one; */
        /* File->Run in progman uses */
        /* .\FILE.EXE :( */
        TRACE("xlpFile=%s,extension=%s\n", debugstr_w(xlpFile), debugstr_w(extension));

        if (extension == NULL || extension[1]==0)
        {
            WARN("Returning SE_ERR_NOASSOC\n");
            return SE_ERR_NOASSOC;
        }

        /* Three places to check: */
        /* 1. win.ini, [windows], programs (NB no leading '.') */
        /* 2. Registry, HKEY_CLASS_ROOT\<classname>\shell\open\command */
        /* 3. win.ini, [extensions], extension (NB no leading '.' */
        /* All I know of the order is that registry is checked before */
        /* extensions; however, it'd make sense to check the programs */
        /* section first, so that's what happens here. */

        /* See if it's a program - if GetProfileString fails, we skip this
         * section. Actually, if GetProfileString fails, we've probably
         * got a lot more to worry about than running a program... */
        if (GetProfileStringW(wWindows, wPrograms, wExtensions, wBuffer, sizeof(wBuffer)/sizeof(WCHAR)) > 0)
        {
            CharLowerW(wBuffer);
            tok = wBuffer;
            while (*tok)
            {
                WCHAR *p = tok;
                while (*p && *p != ' ' && *p != '\t') p++;
                if (*p)
                {
                    *p++ = 0;
                    while (*p == ' ' || *p == '\t') p++;
                }

                if (strcmpiW(tok, &extension[1]) == 0) /* have to skip the leading "." */
                {
                    strcpyW(lpResult, xlpFile);
                    /* Need to perhaps check that the file has a path
                     * attached */
                    TRACE("found %s\n", debugstr_w(lpResult));
                    return 33;
                    /* Greater than 32 to indicate success */
                }
                tok = p;
            }
        }

        /* Check registry */
        if (RegQueryValueW(HKEY_CLASSES_ROOT, extension, classname,
                           &classnamelen) == ERROR_SUCCESS)
        {
            classnamelen /= sizeof(WCHAR);
	    if (classnamelen == sizeof(classname)/sizeof(WCHAR))
		classnamelen--;
            classname[classnamelen] = '\0';
            TRACE("File type: %s\n", debugstr_w(classname));
        }
        else
        {
            *classname = '\0';
        }
    }

    if (*classname)
    {
        /* pass the verb string to SHELL_FindExecutableByVerb() */
        retval = SHELL_FindExecutableByVerb(lpVerb, key, classname, command, sizeof(command));

	if (retval > 32)
	{
	    DWORD finishedLen;
	    SHELL_ArgifyW(lpResult, resultLen, command, xlpFile, pidl, args, &finishedLen);
	    if (finishedLen > resultLen)
		ERR("Argify buffer not large enough.. truncated\n");

	    /* Remove double quotation marks and command line arguments */
	    if (*lpResult == '"')
	    {
		WCHAR *p = lpResult;
		while (*(p + 1) != '"')
		{
		    *p = *(p + 1);
		    p++;
		}
		*p = '\0';
	    }
            else
            {
                /* Truncate on first space */
		WCHAR *p = lpResult;
		while (*p != ' ' && *p != '\0')
                    p++;
                *p='\0';
            }
	}
    }
    else /* Check win.ini */
    {
	static const WCHAR wExtensions[] = {'e','x','t','e','n','s','i','o','n','s',0};

	/* Toss the leading dot */
	extension++;
	if (GetProfileStringW(wExtensions, extension, wszEmpty, command, sizeof(command)/sizeof(WCHAR)) > 0)
        {
            if (strlenW(command) != 0)
            {
                strcpyW(lpResult, command);
                tok = strchrW(lpResult, '^'); /* should be ^.extension? */
                if (tok != NULL)
                {
                    tok[0] = '\0';
                    strcatW(lpResult, xlpFile); /* what if no dir in xlpFile? */
                    tok = strchrW(command, '^'); /* see above */
                    if ((tok != NULL) && (strlenW(tok)>5))
                    {
                        strcatW(lpResult, &tok[5]);
                    }
                }
                retval = 33; /* FIXME - see above */
            }
        }
    }

    TRACE("returning %s\n", debugstr_w(lpResult));
    return retval;
}

/******************************************************************
 *		dde_cb
 *
 * callback for the DDE connection. not really useful
 */
static HDDEDATA CALLBACK dde_cb(UINT uType, UINT uFmt, HCONV hConv,
                                HSZ hsz1, HSZ hsz2, HDDEDATA hData,
                                ULONG_PTR dwData1, ULONG_PTR dwData2)
{
    TRACE("dde_cb: %04x, %04x, %p, %p, %p, %p, %08lx, %08lx\n",
           uType, uFmt, hConv, hsz1, hsz2, hData, dwData1, dwData2);
    return NULL;
}

/******************************************************************
 *		dde_connect
 *
 * ShellExecute helper. Used to do an operation with a DDE connection
 *
 * Handles both the direct connection (try #1), and if it fails,
 * launching an application and trying (#2) to connect to it
 *
 */
static unsigned dde_connect(const WCHAR* key, const WCHAR* start, WCHAR* ddeexec,
                            const WCHAR* lpFile, WCHAR *env,
			    LPCWSTR szCommandline, LPITEMIDLIST pidl, SHELL_ExecuteW32 execfunc,
                            const SHELLEXECUTEINFOW *psei, LPSHELLEXECUTEINFOW psei_out)
{
    static const WCHAR wApplication[] = {'\\','a','p','p','l','i','c','a','t','i','o','n',0};
    static const WCHAR wTopic[] = {'\\','t','o','p','i','c',0};
    WCHAR       regkey[256];
    WCHAR *     endkey = regkey + strlenW(key);
    WCHAR       app[256], topic[256], ifexec[256], static_res[256];
    WCHAR *     dynamic_res=NULL;
    WCHAR *     res;
    LONG        applen, topiclen, ifexeclen;
    WCHAR *     exec;
    DWORD       ddeInst = 0;
    DWORD       tid;
    DWORD       resultLen, endkeyLen;
    HSZ         hszApp, hszTopic;
    HCONV       hConv;
    HDDEDATA    hDdeData;
    unsigned    ret = SE_ERR_NOASSOC;
    BOOL unicode = !(GetVersion() & 0x80000000);

    if (strlenW(key) + 1 > sizeof(regkey) / sizeof(regkey[0]))
    {
        FIXME("input parameter %s larger than buffer\n", debugstr_w(key));
        return 2;
    }
    strcpyW(regkey, key);
    endkeyLen = sizeof(regkey) / sizeof(regkey[0]) - (endkey - regkey);
    if (strlenW(wApplication) + 1 > endkeyLen)
    {
        FIXME("endkey %s overruns buffer\n", debugstr_w(wApplication));
        return 2;
    }
    strcpyW(endkey, wApplication);
    applen = sizeof(app);
    if (RegQueryValueW(HKEY_CLASSES_ROOT, regkey, app, &applen) != ERROR_SUCCESS)
    {
        WCHAR command[1024], fullpath[MAX_PATH];
        static const WCHAR wSo[] = { '.','s','o',0 };
        int sizeSo = sizeof(wSo)/sizeof(WCHAR);
        LPWSTR ptr = NULL;
        DWORD ret = 0;

        /* Get application command from start string and find filename of application */
        if (*start == '"')
        {
            if (strlenW(start + 1) + 1 > sizeof(command) / sizeof(command[0]))
            {
                FIXME("size of input parameter %s larger than buffer\n",
                      debugstr_w(start + 1));
                return 2;
            }
            strcpyW(command, start+1);
            if ((ptr = strchrW(command, '"')))
                *ptr = 0;
            ret = SearchPathW(NULL, command, wszExe, sizeof(fullpath)/sizeof(WCHAR), fullpath, &ptr);
        }
        else
        {
            LPCWSTR p;
            LPWSTR space;
            for (p=start; (space=strchrW(p, ' ')); p=space+1)
            {
                int idx = space-start;
                memcpy(command, start, idx*sizeof(WCHAR));
                command[idx] = '\0';
                if ((ret = SearchPathW(NULL, command, wszExe, sizeof(fullpath)/sizeof(WCHAR), fullpath, &ptr)))
                    break;
            }
            if (!ret)
                ret = SearchPathW(NULL, start, wszExe, sizeof(fullpath)/sizeof(WCHAR), fullpath, &ptr);
        }

        if (!ret)
        {
            ERR("Unable to find application path for command %s\n", debugstr_w(start));
            return ERROR_ACCESS_DENIED;
        }
        if (strlenW(ptr) + 1 > sizeof(app) / sizeof(app[0]))
        {
            FIXME("size of found path %s larger than buffer\n", debugstr_w(ptr));
            return 2;
        }
        strcpyW(app, ptr);

        /* Remove extensions (including .so) */
        ptr = app + strlenW(app) - (sizeSo-1);
        if (strlenW(app) >= sizeSo &&
            !strcmpW(ptr, wSo))
            *ptr = 0;

        ptr = strrchrW(app, '.');
        assert(ptr);
        *ptr = 0;
    }

    if (strlenW(wTopic) + 1 > endkeyLen)
    {
        FIXME("endkey %s overruns buffer\n", debugstr_w(wTopic));
        return 2;
    }
    strcpyW(endkey, wTopic);
    topiclen = sizeof(topic);
    if (RegQueryValueW(HKEY_CLASSES_ROOT, regkey, topic, &topiclen) != ERROR_SUCCESS)
    {
        static const WCHAR wSystem[] = {'S','y','s','t','e','m',0};
        strcpyW(topic, wSystem);
    }

    if (unicode)
    {
        if (DdeInitializeW(&ddeInst, dde_cb, APPCMD_CLIENTONLY, 0L) != DMLERR_NO_ERROR)
            return 2;
    }
    else
    {
        if (DdeInitializeA(&ddeInst, dde_cb, APPCMD_CLIENTONLY, 0L) != DMLERR_NO_ERROR)
            return 2;
    }

    hszApp = DdeCreateStringHandleW(ddeInst, app, CP_WINUNICODE);
    hszTopic = DdeCreateStringHandleW(ddeInst, topic, CP_WINUNICODE);

    hConv = DdeConnect(ddeInst, hszApp, hszTopic, NULL);
    exec = ddeexec;
    if (!hConv)
    {
        static const WCHAR wIfexec[] = {'\\','i','f','e','x','e','c',0};
        TRACE("Launching %s\n", debugstr_w(start));
        ret = execfunc(start, env, TRUE, psei, psei_out);
        if (ret <= 32)
        {
            TRACE("Couldn't launch\n");
            goto error;
        }
        hConv = DdeConnect(ddeInst, hszApp, hszTopic, NULL);
        if (!hConv)
        {
            TRACE("Couldn't connect. ret=%d\n", ret);
            DdeUninitialize(ddeInst);
            SetLastError(ERROR_DDE_FAIL);
            return 30; /* whatever */
        }
        if (strlenW(wIfexec) + 1 > endkeyLen)
        {
            FIXME("endkey %s overruns buffer\n", debugstr_w(wIfexec));
            return 2;
        }
        strcpyW(endkey, wIfexec);
        ifexeclen = sizeof(ifexec);
        if (RegQueryValueW(HKEY_CLASSES_ROOT, regkey, ifexec, &ifexeclen) == ERROR_SUCCESS)
        {
            exec = ifexec;
        }
    }

    SHELL_ArgifyW(static_res, sizeof(static_res)/sizeof(WCHAR), exec, lpFile, pidl, szCommandline, &resultLen);
    if (resultLen > sizeof(static_res)/sizeof(WCHAR))
    {
        res = dynamic_res = HeapAlloc(GetProcessHeap(), 0, resultLen * sizeof(WCHAR));
        SHELL_ArgifyW(dynamic_res, resultLen, exec, lpFile, pidl, szCommandline, NULL);
    }
    else
        res = static_res;
    TRACE("%s %s => %s\n", debugstr_w(exec), debugstr_w(lpFile), debugstr_w(res));

    /* It's documented in the KB 330337 that IE has a bug and returns
     * error DMLERR_NOTPROCESSED on XTYP_EXECUTE request.
     */
    if (unicode)
        hDdeData = DdeClientTransaction((LPBYTE)res, (strlenW(res) + 1) * sizeof(WCHAR), hConv, 0L, 0,
                                         XTYP_EXECUTE, 30000, &tid);
    else
    {
        DWORD lenA = WideCharToMultiByte(CP_ACP, 0, res, -1, NULL, 0, NULL, NULL);
        char *resA = HeapAlloc(GetProcessHeap(), 0, lenA);
        WideCharToMultiByte(CP_ACP, 0, res, -1, resA, lenA, NULL, NULL);
        hDdeData = DdeClientTransaction( (LPBYTE)resA, lenA, hConv, 0L, 0,
                                         XTYP_EXECUTE, 10000, &tid );
        HeapFree(GetProcessHeap(), 0, resA);
    }
    if (hDdeData)
        DdeFreeDataHandle(hDdeData);
    else
        WARN("DdeClientTransaction failed with error %04x\n", DdeGetLastError(ddeInst));
    ret = 33;

    HeapFree(GetProcessHeap(), 0, dynamic_res);

    DdeDisconnect(hConv);

 error:
    DdeUninitialize(ddeInst);

    return ret;
}

/*************************************************************************
 *	execute_from_key [Internal]
 */
static UINT_PTR execute_from_key(LPCWSTR key, LPCWSTR lpFile, WCHAR *env, LPCWSTR szCommandline,
                             LPCWSTR executable_name,
			     SHELL_ExecuteW32 execfunc,
                             LPSHELLEXECUTEINFOW psei, LPSHELLEXECUTEINFOW psei_out)
{
    static const WCHAR wCommand[] = {'c','o','m','m','a','n','d',0};
    static const WCHAR wDdeexec[] = {'d','d','e','e','x','e','c',0};
    WCHAR cmd[256], param[1024], ddeexec[256];
    LONG cmdlen = sizeof(cmd), ddeexeclen = sizeof(ddeexec);
    UINT_PTR retval = SE_ERR_NOASSOC;
    DWORD resultLen;
    LPWSTR tmp;

    TRACE("%s %s %s %s %s\n", debugstr_w(key), debugstr_w(lpFile), debugstr_w(env),
           debugstr_w(szCommandline), debugstr_w(executable_name));

    cmd[0] = '\0';
    param[0] = '\0';

    /* Get the application from the registry */
    if (RegQueryValueW(HKEY_CLASSES_ROOT, key, cmd, &cmdlen) == ERROR_SUCCESS)
    {
        TRACE("got cmd: %s\n", debugstr_w(cmd));

        /* Is there a replace() function anywhere? */
        cmdlen /= sizeof(WCHAR);
	if (cmdlen >= sizeof(cmd)/sizeof(WCHAR))
	    cmdlen = sizeof(cmd)/sizeof(WCHAR)-1;
        cmd[cmdlen] = '\0';
        SHELL_ArgifyW(param, sizeof(param)/sizeof(WCHAR), cmd, lpFile, psei->lpIDList, szCommandline, &resultLen);
        if (resultLen > sizeof(param)/sizeof(WCHAR))
            ERR("Argify buffer not large enough, truncating\n");
    }

    /* Get the parameters needed by the application
       from the associated ddeexec key */
    tmp = strstrW(key, wCommand);
    assert(tmp);
    strcpyW(tmp, wDdeexec);

    if (RegQueryValueW(HKEY_CLASSES_ROOT, key, ddeexec, &ddeexeclen) == ERROR_SUCCESS)
    {
        TRACE("Got ddeexec %s => %s\n", debugstr_w(key), debugstr_w(ddeexec));
        if (!param[0]) strcpyW(param, executable_name);
        retval = dde_connect(key, param, ddeexec, lpFile, env, szCommandline, psei->lpIDList, execfunc, psei, psei_out);
    }
    else if (param[0])
    {
        TRACE("executing: %s\n", debugstr_w(param));
        retval = execfunc(param, env, FALSE, psei, psei_out);
    }
    else
        WARN("Nothing appropriate found for %s\n", debugstr_w(key));

    return retval;
}

/*************************************************************************
 * FindExecutableA			[SHELL32.@]
 */
HINSTANCE WINAPI FindExecutableA(LPCSTR lpFile, LPCSTR lpDirectory, LPSTR lpResult)
{
    HINSTANCE retval;
    WCHAR *wFile = NULL, *wDirectory = NULL;
    WCHAR wResult[MAX_PATH];

    if (lpFile) __SHCloneStrAtoW(&wFile, lpFile);
    if (lpDirectory) __SHCloneStrAtoW(&wDirectory, lpDirectory);

    retval = FindExecutableW(wFile, wDirectory, wResult);
    WideCharToMultiByte(CP_ACP, 0, wResult, -1, lpResult, MAX_PATH, NULL, NULL);
    SHFree( wFile );
    SHFree( wDirectory );

    TRACE("returning %s\n", lpResult);
    return retval;
}

/*************************************************************************
 * FindExecutableW			[SHELL32.@]
 *
 * This function returns the executable associated with the specified file
 * for the default verb.
 *
 * PARAMS
 *  lpFile   [I] The file to find the association for. This must refer to
 *               an existing file otherwise FindExecutable fails and returns
 *               SE_ERR_FNF.
 *  lpResult [O] Points to a buffer into which the executable path is
 *               copied. This parameter must not be NULL otherwise
 *               FindExecutable() segfaults. The buffer must be of size at
 *               least MAX_PATH characters.
 *
 * RETURNS
 *  A value greater than 32 on success, less than or equal to 32 otherwise.
 *  See the SE_ERR_* constants.
 *
 * NOTES
 *  On Windows XP and 2003, FindExecutable() seems to first convert the
 *  filename into 8.3 format, thus taking into account only the first three
 *  characters of the extension, and expects to find an association for those.
 *  However other Windows versions behave sanely.
 */
HINSTANCE WINAPI FindExecutableW(LPCWSTR lpFile, LPCWSTR lpDirectory, LPWSTR lpResult)
{
    UINT_PTR retval = SE_ERR_NOASSOC;
    WCHAR old_dir[1024];
    WCHAR res[MAX_PATH];

    TRACE("File %s, Dir %s\n", debugstr_w(lpFile), debugstr_w(lpDirectory));

    lpResult[0] = '\0'; /* Start off with an empty return string */
    if (lpFile == NULL)
	return (HINSTANCE)SE_ERR_FNF;

    if (lpDirectory)
    {
        GetCurrentDirectoryW(sizeof(old_dir)/sizeof(WCHAR), old_dir);
        SetCurrentDirectoryW(lpDirectory);
    }

    retval = SHELL_FindExecutable(lpDirectory, lpFile, wszOpen, res, MAX_PATH, NULL, NULL, NULL, NULL);

    if (retval > 32)
        strcpyW(lpResult, res);

    TRACE("returning %s\n", debugstr_w(lpResult));
    if (lpDirectory)
        SetCurrentDirectoryW(old_dir);
    return (HINSTANCE)retval;
}

/* FIXME: is this already implemented somewhere else? */
static HKEY ShellExecute_GetClassKey( const SHELLEXECUTEINFOW *sei )
{
    LPCWSTR ext = NULL, lpClass = NULL;
    LPWSTR cls = NULL;
    DWORD type = 0, sz = 0;
    HKEY hkey = 0;
    LONG r;

    if (sei->fMask & SEE_MASK_CLASSALL)
        return sei->hkeyClass;
 
    if (sei->fMask & SEE_MASK_CLASSNAME)
        lpClass = sei->lpClass;
    else
    {
        ext = PathFindExtensionW( sei->lpFile );
        TRACE("ext = %s\n", debugstr_w( ext ) );
        if (!ext)
            return hkey;

        r = RegOpenKeyW( HKEY_CLASSES_ROOT, ext, &hkey );
        if (r != ERROR_SUCCESS )
            return hkey;

        r = RegQueryValueExW( hkey, NULL, 0, &type, NULL, &sz );
        if ( r == ERROR_SUCCESS && type == REG_SZ )
        {
            sz += sizeof (WCHAR);
            cls = HeapAlloc( GetProcessHeap(), 0, sz );
            cls[0] = 0;
            RegQueryValueExW( hkey, NULL, 0, &type, (LPBYTE) cls, &sz );
        }

        RegCloseKey( hkey );
        lpClass = cls;
    }

    TRACE("class = %s\n", debugstr_w(lpClass) );

    hkey = 0;
    if ( lpClass )
        RegOpenKeyW( HKEY_CLASSES_ROOT, lpClass, &hkey );

    HeapFree( GetProcessHeap(), 0, cls );

    return hkey;
}

static IDataObject *shellex_get_dataobj( LPSHELLEXECUTEINFOW sei )
{
    LPCITEMIDLIST pidllast = NULL;
    IDataObject *dataobj = NULL;
    IShellFolder *shf = NULL;
    LPITEMIDLIST pidl = NULL;
    HRESULT r;

    if (sei->fMask & SEE_MASK_CLASSALL)
        pidl = sei->lpIDList;
    else
    {
        WCHAR fullpath[MAX_PATH];
        BOOL ret;

        fullpath[0] = 0;
        ret = GetFullPathNameW( sei->lpFile, MAX_PATH, fullpath, NULL );
        if (!ret)
            goto end;

        pidl = ILCreateFromPathW( fullpath );
    }

    r = SHBindToParent( pidl, &IID_IShellFolder, (LPVOID*)&shf, &pidllast );
    if ( FAILED( r ) )
        goto end;

    IShellFolder_GetUIObjectOf( shf, NULL, 1, &pidllast,
                                &IID_IDataObject, NULL, (LPVOID*) &dataobj );

end:
    if ( pidl != sei->lpIDList )
        ILFree( pidl );
    if ( shf )
        IShellFolder_Release( shf );
    return dataobj;
}

static HRESULT shellex_run_context_menu_default( IShellExtInit *obj,
                                                 LPSHELLEXECUTEINFOW sei )
{
    IContextMenu *cm = NULL;
    CMINVOKECOMMANDINFOEX ici;
    MENUITEMINFOW info;
    WCHAR string[0x80];
    INT i, n, def = -1;
    HMENU hmenu = 0;
    HRESULT r;

    TRACE("%p %p\n", obj, sei );

    r = IShellExtInit_QueryInterface( obj, &IID_IContextMenu, (LPVOID*) &cm );
    if ( FAILED( r ) )
        return r;

    hmenu = CreateMenu();
    if ( !hmenu )
        goto end;

    /* the number of the last menu added is returned in r */
    r = IContextMenu_QueryContextMenu( cm, hmenu, 0, 0x20, 0x7fff, CMF_DEFAULTONLY );
    if ( FAILED( r ) )
        goto end;

    n = GetMenuItemCount( hmenu );
    for ( i = 0; i < n; i++ )
    {
        memset( &info, 0, sizeof info );
        info.cbSize = sizeof info;
        info.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_STATE | MIIM_DATA | MIIM_ID;
        info.dwTypeData = string;
        info.cch = sizeof string;
        string[0] = 0;
        GetMenuItemInfoW( hmenu, i, TRUE, &info );

        TRACE("menu %d %s %08x %08lx %08x %08x\n", i, debugstr_w(string),
            info.fState, info.dwItemData, info.fType, info.wID );
        if ( ( !sei->lpVerb && (info.fState & MFS_DEFAULT) ) ||
             ( sei->lpVerb && !lstrcmpiW( sei->lpVerb, string ) ) )
        {
            def = i;
            break;
        }
    }

    r = E_FAIL;
    if ( def == -1 )
        goto end;

    memset( &ici, 0, sizeof ici );
    ici.cbSize = sizeof ici;
    ici.fMask = CMIC_MASK_UNICODE | (sei->fMask & (SEE_MASK_NO_CONSOLE|SEE_MASK_NOASYNC|SEE_MASK_ASYNCOK|SEE_MASK_FLAG_NO_UI));
    ici.nShow = sei->nShow;
    ici.lpVerb = MAKEINTRESOURCEA( def );
    ici.hwnd = sei->hwnd;
    ici.lpParametersW = sei->lpParameters;
    
    r = IContextMenu_InvokeCommand( cm, (LPCMINVOKECOMMANDINFO) &ici );

    TRACE("invoke command returned %08x\n", r );

end:
    if ( hmenu )
        DestroyMenu( hmenu );
    if ( cm )
        IContextMenu_Release( cm );
    return r;
}

static HRESULT shellex_load_object_and_run( HKEY hkey, LPCGUID guid, LPSHELLEXECUTEINFOW sei )
{
    IDataObject *dataobj = NULL;
    IObjectWithSite *ows = NULL;
    IShellExtInit *obj = NULL;
    HRESULT r;

    TRACE("%p %s %p\n", hkey, debugstr_guid( guid ), sei );

    r = CoInitialize( NULL );
    if ( FAILED( r ) )
        goto end;

    r = CoCreateInstance( guid, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IShellExtInit, (LPVOID*)&obj );
    if ( FAILED( r ) )
    {
        ERR("failed %08x\n", r );
        goto end;
    }

    dataobj = shellex_get_dataobj( sei );
    if ( !dataobj )
    {
        ERR("failed to get data object\n");
        r = E_FAIL;
        goto end;
    }

    r = IShellExtInit_Initialize( obj, NULL, dataobj, hkey );
    if ( FAILED( r ) )
        goto end;

    r = IShellExtInit_QueryInterface( obj, &IID_IObjectWithSite, (LPVOID*) &ows );
    if ( FAILED( r ) )
        goto end;

    IObjectWithSite_SetSite( ows, NULL );

    r = shellex_run_context_menu_default( obj, sei );

end:
    if ( ows )
        IObjectWithSite_Release( ows );
    if ( dataobj )
        IDataObject_Release( dataobj );
    if ( obj )
        IShellExtInit_Release( obj );
    CoUninitialize();
    return r;
}


/*************************************************************************
 *	ShellExecute_FromContextMenu [Internal]
 */
static LONG ShellExecute_FromContextMenu( LPSHELLEXECUTEINFOW sei )
{
    static const WCHAR szcm[] = { 's','h','e','l','l','e','x','\\',
        'C','o','n','t','e','x','t','M','e','n','u','H','a','n','d','l','e','r','s',0 };
    HKEY hkey, hkeycm = 0;
    WCHAR szguid[39];
    HRESULT hr;
    GUID guid;
    DWORD i;
    LONG r;

    TRACE("%s\n", debugstr_w(sei->lpFile) );

    hkey = ShellExecute_GetClassKey( sei );
    if ( !hkey )
        return ERROR_FUNCTION_FAILED;

    r = RegOpenKeyW( hkey, szcm, &hkeycm );
    if ( r == ERROR_SUCCESS )
    {
        i = 0;
        while ( 1 )
        {
            r = RegEnumKeyW( hkeycm, i++, szguid, sizeof(szguid)/sizeof(szguid[0]) );
            if ( r != ERROR_SUCCESS )
                break;

            hr = CLSIDFromString( szguid, &guid );
            if (SUCCEEDED(hr))
            {
                /* stop at the first one that succeeds in running */
                hr = shellex_load_object_and_run( hkey, &guid, sei );
                if ( SUCCEEDED( hr ) )
                    break;
            }
        }
        RegCloseKey( hkeycm );
    }

    if ( hkey != sei->hkeyClass )
        RegCloseKey( hkey );
    return r;
}

static UINT_PTR SHELL_quote_and_execute( LPCWSTR wcmd, LPCWSTR wszParameters, LPCWSTR lpstrProtocol, LPCWSTR wszApplicationName, LPWSTR env, LPSHELLEXECUTEINFOW psei, LPSHELLEXECUTEINFOW psei_out, SHELL_ExecuteW32 execfunc );

static UINT_PTR SHELL_execute_class( LPCWSTR wszApplicationName, LPSHELLEXECUTEINFOW psei, LPSHELLEXECUTEINFOW psei_out, SHELL_ExecuteW32 execfunc )
{
    static const WCHAR wQuote[] = {'"',0};
    static const WCHAR wSpace[] = {' ',0};
    WCHAR execCmd[1024], classname[1024];
    /* launch a document by fileclass like 'WordPad.Document.1' */
    /* the Commandline contains 'c:\Path\wordpad.exe "%1"' */
    /* FIXME: wcmd should not be of a fixed size. Fixed to 1024, MAX_PATH is way too short! */
    ULONG cmask=(psei->fMask & SEE_MASK_CLASSALL);
    DWORD resultLen;
    BOOL done;
    UINT_PTR rslt;

  /* FIXME: remove following block when SHELL_quote_and_execute supports hkeyClass parameter */
  if (cmask != SEE_MASK_CLASSNAME)
  {
    WCHAR wcmd[1024];
    HCR_GetExecuteCommandW((cmask == SEE_MASK_CLASSKEY) ? psei->hkeyClass : NULL,
                           (cmask == SEE_MASK_CLASSNAME) ? psei->lpClass: NULL,
                           psei->lpVerb,
                           execCmd, sizeof(execCmd));

    /* FIXME: get the extension of lpFile, check if it fits to the lpClass */
    TRACE("SEE_MASK_CLASSNAME->%s, doc->%s\n", debugstr_w(execCmd), debugstr_w(wszApplicationName));

    wcmd[0] = '\0';
    done = SHELL_ArgifyW(wcmd, sizeof(wcmd)/sizeof(WCHAR), execCmd, wszApplicationName, psei->lpIDList, NULL, &resultLen);
    if (!done && wszApplicationName[0])
    {
        strcatW(wcmd, wSpace);
        if (*wszApplicationName != '"')
        {
            strcatW(wcmd, wQuote);
            strcatW(wcmd, wszApplicationName);
            strcatW(wcmd, wQuote);
        }
        else
            strcatW(wcmd, wszApplicationName);
    }
    if (resultLen > sizeof(wcmd)/sizeof(WCHAR))
        ERR("Argify buffer not large enough... truncating\n");
    return execfunc(wcmd, NULL, FALSE, psei, psei_out);
  }

    strcpyW(classname, psei->lpClass);
    rslt = SHELL_FindExecutableByVerb(psei->lpVerb, NULL, classname, execCmd, sizeof(execCmd));

    TRACE("SHELL_FindExecutableByVerb returned %u (%s, %s)\n", (unsigned int)rslt, debugstr_w(classname), debugstr_w(execCmd));
    if (33 > rslt)
        return rslt;
    rslt = SHELL_quote_and_execute( execCmd, wszEmpty, classname,
                                      wszApplicationName, NULL, psei,
                                      psei_out, execfunc );
    return rslt;
}

static void SHELL_translate_idlist( LPSHELLEXECUTEINFOW sei, LPWSTR wszParameters, DWORD parametersLen, LPWSTR wszApplicationName, DWORD dwApplicationNameLen )
{
    static const WCHAR wExplorer[] = {'e','x','p','l','o','r','e','r','.','e','x','e',0};
    WCHAR buffer[MAX_PATH];

    /* last chance to translate IDList: now also allow CLSID paths */
    if (SUCCEEDED(SHELL_GetPathFromIDListForExecuteW(sei->lpIDList, buffer, sizeof(buffer)/sizeof(WCHAR)))) {
        if (buffer[0]==':' && buffer[1]==':') {
            /* open shell folder for the specified class GUID */
            if (strlenW(buffer) + 1 > parametersLen)
                ERR("parameters len exceeds buffer size (%i > %i), truncating\n",
                    lstrlenW(buffer) + 1, parametersLen);
            lstrcpynW(wszParameters, buffer, parametersLen);
            if (strlenW(wExplorer) > dwApplicationNameLen)
                ERR("application len exceeds buffer size (%i > %i), truncating\n",
                    lstrlenW(wExplorer) + 1, dwApplicationNameLen);
            lstrcpynW(wszApplicationName, wExplorer, dwApplicationNameLen);

            sei->fMask &= ~SEE_MASK_INVOKEIDLIST;
        } else {
            WCHAR target[MAX_PATH];
            DWORD attribs;
            DWORD resultLen;
            /* Check if we're executing a directory and if so use the
               handler for the Folder class */
            strcpyW(target, buffer);
            attribs = GetFileAttributesW(buffer);
            if (attribs != INVALID_FILE_ATTRIBUTES &&
                (attribs & FILE_ATTRIBUTE_DIRECTORY) &&
                HCR_GetExecuteCommandW(0, wszFolder,
                                       sei->lpVerb,
                                       buffer, sizeof(buffer))) {
                SHELL_ArgifyW(wszApplicationName, dwApplicationNameLen,
                              buffer, target, sei->lpIDList, NULL, &resultLen);
                if (resultLen > dwApplicationNameLen)
                    ERR("Argify buffer not large enough... truncating\n");
            }
            sei->fMask &= ~SEE_MASK_INVOKEIDLIST;
        }
    }
}

static UINT_PTR SHELL_quote_and_execute( LPCWSTR wcmd, LPCWSTR wszParameters, LPCWSTR wszKeyname, LPCWSTR wszApplicationName, LPWSTR env, LPSHELLEXECUTEINFOW psei, LPSHELLEXECUTEINFOW psei_out, SHELL_ExecuteW32 execfunc )
{
    static const WCHAR wQuote[] = {'"',0};
    static const WCHAR wSpace[] = {' ',0};
    UINT_PTR retval;
    DWORD len;
    WCHAR *wszQuotedCmd;

    /* Length of quotes plus length of command plus NULL terminator */
    len = 2 + lstrlenW(wcmd) + 1;
    if (wszParameters[0])
    {
        /* Length of space plus length of parameters */
        len += 1 + lstrlenW(wszParameters);
    }
    wszQuotedCmd = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    /* Must quote to handle case where cmd contains spaces,
     * else security hole if malicious user creates executable file "C:\\Program"
     */
    strcpyW(wszQuotedCmd, wQuote);
    strcatW(wszQuotedCmd, wcmd);
    strcatW(wszQuotedCmd, wQuote);
    if (wszParameters[0]) {
        strcatW(wszQuotedCmd, wSpace);
        strcatW(wszQuotedCmd, wszParameters);
    }
    TRACE("%s/%s => %s/%s\n", debugstr_w(wszApplicationName), debugstr_w(psei->lpVerb), debugstr_w(wszQuotedCmd), debugstr_w(wszKeyname));
    if (*wszKeyname)
        retval = execute_from_key(wszKeyname, wszApplicationName, env, psei->lpParameters, wcmd, execfunc, psei, psei_out);
    else
        retval = execfunc(wszQuotedCmd, env, FALSE, psei, psei_out);
    HeapFree(GetProcessHeap(), 0, wszQuotedCmd);
    return retval;
}

static UINT_PTR SHELL_execute_url( LPCWSTR lpFile, LPCWSTR wcmd, LPSHELLEXECUTEINFOW psei, LPSHELLEXECUTEINFOW psei_out, SHELL_ExecuteW32 execfunc )
{
    static const WCHAR wShell[] = {'\\','s','h','e','l','l','\\',0};
    static const WCHAR wCommand[] = {'\\','c','o','m','m','a','n','d',0};
    UINT_PTR retval;
    WCHAR *lpstrProtocol;
    LPCWSTR lpstrRes;
    INT iSize;
    DWORD len;

    lpstrRes = strchrW(lpFile, ':');
    if (lpstrRes)
        iSize = lpstrRes - lpFile;
    else
        iSize = strlenW(lpFile);

    TRACE("Got URL: %s\n", debugstr_w(lpFile));
    /* Looking for ...<protocol>\shell\<lpVerb>\command */
    len = iSize + lstrlenW(wShell) + lstrlenW(wCommand) + 1;
    if (psei->lpVerb && *psei->lpVerb)
        len += lstrlenW(psei->lpVerb);
    else
        len += lstrlenW(wszOpen);
    lpstrProtocol = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    memcpy(lpstrProtocol, lpFile, iSize*sizeof(WCHAR));
    lpstrProtocol[iSize] = '\0';
    strcatW(lpstrProtocol, wShell);
    strcatW(lpstrProtocol, psei->lpVerb && *psei->lpVerb ? psei->lpVerb: wszOpen);
    strcatW(lpstrProtocol, wCommand);

    retval = execute_from_key(lpstrProtocol, lpFile, NULL, psei->lpParameters,
                              wcmd, execfunc, psei, psei_out);
    HeapFree(GetProcessHeap(), 0, lpstrProtocol);
    return retval;
}

static void do_error_dialog( UINT_PTR retval, HWND hwnd )
{
    WCHAR msg[2048];
    int error_code=GetLastError();

    if (retval == SE_ERR_NOASSOC)
        LoadStringW(shell32_hInstance, IDS_SHLEXEC_NOASSOC, msg, sizeof(msg)/sizeof(WCHAR));
    else
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error_code, 0, msg, sizeof(msg)/sizeof(WCHAR), NULL);

    MessageBoxW(hwnd, msg, NULL, MB_ICONERROR);
}

static WCHAR *expand_environment( const WCHAR *str )
{
    WCHAR *buf;
    DWORD len;

    len = ExpandEnvironmentStringsW(str, NULL, 0);
    if (!len) return NULL;

    buf = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (!buf) return NULL;

    len = ExpandEnvironmentStringsW(str, buf, len);
    if (!len)
    {
        HeapFree(GetProcessHeap(), 0, buf);
        return NULL;
    }
    return buf;
}

/*************************************************************************
 *	SHELL_execute [Internal]
 */
static BOOL SHELL_execute( LPSHELLEXECUTEINFOW sei, SHELL_ExecuteW32 execfunc )
{
    static const WCHAR wWww[] = {'w','w','w',0};
    static const WCHAR wHttp[] = {'h','t','t','p',':','/','/',0};
    static const DWORD unsupportedFlags =
        SEE_MASK_INVOKEIDLIST  | SEE_MASK_ICON         | SEE_MASK_HOTKEY |
        SEE_MASK_CONNECTNETDRV | SEE_MASK_FLAG_DDEWAIT |
        SEE_MASK_UNICODE       | SEE_MASK_ASYNCOK      | SEE_MASK_HMONITOR;

    WCHAR parametersBuffer[1024], dirBuffer[MAX_PATH], wcmdBuffer[1024];
    WCHAR *wszApplicationName, *wszParameters, *wszDir, *wcmd = NULL;
    DWORD dwApplicationNameLen = MAX_PATH+2;
    DWORD parametersLen = sizeof(parametersBuffer) / sizeof(WCHAR);
    DWORD wcmdLen = sizeof(wcmdBuffer) / sizeof(WCHAR);
    DWORD len;
    SHELLEXECUTEINFOW sei_tmp;	/* modifiable copy of SHELLEXECUTEINFO struct */
    WCHAR *env;
    WCHAR wszKeyname[256];
    LPCWSTR lpFile;
    UINT_PTR retval = SE_ERR_NOASSOC;

    /* make a local copy of the LPSHELLEXECUTEINFO structure and work with this from now on */
    sei_tmp = *sei;

    TRACE("mask=0x%08x hwnd=%p verb=%s file=%s parm=%s dir=%s show=0x%08x class=%s\n",
            sei_tmp.fMask, sei_tmp.hwnd, debugstr_w(sei_tmp.lpVerb),
            debugstr_w(sei_tmp.lpFile), debugstr_w(sei_tmp.lpParameters),
            debugstr_w(sei_tmp.lpDirectory), sei_tmp.nShow,
            ((sei_tmp.fMask & SEE_MASK_CLASSALL) == SEE_MASK_CLASSNAME) ?
                debugstr_w(sei_tmp.lpClass) : "not used");

    sei->hProcess = NULL;

    /* make copies of all path/command strings */
    if (!sei_tmp.lpFile)
    {
        wszApplicationName = HeapAlloc(GetProcessHeap(), 0, dwApplicationNameLen*sizeof(WCHAR));
        *wszApplicationName = '\0';
    }
    else if (*sei_tmp.lpFile == '\"' && sei_tmp.lpFile[(len = strlenW(sei_tmp.lpFile))-1] == '\"')
    {
        if(len-1 >= dwApplicationNameLen) dwApplicationNameLen = len;
        wszApplicationName = HeapAlloc(GetProcessHeap(), 0, dwApplicationNameLen*sizeof(WCHAR));
        memcpy(wszApplicationName, sei_tmp.lpFile+1, len*sizeof(WCHAR));
        if(len > 2)
            wszApplicationName[len-2] = '\0';
        TRACE("wszApplicationName=%s\n",debugstr_w(wszApplicationName));
    } else {
        DWORD l = strlenW(sei_tmp.lpFile)+1;
        if(l > dwApplicationNameLen) dwApplicationNameLen = l+1;
        wszApplicationName = HeapAlloc(GetProcessHeap(), 0, dwApplicationNameLen*sizeof(WCHAR));
        memcpy(wszApplicationName, sei_tmp.lpFile, l*sizeof(WCHAR));
    }

    wszParameters = parametersBuffer;
    if (sei_tmp.lpParameters)
    {
        len = lstrlenW(sei_tmp.lpParameters) + 1;
        if (len > parametersLen)
        {
            wszParameters = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
            parametersLen = len;
        }
	strcpyW(wszParameters, sei_tmp.lpParameters);
    }
    else
	*wszParameters = '\0';

    wszDir = dirBuffer;
    if (sei_tmp.lpDirectory)
    {
        len = lstrlenW(sei_tmp.lpDirectory) + 1;
        if (len > sizeof(dirBuffer) / sizeof(WCHAR))
            wszDir = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
	strcpyW(wszDir, sei_tmp.lpDirectory);
    }
    else
	*wszDir = '\0';

    /* adjust string pointers to point to the new buffers */
    sei_tmp.lpFile = wszApplicationName;
    sei_tmp.lpParameters = wszParameters;
    sei_tmp.lpDirectory = wszDir;

    if (sei_tmp.fMask & unsupportedFlags)
    {
        FIXME("flags ignored: 0x%08x\n", sei_tmp.fMask & unsupportedFlags);
    }

    /* process the IDList */
    if (sei_tmp.fMask & SEE_MASK_IDLIST)
    {
	IShellExecuteHookW* pSEH;

	HRESULT hr = SHBindToParent(sei_tmp.lpIDList, &IID_IShellExecuteHookW, (LPVOID*)&pSEH, NULL);

	if (SUCCEEDED(hr))
	{
	    hr = IShellExecuteHookW_Execute(pSEH, &sei_tmp);

	    IShellExecuteHookW_Release(pSEH);

	    if (hr == S_OK) {
                HeapFree(GetProcessHeap(), 0, wszApplicationName);
                if (wszParameters != parametersBuffer)
                    HeapFree(GetProcessHeap(), 0, wszParameters);
                if (wszDir != dirBuffer)
                    HeapFree(GetProcessHeap(), 0, wszDir);
		return TRUE;
            }
	}

        SHGetPathFromIDListW(sei_tmp.lpIDList, wszApplicationName);
        TRACE("-- idlist=%p (%s)\n", sei_tmp.lpIDList, debugstr_w(wszApplicationName));
    }

    if (sei_tmp.fMask & SEE_MASK_DOENVSUBST)
    {
        WCHAR *tmp;

        tmp = expand_environment(sei_tmp.lpFile);
        if (!tmp)
        {
            retval = SE_ERR_OOM;
            goto end;
        }
        HeapFree(GetProcessHeap(), 0, wszApplicationName);
        sei_tmp.lpFile = wszApplicationName = tmp;

        tmp = expand_environment(sei_tmp.lpDirectory);
        if (!tmp)
        {
            retval = SE_ERR_OOM;
            goto end;
        }
        if (wszDir != dirBuffer) HeapFree(GetProcessHeap(), 0, wszDir);
        sei_tmp.lpDirectory = wszDir = tmp;
    }

    if ( ERROR_SUCCESS == ShellExecute_FromContextMenu( &sei_tmp ) )
    {
        sei->hInstApp = (HINSTANCE) 33;
        HeapFree(GetProcessHeap(), 0, wszApplicationName);
        if (wszParameters != parametersBuffer)
            HeapFree(GetProcessHeap(), 0, wszParameters);
        if (wszDir != dirBuffer)
            HeapFree(GetProcessHeap(), 0, wszDir);
        return TRUE;
    }

    if (sei_tmp.fMask & SEE_MASK_CLASSALL)
    {
        retval = SHELL_execute_class( wszApplicationName, &sei_tmp, sei,
                                      execfunc );
        if (retval <= 32 && !(sei_tmp.fMask & SEE_MASK_FLAG_NO_UI))
            do_error_dialog(retval, sei_tmp.hwnd);
        HeapFree(GetProcessHeap(), 0, wszApplicationName);
        if (wszParameters != parametersBuffer)
            HeapFree(GetProcessHeap(), 0, wszParameters);
        if (wszDir != dirBuffer)
            HeapFree(GetProcessHeap(), 0, wszDir);
        return retval > 32;
    }

    /* Has the IDList not yet been translated? */
    if (sei_tmp.fMask & SEE_MASK_IDLIST)
    {
        SHELL_translate_idlist( &sei_tmp, wszParameters,
                                parametersLen,
                                wszApplicationName,
                                dwApplicationNameLen );
    }

    /* convert file URLs */
    if (UrlIsFileUrlW(sei_tmp.lpFile))
    {
        LPWSTR buf;
        DWORD size;

        size = MAX_PATH;
        buf = HeapAlloc(GetProcessHeap(), 0, size * sizeof(WCHAR));
        if (!buf || FAILED(PathCreateFromUrlW(sei_tmp.lpFile, buf, &size, 0))) {
            HeapFree(GetProcessHeap(), 0, buf);
            return SE_ERR_OOM;
        }

        HeapFree(GetProcessHeap(), 0, wszApplicationName);
        wszApplicationName = buf;
        sei_tmp.lpFile = wszApplicationName;
    }
    else /* or expand environment strings (not both!) */
    {
        len = ExpandEnvironmentStringsW(sei_tmp.lpFile, NULL, 0);
        if (len>0)
        {
            LPWSTR buf;
            buf = HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR));

            ExpandEnvironmentStringsW(sei_tmp.lpFile, buf, len + 1);
            HeapFree(GetProcessHeap(), 0, wszApplicationName);
            wszApplicationName = buf;

            sei_tmp.lpFile = wszApplicationName;
        }
    }

    if (*sei_tmp.lpDirectory)
    {
        len = ExpandEnvironmentStringsW(sei_tmp.lpDirectory, NULL, 0);
        if (len > 0)
        {
            LPWSTR buf;
            len++;
            buf = HeapAlloc(GetProcessHeap(),0,len*sizeof(WCHAR));
            ExpandEnvironmentStringsW(sei_tmp.lpDirectory, buf, len);
            if (wszDir != dirBuffer)
                HeapFree(GetProcessHeap(), 0, wszDir);
            wszDir = buf;
            sei_tmp.lpDirectory = wszDir;
        }
    }

    /* Else, try to execute the filename */
    TRACE("execute:%s,%s,%s\n", debugstr_w(wszApplicationName), debugstr_w(wszParameters), debugstr_w(wszDir));
    lpFile = sei_tmp.lpFile;
    wcmd = wcmdBuffer;
    len = lstrlenW(wszApplicationName) + 3;
    if (sei_tmp.lpParameters[0])
        len += 1 + lstrlenW(wszParameters);
    if (len > wcmdLen)
    {
        wcmd = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
        wcmdLen = len;
    }
    wcmd[0] = '\"';
    len = lstrlenW(wszApplicationName);
    memcpy(wcmd+1, wszApplicationName, len * sizeof(WCHAR));
    len++;
    wcmd[len++] = '\"';
    wcmd[len] = 0;
    if (sei_tmp.lpParameters[0]) {
        wcmd[len++] = ' ';
        strcpyW(wcmd+len, wszParameters);
    }

    retval = execfunc(wcmd, NULL, FALSE, &sei_tmp, sei);
    if (retval > 32) {
        HeapFree(GetProcessHeap(), 0, wszApplicationName);
        if (wszParameters != parametersBuffer)
            HeapFree(GetProcessHeap(), 0, wszParameters);
        if (wszDir != dirBuffer)
            HeapFree(GetProcessHeap(), 0, wszDir);
        if (wcmd != wcmdBuffer)
            HeapFree(GetProcessHeap(), 0, wcmd);
        return TRUE;
    }

    /* Else, try to find the executable */
    wcmd[0] = '\0';
    retval = SHELL_FindExecutable(sei_tmp.lpDirectory, lpFile, sei_tmp.lpVerb, wcmd, wcmdLen, wszKeyname, &env, sei_tmp.lpIDList, sei_tmp.lpParameters);
    if (retval > 32)  /* Found */
    {
        retval = SHELL_quote_and_execute( wcmd, wszParameters, wszKeyname,
                                          wszApplicationName, env, &sei_tmp,
                                          sei, execfunc );
        HeapFree( GetProcessHeap(), 0, env );
    }
    else if (PathIsDirectoryW(lpFile))
    {
        static const WCHAR wExplorer[] = {'e','x','p','l','o','r','e','r',0};
        static const WCHAR wQuote[] = {'"',0};
        WCHAR wExec[MAX_PATH];
        WCHAR * lpQuotedFile = HeapAlloc( GetProcessHeap(), 0, sizeof(WCHAR) * (strlenW(lpFile) + 3) );

        if (lpQuotedFile)
        {
            retval = SHELL_FindExecutable( sei_tmp.lpDirectory, wExplorer,
                                           wszOpen, wExec, MAX_PATH,
                                           NULL, &env, NULL, NULL );
            if (retval > 32)
            {
                strcpyW(lpQuotedFile, wQuote);
                strcatW(lpQuotedFile, lpFile);
                strcatW(lpQuotedFile, wQuote);
                retval = SHELL_quote_and_execute( wExec, lpQuotedFile,
                                                  wszKeyname,
                                                  wszApplicationName, env,
                                                  &sei_tmp, sei, execfunc );
                HeapFree( GetProcessHeap(), 0, env );
            }
            HeapFree( GetProcessHeap(), 0, lpQuotedFile );
        }
        else
            retval = 0; /* Out of memory */
    }
    else if (PathIsURLW(lpFile))    /* File not found, check for URL */
    {
        retval = SHELL_execute_url( lpFile, wcmd, &sei_tmp, sei, execfunc );
    }
    /* Check if file specified is in the form www.??????.*** */
    else if (!strncmpiW(lpFile, wWww, 3))
    {
        /* if so, prefix lpFile with http:// and call ShellExecute */
        WCHAR lpstrTmpFile[256];
        strcpyW(lpstrTmpFile, wHttp);
        strcatW(lpstrTmpFile, lpFile);
        retval = (UINT_PTR)ShellExecuteW(sei_tmp.hwnd, sei_tmp.lpVerb, lpstrTmpFile, NULL, NULL, 0);
    }

end:
    TRACE("retval %lu\n", retval);

    HeapFree(GetProcessHeap(), 0, wszApplicationName);
    if (wszParameters != parametersBuffer)
        HeapFree(GetProcessHeap(), 0, wszParameters);
    if (wszDir != dirBuffer)
        HeapFree(GetProcessHeap(), 0, wszDir);
    if (wcmd != wcmdBuffer)
        HeapFree(GetProcessHeap(), 0, wcmd);

    sei->hInstApp = (HINSTANCE)(retval > 32 ? 33 : retval);

    if (retval <= 32 && !(sei_tmp.fMask & SEE_MASK_FLAG_NO_UI))
        do_error_dialog(retval, sei_tmp.hwnd);
    return retval > 32;
}

/*************************************************************************
 * ShellExecuteA			[SHELL32.290]
 */
HINSTANCE WINAPI ShellExecuteA(HWND hWnd, LPCSTR lpVerb, LPCSTR lpFile,
                               LPCSTR lpParameters, LPCSTR lpDirectory, INT iShowCmd)
{
    SHELLEXECUTEINFOA sei;

    TRACE("%p,%s,%s,%s,%s,%d\n",
          hWnd, debugstr_a(lpVerb), debugstr_a(lpFile),
          debugstr_a(lpParameters), debugstr_a(lpDirectory), iShowCmd);

    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.hwnd = hWnd;
    sei.lpVerb = lpVerb;
    sei.lpFile = lpFile;
    sei.lpParameters = lpParameters;
    sei.lpDirectory = lpDirectory;
    sei.nShow = iShowCmd;
    sei.lpIDList = 0;
    sei.lpClass = 0;
    sei.hkeyClass = 0;
    sei.dwHotKey = 0;
    sei.hProcess = 0;

    ShellExecuteExA (&sei);
    return sei.hInstApp;
}

/*************************************************************************
 * ShellExecuteExA				[SHELL32.292]
 *
 */
BOOL WINAPI DECLSPEC_HOTPATCH ShellExecuteExA (LPSHELLEXECUTEINFOA sei)
{
    SHELLEXECUTEINFOW seiW;
    BOOL ret;
    WCHAR *wVerb = NULL, *wFile = NULL, *wParameters = NULL, *wDirectory = NULL, *wClass = NULL;

    TRACE("%p\n", sei);

    memcpy(&seiW, sei, sizeof(SHELLEXECUTEINFOW));

    if (sei->lpVerb)
	seiW.lpVerb = __SHCloneStrAtoW(&wVerb, sei->lpVerb);

    if (sei->lpFile)
        seiW.lpFile = __SHCloneStrAtoW(&wFile, sei->lpFile);

    if (sei->lpParameters)
        seiW.lpParameters = __SHCloneStrAtoW(&wParameters, sei->lpParameters);

    if (sei->lpDirectory)
        seiW.lpDirectory = __SHCloneStrAtoW(&wDirectory, sei->lpDirectory);

    if ((sei->fMask & SEE_MASK_CLASSALL) == SEE_MASK_CLASSNAME && sei->lpClass)
        seiW.lpClass = __SHCloneStrAtoW(&wClass, sei->lpClass);
    else
        seiW.lpClass = NULL;

    ret = SHELL_execute( &seiW, SHELL_ExecuteW );

    sei->hInstApp = seiW.hInstApp;

    if (sei->fMask & SEE_MASK_NOCLOSEPROCESS)
        sei->hProcess = seiW.hProcess;

    SHFree(wVerb);
    SHFree(wFile);
    SHFree(wParameters);
    SHFree(wDirectory);
    SHFree(wClass);

    return ret;
}

/*************************************************************************
 * ShellExecuteExW				[SHELL32.293]
 *
 */
BOOL WINAPI DECLSPEC_HOTPATCH ShellExecuteExW (LPSHELLEXECUTEINFOW sei)
{
    return SHELL_execute( sei, SHELL_ExecuteW );
}

/*************************************************************************
 * ShellExecuteW			[SHELL32.294]
 * from shellapi.h
 * WINSHELLAPI HINSTANCE APIENTRY ShellExecuteW(HWND hwnd, LPCWSTR lpVerb,
 * LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd);
 */
HINSTANCE WINAPI ShellExecuteW(HWND hwnd, LPCWSTR lpVerb, LPCWSTR lpFile,
                               LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd)
{
    SHELLEXECUTEINFOW sei;

    TRACE("\n");
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.hwnd = hwnd;
    sei.lpVerb = lpVerb;
    sei.lpFile = lpFile;
    sei.lpParameters = lpParameters;
    sei.lpDirectory = lpDirectory;
    sei.nShow = nShowCmd;
    sei.lpIDList = 0;
    sei.lpClass = 0;
    sei.hkeyClass = 0;
    sei.dwHotKey = 0;
    sei.hProcess = 0;

    SHELL_execute( &sei, SHELL_ExecuteW );
    return sei.hInstApp;
}

/*************************************************************************
 * WOWShellExecute			[SHELL32.@]
 *
 * FIXME: the callback function most likely doesn't work the same way on Windows.
 */
HINSTANCE WINAPI WOWShellExecute(HWND hWnd, LPCSTR lpVerb,LPCSTR lpFile,
                                 LPCSTR lpParameters,LPCSTR lpDirectory, INT iShowCmd, void *callback)
{
    SHELLEXECUTEINFOW seiW;
    WCHAR *wVerb = NULL, *wFile = NULL, *wParameters = NULL, *wDirectory = NULL;
    HANDLE hProcess = 0;

    seiW.lpVerb = lpVerb ? __SHCloneStrAtoW(&wVerb, lpVerb) : NULL;
    seiW.lpFile = lpFile ? __SHCloneStrAtoW(&wFile, lpFile) : NULL;
    seiW.lpParameters = lpParameters ? __SHCloneStrAtoW(&wParameters, lpParameters) : NULL;
    seiW.lpDirectory = lpDirectory ? __SHCloneStrAtoW(&wDirectory, lpDirectory) : NULL;

    seiW.cbSize = sizeof(seiW);
    seiW.fMask = 0;
    seiW.hwnd = hWnd;
    seiW.nShow = iShowCmd;
    seiW.lpIDList = 0;
    seiW.lpClass = 0;
    seiW.hkeyClass = 0;
    seiW.dwHotKey = 0;
    seiW.hProcess = hProcess;

    SHELL_execute( &seiW, callback );

    SHFree(wVerb);
    SHFree(wFile);
    SHFree(wParameters);
    SHFree(wDirectory);
    return seiW.hInstApp;
}

/*************************************************************************
 * OpenAs_RunDLLA          [SHELL32.@]
 */
void WINAPI OpenAs_RunDLLA(HWND hwnd, HINSTANCE hinst, LPCSTR cmdline, int cmdshow)
{
    FIXME("%p, %p, %s, %d\n", hwnd, hinst, debugstr_a(cmdline), cmdshow);
}

/*************************************************************************
 * OpenAs_RunDLLW          [SHELL32.@]
 */
void WINAPI OpenAs_RunDLLW(HWND hwnd, HINSTANCE hinst, LPCWSTR cmdline, int cmdshow)
{
    FIXME("%p, %p, %s, %d\n", hwnd, hinst, debugstr_w(cmdline), cmdshow);
}

/*************************************************************************
 * RegenerateUserEnvironment          [SHELL32.@]
 */
BOOL WINAPI RegenerateUserEnvironment(WCHAR *wunknown, BOOL bunknown)
{
    FIXME("stub: %p, %d\n", wunknown, bunknown);
    return FALSE;
}

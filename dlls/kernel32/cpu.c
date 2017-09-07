/*
 * What processor?
 *
 * Copyright 1995,1997 Morten Welinder
 * Copyright 1997-1998 Marcus Meissner
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif


#define NONAMELESSUNION
#define NONAMELESSSTRUCT
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "psapi.h"
#include "ddk/wdm.h"
#include "wine/unicode.h"
#include "kernel_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(reg);

extern KSHARED_USER_DATA* CDECL __wine_user_shared_data(void);

/****************************************************************************
 *		QueryPerformanceCounter (KERNEL32.@)
 *
 * Get the current value of the performance counter.
 * 
 * PARAMS
 *  counter [O] Destination for the current counter reading
 *
 * RETURNS
 *  Success: TRUE. counter contains the current reading
 *  Failure: FALSE.
 *
 * SEE ALSO
 *  See QueryPerformanceFrequency.
 */
BOOL WINAPI QueryPerformanceCounter(PLARGE_INTEGER counter)
{
    NtQueryPerformanceCounter( counter, NULL );
    return TRUE;
}


/****************************************************************************
 *		QueryPerformanceFrequency (KERNEL32.@)
 *
 * Get the resolution of the performance counter.
 *
 * PARAMS
 *  frequency [O] Destination for the counter resolution
 *
 * RETURNS
 *  Success. TRUE. Frequency contains the resolution of the counter.
 *  Failure: FALSE.
 *
 * SEE ALSO
 *  See QueryPerformanceCounter.
 */
BOOL WINAPI QueryPerformanceFrequency(PLARGE_INTEGER frequency)
{
    LARGE_INTEGER counter;
    NtQueryPerformanceCounter( &counter, frequency );
    return TRUE;
}


/***********************************************************************
 * 			GetSystemInfo            	[KERNEL32.@]
 *
 * Get information about the system.
 *
 * RETURNS
 *  Nothing.
 */
VOID WINAPI GetSystemInfo(
	LPSYSTEM_INFO si	/* [out] Destination for system information, may not be NULL */)
{
    NTSTATUS                 nts;
    SYSTEM_CPU_INFORMATION   sci;

    TRACE("si=0x%p\n", si);

    if ((nts = NtQuerySystemInformation( SystemCpuInformation, &sci, sizeof(sci), NULL )) != STATUS_SUCCESS)
    {
        SetLastError(RtlNtStatusToDosError(nts));
        return;
    }

    si->u.s.wProcessorArchitecture  = sci.Architecture;
    si->u.s.wReserved               = 0;
    si->dwPageSize                  = system_info.PageSize;
    si->lpMinimumApplicationAddress = system_info.LowestUserAddress;
    si->lpMaximumApplicationAddress = system_info.HighestUserAddress;
    si->dwActiveProcessorMask       = system_info.ActiveProcessorsAffinityMask;
    si->dwNumberOfProcessors        = system_info.NumberOfProcessors;

    switch (sci.Architecture)
    {
    case PROCESSOR_ARCHITECTURE_INTEL:
        switch (sci.Level)
        {
        case 3:  si->dwProcessorType = PROCESSOR_INTEL_386;     break;
        case 4:  si->dwProcessorType = PROCESSOR_INTEL_486;     break;
        case 5:
        case 6:  si->dwProcessorType = PROCESSOR_INTEL_PENTIUM; break;
        default: si->dwProcessorType = PROCESSOR_INTEL_PENTIUM; break;
        }
        break;
    case PROCESSOR_ARCHITECTURE_PPC:
        switch (sci.Level)
        {
        case 1:  si->dwProcessorType = PROCESSOR_PPC_601;       break;
        case 3:
        case 6:  si->dwProcessorType = PROCESSOR_PPC_603;       break;
        case 4:  si->dwProcessorType = PROCESSOR_PPC_604;       break;
        case 9:  si->dwProcessorType = PROCESSOR_PPC_604;       break;
        case 20: si->dwProcessorType = PROCESSOR_PPC_620;       break;
        default: si->dwProcessorType = 0;
        }
        break;
    case PROCESSOR_ARCHITECTURE_AMD64:
        si->dwProcessorType = PROCESSOR_AMD_X8664;
        break;
    case PROCESSOR_ARCHITECTURE_ARM:
        switch (sci.Level)
        {
        case 4:  si->dwProcessorType = PROCESSOR_ARM_7TDMI;     break;
        default: si->dwProcessorType = PROCESSOR_ARM920;
        }
        break;
    default:
        FIXME("Unknown processor architecture %x\n", sci.Architecture);
        si->dwProcessorType = 0;
    }
    si->dwAllocationGranularity     = system_info.AllocationGranularity;
    si->wProcessorLevel             = sci.Level;
    si->wProcessorRevision          = sci.Revision;
}


/***********************************************************************
 * 			GetNativeSystemInfo            	[KERNEL32.@]
 */
VOID WINAPI GetNativeSystemInfo(
    LPSYSTEM_INFO si	/* [out] Destination for system information, may not be NULL */)
{
    BOOL is_wow64;

    GetSystemInfo(si); 

    IsWow64Process(GetCurrentProcess(), &is_wow64);
    if (is_wow64)
    {
        if (si->u.s.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
        {
            si->u.s.wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
            si->dwProcessorType = PROCESSOR_AMD_X8664;
        }
        else
        {
            FIXME("Add the proper information for %d in wow64 mode\n",
                  si->u.s.wProcessorArchitecture);
        }
    }
}

/***********************************************************************
 * 			IsProcessorFeaturePresent	[KERNEL32.@]
 *
 * Determine if the cpu supports a given feature.
 * 
 * RETURNS
 *  TRUE, If the processor supports feature,
 *  FALSE otherwise.
 */
BOOL WINAPI IsProcessorFeaturePresent (
	DWORD feature	/* [in] Feature number, (PF_ constants from "winnt.h") */) 
{
  if (feature < PROCESSOR_FEATURE_MAX)
    return __wine_user_shared_data()->ProcessorFeatures[feature];
  else
    return FALSE;
}

/***********************************************************************
 *           K32GetPerformanceInfo (KERNEL32.@)
 */
BOOL WINAPI K32GetPerformanceInfo(PPERFORMANCE_INFORMATION info, DWORD size)
{
    SYSTEM_PERFORMANCE_INFORMATION performance;
    SYSTEM_BASIC_INFORMATION basic;
    NTSTATUS status;

    TRACE( "(%p, %d)\n", info, size );

    if (size < sizeof(*info))
    {
        SetLastError( ERROR_BAD_LENGTH );
        return FALSE;
    }

    memset( info, 0, sizeof(*info) );
    info->cb = sizeof(*info);

    SERVER_START_REQ( get_system_info )
    {
        status = wine_server_call( req );
        if (!status)
        {
            info->ProcessCount = reply->processes;
            info->HandleCount = reply->handles;
            info->ThreadCount = reply->threads;
        }
    }
    SERVER_END_REQ;

    if (status) goto err;

    /* fields from SYSTEM_PERFORMANCE_INFORMATION */
    status = NtQuerySystemInformation( SystemPerformanceInformation, &performance,
                                       sizeof(performance), NULL );
    if (status) goto err;
    info->CommitTotal        = performance.TotalCommittedPages;
    info->CommitLimit        = performance.TotalCommitLimit;
    info->CommitPeak         = performance.PeakCommitment;
    info->PhysicalAvailable  = performance.AvailablePages;
    info->KernelTotal        = performance.PagedPoolUsage +
                               performance.NonPagedPoolUsage;
    info->KernelPaged        = performance.PagedPoolUsage;
    info->KernelNonpaged     = performance.NonPagedPoolUsage;

    /* fields from SYSTEM_BASIC_INFORMATION */
    status = NtQuerySystemInformation( SystemBasicInformation, &basic,
                                       sizeof(basic), NULL );
    if (status) goto err;
    info->PhysicalTotal = basic.MmNumberOfPhysicalPages;
    info->PageSize      = basic.PageSize;

err:
    if (status)
    {
        SetLastError( RtlNtStatusToDosError( status ) );
        return FALSE;
    }
    return TRUE;
}

/***********************************************************************
 *           GetLargePageMinimum (KERNEL32.@)
 */
SIZE_T WINAPI GetLargePageMinimum(void)
{
#if defined(__i386__) || defined(__x86_64__) || defined(__arm__)
    return 2 * 1024 * 1024;
#endif
    FIXME("Not implemented on your platform/architecture.\n");
    return 0;
}

/***********************************************************************
 *           GetActiveProcessorGroupCount (KERNEL32.@)
 */
WORD WINAPI GetActiveProcessorGroupCount(void)
{
    TRACE("()\n");

    /* systems with less than 64 logical processors only have group 0 */
    return 1;
}

/***********************************************************************
 *           GetActiveProcessorCount (KERNEL32.@)
 */
DWORD WINAPI GetActiveProcessorCount(WORD group)
{
    TRACE("(%u)\n", group);

    if (group && group != ALL_PROCESSOR_GROUPS)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    return system_info.NumberOfProcessors;
}

/***********************************************************************
 *           GetMaximumProcessorGroupCount (KERNEL32.@)
 */
WORD WINAPI GetMaximumProcessorGroupCount(void)
{
    TRACE("()\n");

    /* systems with less than 64 logical processors only have group 0 */
    return 1;
}

/***********************************************************************
 *           GetMaximumProcessorCount (KERNEL32.@)
 */
DWORD WINAPI GetMaximumProcessorCount(WORD group)
{
    TRACE("(%u)\n", group);

    if (group && group != ALL_PROCESSOR_GROUPS)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    /* Wine only counts active processors so far */
    return system_info.NumberOfProcessors;
}

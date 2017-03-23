/* ipstats.h
 * Copyright (C) 2003,2006 Juan Lang
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
 *
 * This module implements functions that get network-related statistics.
 * It's meant to hide some platform-specificisms.
 */
#ifndef WINE_IPSTATS_H_
#define WINE_IPSTATS_H_

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "iprtrmib.h"

/* Fills in entry's interface stats, using name to find them.
 * Returns ERROR_INVALID_PARAMETER if name or entry is NULL, NO_ERROR otherwise.
 */
DWORD getInterfaceStatsByName(const char *name, PMIB_IFROW entry) DECLSPEC_HIDDEN;

DWORD WINAPI AllocateAndGetUdpTableFromStack(PMIB_UDPTABLE *ppUdpTable, BOOL bOrder, HANDLE heap, DWORD flags) DECLSPEC_HIDDEN;
DWORD WINAPI AllocateAndGetTcpTableFromStack(PMIB_TCPTABLE *ppTcpTable, BOOL bOrder, HANDLE heap, DWORD flags) DECLSPEC_HIDDEN;
DWORD WINAPI AllocateAndGetTcpExTableFromStack(VOID **ppTcpTable, BOOL bOrder, HANDLE heap, DWORD flags, DWORD family) DECLSPEC_HIDDEN;
DWORD WINAPI AllocateAndGetIpNetTableFromStack(PMIB_IPNETTABLE *ppIpNetTable, BOOL bOrder, HANDLE heap, DWORD flags) DECLSPEC_HIDDEN;
DWORD WINAPI AllocateAndGetIpForwardTableFromStack(PMIB_IPFORWARDTABLE *ppIpForwardTable, BOOL bOrder, HANDLE heap, DWORD flags) DECLSPEC_HIDDEN;

DWORD build_tcp_table(TCP_TABLE_CLASS, void **, BOOL, HANDLE, DWORD, DWORD *) DECLSPEC_HIDDEN;
DWORD build_udp_table(UDP_TABLE_CLASS, void **, BOOL, HANDLE, DWORD, DWORD *) DECLSPEC_HIDDEN;

#endif /* ndef WINE_IPSTATS_H_ */

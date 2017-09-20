/* Unit test suite for Rtl large integer functions
 *
 * Copyright 2003 Thomas Mertes
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
 * NOTES
 * We use function pointers here as there is no import library for NTDLL on
 * windows.
 */

#include <stdlib.h>

#include "ntdll_test.h"


/* Function ptrs for ntdll calls */
static HMODULE hntdll = 0;
static LONGLONG (WINAPI *pRtlExtendedMagicDivide)(LONGLONG, LONGLONG, INT);
static VOID     (WINAPI *pRtlFreeAnsiString)(PSTRING);
static NTSTATUS (WINAPI *pRtlInt64ToUnicodeString)(ULONGLONG, ULONG, UNICODE_STRING *);
static NTSTATUS (WINAPI *pRtlLargeIntegerToChar)(ULONGLONG *, ULONG, ULONG, PCHAR);
static NTSTATUS (WINAPI *pRtlUnicodeStringToAnsiString)(STRING *, const UNICODE_STRING *, BOOLEAN);
static void     (WINAPI *p_alldvrm)(LONGLONG, LONGLONG);
static void     (WINAPI *p_aulldvrm)(ULONGLONG, ULONGLONG);


static void InitFunctionPtrs(void)
{
    hntdll = LoadLibraryA("ntdll.dll");
    ok(hntdll != 0, "LoadLibrary failed\n");
    if (hntdll) {
	pRtlExtendedMagicDivide = (void *)GetProcAddress(hntdll, "RtlExtendedMagicDivide");
	pRtlFreeAnsiString = (void *)GetProcAddress(hntdll, "RtlFreeAnsiString");
	pRtlInt64ToUnicodeString = (void *)GetProcAddress(hntdll, "RtlInt64ToUnicodeString");
	pRtlLargeIntegerToChar = (void *)GetProcAddress(hntdll, "RtlLargeIntegerToChar");
	pRtlUnicodeStringToAnsiString = (void *)GetProcAddress(hntdll, "RtlUnicodeStringToAnsiString");
        p_alldvrm = (void *)GetProcAddress(hntdll, "_alldvrm");
        p_aulldvrm = (void *)GetProcAddress(hntdll, "_aulldvrm");
    } /* if */
}

#define ULL(a,b) (((ULONGLONG)(a) << 32) | (b))

typedef struct {
    LONGLONG a;
    LONGLONG b;
    INT shift;
    LONGLONG result;
} magic_divide_t;

static const magic_divide_t magic_divide[] = {
    {                          3,  ULL(0x55555555,0x55555555), 0,                   0}, /* 1 */
    {                  333333333,  ULL(0x55555555,0x55555555), 0,           111111110}, /* 111111111 */
    { ULL(0x7fffffff,0xffffffff),  ULL(0x55555555,0x55555555), 0,  ULL(0x2aaaaaaa,0xaaaaaaaa)},
    {                          3,  ULL(0xaaaaaaaa,0xaaaaaaaa), 1,                   0}, /* 1 */
    {                  333333333,  ULL(0xaaaaaaaa,0xaaaaaaaa), 1,           111111110}, /* 111111111 */
    { ULL(0x7fffffff,0xffffffff),  ULL(0xaaaaaaaa,0xaaaaaaaa), 1,  ULL(0x2aaaaaaa,0xaaaaaaaa)},
    {                         -3,  ULL(0x55555555,0x55555555), 0,                   0}, /* -1 */
    {                 -333333333,  ULL(0x55555555,0x55555555), 0,          -111111110}, /* -111111111 */
    {-ULL(0x7fffffff,0xffffffff),  ULL(0x55555555,0x55555555), 0, -ULL(0x2aaaaaaa,0xaaaaaaaa)},
    {                         -3,  ULL(0xaaaaaaaa,0xaaaaaaaa), 1,                   0}, /* -1 */
    {                 -333333333,  ULL(0xaaaaaaaa,0xaaaaaaaa), 1,          -111111110}, /* -111111111 */
    {-ULL(0x7fffffff,0xffffffff),  ULL(0xaaaaaaaa,0xaaaaaaaa), 1, -ULL(0x2aaaaaaa,0xaaaaaaaa)},
    {                         -3, -ULL(0x55555555,0x55555555), 0,                  -2}, /* -1 */
    {                 -333333333, -ULL(0x55555555,0x55555555), 0,          -222222222}, /* -111111111 */
    {-ULL(0x7fffffff,0xffffffff), -ULL(0x55555555,0x55555555), 0, -ULL(0x55555555,0x55555554)},
    {                         -3, -ULL(0xaaaaaaaa,0xaaaaaaaa), 1,                   0}, /* -1 */
    {                 -333333333, -ULL(0xaaaaaaaa,0xaaaaaaaa), 1,           -55555555}, /* -111111111 */
    {-ULL(0x7fffffff,0xffffffff), -ULL(0xaaaaaaaa,0xaaaaaaaa), 1, -ULL(0x15555555,0x55555555)},
    {                          3, -ULL(0x55555555,0x55555555), 0,                   2}, /* -1 */
    {                  333333333, -ULL(0x55555555,0x55555555), 0,           222222222}, /* -111111111 */
    { ULL(0x7fffffff,0xffffffff), -ULL(0x55555555,0x55555555), 0,  ULL(0x55555555,0x55555554)},
    {                          3, -ULL(0xaaaaaaaa,0xaaaaaaaa), 1,                   0}, /* -1 */
    {                  333333333, -ULL(0xaaaaaaaa,0xaaaaaaaa), 1,            55555555}, /* -111111111 */
    { ULL(0x7fffffff,0xffffffff), -ULL(0xaaaaaaaa,0xaaaaaaaa), 1,  ULL(0x15555555,0x55555555)},
    {                          3,  ULL(0xaaaaaaaa,0xaaaaa800), 1,                   0}, /* 1 */
    {                  333333333,  ULL(0xaaaaaaaa,0xaaaaa800), 1,           111111110}, /* 111111111 */
    { ULL(0x7fffffff,0xffffffff),  ULL(0xaaaaaaaa,0xaaaaa800), 1,  ULL(0x2aaaaaaa,0xaaaaa9ff)}, /* 0x2aaaaaaaaaaaaaaa */
    {                          5,  ULL(0x33333333,0x333333ff), 0,                   1},
    {                  555555555,  ULL(0x33333333,0x333333ff), 0,           111111111},
    { ULL(0x7fffffff,0xffffffff),  ULL(0x33333333,0x333333ff), 0,  ULL(0x19999999,0x999999ff)}, /* 0x199999999999999a */
    {                          5,  ULL(0x66666666,0x666667fe), 1,                   1},
    {                  555555555,  ULL(0x66666666,0x666667fe), 1,           111111111},
    { ULL(0x7fffffff,0xffffffff),  ULL(0x66666666,0x666667fe), 1,  ULL(0x19999999,0x999999ff)}, /* 0x199999999999999a */
    {                          5,  ULL(0xcccccccc,0xcccccffd), 2,                   1},
    {                  555555555,  ULL(0xcccccccc,0xcccccffd), 2,           111111111},
    { ULL(0x7fffffff,0xffffffff),  ULL(0xcccccccc,0xcccccffd), 2,  ULL(0x19999999,0x999999ff)}, /* 0x199999999999999a */
    { ULL(0x00000add,0xcafeface),  ULL(0x002f1e28,0xfd1b5cca), 33,                  1},
    { ULL(0x081ac1b9,0xc2310a80),  ULL(0x002f1e28,0xfd1b5cca), 33,             0xbeef},
    { ULL(0x74ae3b5f,0x1558c800),  ULL(0x002f1e28,0xfd1b5cca), 33,            0xabcde},
    { ULL(0x00000add,0xcafeface),  ULL(0x2f1e28fd,0x1b5cca00), 41,                  1},
    { ULL(0x081ac1b9,0xc2310a80),  ULL(0x2f1e28fd,0x1b5cca00), 41,             0xbeef},
    { ULL(0x74ae3b5f,0x1558c800),  ULL(0x2f1e28fd,0x1b5cca00), 41,            0xabcde},

};
#define NB_MAGIC_DIVIDE (sizeof(magic_divide)/sizeof(*magic_divide))


static void test_RtlExtendedMagicDivide(void)
{
    int i;
    LONGLONG result;

    for (i = 0; i < NB_MAGIC_DIVIDE; i++) {
	result = pRtlExtendedMagicDivide(magic_divide[i].a, magic_divide[i].b, magic_divide[i].shift);
	ok(result == magic_divide[i].result,
           "call failed: RtlExtendedMagicDivide(0x%s, 0x%s, %d) has result 0x%s, expected 0x%s\n",
	   wine_dbgstr_longlong(magic_divide[i].a), wine_dbgstr_longlong(magic_divide[i].b), magic_divide[i].shift,
       wine_dbgstr_longlong(result), wine_dbgstr_longlong(magic_divide[i].result));
    }
}


#define LARGE_STRI_BUFFER_LENGTH 67

typedef struct {
    int base;
    ULONGLONG value;
    USHORT Length;
    USHORT MaximumLength;
    const char *Buffer;
    NTSTATUS result;
} largeint2str_t;

/*
 * The native DLL does produce garbage or STATUS_BUFFER_OVERFLOW for
 * base 2, 8 and 16 when the value is larger than 0xFFFFFFFF.
 * Therefore these testcases are commented out.
 */

static const largeint2str_t largeint2str[] = {
    {10,          123,  3, 11, "123\0---------------------------------------------------------------", STATUS_SUCCESS},

    { 0,  0x80000000U, 10, 11, "2147483648\0--------------------------------------------------------", STATUS_SUCCESS},
    { 0,  -2147483647, 20, 21, "18446744071562067969\0----------------------------------------------", STATUS_SUCCESS},
    { 0,           -2, 20, 21, "18446744073709551614\0----------------------------------------------", STATUS_SUCCESS},
    { 0,           -1, 20, 21, "18446744073709551615\0----------------------------------------------", STATUS_SUCCESS},
    { 0,            0,  1, 11, "0\0-----------------------------------------------------------------", STATUS_SUCCESS},
    { 0,            1,  1, 11, "1\0-----------------------------------------------------------------", STATUS_SUCCESS},
    { 0,           12,  2, 11, "12\0----------------------------------------------------------------", STATUS_SUCCESS},
    { 0,          123,  3, 11, "123\0---------------------------------------------------------------", STATUS_SUCCESS},
    { 0,         1234,  4, 11, "1234\0--------------------------------------------------------------", STATUS_SUCCESS},
    { 0,        12345,  5, 11, "12345\0-------------------------------------------------------------", STATUS_SUCCESS},
    { 0,       123456,  6, 11, "123456\0------------------------------------------------------------", STATUS_SUCCESS},
    { 0,      1234567,  7, 11, "1234567\0-----------------------------------------------------------", STATUS_SUCCESS},
    { 0,     12345678,  8, 11, "12345678\0----------------------------------------------------------", STATUS_SUCCESS},
    { 0,    123456789,  9, 11, "123456789\0---------------------------------------------------------", STATUS_SUCCESS},
    { 0,   2147483646, 10, 11, "2147483646\0--------------------------------------------------------", STATUS_SUCCESS},
    { 0,   2147483647, 10, 11, "2147483647\0--------------------------------------------------------", STATUS_SUCCESS},
    { 0,  2147483648U, 10, 11, "2147483648\0--------------------------------------------------------", STATUS_SUCCESS},
    { 0,  2147483649U, 10, 11, "2147483649\0--------------------------------------------------------", STATUS_SUCCESS},
    { 0,  4294967294U, 10, 11, "4294967294\0--------------------------------------------------------", STATUS_SUCCESS},
    { 0,  4294967295U, 10, 11, "4294967295\0--------------------------------------------------------", STATUS_SUCCESS},
    { 0,  ULL(0x2,0xdfdc1c35), 11, 12, "12345678901\0-------------------------------------------------------", STATUS_SUCCESS},
    { 0,  ULL(0xe5,0xf4c8f374), 12, 13, "987654321012\0------------------------------------------------------", STATUS_SUCCESS},
    { 0,  ULL(0x1c0,0xfc161e3e), 13, 14, "1928374656574\0-----------------------------------------------------", STATUS_SUCCESS},
    { 0, ULL(0xbad,0xcafeface), 14, 15, "12841062955726\0----------------------------------------------------", STATUS_SUCCESS},
    { 0, ULL(0x5bad,0xcafeface), 15, 16, "100801993177806\0---------------------------------------------------", STATUS_SUCCESS},
    { 0, ULL(0xaface,0xbeefcafe), 16, 20, "3090515640699646\0--------------------------------------------------", STATUS_SUCCESS},
    { 0, ULL(0xa5beef,0xabcdcafe), 17, 20, "46653307746110206\0-------------------------------------------------", STATUS_SUCCESS},
    { 0, ULL(0x1f8cf9b,0xf2df3af1), 18, 20, "142091656963767025\0------------------------------------------------", STATUS_SUCCESS},
    { 0, ULL(0x0fffffff,0xffffffff), 19, 20, "1152921504606846975\0-----------------------------------------------", STATUS_SUCCESS},
    { 0, ULL(0xffffffff,0xfffffffe), 20, 21, "18446744073709551614\0----------------------------------------------", STATUS_SUCCESS},
    { 0, ULL(0xffffffff,0xffffffff), 20, 21, "18446744073709551615\0----------------------------------------------", STATUS_SUCCESS},

    { 2,  0x80000000U, 32, 33, "10000000000000000000000000000000\0----------------------------------", STATUS_SUCCESS},
/*
 *  { 2,  -2147483647, 64, 65, "1111111111111111111111111111111110000000000000000000000000000001\0--", STATUS_SUCCESS},
 *  { 2,           -2, 64, 65, "1111111111111111111111111111111111111111111111111111111111111110\0--", STATUS_SUCCESS},
 *  { 2,           -1, 64, 65, "1111111111111111111111111111111111111111111111111111111111111111\0--", STATUS_SUCCESS},
 */
    { 2,            0,  1, 33, "0\0-----------------------------------------------------------------", STATUS_SUCCESS},
    { 2,            1,  1, 33, "1\0-----------------------------------------------------------------", STATUS_SUCCESS},
    { 2,           10,  4, 33, "1010\0--------------------------------------------------------------", STATUS_SUCCESS},
    { 2,          100,  7, 33, "1100100\0-----------------------------------------------------------", STATUS_SUCCESS},
    { 2,         1000, 10, 33, "1111101000\0--------------------------------------------------------", STATUS_SUCCESS},
    { 2,        10000, 14, 33, "10011100010000\0----------------------------------------------------", STATUS_SUCCESS},
    { 2,        32767, 15, 33, "111111111111111\0---------------------------------------------------", STATUS_SUCCESS},
    { 2,        32768, 16, 33, "1000000000000000\0--------------------------------------------------", STATUS_SUCCESS},
    { 2,        65535, 16, 33, "1111111111111111\0--------------------------------------------------", STATUS_SUCCESS},
    { 2,       100000, 17, 33, "11000011010100000\0-------------------------------------------------", STATUS_SUCCESS},
    { 2,      1000000, 20, 33, "11110100001001000000\0----------------------------------------------", STATUS_SUCCESS},
    { 2,     10000000, 24, 33, "100110001001011010000000\0------------------------------------------", STATUS_SUCCESS},
    { 2,    100000000, 27, 33, "101111101011110000100000000\0---------------------------------------", STATUS_SUCCESS},
    { 2,   1000000000, 30, 33, "111011100110101100101000000000\0------------------------------------", STATUS_SUCCESS},
    { 2,   1073741823, 30, 33, "111111111111111111111111111111\0------------------------------------", STATUS_SUCCESS},
    { 2,   2147483646, 31, 33, "1111111111111111111111111111110\0-----------------------------------", STATUS_SUCCESS},
    { 2,   2147483647, 31, 33, "1111111111111111111111111111111\0-----------------------------------", STATUS_SUCCESS},
    { 2,  2147483648U, 32, 33, "10000000000000000000000000000000\0----------------------------------", STATUS_SUCCESS},
    { 2,  2147483649U, 32, 33, "10000000000000000000000000000001\0----------------------------------", STATUS_SUCCESS},
    { 2,  4294967294U, 32, 33, "11111111111111111111111111111110\0----------------------------------", STATUS_SUCCESS},
    { 2,   0xFFFFFFFF, 32, 33, "11111111111111111111111111111111\0----------------------------------", STATUS_SUCCESS},
/*
 *  { 2,  0x1FFFFFFFF, 33, 34, "111111111111111111111111111111111\0---------------------------------", STATUS_SUCCESS},
 *  { 2,  0x3FFFFFFFF, 34, 35, "1111111111111111111111111111111111\0--------------------------------", STATUS_SUCCESS},
 *  { 2,  0x7FFFFFFFF, 35, 36, "11111111111111111111111111111111111\0-------------------------------", STATUS_SUCCESS},
 *  { 2,  0xFFFFFFFFF, 36, 37, "111111111111111111111111111111111111\0------------------------------", STATUS_SUCCESS},
 *  { 2, 0x1FFFFFFFFF, 37, 38, "1111111111111111111111111111111111111\0-----------------------------", STATUS_SUCCESS},
 *  { 2, 0x3FFFFFFFFF, 38, 39, "11111111111111111111111111111111111111\0----------------------------", STATUS_SUCCESS},
 *  { 2, 0x7FFFFFFFFF, 39, 40, "111111111111111111111111111111111111111\0---------------------------", STATUS_SUCCESS},
 *  { 2, 0xFFFFFFFFFF, 40, 41, "1111111111111111111111111111111111111111\0--------------------------", STATUS_SUCCESS},
 */

    { 8,  0x80000000U, 11, 12, "20000000000\0-------------------------------------------------------", STATUS_SUCCESS},
/*
 *  { 8,  -2147483647, 22, 23, "1777777777760000000001\0--------------------------------------------", STATUS_SUCCESS},
 *  { 8,           -2, 22, 23, "1777777777777777777776\0--------------------------------------------", STATUS_SUCCESS},
 *  { 8,           -1, 22, 23, "1777777777777777777777\0--------------------------------------------", STATUS_SUCCESS},
 */
    { 8,            0,  1, 12, "0\0-----------------------------------------------------------------", STATUS_SUCCESS},
    { 8,            1,  1, 12, "1\0-----------------------------------------------------------------", STATUS_SUCCESS},
    { 8,   2147483646, 11, 12, "17777777776\0-------------------------------------------------------", STATUS_SUCCESS},
    { 8,   2147483647, 11, 12, "17777777777\0-------------------------------------------------------", STATUS_SUCCESS},
    { 8,  2147483648U, 11, 12, "20000000000\0-------------------------------------------------------", STATUS_SUCCESS},
    { 8,  2147483649U, 11, 12, "20000000001\0-------------------------------------------------------", STATUS_SUCCESS},
    { 8,  4294967294U, 11, 12, "37777777776\0-------------------------------------------------------", STATUS_SUCCESS},
    { 8,  4294967295U, 11, 12, "37777777777\0-------------------------------------------------------", STATUS_SUCCESS},

    {10,  0x80000000U, 10, 11, "2147483648\0--------------------------------------------------------", STATUS_SUCCESS},
    {10,  -2147483647, 20, 21, "18446744071562067969\0----------------------------------------------", STATUS_SUCCESS},
    {10,           -2, 20, 21, "18446744073709551614\0----------------------------------------------", STATUS_SUCCESS},
    {10,           -1, 20, 21, "18446744073709551615\0----------------------------------------------", STATUS_SUCCESS},
    {10,            0,  1, 11, "0\0-----------------------------------------------------------------", STATUS_SUCCESS},
    {10,            1,  1, 11, "1\0-----------------------------------------------------------------", STATUS_SUCCESS},
    {10,   2147483646, 10, 11, "2147483646\0--------------------------------------------------------", STATUS_SUCCESS},
    {10,   2147483647, 10, 11, "2147483647\0--------------------------------------------------------", STATUS_SUCCESS},
    {10,  2147483648U, 10, 11, "2147483648\0--------------------------------------------------------", STATUS_SUCCESS},
    {10,  2147483649U, 10, 11, "2147483649\0--------------------------------------------------------", STATUS_SUCCESS},
    {10,  4294967294U, 10, 11, "4294967294\0--------------------------------------------------------", STATUS_SUCCESS},
    {10,  4294967295U, 10, 11, "4294967295\0--------------------------------------------------------", STATUS_SUCCESS},

    {16,                  0,  1,  9, "0\0-----------------------------------------------------------------", STATUS_SUCCESS},
    {16,                  1,  1,  9, "1\0-----------------------------------------------------------------", STATUS_SUCCESS},
    {16,         2147483646,  8,  9, "7FFFFFFE\0----------------------------------------------------------", STATUS_SUCCESS},
    {16,         2147483647,  8,  9, "7FFFFFFF\0----------------------------------------------------------", STATUS_SUCCESS},
    {16,         0x80000000,  8,  9, "80000000\0----------------------------------------------------------", STATUS_SUCCESS},
    {16,         0x80000001,  8,  9, "80000001\0----------------------------------------------------------", STATUS_SUCCESS},
    {16,         0xFFFFFFFE,  8,  9, "FFFFFFFE\0----------------------------------------------------------", STATUS_SUCCESS},
    {16,         0xFFFFFFFF,  8,  9, "FFFFFFFF\0----------------------------------------------------------", STATUS_SUCCESS},
/*
 *  {16,        0x100000000,  9, 10, "100000000\0---------------------------------------------------------", STATUS_SUCCESS},
 *  {16,      0xBADDEADBEEF, 11, 12, "BADDEADBEEF\0-------------------------------------------------------", STATUS_SUCCESS},
 *  {16, 0x8000000000000000, 16, 17, "8000000000000000\0--------------------------------------------------", STATUS_SUCCESS},
 *  {16, 0xFEDCBA9876543210, 16, 17, "FEDCBA9876543210\0--------------------------------------------------", STATUS_SUCCESS},
 *  {16, 0xFFFFFFFF80000001, 16, 17, "FFFFFFFF80000001\0--------------------------------------------------", STATUS_SUCCESS},
 *  {16, 0xFFFFFFFFFFFFFFFE, 16, 17, "FFFFFFFFFFFFFFFE\0--------------------------------------------------", STATUS_SUCCESS},
 *  {16, 0xFFFFFFFFFFFFFFFF, 16, 17, "FFFFFFFFFFFFFFFF\0--------------------------------------------------", STATUS_SUCCESS},
 */

    { 2,        32768, 16, 17, "1000000000000000\0--------------------------------------------------", STATUS_SUCCESS},
    { 2,        32768, 16, 16, "1000000000000000---------------------------------------------------",  STATUS_SUCCESS},
    { 2,        65536, 17, 18, "10000000000000000\0-------------------------------------------------", STATUS_SUCCESS},
    { 2,        65536, 17, 17, "10000000000000000--------------------------------------------------",  STATUS_SUCCESS},
    { 2,       131072, 18, 19, "100000000000000000\0------------------------------------------------", STATUS_SUCCESS},
    { 2,       131072, 18, 18, "100000000000000000-------------------------------------------------",  STATUS_SUCCESS},
    {16,   0xffffffff,  8,  9, "FFFFFFFF\0----------------------------------------------------------", STATUS_SUCCESS},
    {16,   0xffffffff,  8,  8, "FFFFFFFF-----------------------------------------------------------",  STATUS_SUCCESS},
    {16,   0xffffffff,  8,  7, "-------------------------------------------------------------------",  STATUS_BUFFER_OVERFLOW},
    {16,          0xa,  1,  2, "A\0-----------------------------------------------------------------", STATUS_SUCCESS},
    {16,          0xa,  1,  1, "A------------------------------------------------------------------",  STATUS_SUCCESS},
    {16,            0,  1,  0, "-------------------------------------------------------------------",  STATUS_BUFFER_OVERFLOW},
    {20,   0xdeadbeef,  0,  9, "-------------------------------------------------------------------",  STATUS_INVALID_PARAMETER},
    {-8,     07654321,  0, 12, "-------------------------------------------------------------------",  STATUS_INVALID_PARAMETER},
};
#define NB_LARGEINT2STR (sizeof(largeint2str)/sizeof(*largeint2str))


static void one_RtlInt64ToUnicodeString_test(int test_num, const largeint2str_t *largeint2str)
{
    int pos;
    WCHAR expected_str_Buffer[LARGE_STRI_BUFFER_LENGTH + 1];
    UNICODE_STRING expected_unicode_string;
    STRING expected_ansi_str;
    WCHAR str_Buffer[LARGE_STRI_BUFFER_LENGTH + 1];
    UNICODE_STRING unicode_string;
    STRING ansi_str;
    NTSTATUS result;

#ifdef _WIN64
    if (largeint2str->value >> 32 == 0xffffffff)  /* this crashes on 64-bit Vista */
    {
        skip( "Value ffffffff%08x broken on 64-bit windows\n", (DWORD)largeint2str->value );
        return;
    }
#endif

    for (pos = 0; pos < LARGE_STRI_BUFFER_LENGTH; pos++) {
	expected_str_Buffer[pos] = largeint2str->Buffer[pos];
    } /* for */
    expected_unicode_string.Length = largeint2str->Length * sizeof(WCHAR);
    expected_unicode_string.MaximumLength = largeint2str->MaximumLength * sizeof(WCHAR);
    expected_unicode_string.Buffer = expected_str_Buffer;
    pRtlUnicodeStringToAnsiString(&expected_ansi_str, &expected_unicode_string, 1);

    for (pos = 0; pos < LARGE_STRI_BUFFER_LENGTH; pos++) {
	str_Buffer[pos] = '-';
    } /* for */
    unicode_string.Length = 0;
    unicode_string.MaximumLength = largeint2str->MaximumLength * sizeof(WCHAR);
    unicode_string.Buffer = str_Buffer;

    if (largeint2str->base == 0) {
	result = pRtlInt64ToUnicodeString(largeint2str->value, 10, &unicode_string);
    } else {
	result = pRtlInt64ToUnicodeString(largeint2str->value, largeint2str->base, &unicode_string);
    } /* if */
    pRtlUnicodeStringToAnsiString(&ansi_str, &unicode_string, 1);
    if (result == STATUS_BUFFER_OVERFLOW) {
	/* On BUFFER_OVERFLOW the string Buffer should be unchanged */
	for (pos = 0; pos < LARGE_STRI_BUFFER_LENGTH; pos++) {
	    expected_str_Buffer[pos] = '-';
	} /* for */
	/* w2k: The native function has two reasons for BUFFER_OVERFLOW: */
	/* If the value is too large to convert: The Length is unchanged */
	/* If str is too small to hold the string: Set str->Length to the length */
	/* the string would have (which can be larger than the MaximumLength). */
	/* To allow all this in the tests we do the following: */
	if (expected_unicode_string.Length >= 64) {
	    /* The value is too large to convert only triggered when testing native */
	    /* Length is not filled with the expected string length (garbage?) */
	    expected_unicode_string.Length = unicode_string.Length;
	} /* if */
    } else {
	ok(result == largeint2str->result,
           "(test %d): RtlInt64ToUnicodeString(0x%s, %d, [out]) has result %x, expected: %x\n",
	   test_num, wine_dbgstr_longlong(largeint2str->value), largeint2str->base, result, largeint2str->result);
	if (result == STATUS_SUCCESS) {
	    ok(unicode_string.Buffer[unicode_string.Length/sizeof(WCHAR)] == '\0',
               "(test %d): RtlInt64ToUnicodeString(0x%s, %d, [out]) string \"%s\" is not NULL terminated\n",
	       test_num, wine_dbgstr_longlong(largeint2str->value), largeint2str->base, ansi_str.Buffer);
	} /* if */
    } /* if */
    ok(memcmp(unicode_string.Buffer, expected_unicode_string.Buffer, LARGE_STRI_BUFFER_LENGTH * sizeof(WCHAR)) == 0,
       "(test %d): RtlInt64ToUnicodeString(0x%x%08x, %d, [out]) assigns string \"%s\", expected: \"%s\"\n",
       test_num, (DWORD)(largeint2str->value >>32), (DWORD)largeint2str->value, largeint2str->base, 
       ansi_str.Buffer, expected_ansi_str.Buffer);
    ok(unicode_string.Length == expected_unicode_string.Length,
       "(test %d): RtlInt64ToUnicodeString(0x%s, %d, [out]) string has Length %d, expected: %d\n",
       test_num, wine_dbgstr_longlong(largeint2str->value), largeint2str->base,
       unicode_string.Length, expected_unicode_string.Length);
    ok(unicode_string.MaximumLength == expected_unicode_string.MaximumLength,
       "(test %d): RtlInt64ToUnicodeString(0x%s, %d, [out]) string has MaximumLength %d, expected: %d\n",
       test_num, wine_dbgstr_longlong(largeint2str->value), largeint2str->base,
       unicode_string.MaximumLength, expected_unicode_string.MaximumLength);
    pRtlFreeAnsiString(&expected_ansi_str);
    pRtlFreeAnsiString(&ansi_str);
}


static void test_RtlInt64ToUnicodeString(void)
{
    int test_num;

    for (test_num = 0; test_num < NB_LARGEINT2STR; test_num++) {
	one_RtlInt64ToUnicodeString_test(test_num, &largeint2str[test_num]);
    } /* for */
}


static void one_RtlLargeIntegerToChar_test(int test_num, const largeint2str_t *largeint2str)
{
    NTSTATUS result;
    char dest_str[LARGE_STRI_BUFFER_LENGTH + 1];
    ULONGLONG value;

#ifdef _WIN64
    if (largeint2str->value >> 32 == 0xffffffff)  /* this crashes on 64-bit Vista */
    {
        skip( "Value ffffffff%08x broken on 64-bit windows\n", (DWORD)largeint2str->value );
        return;
    }
#endif

    memset(dest_str, '-', LARGE_STRI_BUFFER_LENGTH);
    dest_str[LARGE_STRI_BUFFER_LENGTH] = '\0';
    value = largeint2str->value;
    if (largeint2str->base == 0) {
	result = pRtlLargeIntegerToChar(&value, 10, largeint2str->MaximumLength, dest_str);
    } else {
	result = pRtlLargeIntegerToChar(&value, largeint2str->base, largeint2str->MaximumLength, dest_str);
    } /* if */
    ok(result == largeint2str->result,
       "(test %d): RtlLargeIntegerToChar(0x%s, %d, %d, [out]) has result %x, expected: %x\n",
       test_num, wine_dbgstr_longlong(largeint2str->value), largeint2str->base,
       largeint2str->MaximumLength, result, largeint2str->result);
    ok(memcmp(dest_str, largeint2str->Buffer, LARGE_STRI_BUFFER_LENGTH) == 0,
       "(test %d): RtlLargeIntegerToChar(0x%s, %d, %d, [out]) assigns string \"%s\", expected: \"%s\"\n",
       test_num, wine_dbgstr_longlong(largeint2str->value), largeint2str->base,
       largeint2str->MaximumLength, dest_str, largeint2str->Buffer);
}


static void test_RtlLargeIntegerToChar(void)
{
    NTSTATUS result;
    int test_num;
    ULONGLONG value;

    for (test_num = 0; test_num < NB_LARGEINT2STR; test_num++) {
	one_RtlLargeIntegerToChar_test(test_num, &largeint2str[test_num]);
    } /* for */

    value = largeint2str[0].value;
    result = pRtlLargeIntegerToChar(&value, 20, largeint2str[0].MaximumLength, NULL);
    ok(result == STATUS_INVALID_PARAMETER,
       "(test a): RtlLargeIntegerToChar(0x%s, %d, %d, NULL) has result %x, expected: %x\n",
       wine_dbgstr_longlong(largeint2str[0].value), 20,
       largeint2str[0].MaximumLength, result, STATUS_INVALID_PARAMETER);

    result = pRtlLargeIntegerToChar(&value, 20, 0, NULL);
    ok(result == STATUS_INVALID_PARAMETER,
       "(test b): RtlLargeIntegerToChar(0x%s, %d, %d, NULL) has result %x, expected: %x\n",
       wine_dbgstr_longlong(largeint2str[0].value), 20,
       largeint2str[0].MaximumLength, result, STATUS_INVALID_PARAMETER);

    result = pRtlLargeIntegerToChar(&value, largeint2str[0].base, 0, NULL);
    ok(result == STATUS_BUFFER_OVERFLOW,
       "(test c): RtlLargeIntegerToChar(0x%s, %d, %d, NULL) has result %x, expected: %x\n",
       wine_dbgstr_longlong(largeint2str[0].value), largeint2str[0].base, 0, result, STATUS_BUFFER_OVERFLOW);

    result = pRtlLargeIntegerToChar(&value, largeint2str[0].base, largeint2str[0].MaximumLength, NULL);
    ok(result == STATUS_ACCESS_VIOLATION,
       "(test d): RtlLargeIntegerToChar(0x%s, %d, %d, NULL) has result %x, expected: %x\n",
       wine_dbgstr_longlong(largeint2str[0].value),
       largeint2str[0].base, largeint2str[0].MaximumLength, result, STATUS_ACCESS_VIOLATION);
}


#ifdef __i386__

#include "pshpack1.h"
struct lldvrm_thunk
{
    BYTE push_ebx;      /* pushl %ebx */
    DWORD push_esp1;    /* pushl 24(%esp) */
    DWORD push_esp2;    /* pushl 24(%esp) */
    DWORD push_esp3;    /* pushl 24(%esp) */
    DWORD push_esp4;    /* pushl 24(%esp) */
    DWORD call;         /* call 24(%esp) */
    WORD mov_ecx_eax;   /* movl %ecx,%eax */
    WORD mov_ebx_edx;   /* movl %ebx,%edx */
    BYTE pop_ebx;       /* popl %ebx */
    BYTE ret;           /* ret */
};
#include "poppack.h"

static void test__alldvrm(void)
{
    struct lldvrm_thunk *thunk = VirtualAlloc(NULL, sizeof(*thunk), MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    ULONGLONG (CDECL *call_lldvrm_func)(void *func, ULONGLONG, ULONGLONG) = (void *)thunk;
    ULONGLONG ret;

    memset(thunk, 0x90, sizeof(*thunk));
    thunk->push_ebx  = 0x53;        /* pushl %ebx */
    thunk->push_esp1 = 0x182474ff;  /* pushl 24(%esp) */
    thunk->push_esp2 = 0x182474ff;  /* pushl 24(%esp) */
    thunk->push_esp3 = 0x182474ff;  /* pushl 24(%esp) */
    thunk->push_esp4 = 0x182474ff;  /* pushl 24(%esp) */
    thunk->call      = 0x182454ff;  /* call 24(%esp) */
    thunk->pop_ebx   = 0x5b;        /* popl %ebx */
    thunk->ret       = 0xc3;        /* ret */

    ret = call_lldvrm_func(p_alldvrm, 0x0123456701234567ULL, 3);
    ok(ret == 0x61172255b66c77ULL, "got %x%08x\n", (DWORD)(ret >> 32), (DWORD)ret);
    ret = call_lldvrm_func(p_alldvrm, 0x0123456701234567ULL, -3);
    ok(ret == 0xff9ee8ddaa499389ULL, "got %x%08x\n", (DWORD)(ret >> 32), (DWORD)ret);

    ret = call_lldvrm_func(p_aulldvrm, 0x0123456701234567ULL, 3);
    ok(ret == 0x61172255b66c77ULL, "got %x%08x\n", (DWORD)(ret >> 32), (DWORD)ret);
    ret = call_lldvrm_func(p_aulldvrm, 0x0123456701234567ULL, -3);
    ok(ret == 0, "got %x%08x\n", (DWORD)(ret >> 32), (DWORD)ret);

    thunk->mov_ecx_eax = 0xc889;
    thunk->mov_ebx_edx = 0xda89;

    ret = call_lldvrm_func(p_alldvrm, 0x0123456701234567ULL, 3);
    ok(ret == 2, "got %x%08x\n", (DWORD)(ret >> 32), (DWORD)ret);
    ret = call_lldvrm_func(p_alldvrm, 0x0123456701234567ULL, -3);
    ok(ret == 2, "got %x%08x\n", (DWORD)(ret >> 32), (DWORD)ret);

    ret = call_lldvrm_func(p_aulldvrm, 0x0123456701234567ULL, 3);
    ok(ret == 2, "got %x%08x\n", (DWORD)(ret >> 32), (DWORD)ret);
    ret = call_lldvrm_func(p_aulldvrm, 0x0123456701234567ULL, -3);
    ok(ret == 0x123456701234567ULL, "got %x%08x\n", (DWORD)(ret >> 32), (DWORD)ret);
}
#endif  /* __i386__ */


START_TEST(large_int)
{
    InitFunctionPtrs();

    if (pRtlExtendedMagicDivide)
        test_RtlExtendedMagicDivide();
    if (pRtlInt64ToUnicodeString)
	    test_RtlInt64ToUnicodeString();
    if (pRtlLargeIntegerToChar)
        test_RtlLargeIntegerToChar();

#ifdef __i386__
    test__alldvrm();
#endif  /* __i386__ */
}

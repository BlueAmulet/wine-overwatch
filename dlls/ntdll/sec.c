/*
 *	Security functions
 *
 *	Copyright 1996-1998 Marcus Meissner
 * 	Copyright 2003 CodeWeavers Inc. (Ulrich Czekalla)
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "ntdll_misc.h"
#include "wine/exception.h"
#include "wine/library.h"
#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ntdll);

#define NT_SUCCESS(status) (status == STATUS_SUCCESS)

#define SELF_RELATIVE_FIELD(sd,field) ((BYTE *)(sd) + ((SECURITY_DESCRIPTOR_RELATIVE *)(sd))->field)

/* helper function to retrieve active length of an ACL */
static size_t acl_bytesInUse(PACL pAcl)
{
    int i;
    size_t bytesInUse = sizeof(ACL);
    PACE_HEADER ace = (PACE_HEADER) (pAcl + 1);
    for (i = 0; i < pAcl->AceCount; i++)
    {
	bytesInUse += ace->AceSize;
	ace = (PACE_HEADER)(((BYTE*)ace)+ace->AceSize);
    }
    return bytesInUse;
}

/* helper function to copy an ACL */
static BOOLEAN copy_acl(DWORD nDestinationAclLength, PACL pDestinationAcl, PACL pSourceAcl)
{
    DWORD size;

    if (!pSourceAcl || !RtlValidAcl(pSourceAcl))
        return FALSE;

    size = pSourceAcl->AclSize;
    if (nDestinationAclLength < size)
        return FALSE;

    memmove(pDestinationAcl, pSourceAcl, size);
    return TRUE;
}

/* generically adds an ACE to an ACL */
static NTSTATUS add_access_ace(PACL pAcl, DWORD dwAceRevision, DWORD dwAceFlags,
                               DWORD dwAccessMask, PSID pSid, DWORD dwAceType)
{
    ACE_HEADER *pAceHeader;
    DWORD dwLengthSid;
    DWORD dwAceSize;
    DWORD *pAccessMask;
    DWORD *pSidStart;

    if (!RtlValidSid(pSid))
        return STATUS_INVALID_SID;

    if (pAcl->AclRevision > MAX_ACL_REVISION || dwAceRevision > MAX_ACL_REVISION)
        return STATUS_REVISION_MISMATCH;

    if (!RtlValidAcl(pAcl))
        return STATUS_INVALID_ACL;

    if (!RtlFirstFreeAce(pAcl, &pAceHeader))
        return STATUS_INVALID_ACL;

    if (!pAceHeader)
        return STATUS_ALLOTTED_SPACE_EXCEEDED;

    /* calculate generic size of the ACE */
    dwLengthSid = RtlLengthSid(pSid);
    dwAceSize = sizeof(ACE_HEADER) + sizeof(DWORD) + dwLengthSid;
    if ((char *)pAceHeader + dwAceSize > (char *)pAcl + pAcl->AclSize)
        return STATUS_ALLOTTED_SPACE_EXCEEDED;

    /* fill the new ACE */
    pAceHeader->AceType = dwAceType;
    pAceHeader->AceFlags = dwAceFlags;
    pAceHeader->AceSize = dwAceSize;

    /* skip past the ACE_HEADER of the ACE */
    pAccessMask = (DWORD *)(pAceHeader + 1);
    *pAccessMask = dwAccessMask;

    /* skip past ACE->Mask */
    pSidStart = pAccessMask + 1;
    RtlCopySid(dwLengthSid, pSidStart, pSid);

    pAcl->AclRevision = max(pAcl->AclRevision, dwAceRevision);
    pAcl->AceCount++;

    return STATUS_SUCCESS;
}

/*
 *	SID FUNCTIONS
 */

/******************************************************************************
 *  RtlAllocateAndInitializeSid		[NTDLL.@]
 *
 */
NTSTATUS WINAPI RtlAllocateAndInitializeSid (
	PSID_IDENTIFIER_AUTHORITY pIdentifierAuthority,
	BYTE nSubAuthorityCount,
	DWORD nSubAuthority0, DWORD nSubAuthority1,
	DWORD nSubAuthority2, DWORD nSubAuthority3,
	DWORD nSubAuthority4, DWORD nSubAuthority5,
	DWORD nSubAuthority6, DWORD nSubAuthority7,
	PSID *pSid )
{
    SID *tmp_sid;

    TRACE("(%p, 0x%04x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,%p)\n",
		pIdentifierAuthority,nSubAuthorityCount,
		nSubAuthority0, nSubAuthority1,	nSubAuthority2, nSubAuthority3,
		nSubAuthority4, nSubAuthority5,	nSubAuthority6, nSubAuthority7, pSid);

    if (nSubAuthorityCount > 8) return STATUS_INVALID_SID;

    if (!(tmp_sid= RtlAllocateHeap( GetProcessHeap(), 0,
                                    RtlLengthRequiredSid(nSubAuthorityCount))))
        return STATUS_NO_MEMORY;

    tmp_sid->Revision = SID_REVISION;

    if (pIdentifierAuthority)
        tmp_sid->IdentifierAuthority = *pIdentifierAuthority;
    tmp_sid->SubAuthorityCount = nSubAuthorityCount;

    switch( nSubAuthorityCount )
    {
        case 8: tmp_sid->SubAuthority[7]= nSubAuthority7;
            /* fall through */
        case 7: tmp_sid->SubAuthority[6]= nSubAuthority6;
            /* fall through */
        case 6: tmp_sid->SubAuthority[5]= nSubAuthority5;
            /* fall through */
        case 5: tmp_sid->SubAuthority[4]= nSubAuthority4;
            /* fall through */
        case 4: tmp_sid->SubAuthority[3]= nSubAuthority3;
            /* fall through */
        case 3: tmp_sid->SubAuthority[2]= nSubAuthority2;
            /* fall through */
        case 2: tmp_sid->SubAuthority[1]= nSubAuthority1;
            /* fall through */
        case 1: tmp_sid->SubAuthority[0]= nSubAuthority0;
        break;
    }
    *pSid = tmp_sid;
    return STATUS_SUCCESS;
}

/******************************************************************************
 *  RtlEqualSid		[NTDLL.@]
 *
 * Determine if two SIDs are equal.
 *
 * PARAMS
 *  pSid1 [I] Source SID
 *  pSid2 [I] SID to compare with
 *
 * RETURNS
 *  TRUE, if pSid1 is equal to pSid2,
 *  FALSE otherwise.
 */
BOOL WINAPI RtlEqualSid( PSID pSid1, PSID pSid2 )
{
    if (!RtlValidSid(pSid1) || !RtlValidSid(pSid2))
        return FALSE;

    if (*RtlSubAuthorityCountSid(pSid1) != *RtlSubAuthorityCountSid(pSid2))
        return FALSE;

    if (memcmp(pSid1, pSid2, RtlLengthSid(pSid1)) != 0)
        return FALSE;

    return TRUE;
}

/******************************************************************************
 * RtlEqualPrefixSid	[NTDLL.@]
 */
BOOL WINAPI RtlEqualPrefixSid (PSID pSid1, PSID pSid2)
{
    if (!RtlValidSid(pSid1) || !RtlValidSid(pSid2))
        return FALSE;

    if (*RtlSubAuthorityCountSid(pSid1) != *RtlSubAuthorityCountSid(pSid2))
        return FALSE;

    if (memcmp(pSid1, pSid2, RtlLengthRequiredSid(((SID*)pSid1)->SubAuthorityCount - 1)) != 0)
        return FALSE;

    return TRUE;
}


/******************************************************************************
 *  RtlFreeSid		[NTDLL.@]
 *
 * Free the resources used by a SID.
 *
 * PARAMS
 *  pSid [I] SID to Free.
 *
 * RETURNS
 *  STATUS_SUCCESS.
 */
DWORD WINAPI RtlFreeSid(PSID pSid)
{
	TRACE("(%p)\n", pSid);
	RtlFreeHeap( GetProcessHeap(), 0, pSid );
	return STATUS_SUCCESS;
}

/**************************************************************************
 * RtlLengthRequiredSid	[NTDLL.@]
 *
 * Determine the amount of memory a SID will use
 *
 * PARAMS
 *   nrofsubauths [I] Number of Sub Authorities in the SID.
 *
 * RETURNS
 *   The size, in bytes, of a SID with nrofsubauths Sub Authorities.
 */
DWORD WINAPI RtlLengthRequiredSid(DWORD nrofsubauths)
{
	return (nrofsubauths-1)*sizeof(DWORD) + sizeof(SID);
}

/**************************************************************************
 *                 RtlLengthSid				[NTDLL.@]
 *
 * Determine the amount of memory a SID is using
 *
 * PARAMS
 *  pSid [I] SID to get the size of.
 *
 * RETURNS
 *  The size, in bytes, of pSid.
 */
DWORD WINAPI RtlLengthSid(PSID pSid)
{
	TRACE("sid=%p\n",pSid);
	if (!pSid) return 0;
	return RtlLengthRequiredSid(*RtlSubAuthorityCountSid(pSid));
}

/**************************************************************************
 *                 RtlInitializeSid			[NTDLL.@]
 *
 * Initialise a SID.
 *
 * PARAMS
 *  pSid                 [I] SID to initialise
 *  pIdentifierAuthority [I] Identifier Authority
 *  nSubAuthorityCount   [I] Number of Sub Authorities
 *
 * RETURNS
 *  Success: TRUE. pSid is initialised with the details given.
 *  Failure: FALSE, if nSubAuthorityCount is >= SID_MAX_SUB_AUTHORITIES.
 */
BOOL WINAPI RtlInitializeSid(
	PSID pSid,
	PSID_IDENTIFIER_AUTHORITY pIdentifierAuthority,
	BYTE nSubAuthorityCount)
{
	int i;
	SID* pisid=pSid;

	if (nSubAuthorityCount >= SID_MAX_SUB_AUTHORITIES)
	  return FALSE;

	pisid->Revision = SID_REVISION;
	pisid->SubAuthorityCount = nSubAuthorityCount;
	if (pIdentifierAuthority)
	  pisid->IdentifierAuthority = *pIdentifierAuthority;

	for (i = 0; i < nSubAuthorityCount; i++)
	  *RtlSubAuthoritySid(pSid, i) = 0;

	return TRUE;
}

/**************************************************************************
 *                 RtlSubAuthoritySid			[NTDLL.@]
 *
 * Return the Sub Authority of a SID
 *
 * PARAMS
 *   pSid          [I] SID to get the Sub Authority from.
 *   nSubAuthority [I] Sub Authority number.
 *
 * RETURNS
 *   A pointer to The Sub Authority value of pSid.
 */
LPDWORD WINAPI RtlSubAuthoritySid( PSID pSid, DWORD nSubAuthority )
{
    return &(((SID*)pSid)->SubAuthority[nSubAuthority]);
}

/**************************************************************************
 * RtlIdentifierAuthoritySid	[NTDLL.@]
 *
 * Return the Identifier Authority of a SID.
 *
 * PARAMS
 *   pSid [I] SID to get the Identifier Authority from.
 *
 * RETURNS
 *   A pointer to the Identifier Authority value of pSid.
 */
PSID_IDENTIFIER_AUTHORITY WINAPI RtlIdentifierAuthoritySid( PSID pSid )
{
    return &(((SID*)pSid)->IdentifierAuthority);
}

/**************************************************************************
 *                 RtlSubAuthorityCountSid		[NTDLL.@]
 *
 * Get the number of Sub Authorities in a SID.
 *
 * PARAMS
 *   pSid [I] SID to get the count from.
 *
 * RETURNS
 *  A pointer to the Sub Authority count of pSid.
 */
LPBYTE WINAPI RtlSubAuthorityCountSid(PSID pSid)
{
    return &(((SID*)pSid)->SubAuthorityCount);
}

/**************************************************************************
 *                 RtlCopySid				[NTDLL.@]
 */
BOOLEAN WINAPI RtlCopySid( DWORD nDestinationSidLength, PSID pDestinationSid, PSID pSourceSid )
{
	if (!pSourceSid || !RtlValidSid(pSourceSid) ||
	    (nDestinationSidLength < RtlLengthSid(pSourceSid)))
          return FALSE;

	if (nDestinationSidLength < (((SID*)pSourceSid)->SubAuthorityCount*4+8))
	  return FALSE;

	memmove(pDestinationSid, pSourceSid, ((SID*)pSourceSid)->SubAuthorityCount*4+8);
	return TRUE;
}
/******************************************************************************
 * RtlValidSid [NTDLL.@]
 *
 * Determine if a SID is valid.
 *
 * PARAMS
 *   pSid [I] SID to check
 *
 * RETURNS
 *   TRUE if pSid is valid,
 *   FALSE otherwise.
 */
BOOLEAN WINAPI RtlValidSid( PSID pSid )
{
    BOOL ret;
    __TRY
    {
        ret = TRUE;
        if (!pSid || ((SID*)pSid)->Revision != SID_REVISION ||
            ((SID*)pSid)->SubAuthorityCount > SID_MAX_SUB_AUTHORITIES)
        {
            ret = FALSE;
        }
    }
    __EXCEPT_PAGE_FAULT
    {
        WARN("(%p): invalid pointer!\n", pSid);
        return FALSE;
    }
    __ENDTRY
    return ret;
}


/*
 *	security descriptor functions
 */

/**************************************************************************
 * RtlCreateSecurityDescriptor			[NTDLL.@]
 *
 * Initialise a SECURITY_DESCRIPTOR.
 *
 * PARAMS
 *  lpsd [O] Descriptor to initialise.
 *  rev  [I] Revision, must be set to SECURITY_DESCRIPTOR_REVISION.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: STATUS_UNKNOWN_REVISION if rev is incorrect.
 */
NTSTATUS WINAPI RtlCreateSecurityDescriptor(
	PSECURITY_DESCRIPTOR lpsd,
	DWORD rev)
{
	if (rev!=SECURITY_DESCRIPTOR_REVISION)
		return STATUS_UNKNOWN_REVISION;
	memset(lpsd,'\0',sizeof(SECURITY_DESCRIPTOR));
	((SECURITY_DESCRIPTOR*)lpsd)->Revision = SECURITY_DESCRIPTOR_REVISION;
	return STATUS_SUCCESS;
}

/**************************************************************************
 * RtlCopySecurityDescriptor            [NTDLL.@]
 *
 * Copies an absolute or sefl-relative SECURITY_DESCRIPTOR.
 *
 * PARAMS
 *  pSourceSD      [O] SD to copy from.
 *  pDestinationSD [I] Destination SD.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: STATUS_UNKNOWN_REVISION if rev is incorrect.
 */
NTSTATUS WINAPI RtlCopySecurityDescriptor(PSECURITY_DESCRIPTOR pSourceSD, PSECURITY_DESCRIPTOR pDestinationSD)
{
    PSID Owner, Group;
    PACL Dacl, Sacl;
    DWORD length;

    if (((SECURITY_DESCRIPTOR *)pSourceSD)->Control & SE_SELF_RELATIVE)
    {
        SECURITY_DESCRIPTOR_RELATIVE *src = pSourceSD;
        SECURITY_DESCRIPTOR_RELATIVE *dst = pDestinationSD;

        if (src->Revision != SECURITY_DESCRIPTOR_REVISION)
            return STATUS_UNKNOWN_REVISION;

        *dst = *src;
        if (src->Owner)
        {
            Owner = (PSID)SELF_RELATIVE_FIELD( src, Owner );
            length = RtlLengthSid( Owner );
            RtlCopySid(length, SELF_RELATIVE_FIELD( dst, Owner ), Owner);
        }
        if (src->Group)
        {
            Group = (PSID)SELF_RELATIVE_FIELD( src, Group );
            length = RtlLengthSid( Group );
            RtlCopySid(length, SELF_RELATIVE_FIELD( dst, Group ), Group);
        }
        if ((src->Control & SE_SACL_PRESENT) && src->Sacl)
        {
            Sacl = (PACL)SELF_RELATIVE_FIELD( src, Sacl );
            copy_acl(Sacl->AclSize, (PACL)SELF_RELATIVE_FIELD( dst, Sacl ), Sacl);
        }
        if ((src->Control & SE_DACL_PRESENT) && src->Dacl)
        {
            Dacl = (PACL)SELF_RELATIVE_FIELD( src, Dacl );
            copy_acl(Dacl->AclSize, (PACL)SELF_RELATIVE_FIELD( dst, Dacl ), Dacl);
        }
    }
    else
    {
        SECURITY_DESCRIPTOR *src = pSourceSD;
        SECURITY_DESCRIPTOR *dst = pDestinationSD;

        if (src->Revision != SECURITY_DESCRIPTOR_REVISION)
            return STATUS_UNKNOWN_REVISION;

        *dst = *src;
        if (src->Owner)
        {
            length = RtlLengthSid( src->Owner );
            dst->Owner = RtlAllocateHeap(GetProcessHeap(), 0, length);
            RtlCopySid(length, dst->Owner, src->Owner);
        }
        if (src->Group)
        {
            length = RtlLengthSid( src->Group );
            dst->Group = RtlAllocateHeap(GetProcessHeap(), 0, length);
            RtlCopySid(length, dst->Group, src->Group);
        }
        if (src->Control & SE_SACL_PRESENT)
        {
            length = src->Sacl->AclSize;
            dst->Sacl = RtlAllocateHeap(GetProcessHeap(), 0, length);
            copy_acl(length, dst->Sacl, src->Sacl);
        }
        if (src->Control & SE_DACL_PRESENT)
        {
            length = src->Dacl->AclSize;
            dst->Dacl = RtlAllocateHeap(GetProcessHeap(), 0, length);
            copy_acl(length, dst->Dacl, src->Dacl);
        }
    }

    return STATUS_SUCCESS;
}

/**************************************************************************
 * RtlValidSecurityDescriptor			[NTDLL.@]
 *
 * Determine if a SECURITY_DESCRIPTOR is valid.
 *
 * PARAMS
 *  SecurityDescriptor [I] Descriptor to check.
 *
 * RETURNS
 *   Success: STATUS_SUCCESS.
 *   Failure: STATUS_INVALID_SECURITY_DESCR or STATUS_UNKNOWN_REVISION.
 */
NTSTATUS WINAPI RtlValidSecurityDescriptor(
	PSECURITY_DESCRIPTOR SecurityDescriptor)
{
	if ( ! SecurityDescriptor )
		return STATUS_INVALID_SECURITY_DESCR;
	if ( ((SECURITY_DESCRIPTOR*)SecurityDescriptor)->Revision != SECURITY_DESCRIPTOR_REVISION )
		return STATUS_UNKNOWN_REVISION;

	return STATUS_SUCCESS;
}

/**************************************************************************
 * RtlValidRelativeSecurityDescriptor		[NTDLL.@]
 */
BOOLEAN WINAPI RtlValidRelativeSecurityDescriptor(PSECURITY_DESCRIPTOR descriptor,
    ULONG length, SECURITY_INFORMATION info)
{
    FIXME("%p,%u,%d: semi-stub\n", descriptor, length, info);
    return RtlValidSecurityDescriptor(descriptor) == STATUS_SUCCESS;
}

/**************************************************************************
 *  RtlLengthSecurityDescriptor			[NTDLL.@]
 */
ULONG WINAPI RtlLengthSecurityDescriptor(
	PSECURITY_DESCRIPTOR pSecurityDescriptor)
{
	ULONG size;

	if ( pSecurityDescriptor == NULL )
		return 0;

	if (((SECURITY_DESCRIPTOR *)pSecurityDescriptor)->Control & SE_SELF_RELATIVE)
        {
            SECURITY_DESCRIPTOR_RELATIVE *sd = pSecurityDescriptor;
            size = sizeof(*sd);
            if (sd->Owner) size += RtlLengthSid((PSID)SELF_RELATIVE_FIELD(sd,Owner));
            if (sd->Group) size += RtlLengthSid((PSID)SELF_RELATIVE_FIELD(sd,Group));
            if ((sd->Control & SE_SACL_PRESENT) && sd->Sacl)
		size += ((PACL)SELF_RELATIVE_FIELD(sd,Sacl))->AclSize;
            if ((sd->Control & SE_DACL_PRESENT) && sd->Dacl)
		size += ((PACL)SELF_RELATIVE_FIELD(sd,Dacl))->AclSize;
        }
        else
        {
            SECURITY_DESCRIPTOR *sd = pSecurityDescriptor;
            size = sizeof(*sd);
            if (sd->Owner) size += RtlLengthSid( sd->Owner );
            if (sd->Group) size += RtlLengthSid( sd->Group );
            if ((sd->Control & SE_SACL_PRESENT) && sd->Sacl) size += sd->Sacl->AclSize;
            if ((sd->Control & SE_DACL_PRESENT) && sd->Dacl) size += sd->Dacl->AclSize;
        }
	return size;
}

/******************************************************************************
 *  RtlGetDaclSecurityDescriptor		[NTDLL.@]
 *
 */
NTSTATUS WINAPI RtlGetDaclSecurityDescriptor(
	IN PSECURITY_DESCRIPTOR pSecurityDescriptor,
	OUT PBOOLEAN lpbDaclPresent,
	OUT PACL *pDacl,
	OUT PBOOLEAN lpbDaclDefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	TRACE("(%p,%p,%p,%p)\n",
	pSecurityDescriptor, lpbDaclPresent, pDacl, lpbDaclDefaulted);

	if (lpsd->Revision != SECURITY_DESCRIPTOR_REVISION)
	  return STATUS_UNKNOWN_REVISION ;

	if ( (*lpbDaclPresent = (SE_DACL_PRESENT & lpsd->Control) ? 1 : 0) )
	{
            if (lpsd->Control & SE_SELF_RELATIVE)
            {
                SECURITY_DESCRIPTOR_RELATIVE *sdr = pSecurityDescriptor;
                if (sdr->Dacl) *pDacl = (PACL)SELF_RELATIVE_FIELD( sdr, Dacl );
                else *pDacl = NULL;
            }
            else *pDacl = lpsd->Dacl;

            *lpbDaclDefaulted = (lpsd->Control & SE_DACL_DEFAULTED) != 0;
        }
        else
        {
            *pDacl = NULL;
            *lpbDaclDefaulted = 0;
        }

	return STATUS_SUCCESS;
}

/**************************************************************************
 *  RtlSetDaclSecurityDescriptor		[NTDLL.@]
 */
NTSTATUS WINAPI RtlSetDaclSecurityDescriptor (
	PSECURITY_DESCRIPTOR pSecurityDescriptor,
	BOOLEAN daclpresent,
	PACL dacl,
	BOOLEAN dacldefaulted )
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	if (lpsd->Revision!=SECURITY_DESCRIPTOR_REVISION)
		return STATUS_UNKNOWN_REVISION;
	if (lpsd->Control & SE_SELF_RELATIVE)
		return STATUS_INVALID_SECURITY_DESCR;

	if (!daclpresent)
	{
		lpsd->Control &= ~SE_DACL_PRESENT;
		return STATUS_SUCCESS;
	}

	lpsd->Control |= SE_DACL_PRESENT;
	lpsd->Dacl = dacl;

	if (dacldefaulted)
		lpsd->Control |= SE_DACL_DEFAULTED;
	else
		lpsd->Control &= ~SE_DACL_DEFAULTED;

	return STATUS_SUCCESS;
}

/******************************************************************************
 *  RtlGetSaclSecurityDescriptor		[NTDLL.@]
 *
 */
NTSTATUS WINAPI RtlGetSaclSecurityDescriptor(
	IN PSECURITY_DESCRIPTOR pSecurityDescriptor,
	OUT PBOOLEAN lpbSaclPresent,
	OUT PACL *pSacl,
	OUT PBOOLEAN lpbSaclDefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	TRACE("(%p,%p,%p,%p)\n",
	pSecurityDescriptor, lpbSaclPresent, pSacl, lpbSaclDefaulted);

	if (lpsd->Revision != SECURITY_DESCRIPTOR_REVISION)
	  return STATUS_UNKNOWN_REVISION;

	if ( (*lpbSaclPresent = (SE_SACL_PRESENT & lpsd->Control) ? 1 : 0) )
	{
            if (lpsd->Control & SE_SELF_RELATIVE)
            {
                SECURITY_DESCRIPTOR_RELATIVE *sdr = pSecurityDescriptor;
                if (sdr->Sacl) *pSacl = (PACL)SELF_RELATIVE_FIELD( sdr, Sacl );
                else *pSacl = NULL;
            }
            else *pSacl = lpsd->Sacl;

            *lpbSaclDefaulted = (lpsd->Control & SE_SACL_DEFAULTED) != 0;
	}
	return STATUS_SUCCESS;
}

/**************************************************************************
 * RtlSetSaclSecurityDescriptor			[NTDLL.@]
 */
NTSTATUS WINAPI RtlSetSaclSecurityDescriptor (
	PSECURITY_DESCRIPTOR pSecurityDescriptor,
	BOOLEAN saclpresent,
	PACL sacl,
	BOOLEAN sacldefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	if (lpsd->Revision!=SECURITY_DESCRIPTOR_REVISION)
		return STATUS_UNKNOWN_REVISION;
	if (lpsd->Control & SE_SELF_RELATIVE)
		return STATUS_INVALID_SECURITY_DESCR;
	if (!saclpresent) {
		lpsd->Control &= ~SE_SACL_PRESENT;
		return 0;
	}
	lpsd->Control |= SE_SACL_PRESENT;
	lpsd->Sacl = sacl;
	if (sacldefaulted)
		lpsd->Control |= SE_SACL_DEFAULTED;
	else
		lpsd->Control &= ~SE_SACL_DEFAULTED;
	return STATUS_SUCCESS;
}

/**************************************************************************
 * RtlGetOwnerSecurityDescriptor		[NTDLL.@]
 */
NTSTATUS WINAPI RtlGetOwnerSecurityDescriptor(
	PSECURITY_DESCRIPTOR pSecurityDescriptor,
	PSID *Owner,
	PBOOLEAN OwnerDefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	if ( !lpsd  || !Owner || !OwnerDefaulted )
		return STATUS_INVALID_PARAMETER;

        if ( lpsd->Control & SE_OWNER_DEFAULTED )
            *OwnerDefaulted = TRUE;
        else
            *OwnerDefaulted = FALSE;

        if (lpsd->Control & SE_SELF_RELATIVE)
        {
            SECURITY_DESCRIPTOR_RELATIVE *sd = pSecurityDescriptor;
            if (sd->Owner) *Owner = (PSID)SELF_RELATIVE_FIELD( sd, Owner );
            else *Owner = NULL;
        }
        else
            *Owner = lpsd->Owner;

	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlSetOwnerSecurityDescriptor		[NTDLL.@]
 */
NTSTATUS WINAPI RtlSetOwnerSecurityDescriptor(
	PSECURITY_DESCRIPTOR pSecurityDescriptor,
	PSID owner,
	BOOLEAN ownerdefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	if (lpsd->Revision!=SECURITY_DESCRIPTOR_REVISION)
		return STATUS_UNKNOWN_REVISION;
	if (lpsd->Control & SE_SELF_RELATIVE)
		return STATUS_INVALID_SECURITY_DESCR;

	lpsd->Owner = owner;
	if (ownerdefaulted)
		lpsd->Control |= SE_OWNER_DEFAULTED;
	else
		lpsd->Control &= ~SE_OWNER_DEFAULTED;
	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlSetGroupSecurityDescriptor		[NTDLL.@]
 */
NTSTATUS WINAPI RtlSetGroupSecurityDescriptor (
	PSECURITY_DESCRIPTOR pSecurityDescriptor,
	PSID group,
	BOOLEAN groupdefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	if (lpsd->Revision!=SECURITY_DESCRIPTOR_REVISION)
		return STATUS_UNKNOWN_REVISION;
	if (lpsd->Control & SE_SELF_RELATIVE)
		return STATUS_INVALID_SECURITY_DESCR;

	lpsd->Group = group;
	if (groupdefaulted)
		lpsd->Control |= SE_GROUP_DEFAULTED;
	else
		lpsd->Control &= ~SE_GROUP_DEFAULTED;
	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlGetGroupSecurityDescriptor		[NTDLL.@]
 */
NTSTATUS WINAPI RtlGetGroupSecurityDescriptor(
	PSECURITY_DESCRIPTOR pSecurityDescriptor,
	PSID *Group,
	PBOOLEAN GroupDefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	if ( !lpsd || !Group || !GroupDefaulted )
		return STATUS_INVALID_PARAMETER;

        if ( lpsd->Control & SE_GROUP_DEFAULTED )
            *GroupDefaulted = TRUE;
        else
            *GroupDefaulted = FALSE;

        if (lpsd->Control & SE_SELF_RELATIVE)
        {
            SECURITY_DESCRIPTOR_RELATIVE *sd = pSecurityDescriptor;
            if (sd->Group) *Group = (PSID)SELF_RELATIVE_FIELD( sd, Group );
            else *Group = NULL;
        }
        else
            *Group = lpsd->Group;

	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlMakeSelfRelativeSD		[NTDLL.@]
 */
NTSTATUS WINAPI RtlMakeSelfRelativeSD(
	IN PSECURITY_DESCRIPTOR pAbsoluteSecurityDescriptor,
	IN PSECURITY_DESCRIPTOR pSelfRelativeSecurityDescriptor,
	IN OUT LPDWORD lpdwBufferLength)
{
    DWORD offsetRel;
    ULONG length;
    SECURITY_DESCRIPTOR* pAbs = pAbsoluteSecurityDescriptor;
    SECURITY_DESCRIPTOR_RELATIVE *pRel = pSelfRelativeSecurityDescriptor;

    TRACE(" %p %p %p(%d)\n", pAbs, pRel, lpdwBufferLength,
        lpdwBufferLength ? *lpdwBufferLength: -1);

    if (!lpdwBufferLength || !pAbs)
        return STATUS_INVALID_PARAMETER;

    length = RtlLengthSecurityDescriptor(pAbs);
    if (*lpdwBufferLength < length)
    {
        *lpdwBufferLength = length;
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (!pRel)
        return STATUS_INVALID_PARAMETER;

    if (pAbs->Control & SE_SELF_RELATIVE)
    {
        memcpy(pRel, pAbs, length);
        return STATUS_SUCCESS;
    }

    pRel->Revision = pAbs->Revision;
    pRel->Sbz1 = pAbs->Sbz1;
    pRel->Control = pAbs->Control | SE_SELF_RELATIVE;

    offsetRel = sizeof(SECURITY_DESCRIPTOR_RELATIVE);
    if (pAbs->Owner)
    {
        pRel->Owner = offsetRel;
        length = RtlLengthSid(pAbs->Owner);
        memcpy((LPBYTE)pRel + offsetRel, pAbs->Owner, length);
        offsetRel += length;
    }
    else
    {
        pRel->Owner = 0;
    }

    if (pAbs->Group)
    {
        pRel->Group = offsetRel;
        length = RtlLengthSid(pAbs->Group);
        memcpy((LPBYTE)pRel + offsetRel, pAbs->Group, length);
        offsetRel += length;
    }
    else
    {
        pRel->Group = 0;
    }

    if (pAbs->Sacl)
    {
        pRel->Sacl = offsetRel;
        length = pAbs->Sacl->AclSize;
        memcpy((LPBYTE)pRel + offsetRel, pAbs->Sacl, length);
        offsetRel += length;
    }
    else
    {
        pRel->Sacl = 0;
    }

    if (pAbs->Dacl)
    {
        pRel->Dacl = offsetRel;
        length = pAbs->Dacl->AclSize;
        memcpy((LPBYTE)pRel + offsetRel, pAbs->Dacl, length);
    }
    else
    {
        pRel->Dacl = 0;
    }

    return STATUS_SUCCESS;
}


/**************************************************************************
 *                 RtlSelfRelativeToAbsoluteSD [NTDLL.@]
 */
NTSTATUS WINAPI RtlSelfRelativeToAbsoluteSD(
        IN PSECURITY_DESCRIPTOR pSelfRelativeSecurityDescriptor,
	OUT PSECURITY_DESCRIPTOR pAbsoluteSecurityDescriptor,
	OUT LPDWORD lpdwAbsoluteSecurityDescriptorSize,
	OUT PACL pDacl,
	OUT LPDWORD lpdwDaclSize,
	OUT PACL pSacl,
	OUT LPDWORD lpdwSaclSize,
	OUT PSID pOwner,
	OUT LPDWORD lpdwOwnerSize,
	OUT PSID pPrimaryGroup,
	OUT LPDWORD lpdwPrimaryGroupSize)
{
    NTSTATUS status = STATUS_SUCCESS;
    SECURITY_DESCRIPTOR* pAbs = pAbsoluteSecurityDescriptor;
    SECURITY_DESCRIPTOR_RELATIVE* pRel = pSelfRelativeSecurityDescriptor;

    if (!pRel ||
        !lpdwAbsoluteSecurityDescriptorSize ||
        !lpdwDaclSize ||
        !lpdwSaclSize ||
        !lpdwOwnerSize ||
        !lpdwPrimaryGroupSize ||
        ~pRel->Control & SE_SELF_RELATIVE)
        return STATUS_INVALID_PARAMETER;

    /* Confirm buffers are sufficiently large */
    if (*lpdwAbsoluteSecurityDescriptorSize < sizeof(SECURITY_DESCRIPTOR))
    {
        *lpdwAbsoluteSecurityDescriptorSize = sizeof(SECURITY_DESCRIPTOR);
        status = STATUS_BUFFER_TOO_SMALL;
    }

    if ((pRel->Control & SE_DACL_PRESENT) && pRel->Dacl &&
        *lpdwDaclSize  < ((PACL)SELF_RELATIVE_FIELD(pRel,Dacl))->AclSize)
    {
        *lpdwDaclSize = ((PACL)SELF_RELATIVE_FIELD(pRel,Dacl))->AclSize;
        status = STATUS_BUFFER_TOO_SMALL;
    }

    if ((pRel->Control & SE_SACL_PRESENT) && pRel->Sacl &&
        *lpdwSaclSize  < ((PACL)SELF_RELATIVE_FIELD(pRel,Sacl))->AclSize)
    {
        *lpdwSaclSize = ((PACL)SELF_RELATIVE_FIELD(pRel,Sacl))->AclSize;
        status = STATUS_BUFFER_TOO_SMALL;
    }

    if (pRel->Owner &&
        *lpdwOwnerSize < RtlLengthSid((PSID)SELF_RELATIVE_FIELD(pRel,Owner)))
    {
        *lpdwOwnerSize = RtlLengthSid((PSID)SELF_RELATIVE_FIELD(pRel,Owner));
        status = STATUS_BUFFER_TOO_SMALL;
    }

    if (pRel->Group &&
        *lpdwPrimaryGroupSize < RtlLengthSid((PSID)SELF_RELATIVE_FIELD(pRel,Group)))
    {
        *lpdwPrimaryGroupSize = RtlLengthSid((PSID)SELF_RELATIVE_FIELD(pRel,Group));
        status = STATUS_BUFFER_TOO_SMALL;
    }

    if (status != STATUS_SUCCESS)
        return status;

    /* Copy structures, and clear the ones we don't set */
    pAbs->Revision = pRel->Revision;
    pAbs->Control = pRel->Control & ~SE_SELF_RELATIVE;
    pAbs->Sacl = NULL;
    pAbs->Dacl = NULL;
    pAbs->Owner = NULL;
    pAbs->Group = NULL;

    if ((pRel->Control & SE_SACL_PRESENT) && pRel->Sacl)
    {
        PACL pAcl = (PACL)SELF_RELATIVE_FIELD( pRel, Sacl );

        memcpy(pSacl, pAcl, pAcl->AclSize);
        pAbs->Sacl = pSacl;
    }

    if ((pRel->Control & SE_DACL_PRESENT) && pRel->Dacl)
    {
        PACL pAcl = (PACL)SELF_RELATIVE_FIELD( pRel, Dacl );
        memcpy(pDacl, pAcl, pAcl->AclSize);
        pAbs->Dacl = pDacl;
    }

    if (pRel->Owner)
    {
        PSID psid = (PSID)SELF_RELATIVE_FIELD( pRel, Owner );
        memcpy(pOwner, psid, RtlLengthSid(psid));
        pAbs->Owner = pOwner;
    }

    if (pRel->Group)
    {
        PSID psid = (PSID)SELF_RELATIVE_FIELD( pRel, Group );
        memcpy(pPrimaryGroup, psid, RtlLengthSid(psid));
        pAbs->Group = pPrimaryGroup;
    }

    return status;
}

/******************************************************************************
 * RtlGetControlSecurityDescriptor (NTDLL.@)
 */
NTSTATUS WINAPI RtlGetControlSecurityDescriptor(
    PSECURITY_DESCRIPTOR pSecurityDescriptor,
    PSECURITY_DESCRIPTOR_CONTROL pControl,
    LPDWORD lpdwRevision)
{
    SECURITY_DESCRIPTOR *lpsd = pSecurityDescriptor;

    TRACE("(%p,%p,%p)\n",pSecurityDescriptor,pControl,lpdwRevision);

    *lpdwRevision = lpsd->Revision;

    if (*lpdwRevision != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;

    *pControl = lpsd->Control;

    return STATUS_SUCCESS;
}

/******************************************************************************
 * RtlSetControlSecurityDescriptor (NTDLL.@)
 */
NTSTATUS WINAPI RtlSetControlSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor,
    SECURITY_DESCRIPTOR_CONTROL ControlBitsOfInterest,
    SECURITY_DESCRIPTOR_CONTROL ControlBitsToSet)
{
    SECURITY_DESCRIPTOR_CONTROL const immutable
       = SE_OWNER_DEFAULTED  | SE_GROUP_DEFAULTED
       | SE_DACL_PRESENT     | SE_DACL_DEFAULTED
       | SE_SACL_PRESENT     | SE_SACL_DEFAULTED
       | SE_RM_CONTROL_VALID | SE_SELF_RELATIVE
       ;

    SECURITY_DESCRIPTOR *lpsd = SecurityDescriptor;

    TRACE("(%p 0x%04x 0x%04x)\n", SecurityDescriptor,
          ControlBitsOfInterest, ControlBitsToSet);

    if ((ControlBitsOfInterest | ControlBitsToSet) & immutable)
        return STATUS_INVALID_PARAMETER;

    lpsd->Control |=  (ControlBitsOfInterest &  ControlBitsToSet);
    lpsd->Control &= ~(ControlBitsOfInterest & ~ControlBitsToSet);

    return STATUS_SUCCESS;
}


/**************************************************************************
 *                 RtlAbsoluteToSelfRelativeSD [NTDLL.@]
 */
NTSTATUS WINAPI RtlAbsoluteToSelfRelativeSD(
    PSECURITY_DESCRIPTOR AbsoluteSecurityDescriptor,
    PSECURITY_DESCRIPTOR SelfRelativeSecurityDescriptor,
    PULONG BufferLength)
{
    SECURITY_DESCRIPTOR *abs = AbsoluteSecurityDescriptor;

    TRACE("%p %p %p\n", AbsoluteSecurityDescriptor,
          SelfRelativeSecurityDescriptor, BufferLength);

    if (abs->Control & SE_SELF_RELATIVE)
        return STATUS_BAD_DESCRIPTOR_FORMAT;

    return RtlMakeSelfRelativeSD(AbsoluteSecurityDescriptor, 
        SelfRelativeSecurityDescriptor, BufferLength);
}


/*
 *	access control list's
 */

/**************************************************************************
 *                 RtlCreateAcl				[NTDLL.@]
 *
 * NOTES
 *    This should return NTSTATUS
 */
NTSTATUS WINAPI RtlCreateAcl(PACL acl,DWORD size,DWORD rev)
{
	TRACE("%p 0x%08x 0x%08x\n", acl, size, rev);

	if (rev < MIN_ACL_REVISION || rev > MAX_ACL_REVISION)
		return STATUS_INVALID_PARAMETER;
	if (size<sizeof(ACL))
		return STATUS_BUFFER_TOO_SMALL;
	if (size>0xFFFF)
		return STATUS_INVALID_PARAMETER;

	memset(acl,'\0',sizeof(ACL));
	acl->AclRevision	= rev;
	acl->AclSize		= size;
	acl->AceCount		= 0;
	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlFirstFreeAce			[NTDLL.@]
 * looks for the AceCount+1 ACE, and if it is still within the alloced
 * ACL, return a pointer to it
 */
BOOLEAN WINAPI RtlFirstFreeAce(
	PACL acl,
	PACE_HEADER *x)
{
	PACE_HEADER	ace;
	int		i;

	*x = 0;
	ace = (PACE_HEADER)(acl+1);
	for (i=0;i<acl->AceCount;i++) {
		if ((BYTE *)ace >= (BYTE *)acl + acl->AclSize)
			return FALSE;
		ace = (PACE_HEADER)(((BYTE*)ace)+ace->AceSize);
	}
	if ((BYTE *)ace >= (BYTE *)acl + acl->AclSize)
		return FALSE;
	*x = ace;
	return TRUE;
}

/**************************************************************************
 *                 RtlAddAce				[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAce(
	PACL acl,
	DWORD rev,
	DWORD xnrofaces,
	PACE_HEADER acestart,
	DWORD acelen)
{
	PACE_HEADER	ace,targetace;
	int		nrofaces;

	if (!RtlValidAcl(acl))
		return STATUS_INVALID_PARAMETER;
	if (!RtlFirstFreeAce(acl,&targetace))
		return STATUS_INVALID_PARAMETER;
	nrofaces=0;ace=acestart;
	while (((BYTE *)ace - (BYTE *)acestart) < acelen) {
		nrofaces++;
		ace = (PACE_HEADER)(((BYTE*)ace)+ace->AceSize);
	}
	if ((BYTE *)targetace + acelen > (BYTE *)acl + acl->AclSize) /* too much aces */
		return STATUS_INVALID_PARAMETER;
	memcpy(targetace,acestart,acelen);
	acl->AceCount+=nrofaces;
	if (rev > acl->AclRevision)
		acl->AclRevision = rev;
	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlDeleteAce				[NTDLL.@]
 */
NTSTATUS  WINAPI RtlDeleteAce(PACL pAcl, DWORD dwAceIndex)
{
	NTSTATUS status;
	PACE_HEADER pAce;

	status = RtlGetAce(pAcl,dwAceIndex,(LPVOID*)&pAce);

	if (STATUS_SUCCESS == status)
	{
		PACE_HEADER pcAce;
		DWORD len = 0;

		/* skip over the ACE we are deleting */
		pcAce = (PACE_HEADER)(((BYTE*)pAce)+pAce->AceSize);
		dwAceIndex++;

		/* calculate the length of the rest */
		for (; dwAceIndex < pAcl->AceCount; dwAceIndex++)
		{
			len += pcAce->AceSize;
			pcAce = (PACE_HEADER)(((BYTE*)pcAce) + pcAce->AceSize);
		}

		/* slide them all backwards */
		memmove(pAce, ((BYTE*)pAce)+pAce->AceSize, len);
		pAcl->AceCount--;
	}

	TRACE("pAcl=%p dwAceIndex=%d status=0x%08x\n", pAcl, dwAceIndex, status);

	return status;
}

/******************************************************************************
 *  RtlAddAccessAllowedAce		[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAccessAllowedAce(
	IN OUT PACL pAcl,
	IN DWORD dwAceRevision,
	IN DWORD AccessMask,
	IN PSID pSid)
{
	return RtlAddAccessAllowedAceEx( pAcl, dwAceRevision, 0, AccessMask, pSid);
}
 
/******************************************************************************
 *  RtlAddAccessAllowedAceEx		[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAccessAllowedAceEx(
	IN OUT PACL pAcl,
	IN DWORD dwAceRevision,
	IN DWORD AceFlags,
	IN DWORD AccessMask,
	IN PSID pSid)
{
   TRACE("(%p,0x%08x,0x%08x,%p)\n", pAcl, dwAceRevision, AccessMask, pSid);

    return add_access_ace(pAcl, dwAceRevision, AceFlags,
                          AccessMask, pSid, ACCESS_ALLOWED_ACE_TYPE);
}

/******************************************************************************
 *  RtlAddAccessAllowedObjectAce		[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAccessAllowedObjectAce(
    IN OUT PACL pAcl,
    IN DWORD dwAceRevision,
    IN DWORD dwAceFlags,
    IN DWORD dwAccessMask,
    IN GUID* pObjectTypeGuid,
    IN GUID* pInheritedObjectTypeGuid,
    IN PSID pSid)
{
    FIXME("%p %x %x %x %p %p %p - stub\n", pAcl, dwAceRevision, dwAceFlags, dwAccessMask,
          pObjectTypeGuid, pInheritedObjectTypeGuid, pSid);
    return STATUS_NOT_IMPLEMENTED;
}

/******************************************************************************
 *  RtlAddAccessDeniedAce		[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAccessDeniedAce(
	IN OUT PACL pAcl,
	IN DWORD dwAceRevision,
	IN DWORD AccessMask,
	IN PSID pSid)
{
	return RtlAddAccessDeniedAceEx( pAcl, dwAceRevision, 0, AccessMask, pSid);
}

/******************************************************************************
 *  RtlAddAccessDeniedAceEx		[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAccessDeniedAceEx(
	IN OUT PACL pAcl,
	IN DWORD dwAceRevision,
	IN DWORD AceFlags,
	IN DWORD AccessMask,
	IN PSID pSid)
{
   TRACE("(%p,0x%08x,0x%08x,%p)\n", pAcl, dwAceRevision, AccessMask, pSid);

    return add_access_ace(pAcl, dwAceRevision, AceFlags,
                          AccessMask, pSid, ACCESS_DENIED_ACE_TYPE);
}

/******************************************************************************
 *  RtlAddAccessDeniedObjectAce [NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAccessDeniedObjectAce(
    IN OUT PACL pAcl,
    IN DWORD dwAceRevision,
    IN DWORD dwAceFlags,
    IN DWORD dwAccessMask,
    IN GUID* pObjectTypeGuid,
    IN GUID* pInheritedObjectTypeGuid,
    IN PSID pSid)
{
    FIXME("%p %x %x %x %p %p %p - stub\n", pAcl, dwAceRevision, dwAceFlags, dwAccessMask,
          pObjectTypeGuid, pInheritedObjectTypeGuid, pSid);
    return STATUS_NOT_IMPLEMENTED;
}

/************************************************************************** 
 *  RtlAddAuditAccessAce     [NTDLL.@] 
 */ 
NTSTATUS WINAPI RtlAddAuditAccessAceEx(
    IN OUT PACL pAcl, 
    IN DWORD dwAceRevision, 
    IN DWORD dwAceFlags,
    IN DWORD dwAccessMask, 
    IN PSID pSid, 
    IN BOOL bAuditSuccess, 
    IN BOOL bAuditFailure) 
{ 
    TRACE("(%p,%d,0x%08x,0x%08x,%p,%u,%u)\n",pAcl,dwAceRevision,dwAceFlags,dwAccessMask,
          pSid,bAuditSuccess,bAuditFailure);

    if (bAuditSuccess)
        dwAceFlags |= SUCCESSFUL_ACCESS_ACE_FLAG;

    if (bAuditFailure)
        dwAceFlags |= FAILED_ACCESS_ACE_FLAG;

    return add_access_ace(pAcl, dwAceRevision, dwAceFlags,
                          dwAccessMask, pSid, SYSTEM_AUDIT_ACE_TYPE);
} 

/**************************************************************************
 *  RtlAddAuditAccessAce     [NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAuditAccessAce(
    IN OUT PACL pAcl,
    IN DWORD dwAceRevision,
    IN DWORD dwAccessMask,
    IN PSID pSid,
    IN BOOL bAuditSuccess,
    IN BOOL bAuditFailure)
{
    return RtlAddAuditAccessAceEx(pAcl, dwAceRevision, 0, dwAccessMask, pSid, bAuditSuccess, bAuditFailure);
}

/******************************************************************************
 *  RtlAddAuditAccessObjectAce [NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAuditAccessObjectAce(
    IN OUT PACL pAcl,
    IN DWORD dwAceRevision,
    IN DWORD dwAceFlags,
    IN DWORD dwAccessMask,
    IN GUID* pObjectTypeGuid,
    IN GUID* pInheritedObjectTypeGuid,
    IN PSID pSid,
    IN BOOL bAuditSuccess,
    IN BOOL bAuditFailure)
{
    FIXME("%p %x %x %x %p %p %p %d %d - stub\n", pAcl, dwAceRevision, dwAceFlags, dwAccessMask,
          pObjectTypeGuid, pInheritedObjectTypeGuid, pSid, bAuditSuccess, bAuditFailure);
    return STATUS_NOT_IMPLEMENTED;
}

/**************************************************************************
 *  RtlAddMandatoryAce     [NTDLL.@]
 */
NTSTATUS WINAPI RtlAddMandatoryAce(
    IN OUT PACL pAcl,
    IN DWORD dwAceRevision,
    IN DWORD dwAceFlags,
    IN DWORD dwMandatoryFlags,
    IN DWORD dwAceType,
    IN PSID pSid)
{
    static const DWORD valid_flags = SYSTEM_MANDATORY_LABEL_NO_WRITE_UP |
                                     SYSTEM_MANDATORY_LABEL_NO_READ_UP |
                                     SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP;

    TRACE("(%p, %u, 0x%08x, 0x%08x, %u, %p)\n", pAcl, dwAceRevision, dwAceFlags,
                                                dwMandatoryFlags, dwAceType, pSid);

    if (dwAceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE)
        return STATUS_INVALID_PARAMETER;

    if (dwMandatoryFlags & ~valid_flags)
        return STATUS_INVALID_PARAMETER;

    return add_access_ace(pAcl, dwAceRevision, dwAceFlags, dwMandatoryFlags, pSid, dwAceType);
}

/******************************************************************************
 *  RtlValidAcl		[NTDLL.@]
 */
BOOLEAN WINAPI RtlValidAcl(PACL pAcl)
{
        BOOLEAN ret;
	TRACE("(%p)\n", pAcl);

	__TRY
	{
		PACE_HEADER	ace;
		int		i;

                if (pAcl->AclRevision < MIN_ACL_REVISION ||
                    pAcl->AclRevision > MAX_ACL_REVISION)
                    ret = FALSE;
                else
                {
                    ace = (PACE_HEADER)(pAcl+1);
                    ret = TRUE;
                    for (i=0;i<=pAcl->AceCount;i++)
                    {
                        if ((char *)ace > (char *)pAcl + pAcl->AclSize)
                        {
                            ret = FALSE;
                            break;
                        }
                        if (i != pAcl->AceCount)
                            ace = (PACE_HEADER)(((BYTE*)ace)+ace->AceSize);
                    }
                }
	}
	__EXCEPT_PAGE_FAULT
	{
		WARN("(%p): invalid pointer!\n", pAcl);
		return FALSE;
	}
	__ENDTRY
        return ret;
}

/******************************************************************************
 *  RtlGetAce		[NTDLL.@]
 */
NTSTATUS WINAPI RtlGetAce(PACL pAcl,DWORD dwAceIndex,LPVOID *pAce )
{
	PACE_HEADER ace;

	TRACE("(%p,%d,%p)\n",pAcl,dwAceIndex,pAce);

	if (dwAceIndex >= pAcl->AceCount)
		return STATUS_INVALID_PARAMETER;

	ace = (PACE_HEADER)(pAcl + 1);
	for (;dwAceIndex;dwAceIndex--)
		ace = (PACE_HEADER)(((BYTE*)ace)+ace->AceSize);

	*pAce = ace;

	return STATUS_SUCCESS;
}

/*
 *	misc
 */

/******************************************************************************
 *  RtlAdjustPrivilege		[NTDLL.@]
 *
 * Enables or disables a privilege from the calling thread or process.
 *
 * PARAMS
 *  Privilege     [I] Privilege index to change.
 *  Enable        [I] If TRUE, then enable the privilege otherwise disable.
 *  CurrentThread [I] If TRUE, then enable in calling thread, otherwise process.
 *  Enabled       [O] Whether privilege was previously enabled or disabled.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: NTSTATUS code.
 *
 * SEE ALSO
 *  NtAdjustPrivilegesToken, NtOpenThreadToken, NtOpenProcessToken.
 *
 */
NTSTATUS WINAPI
RtlAdjustPrivilege(ULONG Privilege,
                   BOOLEAN Enable,
                   BOOLEAN CurrentThread,
                   PBOOLEAN Enabled)
{
    TOKEN_PRIVILEGES NewState;
    TOKEN_PRIVILEGES OldState;
    ULONG ReturnLength;
    HANDLE TokenHandle;
    NTSTATUS Status;

    TRACE("(%d, %s, %s, %p)\n", Privilege, Enable ? "TRUE" : "FALSE",
        CurrentThread ? "TRUE" : "FALSE", Enabled);

    if (CurrentThread)
    {
        Status = NtOpenThreadToken(GetCurrentThread(),
                                   TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                                   FALSE,
                                   &TokenHandle);
    }
    else
    {
        Status = NtOpenProcessToken(GetCurrentProcess(),
                                    TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                                    &TokenHandle);
    }

    if (!NT_SUCCESS(Status))
    {
        WARN("Retrieving token handle failed (Status %x)\n", Status);
        return Status;
    }

    OldState.PrivilegeCount = 1;

    NewState.PrivilegeCount = 1;
    NewState.Privileges[0].Luid.LowPart = Privilege;
    NewState.Privileges[0].Luid.HighPart = 0;
    NewState.Privileges[0].Attributes = (Enable) ? SE_PRIVILEGE_ENABLED : 0;

    Status = NtAdjustPrivilegesToken(TokenHandle,
                                     FALSE,
                                     &NewState,
                                     sizeof(TOKEN_PRIVILEGES),
                                     &OldState,
                                     &ReturnLength);
    NtClose (TokenHandle);
    if (Status == STATUS_NOT_ALL_ASSIGNED)
    {
        TRACE("Failed to assign all privileges\n");
        return STATUS_PRIVILEGE_NOT_HELD;
    }
    if (!NT_SUCCESS(Status))
    {
        WARN("NtAdjustPrivilegesToken() failed (Status %x)\n", Status);
        return Status;
    }

    if (OldState.PrivilegeCount == 0)
        *Enabled = Enable;
    else
        *Enabled = (OldState.Privileges[0].Attributes & SE_PRIVILEGE_ENABLED);

    return STATUS_SUCCESS;
}

/******************************************************************************
 *  RtlImpersonateSelf		[NTDLL.@]
 *
 * Makes an impersonation token that represents the process user and assigns
 * to the current thread.
 *
 * PARAMS
 *  ImpersonationLevel [I] Level at which to impersonate.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: NTSTATUS code.
 */
NTSTATUS WINAPI
RtlImpersonateSelf(SECURITY_IMPERSONATION_LEVEL ImpersonationLevel)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE ProcessToken;
    HANDLE ImpersonationToken;

    TRACE("(%08x)\n", ImpersonationLevel);

    Status = NtOpenProcessToken( NtCurrentProcess(), TOKEN_DUPLICATE,
                                 &ProcessToken);
    if (Status != STATUS_SUCCESS)
        return Status;

    InitializeObjectAttributes( &ObjectAttributes, NULL, 0, NULL, NULL );

    Status = NtDuplicateToken( ProcessToken,
                               TOKEN_IMPERSONATE,
                               &ObjectAttributes,
                               ImpersonationLevel,
                               TokenImpersonation,
                               &ImpersonationToken );
    if (Status != STATUS_SUCCESS)
    {
        NtClose( ProcessToken );
        return Status;
    }

    Status = NtSetInformationThread( GetCurrentThread(),
                                     ThreadImpersonationToken,
                                     &ImpersonationToken,
                                     sizeof(ImpersonationToken) );

    NtClose( ImpersonationToken );
    NtClose( ProcessToken );

    return Status;
}

/******************************************************************************
 *  NtImpersonateAnonymousToken      [NTDLL.@]
 */
NTSTATUS WINAPI
NtImpersonateAnonymousToken(HANDLE thread)
{
    FIXME("(%p): stub\n", thread);
    return STATUS_NOT_IMPLEMENTED;
}

/******************************************************************************
 *  NtAccessCheck		[NTDLL.@]
 *  ZwAccessCheck		[NTDLL.@]
 *
 * Checks that a user represented by a token is allowed to access an object
 * represented by a security descriptor.
 *
 * PARAMS
 *  SecurityDescriptor [I] The security descriptor of the object to check.
 *  ClientToken        [I] Token of the user accessing the object.
 *  DesiredAccess      [I] The desired access to the object.
 *  GenericMapping     [I] Mapping used to transform access rights in the SD to their specific forms.
 *  PrivilegeSet       [I/O] Privileges used during the access check.
 *  ReturnLength       [O] Number of bytes stored into PrivilegeSet.
 *  GrantedAccess      [O] The actual access rights granted.
 *  AccessStatus       [O] The status of the access check.
 *
 * RETURNS
 *  NTSTATUS code.
 *
 * NOTES
 *  DesiredAccess may be MAXIMUM_ALLOWED, in which case the function determines
 *  the maximum access rights allowed by the SD and returns them in
 *  GrantedAccess.
 *  The SecurityDescriptor must have a valid owner and groups present,
 *  otherwise the function will fail.
 */
NTSTATUS WINAPI
NtAccessCheck(
    PSECURITY_DESCRIPTOR SecurityDescriptor,
    HANDLE ClientToken,
    ACCESS_MASK DesiredAccess,
    PGENERIC_MAPPING GenericMapping,
    PPRIVILEGE_SET PrivilegeSet,
    PULONG ReturnLength,
    PULONG GrantedAccess,
    NTSTATUS *AccessStatus)
{
    NTSTATUS status;

    TRACE("(%p, %p, %08x, %p, %p, %p, %p, %p)\n",
        SecurityDescriptor, ClientToken, DesiredAccess, GenericMapping,
        PrivilegeSet, ReturnLength, GrantedAccess, AccessStatus);

    if (!ReturnLength)
        return STATUS_ACCESS_VIOLATION;

    if (*ReturnLength == 0)
    {
        *ReturnLength = sizeof(PRIVILEGE_SET);
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (!PrivilegeSet)
        return STATUS_ACCESS_VIOLATION;

    SERVER_START_REQ( access_check )
    {
        struct security_descriptor sd;
        PSID owner;
        PSID group;
        PACL sacl;
        PACL dacl;
        BOOLEAN defaulted, present;
        DWORD revision;
        SECURITY_DESCRIPTOR_CONTROL control;

        req->handle = wine_server_obj_handle( ClientToken );
        req->desired_access = DesiredAccess;
        req->mapping_read = GenericMapping->GenericRead;
        req->mapping_write = GenericMapping->GenericWrite;
        req->mapping_execute = GenericMapping->GenericExecute;
        req->mapping_all = GenericMapping->GenericAll;

        /* marshal security descriptor */
        RtlGetControlSecurityDescriptor( SecurityDescriptor, &control, &revision );
        sd.control = control & ~SE_SELF_RELATIVE;
        RtlGetOwnerSecurityDescriptor( SecurityDescriptor, &owner, &defaulted );
        sd.owner_len = RtlLengthSid( owner );
        RtlGetGroupSecurityDescriptor( SecurityDescriptor, &group, &defaulted );
        sd.group_len = RtlLengthSid( group );
        RtlGetSaclSecurityDescriptor( SecurityDescriptor, &present, &sacl, &defaulted );
        sd.sacl_len = ((present && sacl) ? acl_bytesInUse(sacl) : 0);
        RtlGetDaclSecurityDescriptor( SecurityDescriptor, &present, &dacl, &defaulted );
        sd.dacl_len = ((present && dacl) ? acl_bytesInUse(dacl) : 0);

        wine_server_add_data( req, &sd, sizeof(sd) );
        wine_server_add_data( req, owner, sd.owner_len );
        wine_server_add_data( req, group, sd.group_len );
        wine_server_add_data( req, sacl, sd.sacl_len );
        wine_server_add_data( req, dacl, sd.dacl_len );

        wine_server_set_reply( req, PrivilegeSet->Privilege, *ReturnLength - FIELD_OFFSET( PRIVILEGE_SET, Privilege ) );

        status = wine_server_call( req );

        *ReturnLength = FIELD_OFFSET( PRIVILEGE_SET, Privilege ) + reply->privileges_len;
        PrivilegeSet->PrivilegeCount = reply->privileges_len / sizeof(LUID_AND_ATTRIBUTES);

        if (status == STATUS_SUCCESS)
        {
            *AccessStatus = reply->access_status;
            *GrantedAccess = reply->access_granted;
        }
    }
    SERVER_END_REQ;

    return status;
}

/******************************************************************************
 *  NtSetSecurityObject		[NTDLL.@]
 *  ZwSetSecurityObject		[NTDLL.@]
 *
 * Sets specified parts of the object's security descriptor.
 *
 * PARAMS
 *  Handle              [I] Handle to the object to change security descriptor of.
 *  SecurityInformation [I] Specifies which parts of the security descriptor to set.
 *  SecurityDescriptor  [I] New parts of a security descriptor for the object.
 *
 * RETURNS
 *  NTSTATUS code.
 *
 */
NTSTATUS WINAPI NtSetSecurityObject(HANDLE Handle,
        SECURITY_INFORMATION SecurityInformation,
        PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    NTSTATUS status;
    struct security_descriptor sd;
    PACL dacl = NULL, sacl = NULL;
    PSID owner = NULL, group = NULL;
    BOOLEAN defaulted, present;
    DWORD revision;
    SECURITY_DESCRIPTOR_CONTROL control;

    TRACE("%p 0x%08x %p\n", Handle, SecurityInformation, SecurityDescriptor);

    if (!SecurityDescriptor) return STATUS_ACCESS_VIOLATION;

    memset( &sd, 0, sizeof(sd) );
    status = RtlGetControlSecurityDescriptor( SecurityDescriptor, &control, &revision );
    if (status != STATUS_SUCCESS) return status;
    sd.control = control & ~SE_SELF_RELATIVE;

    if (SecurityInformation & OWNER_SECURITY_INFORMATION)
    {
        status = RtlGetOwnerSecurityDescriptor( SecurityDescriptor, &owner, &defaulted );
        if (status != STATUS_SUCCESS) return status;
        if (!(sd.owner_len = RtlLengthSid( owner )))
            return STATUS_INVALID_SECURITY_DESCR;
    }

    if (SecurityInformation & GROUP_SECURITY_INFORMATION)
    {
        status = RtlGetGroupSecurityDescriptor( SecurityDescriptor, &group, &defaulted );
        if (status != STATUS_SUCCESS) return status;
        if (!(sd.group_len = RtlLengthSid( group )))
            return STATUS_INVALID_SECURITY_DESCR;
    }

    if (SecurityInformation & SACL_SECURITY_INFORMATION ||
        SecurityInformation & LABEL_SECURITY_INFORMATION)
    {
        status = RtlGetSaclSecurityDescriptor( SecurityDescriptor, &present, &sacl, &defaulted );
        if (status != STATUS_SUCCESS) return status;
        sd.sacl_len = (sacl && present) ? acl_bytesInUse(sacl) : 0;
        sd.control |= SE_SACL_PRESENT;
    }

    if (SecurityInformation & DACL_SECURITY_INFORMATION)
    {
        status = RtlGetDaclSecurityDescriptor( SecurityDescriptor, &present, &dacl, &defaulted );
        if (status != STATUS_SUCCESS) return status;
        sd.dacl_len = (dacl && present) ? acl_bytesInUse(dacl) : 0;
        sd.control |= SE_DACL_PRESENT;
    }

    SERVER_START_REQ( set_security_object )
    {
        req->handle = wine_server_obj_handle( Handle );
        req->security_info = SecurityInformation;

        wine_server_add_data( req, &sd, sizeof(sd) );
        wine_server_add_data( req, owner, sd.owner_len );
        wine_server_add_data( req, group, sd.group_len );
        wine_server_add_data( req, sacl, sd.sacl_len );
        wine_server_add_data( req, dacl, sd.dacl_len );
        status = wine_server_call( req );
    }
    SERVER_END_REQ;

    return status;
}

/******************************************************************************
 * RtlConvertSidToUnicodeString (NTDLL.@)
 *
 * The returned SID is used to access the USER registry hive usually
 *
 * the native function returns something like
 * "S-1-5-21-0000000000-000000000-0000000000-500";
 */
NTSTATUS WINAPI RtlConvertSidToUnicodeString(
       PUNICODE_STRING String,
       PSID pSid,
       BOOLEAN AllocateString)
{
    static const WCHAR formatW[] = {'-','%','u',0};
    WCHAR buffer[2 + 10 + 10 + 10 * SID_MAX_SUB_AUTHORITIES];
    WCHAR *p = buffer;
    const SID *sid = pSid;
    DWORD i, len;

    *p++ = 'S';
    p += sprintfW( p, formatW, sid->Revision );
    p += sprintfW( p, formatW, MAKELONG( MAKEWORD( sid->IdentifierAuthority.Value[5],
                                                   sid->IdentifierAuthority.Value[4] ),
                                         MAKEWORD( sid->IdentifierAuthority.Value[3],
                                                   sid->IdentifierAuthority.Value[2] )));
    for (i = 0; i < sid->SubAuthorityCount; i++)
        p += sprintfW( p, formatW, sid->SubAuthority[i] );

    len = (p + 1 - buffer) * sizeof(WCHAR);

    String->Length = len - sizeof(WCHAR);
    if (AllocateString)
    {
        String->MaximumLength = len;
        if (!(String->Buffer = RtlAllocateHeap( GetProcessHeap(), 0, len )))
            return STATUS_NO_MEMORY;
    }
    else if (len > String->MaximumLength) return STATUS_BUFFER_OVERFLOW;

    memcpy( String->Buffer, buffer, len );
    return STATUS_SUCCESS;
}

/******************************************************************************
 * RtlQueryInformationAcl (NTDLL.@)
 */
NTSTATUS WINAPI RtlQueryInformationAcl(
    PACL pAcl,
    LPVOID pAclInformation,
    DWORD nAclInformationLength,
    ACL_INFORMATION_CLASS dwAclInformationClass)
{
    NTSTATUS status = STATUS_SUCCESS;

    TRACE("pAcl=%p pAclInfo=%p len=%d, class=%d\n", 
        pAcl, pAclInformation, nAclInformationLength, dwAclInformationClass);

    switch (dwAclInformationClass)
    {
        case AclRevisionInformation:
        {
            PACL_REVISION_INFORMATION paclrev = pAclInformation;

            if (nAclInformationLength < sizeof(ACL_REVISION_INFORMATION))
                status = STATUS_INVALID_PARAMETER;
            else
                paclrev->AclRevision = pAcl->AclRevision;

            break;
        }

        case AclSizeInformation:
        {
            PACL_SIZE_INFORMATION paclsize = pAclInformation;

            if (nAclInformationLength < sizeof(ACL_SIZE_INFORMATION))
                status = STATUS_INVALID_PARAMETER;
            else
            {
                paclsize->AceCount = pAcl->AceCount;
                paclsize->AclBytesInUse = acl_bytesInUse(pAcl);
		if (pAcl->AclSize < paclsize->AclBytesInUse)
                {
                    WARN("Acl uses %d bytes, but only has %d allocated!  Returning smaller of the two values.\n", pAcl->AclSize, paclsize->AclBytesInUse);
                    paclsize->AclBytesFree = 0;
                    paclsize->AclBytesInUse = pAcl->AclSize;
                }
                else
                    paclsize->AclBytesFree = pAcl->AclSize - paclsize->AclBytesInUse;
            }

            break;
        }

        default:
            WARN("Unknown AclInformationClass value: %d\n", dwAclInformationClass);
            status = STATUS_INVALID_PARAMETER;
    }

    return status;
}

BOOL WINAPI RtlConvertToAutoInheritSecurityObject(
        PSECURITY_DESCRIPTOR pdesc,
        PSECURITY_DESCRIPTOR cdesc,
        PSECURITY_DESCRIPTOR* ndesc,
        GUID* objtype,
        BOOL isdir,
        PGENERIC_MAPPING genmap )
{
    FIXME("%p %p %p %p %d %p - stub\n", pdesc, cdesc, ndesc, objtype, isdir, genmap);

    return FALSE;
}

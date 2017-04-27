/*
 * Mount manager service implementation
 *
 * Copyright 2008 Alexandre Julliard
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

#include <stdarg.h>
#include <unistd.h>

#define NONAMELESSUNION

#include "mountmgr.h"
#include "winreg.h"
#include "wine/library.h"
#include "wine/list.h"
#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mountmgr);

#define MIN_ID_LEN     4

struct mount_point
{
    struct list    entry;   /* entry in mount points list */
    DEVICE_OBJECT *device;  /* disk device */
    UNICODE_STRING name;    /* device name */
    UNICODE_STRING link;    /* DOS device symlink */
    void          *id;      /* device unique id */
    unsigned int   id_len;
};

static struct list mount_points_list = LIST_INIT(mount_points_list);
static HKEY mount_key;

void set_mount_point_id( struct mount_point *mount, const void *id, unsigned int id_len, int drive )
{
    WCHAR logicalW[] = {'\\','\\','.','\\','a',':',0};
    RtlFreeHeap( GetProcessHeap(), 0, mount->id );
    mount->id_len = max( MIN_ID_LEN, id_len );
    if ((mount->id = RtlAllocateHeap( GetProcessHeap(), HEAP_ZERO_MEMORY, mount->id_len )))
    {
        memcpy( mount->id, id, id_len );
        if (drive < 0)
            RegSetValueExW( mount_key, mount->link.Buffer, 0, REG_BINARY, mount->id, mount->id_len );
        else
        {
            logicalW[4] = 'a' + drive;
            RegSetValueExW( mount_key, mount->link.Buffer, 0, REG_BINARY, (BYTE*)logicalW, sizeof(logicalW) );
        }
    }
    else mount->id_len = 0;
}

static struct mount_point *add_mount_point( DEVICE_OBJECT *device, UNICODE_STRING *device_name,
                                            const WCHAR *link )
{
    struct mount_point *mount;
    WCHAR *str;
    UINT len = (strlenW(link) + 1) * sizeof(WCHAR) + device_name->Length + sizeof(WCHAR);

    if (!(mount = RtlAllocateHeap( GetProcessHeap(), 0, sizeof(*mount) + len ))) return NULL;

    str = (WCHAR *)(mount + 1);
    strcpyW( str, link );
    RtlInitUnicodeString( &mount->link, str );
    str += strlenW(str) + 1;
    memcpy( str, device_name->Buffer, device_name->Length );
    str[device_name->Length / sizeof(WCHAR)] = 0;
    mount->name.Buffer = str;
    mount->name.Length = device_name->Length;
    mount->name.MaximumLength = device_name->Length + sizeof(WCHAR);
    mount->device = device;
    mount->id = NULL;
    list_add_tail( &mount_points_list, &mount->entry );

    IoCreateSymbolicLink( &mount->link, device_name );

    TRACE( "created %s id %s for %s\n", debugstr_w(mount->link.Buffer),
           debugstr_a(mount->id), debugstr_w(mount->name.Buffer) );
    return mount;
}

/* create the DosDevices mount point symlink for a new device */
struct mount_point *add_dosdev_mount_point( DEVICE_OBJECT *device, UNICODE_STRING *device_name, int drive )
{
    static const WCHAR driveW[] = {'\\','D','o','s','D','e','v','i','c','e','s','\\','%','c',':',0};
    WCHAR link[sizeof(driveW)];

    sprintfW( link, driveW, 'A' + drive );
    return add_mount_point( device, device_name, link );
}

/* create the Volume mount point symlink for a new device */
struct mount_point *add_volume_mount_point( DEVICE_OBJECT *device, UNICODE_STRING *device_name,
                                            const GUID *guid )
{
    static const WCHAR volumeW[] = {'\\','?','?','\\','V','o','l','u','m','e','{',
                                    '%','0','8','x','-','%','0','4','x','-','%','0','4','x','-',
                                    '%','0','2','x','%','0','2','x','-','%','0','2','x','%','0','2','x',
                                    '%','0','2','x','%','0','2','x','%','0','2','x','%','0','2','x','}',0};
    WCHAR link[sizeof(volumeW)];

    sprintfW( link, volumeW, guid->Data1, guid->Data2, guid->Data3,
              guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
              guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
    return add_mount_point( device, device_name, link );
}

/* delete the mount point symlinks when a device goes away */
void delete_mount_point( struct mount_point *mount )
{
    TRACE( "deleting %s\n", debugstr_w(mount->link.Buffer) );
    list_remove( &mount->entry );
    RegDeleteValueW( mount_key, mount->link.Buffer );
    IoDeleteSymbolicLink( &mount->link );
    RtlFreeHeap( GetProcessHeap(), 0, mount->id );
    RtlFreeHeap( GetProcessHeap(), 0, mount );
}

/* check if a given mount point matches the requested specs */
static BOOL matching_mount_point( const struct mount_point *mount, const MOUNTMGR_MOUNT_POINT *spec )
{
    if (spec->SymbolicLinkNameOffset)
    {
        const WCHAR *name = (const WCHAR *)((const char *)spec + spec->SymbolicLinkNameOffset);
        if (spec->SymbolicLinkNameLength != mount->link.Length) return FALSE;
        if (memicmpW( name, mount->link.Buffer, mount->link.Length/sizeof(WCHAR)))
            return FALSE;
    }
    if (spec->DeviceNameOffset)
    {
        const WCHAR *name = (const WCHAR *)((const char *)spec + spec->DeviceNameOffset);
        if (spec->DeviceNameLength != mount->name.Length) return FALSE;
        if (memicmpW( name, mount->name.Buffer, mount->name.Length/sizeof(WCHAR)))
            return FALSE;
    }
    if (spec->UniqueIdOffset)
    {
        const void *id = ((const char *)spec + spec->UniqueIdOffset);
        if (spec->UniqueIdLength != mount->id_len) return FALSE;
        if (memcmp( id, mount->id, mount->id_len )) return FALSE;
    }
    return TRUE;
}

/* implementation of IOCTL_MOUNTMGR_QUERY_POINTS */
static NTSTATUS query_mount_points( void *buff, SIZE_T insize,
                                    SIZE_T outsize, IO_STATUS_BLOCK *iosb )
{
    UINT count, pos, size;
    MOUNTMGR_MOUNT_POINT *input = buff;
    MOUNTMGR_MOUNT_POINTS *info;
    struct mount_point *mount;

    /* sanity checks */
    if (input->SymbolicLinkNameOffset + input->SymbolicLinkNameLength > insize ||
        input->UniqueIdOffset + input->UniqueIdLength > insize ||
        input->DeviceNameOffset + input->DeviceNameLength > insize ||
        input->SymbolicLinkNameOffset + input->SymbolicLinkNameLength < input->SymbolicLinkNameOffset ||
        input->UniqueIdOffset + input->UniqueIdLength < input->UniqueIdOffset ||
        input->DeviceNameOffset + input->DeviceNameLength < input->DeviceNameOffset)
        return STATUS_INVALID_PARAMETER;

    count = size = 0;
    LIST_FOR_EACH_ENTRY( mount, &mount_points_list, struct mount_point, entry )
    {
        if (!matching_mount_point( mount, input )) continue;
        size += mount->name.Length;
        size += mount->link.Length;
        size += mount->id_len;
        size = (size + sizeof(WCHAR) - 1) & ~(sizeof(WCHAR) - 1);
        count++;
    }
    pos = FIELD_OFFSET( MOUNTMGR_MOUNT_POINTS, MountPoints[count] );
    size += pos;

    if (size > outsize)
    {
        info = buff;
        if (size >= sizeof(info->Size)) info->Size = size;
        iosb->Information = sizeof(info->Size);
        return STATUS_MORE_ENTRIES;
    }

    input = HeapAlloc( GetProcessHeap(), 0, insize );
    if (!input)
        return STATUS_NO_MEMORY;
    memcpy( input, buff, insize );
    info = buff;

    info->NumberOfMountPoints = count;
    count = 0;
    LIST_FOR_EACH_ENTRY( mount, &mount_points_list, struct mount_point, entry )
    {
        if (!matching_mount_point( mount, input )) continue;

        info->MountPoints[count].DeviceNameOffset = pos;
        info->MountPoints[count].DeviceNameLength = mount->name.Length;
        memcpy( (char *)buff + pos, mount->name.Buffer, mount->name.Length );
        pos += mount->name.Length;

        info->MountPoints[count].SymbolicLinkNameOffset = pos;
        info->MountPoints[count].SymbolicLinkNameLength = mount->link.Length;
        memcpy( (char *)buff + pos, mount->link.Buffer, mount->link.Length );
        pos += mount->link.Length;

        info->MountPoints[count].UniqueIdOffset = pos;
        info->MountPoints[count].UniqueIdLength = mount->id_len;
        memcpy( (char *)buff + pos, mount->id, mount->id_len );
        pos += mount->id_len;
        pos = (pos + sizeof(WCHAR) - 1) & ~(sizeof(WCHAR) - 1);
        count++;
    }
    info->Size = pos;
    iosb->Information = pos;
    HeapFree( GetProcessHeap(), 0, input );
    return STATUS_SUCCESS;
}

/* implementation of IOCTL_MOUNTMGR_DEFINE_UNIX_DRIVE */
static NTSTATUS define_unix_drive( const void *in_buff, SIZE_T insize )
{
    const struct mountmgr_unix_drive *input = in_buff;
    const char *mount_point = NULL, *device = NULL;
    unsigned int i;
    WCHAR letter = tolowerW( input->letter );

    if (letter < 'a' || letter > 'z') return STATUS_INVALID_PARAMETER;
    if (input->type > DRIVE_RAMDISK) return STATUS_INVALID_PARAMETER;
    if (input->mount_point_offset > insize || input->device_offset > insize)
        return STATUS_INVALID_PARAMETER;

    /* make sure string are null-terminated */
    if (input->mount_point_offset)
    {
        mount_point = (const char *)in_buff + input->mount_point_offset;
        for (i = input->mount_point_offset; i < insize; i++)
            if (!*((const char *)in_buff + i)) break;
        if (i >= insize) return STATUS_INVALID_PARAMETER;
    }
    if (input->device_offset)
    {
        device = (const char *)in_buff + input->device_offset;
        for (i = input->device_offset; i < insize; i++)
            if (!*((const char *)in_buff + i)) break;
        if (i >= insize) return STATUS_INVALID_PARAMETER;
    }

    if (input->type != DRIVE_NO_ROOT_DIR)
    {
        enum device_type type = DEVICE_UNKNOWN;

        TRACE( "defining %c: dev %s mount %s type %u\n",
               letter, debugstr_a(device), debugstr_a(mount_point), input->type );
        switch (input->type)
        {
        case DRIVE_REMOVABLE: type = (letter >= 'c') ? DEVICE_HARDDISK : DEVICE_FLOPPY; break;
        case DRIVE_REMOTE:    type = DEVICE_NETWORK; break;
        case DRIVE_CDROM:     type = DEVICE_CDROM; break;
        case DRIVE_RAMDISK:   type = DEVICE_RAMDISK; break;
        case DRIVE_FIXED:     type = DEVICE_HARDDISK_VOL; break;
        }
        return add_dos_device( letter - 'a', NULL, device, mount_point, type, NULL );
    }
    else
    {
        TRACE( "removing %c:\n", letter );
        return remove_dos_device( letter - 'a', NULL );
    }
}

/* implementation of IOCTL_MOUNTMGR_QUERY_UNIX_DRIVE */
static NTSTATUS query_unix_drive( void *buff, SIZE_T insize,
                                  SIZE_T outsize, IO_STATUS_BLOCK *iosb )
{
    const struct mountmgr_unix_drive *input = buff;
    struct mountmgr_unix_drive *output = NULL;
    char *device, *mount_point;
    int letter = tolowerW( input->letter );
    NTSTATUS status;
    DWORD size, type = DEVICE_UNKNOWN;
    enum device_type device_type;
    char *ptr;

    if (letter < 'a' || letter > 'z') return STATUS_INVALID_PARAMETER;

    if ((status = query_dos_device( letter - 'a', &device_type, &device, &mount_point ))) return status;
    switch (device_type)
    {
    case DEVICE_UNKNOWN:      type = DRIVE_UNKNOWN; break;
    case DEVICE_HARDDISK:     type = DRIVE_REMOVABLE; break;
    case DEVICE_HARDDISK_VOL: type = DRIVE_FIXED; break;
    case DEVICE_FLOPPY:       type = DRIVE_REMOVABLE; break;
    case DEVICE_CDROM:        type = DRIVE_CDROM; break;
    case DEVICE_DVD:          type = DRIVE_CDROM; break;
    case DEVICE_NETWORK:      type = DRIVE_REMOTE; break;
    case DEVICE_RAMDISK:      type = DRIVE_RAMDISK; break;
    }

    size = sizeof(*output);
    if (device) size += strlen(device) + 1;
    if (mount_point) size += strlen(mount_point) + 1;

    input = NULL;
    output = buff;

    if (size > outsize)
    {
        iosb->Information = 0;
        if (size >= FIELD_OFFSET( struct mountmgr_unix_drive, size ) + sizeof(output->size))
        {
            output->size = size;
            iosb->Information = FIELD_OFFSET( struct mountmgr_unix_drive, size ) + sizeof(output->size);
        }
        if (size >= FIELD_OFFSET( struct mountmgr_unix_drive, type ) + sizeof(output->type))
        {
            output->type = type;
            iosb->Information = FIELD_OFFSET( struct mountmgr_unix_drive, type ) + sizeof(output->type);
        }
        status = STATUS_MORE_ENTRIES;
        goto done;
    }
    output->size = size;
    output->letter = letter;
    output->type = type;
    ptr = (char *)(output + 1);

    if (mount_point)
    {
        output->mount_point_offset = ptr - (char *)output;
        strcpy( ptr, mount_point );
        ptr += strlen(ptr) + 1;
    }
    else output->mount_point_offset = 0;

    if (device)
    {
        output->device_offset = ptr - (char *)output;
        strcpy( ptr, device );
        ptr += strlen(ptr) + 1;
    }
    else output->device_offset = 0;

    TRACE( "returning %c: dev %s mount %s type %u\n",
           letter, debugstr_a(device), debugstr_a(mount_point), type );

    iosb->Information = ptr - (char *)output;
done:
    RtlFreeHeap( GetProcessHeap(), 0, device );
    RtlFreeHeap( GetProcessHeap(), 0, mount_point );
    return status;
}

/* handler for ioctls on the mount manager device */
static NTSTATUS WINAPI mountmgr_ioctl( DEVICE_OBJECT *device, IRP *irp )
{
    IO_STACK_LOCATION *irpsp = IoGetCurrentIrpStackLocation( irp );

    TRACE( "ioctl %x insize %u outsize %u\n",
           irpsp->Parameters.DeviceIoControl.IoControlCode,
           irpsp->Parameters.DeviceIoControl.InputBufferLength,
           irpsp->Parameters.DeviceIoControl.OutputBufferLength );

    switch(irpsp->Parameters.DeviceIoControl.IoControlCode)
    {
    case IOCTL_MOUNTMGR_QUERY_POINTS:
        if (irpsp->Parameters.DeviceIoControl.InputBufferLength < sizeof(MOUNTMGR_MOUNT_POINT))
        {
            irp->IoStatus.u.Status = STATUS_INVALID_PARAMETER;
            break;
        }
        irp->IoStatus.u.Status = query_mount_points( irp->AssociatedIrp.SystemBuffer,
                                                     irpsp->Parameters.DeviceIoControl.InputBufferLength,
                                                     irpsp->Parameters.DeviceIoControl.OutputBufferLength,
                                                     &irp->IoStatus );
        break;
    case IOCTL_MOUNTMGR_DEFINE_UNIX_DRIVE:
        if (irpsp->Parameters.DeviceIoControl.InputBufferLength < sizeof(struct mountmgr_unix_drive))
        {
            irp->IoStatus.u.Status = STATUS_INVALID_PARAMETER;
            break;
        }
        irp->IoStatus.Information = 0;
        irp->IoStatus.u.Status = define_unix_drive( irp->AssociatedIrp.SystemBuffer,
                                                    irpsp->Parameters.DeviceIoControl.InputBufferLength );
        break;
    case IOCTL_MOUNTMGR_QUERY_UNIX_DRIVE:
        if (irpsp->Parameters.DeviceIoControl.InputBufferLength < sizeof(struct mountmgr_unix_drive))
        {
            irp->IoStatus.u.Status = STATUS_INVALID_PARAMETER;
            break;
        }
        irp->IoStatus.u.Status = query_unix_drive( irp->AssociatedIrp.SystemBuffer,
                                                   irpsp->Parameters.DeviceIoControl.InputBufferLength,
                                                   irpsp->Parameters.DeviceIoControl.OutputBufferLength,
                                                   &irp->IoStatus );
        break;
    default:
        FIXME( "ioctl %x not supported\n", irpsp->Parameters.DeviceIoControl.IoControlCode );
        irp->IoStatus.u.Status = STATUS_NOT_SUPPORTED;
        break;
    }
    IoCompleteRequest( irp, IO_NO_INCREMENT );
    return STATUS_SUCCESS;
}

/* main entry point for the mount point manager driver */
NTSTATUS WINAPI DriverEntry( DRIVER_OBJECT *driver, UNICODE_STRING *path )
{
    static const WCHAR mounted_devicesW[] = {'S','y','s','t','e','m','\\','M','o','u','n','t','e','d','D','e','v','i','c','e','s',0};
    static const WCHAR device_mountmgrW[] = {'\\','D','e','v','i','c','e','\\','M','o','u','n','t','P','o','i','n','t','M','a','n','a','g','e','r',0};
    static const WCHAR link_mountmgrW[] = {'\\','?','?','\\','M','o','u','n','t','P','o','i','n','t','M','a','n','a','g','e','r',0};
    static const WCHAR harddiskW[] = {'\\','D','r','i','v','e','r','\\','H','a','r','d','d','i','s','k',0};
    static const WCHAR devicemapW[] = {'H','A','R','D','W','A','R','E','\\','D','E','V','I','C','E','M','A','P',0};
    static const WCHAR parallelW[] = {'P','A','R','A','L','L','E','L',' ','P','O','R','T','S',0};
    static const WCHAR serialW[] = {'S','E','R','I','A','L','C','O','M','M',0};

    UNICODE_STRING nameW, linkW;
    DEVICE_OBJECT *device;
    NTSTATUS status;
    HKEY hkey, devicemap_key;

    TRACE( "%s\n", debugstr_w(path->Buffer) );

    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = mountmgr_ioctl;

    RtlInitUnicodeString( &nameW, device_mountmgrW );
    RtlInitUnicodeString( &linkW, link_mountmgrW );
    if (!(status = IoCreateDevice( driver, 0, &nameW, 0, 0, FALSE, &device )))
        status = IoCreateSymbolicLink( &linkW, &nameW );
    if (status)
    {
        FIXME( "failed to create device error %x\n", status );
        return status;
    }

    RegCreateKeyExW( HKEY_LOCAL_MACHINE, mounted_devicesW, 0, NULL,
                     REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &mount_key, NULL );

    if (!RegCreateKeyExW( HKEY_LOCAL_MACHINE, devicemapW, 0, NULL, REG_OPTION_VOLATILE,
                          KEY_ALL_ACCESS, NULL, &devicemap_key, NULL ))
    {
        if (!RegCreateKeyExW( devicemap_key, parallelW, 0, NULL, REG_OPTION_VOLATILE,
                              KEY_ALL_ACCESS, NULL, &hkey, NULL ))
            RegCloseKey( hkey );
        if (!RegCreateKeyExW( devicemap_key, serialW, 0, NULL, REG_OPTION_VOLATILE,
                              KEY_ALL_ACCESS, NULL, &hkey, NULL ))
            RegCloseKey( hkey );
        RegCloseKey( devicemap_key );
    }

    RtlInitUnicodeString( &nameW, harddiskW );
    status = IoCreateDriver( &nameW, harddisk_driver_entry );

    initialize_dbus();
    initialize_diskarbitration();

    return status;
}

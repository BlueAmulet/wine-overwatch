/*
 * Support for converting from old configuration format
 *
 * Copyright 2005 Alexandre Julliard
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
 *   This file should be removed after a suitable transition period.
 */

#include "config.h"
#include "wine/port.h"

#include <stdarg.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#include <fcntl.h>
#ifdef HAVE_DIRENT_H
# include <dirent.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_LINUX_HDREG_H
# include <linux/hdreg.h>
#endif

#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "ntddscsi.h"
#include "wine/library.h"
#include "wine/unicode.h"
#include "wine/debug.h"
#include "kernel_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(reg);

static NTSTATUS create_key( HANDLE root, const char *name, HANDLE *key, DWORD *disp )
{
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING nameW;
    NTSTATUS status;

    attr.Length = sizeof(attr);
    attr.RootDirectory = root;
    attr.ObjectName = &nameW;
    attr.Attributes = 0;
    attr.SecurityDescriptor = NULL;
    attr.SecurityQualityOfService = NULL;

    if (!RtlCreateUnicodeStringFromAsciiz( &nameW, name )) return STATUS_NO_MEMORY;
    status = NtCreateKey( key, KEY_ALL_ACCESS, &attr, 0, NULL, REG_OPTION_VOLATILE, disp );
    if (status) ERR("Cannot create %s registry key\n", name );
    RtlFreeUnicodeString( &nameW );
    return status;
}

/******************************************************************
 *		create_scsi_entry
 *
 * Initializes registry to contain scsi info about the cdrom in NT.
 * All devices (even not real scsi ones) have this info in NT.
 * NOTE: programs usually read these registry entries after sending the
 *       IOCTL_SCSI_GET_ADDRESS ioctl to the cdrom
 */
static void create_scsi_entry( PSCSI_ADDRESS scsi_addr, LPCSTR lpDriver, UINT uDriveType,
    LPSTR lpDriveName, LPSTR lpUnixDeviceName )
{
    static UCHAR uCdromNumber = 0;
    static UCHAR uTapeNumber = 0;

    UNICODE_STRING nameW;
    WCHAR dataW[50];
    DWORD sizeW;
    char buffer[40];
    DWORD value;
    const char *data;
    HANDLE scsiKey;
    HANDLE portKey;
    HANDLE busKey;
    HANDLE targetKey;
    HANDLE lunKey;
    DWORD disp;

    if (create_key( 0, "\\Registry\\Machine\\HARDWARE\\DEVICEMAP", &scsiKey, &disp )) return;
    NtClose( scsiKey );

    /* Ensure there is Scsi key */
    if (create_key( 0, "\\Registry\\Machine\\HARDWARE\\DEVICEMAP\\Scsi", &scsiKey, &disp )) return;

    snprintf(buffer,sizeof(buffer),"Scsi Port %d",scsi_addr->PortNumber);
    if (create_key( scsiKey, buffer, &portKey, &disp )) return;

    RtlCreateUnicodeStringFromAsciiz( &nameW, "Driver" );
    RtlMultiByteToUnicodeN( dataW, sizeof(dataW), &sizeW, lpDriver, strlen(lpDriver)+1);
    NtSetValueKey( portKey, &nameW, 0, REG_SZ, dataW, sizeW );
    RtlFreeUnicodeString( &nameW );
    value = 10;
    RtlCreateUnicodeStringFromAsciiz( &nameW, "FirstBusTimeScanInMs" );
    NtSetValueKey( portKey,&nameW, 0, REG_DWORD, &value, sizeof(DWORD) );
    RtlFreeUnicodeString( &nameW );

    value = 0;
    if (strcmp(lpDriver, "atapi") == 0)
    {
#ifdef HDIO_GET_DMA
        int fd, dma;
        
        fd = open(lpUnixDeviceName, O_RDONLY|O_NONBLOCK);
        if (fd != -1)
        {
            if (ioctl(fd, HDIO_GET_DMA, &dma) != -1) value = dma;
            close(fd);
        }
#endif
        RtlCreateUnicodeStringFromAsciiz( &nameW, "DMAEnabled" );
        NtSetValueKey( portKey,&nameW, 0, REG_DWORD, &value, sizeof(DWORD) );
        RtlFreeUnicodeString( &nameW );
    }

    snprintf(buffer, sizeof(buffer),"Scsi Bus %d", scsi_addr->PathId);
    if (create_key( portKey, buffer, &busKey, &disp )) return;

    /* FIXME: get real controller Id for SCSI */
    if (create_key( busKey, buffer, &targetKey, &disp )) return;
    NtClose( targetKey );

    snprintf(buffer, sizeof(buffer),"Target Id %d", scsi_addr->TargetId);
    if (create_key( busKey, buffer, &targetKey, &disp )) return;

    snprintf(buffer, sizeof(buffer),"Logical Unit Id %d", scsi_addr->Lun);
    if (create_key( targetKey, buffer, &lunKey, &disp )) return;

    switch (uDriveType)
    {
        case DRIVE_NO_ROOT_DIR:
        default:
            data = "OtherPeripheral"; break;
        case DRIVE_FIXED:
            data = "DiskPeripheral"; break;
        case DRIVE_REMOVABLE:
            data = "TapePeripheral";
            snprintf(buffer, sizeof(buffer), "Tape%d", uTapeNumber++);
            break;
        case DRIVE_CDROM:
            data = "CdRomPeripheral";
            snprintf(buffer, sizeof(buffer), "Cdrom%d", uCdromNumber++);
            break;
    }
    RtlCreateUnicodeStringFromAsciiz( &nameW, "Type" );
    RtlMultiByteToUnicodeN( dataW, sizeof(dataW), &sizeW, data, strlen(data)+1);
    NtSetValueKey( lunKey, &nameW, 0, REG_SZ, dataW, sizeW );
    RtlFreeUnicodeString( &nameW );

    RtlCreateUnicodeStringFromAsciiz( &nameW, "Identifier" );
    RtlMultiByteToUnicodeN( dataW, sizeof(dataW), &sizeW, lpDriveName, strlen(lpDriveName)+1);
    NtSetValueKey( lunKey, &nameW, 0, REG_SZ, dataW, sizeW );
    RtlFreeUnicodeString( &nameW );

    if (uDriveType == DRIVE_CDROM || uDriveType == DRIVE_REMOVABLE)
    {
        RtlCreateUnicodeStringFromAsciiz( &nameW, "DeviceName" );
        RtlMultiByteToUnicodeN( dataW, sizeof(dataW), &sizeW, buffer, strlen(buffer)+1);
        NtSetValueKey( lunKey, &nameW, 0, REG_SZ, dataW, sizeW );
        RtlFreeUnicodeString( &nameW );
    }

    RtlCreateUnicodeStringFromAsciiz( &nameW, "UnixDeviceName" );
    RtlMultiByteToUnicodeN( dataW, sizeof(dataW), &sizeW, lpUnixDeviceName, strlen(lpUnixDeviceName)+1);
    NtSetValueKey( lunKey, &nameW, 0, REG_SZ, dataW, sizeW );
    RtlFreeUnicodeString( &nameW );

    NtClose( lunKey );
    NtClose( targetKey );
    NtClose( busKey );
    NtClose( portKey );
    NtClose( scsiKey );
}

struct LinuxProcScsiDevice
{
    int host;
    int channel;
    int target;
    int lun;
    char vendor[9];
    char model[17];
    char rev[5];
    int type;
    int ansirev;
};

/*
 * we need to declare white spaces explicitly via %*1[ ],
 * as there are very dainbread CD-ROM devices out there
 * which have their manufacturer name blanked out via spaces,
 * which confuses fscanf's parsing (skips all blank spaces)
 */
static int SCSI_getprocentry( FILE * procfile, struct LinuxProcScsiDevice * dev )
{
    int result;
    char type[33];

    result = fscanf( procfile,
        "Host:%*1[ ]scsi%d%*1[ ]Channel:%*1[ ]%d%*1[ ]Id:%*1[ ]%d%*1[ ]Lun:%*1[ ]%d\n",
	&dev->host,
	&dev->channel,
	&dev->target,
	&dev->lun );
    if( result == EOF )
    {
        /* "end of entries" return, so no TRACE() here */
        return EOF;
    }
    if( result != 4 )
    {
        ERR("bus id line scan count error (fscanf returns %d, expected 4)\n", result);
        return 0;
    }
    result = fscanf( procfile,
        "  Vendor:%*1[ ]%8c%*1[ ]Model:%*1[ ]%16c%*1[ ]Rev:%*1[ ]%4c\n",
        dev->vendor,
        dev->model,
        dev->rev );
    if( result != 3 )
    {
        ERR("model line scan count error (fscanf returns %d, expected 3)\n", result);
        return 0;
    }

    result = fscanf( procfile,
        "  Type:%*3[ ]%32c%*1[ ]ANSI SCSI%*1[ ]revision:%*1[ ]%x\n",
        type,
        &dev->ansirev );
    if( result != 2 )
    {
        ERR("SCSI type line scan count error (fscanf returns %d, expected 2)\n", result);
        return 0;
    }
    /* Since we fscanf with %XXc instead of %s.. put a NULL at end */
    dev->vendor[8] = 0;
    dev->model[16] = 0;
    dev->rev[4] = 0;
    type[32] = 0;

    if (strncmp(type, "Direct-Access", 13) == 0) dev->type = DRIVE_FIXED;
    else if (strncmp(type, "Sequential-Access", 17) == 0) dev->type = DRIVE_REMOVABLE;
    else if (strncmp(type, "CD-ROM", 6) == 0) dev->type = DRIVE_CDROM;
    else if (strncmp(type, "Processor", 9) == 0) dev->type = DRIVE_NO_ROOT_DIR;
    else if (strncmp(type, "Scanner", 7) == 0) dev->type = DRIVE_NO_ROOT_DIR;
    else if (strncmp(type, "Printer", 7) == 0) dev->type = DRIVE_NO_ROOT_DIR;
    else dev->type = DRIVE_UNKNOWN;

    return 1;
}

static BOOL read_file_content(char *path, char *buffer, int length)
{
    size_t read;

    FILE *f = fopen(path, "r");
    if (!f) return FALSE;

    read = fread(buffer, 1, length-1, f);
    fclose(f);
    if (!read) return FALSE;

    /* ensure NULL termination */
    buffer[read] = 0;
    return TRUE;
}

static BOOL read_file_content_int(char *path, int *result)
{
    char buffer[20];

    if (!read_file_content(path, buffer, sizeof(buffer)))
        return FALSE;

    *result = atoi(buffer);
    return TRUE;
}

static BOOL SCSI_getsysentry(char *device_key, struct LinuxProcScsiDevice *dev, char *unix_path)
{
    struct dirent *dent = NULL;
    char path_buffer[100];
    DIR *generic_dir;
    int result, type;

    result = sscanf(device_key, "%d:%d:%d:%d", &dev->host, &dev->channel, &dev->target, &dev->lun);
    if (result != 4)
    {
        ERR("Failed to extract device information from %s\n", device_key);
        return FALSE;
    }

    snprintf(path_buffer, sizeof(path_buffer), "/sys/class/scsi_device/%s/device/vendor", device_key);
    if (!read_file_content(path_buffer, dev->vendor, sizeof(dev->vendor))) return FALSE;

    snprintf(path_buffer, sizeof(path_buffer), "/sys/class/scsi_device/%s/device/model", device_key);
    if (!read_file_content(path_buffer, dev->model, sizeof(dev->model))) return FALSE;

    snprintf(path_buffer, sizeof(path_buffer), "/sys/class/scsi_device/%s/device/rev", device_key);
    if (!read_file_content(path_buffer, dev->rev, sizeof(dev->rev))) return FALSE;

    snprintf(path_buffer, sizeof(path_buffer), "/sys/class/scsi_device/%s/device/type", device_key);
    if (!read_file_content_int(path_buffer, &type)) return FALSE;

    /* see SCSI specification standard for values */
    if (type == 0x0) dev->type = DRIVE_FIXED;
    else if (type == 0x1) dev->type = DRIVE_REMOVABLE;
    else if (type == 0x5) dev->type = DRIVE_CDROM;
    else dev->type = DRIVE_NO_ROOT_DIR;

    /* FIXME: verify */
    snprintf(path_buffer, sizeof(path_buffer), "/sys/class/scsi_device/%s/device/scsi_level", device_key);
    if (!read_file_content_int(path_buffer, &dev->ansirev)) return FALSE;

    result = FALSE;

    snprintf(path_buffer, sizeof(path_buffer), "/sys/class/scsi_device/%s/device/scsi_generic", device_key);
    generic_dir = opendir(path_buffer);
    if (generic_dir)
    {
        while ((dent = readdir(generic_dir)))
        {
            if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
                continue;

            sprintf(unix_path, "/dev/%s", dent->d_name);
            result = TRUE;
            break;
        }
        closedir(generic_dir);
    }

    return result;
}

/* create the hardware registry branch */
static void create_hardware_branch(void)
{
    /* The following mostly will work on Linux, but should not cause
     * problems on other systems. */
    static const char procname_ide_media[] = "/proc/ide/%s/media";
    static const char procname_ide_model[] = "/proc/ide/%s/model";
    static const char procname_scsi[] = "/proc/scsi/scsi";
    DIR *idedir, *scsidir;
    struct dirent *dent = NULL;
    FILE *procfile = NULL;
    char cStr[40], cDevModel[40], cUnixDeviceName[40], read1[10] = "\0", read2[10] = "\0";
    SCSI_ADDRESS scsi_addr;
    UINT nType;
    struct LinuxProcScsiDevice dev;
    int result = 0, nSgNumber = 0;
    UCHAR uFirstSCSIPort = 0;

    /* Enumerate all ide devices first */
    idedir = opendir("/proc/ide");
    if (idedir)
    {
        while ((dent = readdir(idedir)))
        {
            if (strncmp(dent->d_name, "hd", 2) == 0)
            {
                if (snprintf(cStr, sizeof(cStr), procname_ide_media, dent->d_name) >= sizeof(cStr))
                    continue;
                procfile = fopen(cStr, "r");
                if (!procfile)
                {
                    ERR("Could not open %s\n", cStr);
                    continue;
                } else {
                    fgets(cStr, sizeof(cStr), procfile);
                    fclose(procfile);
                    nType = DRIVE_UNKNOWN;
                    if (strncasecmp(cStr, "disk", 4)  == 0) nType = DRIVE_FIXED;
                    if (strncasecmp(cStr, "cdrom", 5) == 0) nType = DRIVE_CDROM;

                    if (nType == DRIVE_UNKNOWN) continue;
                }

                if (snprintf(cStr, sizeof(cStr), procname_ide_model, dent->d_name) >= sizeof(cStr))
                    continue;
                procfile = fopen(cStr, "r");
                if (!procfile)
                {
                    ERR("Could not open %s\n", cStr);
                    switch (nType)
                    {
                    case DRIVE_FIXED: strcpy(cDevModel, "Wine harddisk"); break;
                    case DRIVE_CDROM: strcpy(cDevModel, "Wine CDROM"); break;
                    }
                } else {
                    fgets(cDevModel, sizeof(cDevModel), procfile);
                    fclose(procfile);
                    cDevModel[strlen(cDevModel) - 1] = 0;
                }

                if (snprintf(cUnixDeviceName, sizeof(cUnixDeviceName), "/dev/%s", dent->d_name) >= sizeof(cUnixDeviceName))
                    continue;
                scsi_addr.PortNumber = (dent->d_name[2] - 'a') / 2;
                scsi_addr.PathId = 0;
                scsi_addr.TargetId = (dent->d_name[2] - 'a') % 2;
                scsi_addr.Lun = 0;
                if (scsi_addr.PortNumber + 1 > uFirstSCSIPort)
                    uFirstSCSIPort = scsi_addr.PortNumber + 1;

                create_scsi_entry(&scsi_addr, "atapi", nType, cDevModel, cUnixDeviceName);
            }
        }
        closedir(idedir);
    }

    /* Now goes SCSI */
    scsidir = opendir("/sys/class/scsi_device");
    if (scsidir)
    {
        while ((dent = readdir(scsidir)))
        {
            if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
                continue;

            if (!SCSI_getsysentry(dent->d_name, &dev, cUnixDeviceName))
                continue;

            scsi_addr.PortNumber = dev.host;
            scsi_addr.PathId = dev.channel;
            scsi_addr.TargetId = dev.target;
            scsi_addr.Lun = dev.lun;

            scsi_addr.PortNumber += uFirstSCSIPort;

            strcpy(cDevModel, dev.vendor);
            strcat(cDevModel, dev.model);
            strcat(cDevModel, dev.rev);

            /* FIXME: get real driver name */
            create_scsi_entry(&scsi_addr, "WINE SCSI", dev.type, cDevModel, cUnixDeviceName);
        }
        closedir(scsidir);
        return;
    }

    procfile = fopen(procname_scsi, "r");
    if (!procfile)
    {
        TRACE("Could not open %s\n", procname_scsi);
        return;
    }
    fgets(cStr, 40, procfile);
    sscanf(cStr, "Attached %9s %9s", read1, read2);

    if (strcmp(read1, "devices:") != 0)
    {
        WARN("Incorrect %s format\n", procname_scsi);
        fclose( procfile );
        return;
    }
    if (strcmp(read2, "none") == 0)
    {
        TRACE("No SCSI devices found in %s.\n", procname_scsi);
        fclose( procfile );
        return;
    }

    /* Read info for one device */
    while ((result = SCSI_getprocentry(procfile, &dev)) > 0)
    {
        if (dev.type == DRIVE_UNKNOWN)
            continue;

        scsi_addr.PortNumber = dev.host;
        scsi_addr.PathId = dev.channel;
        scsi_addr.TargetId = dev.target;
        scsi_addr.Lun = dev.lun;

        scsi_addr.PortNumber += uFirstSCSIPort;

        strcpy(cDevModel, dev.vendor);
        strcat(cDevModel, dev.model);
        strcat(cDevModel, dev.rev);
        sprintf(cUnixDeviceName, "/dev/sg%d", nSgNumber++);
        /* FIXME: get real driver name */
        create_scsi_entry(&scsi_addr, "WINE SCSI", dev.type, cDevModel, cUnixDeviceName);
    }
    if( result != EOF )
        WARN("Incorrect %s format\n", procname_scsi);
    fclose( procfile );
}


/***********************************************************************
 *              convert_old_config
 */
void convert_old_config(void)
{
    HANDLE key;
    DWORD disp;

    /* create some hardware keys (FIXME: should not be done here) */
    if (create_key( 0, "\\Registry\\Machine\\HARDWARE", &key, &disp )) return;
    NtClose( key );
    if (disp != REG_OPENED_EXISTING_KEY) create_hardware_branch();
}

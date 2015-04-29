/*
 * xen_xm.c: Xen XM parsing functions
 *
 * Copyright (C) 2006-2007, 2009-2014 Red Hat, Inc.
 * Copyright (C) 2011 Univention GmbH
 * Copyright (C) 2006 Daniel P. Berrange
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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Daniel P. Berrange <berrange@redhat.com>
 * Author: Markus Groß <gross@univention.de>
 */

#include <config.h>

#include "internal.h"
#include "virerror.h"
#include "virconf.h"
#include "viralloc.h"
#include "verify.h"
#include "xenxs_private.h"
#include "xen_xm.h"
#include "domain_conf.h"
#include "virstring.h"
#include "xen_common.h"


static int
xenParseXMOS(virConfPtr conf, virDomainDefPtr def)
{
    size_t i;

    if (def->os.type == VIR_DOMAIN_OSTYPE_HVM) {
        const char *boot;

        if (VIR_ALLOC(def->os.loader) < 0 ||
            xenConfigCopyString(conf, "kernel", &def->os.loader->path) < 0)
            return -1;

        if (xenConfigGetString(conf, "boot", &boot, "c") < 0)
            return -1;

        for (i = 0; i < VIR_DOMAIN_BOOT_LAST && boot[i]; i++) {
            switch (boot[i]) {
            case 'a':
                def->os.bootDevs[i] = VIR_DOMAIN_BOOT_FLOPPY;
                break;
            case 'd':
                def->os.bootDevs[i] = VIR_DOMAIN_BOOT_CDROM;
                break;
            case 'n':
                def->os.bootDevs[i] = VIR_DOMAIN_BOOT_NET;
                break;
            case 'c':
            default:
                def->os.bootDevs[i] = VIR_DOMAIN_BOOT_DISK;
                break;
            }
            def->os.nBootDevs++;
        }
    } else {
        const char *extra, *root;

        if (xenConfigCopyStringOpt(conf, "bootloader", &def->os.bootloader) < 0)
            return -1;
        if (xenConfigCopyStringOpt(conf, "bootargs", &def->os.bootloaderArgs) < 0)
            return -1;

        if (xenConfigCopyStringOpt(conf, "kernel", &def->os.kernel) < 0)
            return -1;

        if (xenConfigCopyStringOpt(conf, "ramdisk", &def->os.initrd) < 0)
            return -1;

        if (xenConfigGetString(conf, "extra", &extra, NULL) < 0)
            return -1;

        if (xenConfigGetString(conf, "root", &root, NULL) < 0)
            return -1;

        if (root) {
            if (virAsprintf(&def->os.cmdline, "root=%s %s", root, extra) < 0)
                return -1;
        } else {
            if (VIR_STRDUP(def->os.cmdline, extra) < 0)
                return -1;
        }
    }

    return 0;
}


static int
xenParseXMDisk(virConfPtr conf, virDomainDefPtr def, int xendConfigVersion)
{
    const char *str = NULL;
    virDomainDiskDefPtr disk = NULL;
    int hvm = def->os.type == VIR_DOMAIN_OSTYPE_HVM;
    virConfValuePtr list = virConfGetValue(conf, "disk");

    if (list && list->type == VIR_CONF_LIST) {
        list = list->list;
        while (list) {
            char *head;
            char *offset;
            char *tmp;
            const char *src;

            if ((list->type != VIR_CONF_STRING) || (list->str == NULL))
                goto skipdisk;

            head = list->str;
            if (!(disk = virDomainDiskDefNew()))
                return -1;

            /*
             * Disks have 3 components, SOURCE,DEST-DEVICE,MODE
             * eg, phy:/dev/HostVG/XenGuest1,xvda,w
             * The SOURCE is usually prefixed with a driver type,
             * and optionally driver sub-type
             * The DEST-DEVICE is optionally post-fixed with disk type
             */

            /* Extract the source file path*/
            if (!(offset = strchr(head, ',')))
                goto skipdisk;

            if (offset == head) {
                /* No source file given, eg CDROM with no media */
                ignore_value(virDomainDiskSetSource(disk, NULL));
            } else {
                if (VIR_STRNDUP(tmp, head, offset - head) < 0)
                    goto cleanup;

                if (virDomainDiskSetSource(disk, tmp) < 0) {
                    VIR_FREE(tmp);
                    goto cleanup;
                }
                VIR_FREE(tmp);
            }

            head = offset + 1;
            /* Remove legacy ioemu: junk */
            if (STRPREFIX(head, "ioemu:"))
                head = head + 6;

            /* Extract the dest device name */
            if (!(offset = strchr(head, ',')))
                goto skipdisk;

            if (VIR_ALLOC_N(disk->dst, (offset - head) + 1) < 0)
                goto cleanup;

            if (virStrncpy(disk->dst, head, offset - head,
                           (offset - head) + 1) == NULL) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Dest file %s too big for destination"), head);
                goto cleanup;
            }

            head = offset + 1;
            /* Extract source driver type */
            src = virDomainDiskGetSource(disk);
            if (src) {
                size_t len;
                /* The main type  phy:, file:, tap: ... */
                if ((tmp = strchr(src, ':')) != NULL) {
                    len = tmp - src;
                    if (VIR_STRNDUP(tmp, src, len) < 0)
                        goto cleanup;

                    if (virDomainDiskSetDriver(disk, tmp) < 0) {
                        VIR_FREE(tmp);
                        goto cleanup;
                    }
                    VIR_FREE(tmp);

                    /* Strip the prefix we found off the source file name */
                    if (virDomainDiskSetSource(disk, src + len + 1) < 0)
                        goto cleanup;

                    src = virDomainDiskGetSource(disk);
                }

                /* And the sub-type for tap:XXX: type */
                if (STREQ_NULLABLE(virDomainDiskGetDriver(disk), "tap")) {
                    char *driverType;

                    if (!(tmp = strchr(src, ':')))
                        goto skipdisk;
                    len = tmp - src;

                    if (VIR_STRNDUP(driverType, src, len) < 0)
                        goto cleanup;

                    if (STREQ(driverType, "aio"))
                        virDomainDiskSetFormat(disk, VIR_STORAGE_FILE_RAW);
                    else
                        virDomainDiskSetFormat(disk,
                                               virStorageFileFormatTypeFromString(driverType));
                    VIR_FREE(driverType);
                    if (virDomainDiskGetFormat(disk) <= 0) {
                        virReportError(VIR_ERR_INTERNAL_ERROR,
                                       _("Unknown driver type %s"),
                                       src);
                        goto cleanup;
                    }

                    /* Strip the prefix we found off the source file name */
                    if (virDomainDiskSetSource(disk, src + len + 1) < 0)
                        goto cleanup;
                    src = virDomainDiskGetSource(disk);
                }
            }

            /* No source, or driver name, so fix to phy: */
            if (!virDomainDiskGetDriver(disk) &&
                virDomainDiskSetDriver(disk, "phy") < 0)
                goto cleanup;

            /* phy: type indicates a block device */
            virDomainDiskSetType(disk,
                                 STREQ(virDomainDiskGetDriver(disk), "phy") ?
                                 VIR_STORAGE_TYPE_BLOCK :
                                 VIR_STORAGE_TYPE_FILE);

            /* Check for a :cdrom/:disk postfix */
            disk->device = VIR_DOMAIN_DISK_DEVICE_DISK;
            if ((tmp = strchr(disk->dst, ':')) != NULL) {
                if (STREQ(tmp, ":cdrom"))
                    disk->device = VIR_DOMAIN_DISK_DEVICE_CDROM;
                tmp[0] = '\0';
            }

            if (STRPREFIX(disk->dst, "xvd") || !hvm) {
                disk->bus = VIR_DOMAIN_DISK_BUS_XEN;
            } else if (STRPREFIX(disk->dst, "sd")) {
                disk->bus = VIR_DOMAIN_DISK_BUS_SCSI;
            } else {
                disk->bus = VIR_DOMAIN_DISK_BUS_IDE;
            }

            if (STREQ(head, "r") ||
                STREQ(head, "ro"))
                disk->src->readonly = true;
            else if ((STREQ(head, "w!")) ||
                     (STREQ(head, "!")))
                disk->src->shared = true;

            /* Maintain list in sorted order according to target device name */
            if (VIR_APPEND_ELEMENT(def->disks, def->ndisks, disk) < 0)
                goto cleanup;

            skipdisk:
            list = list->next;
            virDomainDiskDefFree(disk);
            disk = NULL;
        }
    }

    if (hvm && xendConfigVersion == XEND_CONFIG_VERSION_3_0_2) {
        if (xenConfigGetString(conf, "cdrom", &str, NULL) < 0)
            goto cleanup;
        if (str) {
            if (!(disk = virDomainDiskDefNew()))
                goto cleanup;

            virDomainDiskSetType(disk, VIR_STORAGE_TYPE_FILE);
            disk->device = VIR_DOMAIN_DISK_DEVICE_CDROM;
            if (virDomainDiskSetDriver(disk, "file") < 0)
                goto cleanup;
            if (virDomainDiskSetSource(disk, str) < 0)
                goto cleanup;
            if (VIR_STRDUP(disk->dst, "hdc") < 0)
                goto cleanup;
            disk->bus = VIR_DOMAIN_DISK_BUS_IDE;
            disk->src->readonly = true;

            if (VIR_APPEND_ELEMENT(def->disks, def->ndisks, disk) < 0)
                goto cleanup;
        }
    }

    return 0;

 cleanup:
    virDomainDiskDefFree(disk);
    return -1;
}


static int
xenFormatXMDisk(virConfValuePtr list,
                virDomainDiskDefPtr disk,
                int hvm,
                int xendConfigVersion)
{
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    virConfValuePtr val, tmp;
    const char *src = virDomainDiskGetSource(disk);
    int format = virDomainDiskGetFormat(disk);
    const char *driver = virDomainDiskGetDriver(disk);

    if (src) {
        if (format) {
            const char *type;

            if (format == VIR_STORAGE_FILE_RAW)
                type = "aio";
            else
                type = virStorageFileFormatTypeToString(format);
            virBufferAsprintf(&buf, "%s:", driver);
            if (STREQ(driver, "tap"))
                virBufferAsprintf(&buf, "%s:", type);
        } else {
            switch (virDomainDiskGetType(disk)) {
            case VIR_STORAGE_TYPE_FILE:
                virBufferAddLit(&buf, "file:");
                break;
            case VIR_STORAGE_TYPE_BLOCK:
                virBufferAddLit(&buf, "phy:");
                break;
            default:
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("unsupported disk type %s"),
                               virStorageTypeToString(virDomainDiskGetType(disk)));
                goto cleanup;
            }
        }
        virBufferAdd(&buf, src, -1);
    }
    virBufferAddLit(&buf, ",");
    if (hvm && xendConfigVersion == XEND_CONFIG_VERSION_3_0_2)
        virBufferAddLit(&buf, "ioemu:");

    virBufferAdd(&buf, disk->dst, -1);
    if (disk->device == VIR_DOMAIN_DISK_DEVICE_CDROM)
        virBufferAddLit(&buf, ":cdrom");

    if (disk->src->readonly)
        virBufferAddLit(&buf, ",r");
    else if (disk->src->shared)
        virBufferAddLit(&buf, ",!");
    else
        virBufferAddLit(&buf, ",w");
    if (disk->transient) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("transient disks not supported yet"));
        return -1;
    }

    if (virBufferCheckError(&buf) < 0)
        goto cleanup;

    if (VIR_ALLOC(val) < 0)
        goto cleanup;

    val->type = VIR_CONF_STRING;
    val->str = virBufferContentAndReset(&buf);
    tmp = list->list;
    while (tmp && tmp->next)
        tmp = tmp->next;
    if (tmp)
        tmp->next = val;
    else
        list->list = val;

    return 0;

 cleanup:
    virBufferFreeAndReset(&buf);
    return -1;
}


static int
xenFormatXMDisks(virConfPtr conf, virDomainDefPtr def, int xendConfigVersion)
{
    virConfValuePtr diskVal = NULL;
    size_t i = 0;
    int hvm = def->os.type == VIR_DOMAIN_OSTYPE_HVM;

    if (VIR_ALLOC(diskVal) < 0)
        goto cleanup;

    diskVal->type = VIR_CONF_LIST;
    diskVal->list = NULL;

    for (i = 0; i < def->ndisks; i++) {
        if (xendConfigVersion == XEND_CONFIG_VERSION_3_0_2 &&
            def->disks[i]->device == VIR_DOMAIN_DISK_DEVICE_CDROM &&
            def->disks[i]->dst &&
            STREQ(def->disks[i]->dst, "hdc")) {
            continue;
        }

        if (def->disks[i]->device == VIR_DOMAIN_DISK_DEVICE_FLOPPY)
            continue;

        if (xenFormatXMDisk(diskVal, def->disks[i],
                            hvm, xendConfigVersion) < 0)
            goto cleanup;
    }

    if (diskVal->list != NULL) {
        int ret = virConfSetValue(conf, "disk", diskVal);
        diskVal = NULL;
        if (ret < 0)
            goto cleanup;
    }
    VIR_FREE(diskVal);

    return 0;

 cleanup:
    virConfFreeValue(diskVal);
    return -1;
}


static int
xenParseXMInputDevs(virConfPtr conf, virDomainDefPtr def)
{
    const char *str;

    if (def->os.type == VIR_DOMAIN_OSTYPE_HVM) {
        if (xenConfigGetString(conf, "usbdevice", &str, NULL) < 0)
            return -1;
        if (str &&
                (STREQ(str, "tablet") ||
                 STREQ(str, "mouse") ||
                 STREQ(str, "keyboard"))) {
            virDomainInputDefPtr input;
            if (VIR_ALLOC(input) < 0)
                return -1;

            input->bus = VIR_DOMAIN_INPUT_BUS_USB;
            if (STREQ(str, "mouse"))
                input->type = VIR_DOMAIN_INPUT_TYPE_MOUSE;
            else if (STREQ(str, "tablet"))
                input->type = VIR_DOMAIN_INPUT_TYPE_TABLET;
            else if (STREQ(str, "keyboard"))
                input->type = VIR_DOMAIN_INPUT_TYPE_KBD;
            if (VIR_APPEND_ELEMENT(def->inputs, def->ninputs, input) < 0) {
                virDomainInputDefFree(input);
                return -1;
            }
        }
    }
    return 0;
}

/*
 * Convert an XM config record into a virDomainDef object.
 */
virDomainDefPtr
xenParseXM(virConfPtr conf,
           int xendConfigVersion,
           virCapsPtr caps)
{
    virDomainDefPtr def = NULL;

    if (!(def = virDomainDefNew()))
        return NULL;

    def->virtType = VIR_DOMAIN_VIRT_XEN;
    def->id = -1;

    if (xenParseConfigCommon(conf, def, caps, xendConfigVersion) < 0)
        goto cleanup;

    if (xenParseXMOS(conf, def) < 0)
         goto cleanup;

    if (xenParseXMDisk(conf, def, xendConfigVersion) < 0)
         goto cleanup;

    if (xenParseXMInputDevs(conf, def) < 0)
         goto cleanup;

    return def;

 cleanup:
    virDomainDefFree(def);
    return NULL;
}

static int
xenFormatXMOS(virConfPtr conf, virDomainDefPtr def)
{
    size_t i;

    if (def->os.type == VIR_DOMAIN_OSTYPE_HVM) {
        char boot[VIR_DOMAIN_BOOT_LAST+1];
        if (xenConfigSetString(conf, "builder", "hvm") < 0)
            return -1;

        if (def->os.loader && def->os.loader->path &&
            xenConfigSetString(conf, "kernel", def->os.loader->path) < 0)
            return -1;

        for (i = 0; i < def->os.nBootDevs; i++) {
            switch (def->os.bootDevs[i]) {
            case VIR_DOMAIN_BOOT_FLOPPY:
                boot[i] = 'a';
                break;
            case VIR_DOMAIN_BOOT_CDROM:
                boot[i] = 'd';
                break;
            case VIR_DOMAIN_BOOT_NET:
                boot[i] = 'n';
                break;
            case VIR_DOMAIN_BOOT_DISK:
            default:
                boot[i] = 'c';
                break;
            }
        }

        if (!def->os.nBootDevs) {
            boot[0] = 'c';
            boot[1] = '\0';
        } else {
            boot[def->os.nBootDevs] = '\0';
        }

        if (xenConfigSetString(conf, "boot", boot) < 0)
            return -1;

        /* XXX floppy disks */
    } else {
        if (def->os.bootloader &&
             xenConfigSetString(conf, "bootloader", def->os.bootloader) < 0)
            return -1;

         if (def->os.bootloaderArgs &&
             xenConfigSetString(conf, "bootargs", def->os.bootloaderArgs) < 0)
            return -1;

         if (def->os.kernel &&
             xenConfigSetString(conf, "kernel", def->os.kernel) < 0)
            return -1;

         if (def->os.initrd &&
             xenConfigSetString(conf, "ramdisk", def->os.initrd) < 0)
            return -1;

         if (def->os.cmdline &&
             xenConfigSetString(conf, "extra", def->os.cmdline) < 0)
            return -1;
     } /* !hvm */

    return 0;
}


static int
xenFormatXMInputDevs(virConfPtr conf, virDomainDefPtr def)
{
    size_t i;
    const char *devtype;

    if (def->os.type == VIR_DOMAIN_OSTYPE_HVM) {
        for (i = 0; i < def->ninputs; i++) {
            if (def->inputs[i]->bus == VIR_DOMAIN_INPUT_BUS_USB) {
                if (xenConfigSetInt(conf, "usb", 1) < 0)
                    return -1;

                switch (def->inputs[i]->type) {
                    case VIR_DOMAIN_INPUT_TYPE_MOUSE:
                        devtype = "mouse";
                        break;
                    case VIR_DOMAIN_INPUT_TYPE_TABLET:
                        devtype = "tablet";
                        break;
                    case VIR_DOMAIN_INPUT_TYPE_KBD:
                        devtype = "keyboard";
                        break;
                    default:
                        continue;
                }
                if (xenConfigSetString(conf, "usbdevice", devtype) < 0)
                    return -1;
                break;
            }
        }
    }
    return 0;
}


/* Computing the vcpu_avail bitmask works because MAX_VIRT_CPUS is
   either 32, or 64 on a platform where long is big enough.  */
verify(MAX_VIRT_CPUS <= sizeof(1UL) * CHAR_BIT);

/*
 * Convert a virDomainDef object into an XM config record.
 */
virConfPtr
xenFormatXM(virConnectPtr conn,
            virDomainDefPtr def,
            int xendConfigVersion)
{
    virConfPtr conf = NULL;

    if (!(conf = virConfNew()))
        goto cleanup;

    if (xenFormatConfigCommon(conf, def, conn, xendConfigVersion) < 0)
        goto cleanup;

    if (xenFormatXMOS(conf, def) < 0)
        goto cleanup;

    if (xenFormatXMDisks(conf, def, xendConfigVersion) < 0)
        goto cleanup;

    if (xenFormatXMInputDevs(conf, def) < 0)
        goto cleanup;

    return conf;

 cleanup:
    if (conf)
        virConfFree(conf);
    return NULL;
}

/*
 * virobject.h: libvirt reference counted object
 *
 * Copyright (C) 2012 Red Hat, Inc.
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
 */

#ifndef __VIR_OBJECT_H__
# define __VIR_OBJECT_H__

# include "internal.h"

typedef struct _virClass virClass;
typedef virClass *virClassPtr;

typedef struct _virObject virObject;
typedef virObject *virObjectPtr;

typedef void (*virObjectDisposeCallback)(void *obj);

struct _virObject {
    unsigned int magic;
    int refs;
    virClassPtr klass;
};

virClassPtr virClassNew(const char *name,
                        size_t objectSize,
                        virObjectDisposeCallback dispose)
    ATTRIBUTE_NONNULL(1);

const char *virClassName(virClassPtr klass)
    ATTRIBUTE_NONNULL(1);

void *virObjectNew(virClassPtr klass)
    ATTRIBUTE_NONNULL(1);
bool virObjectUnref(void *obj);
void *virObjectRef(void *obj);

bool virObjectIsClass(void *obj,
                      virClassPtr klass)
    ATTRIBUTE_NONNULL(2);

void virObjectFreeCallback(void *opaque);

#endif /* __VIR_OBJECT_H */

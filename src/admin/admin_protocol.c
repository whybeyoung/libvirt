#include <config.h>
/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include "admin_protocol.h"
#include "virxdrdefs.h"

bool_t
xdr_admin_nonnull_string (XDR *xdrs, admin_nonnull_string *objp)
{

         if (!xdr_string (xdrs, objp, ADMIN_STRING_MAX))
                 return FALSE;
        return TRUE;
}

bool_t
xdr_admin_string (XDR *xdrs, admin_string *objp)
{

         if (!xdr_pointer (xdrs, (char **)objp, sizeof (admin_nonnull_string), (xdrproc_t) xdr_admin_nonnull_string))
                 return FALSE;
        return TRUE;
}

bool_t
xdr_admin_connect_open_args (XDR *xdrs, admin_connect_open_args *objp)
{

         if (!xdr_u_int (xdrs, &objp->flags))
                 return FALSE;
        return TRUE;
}

bool_t
xdr_admin_connect_get_lib_version_ret (XDR *xdrs, admin_connect_get_lib_version_ret *objp)
{

         if (!xdr_uint64_t (xdrs, &objp->libVer))
                 return FALSE;
        return TRUE;
}

bool_t
xdr_admin_procedure (XDR *xdrs, admin_procedure *objp)
{

         if (!xdr_enum (xdrs, (enum_t *) objp))
                 return FALSE;
        return TRUE;
}
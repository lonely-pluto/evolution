/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2001 Ximian Inc (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


#ifndef CAMEL_SPOOLD_STORE_H
#define CAMEL_SPOOLD_STORE_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include "camel-spool-store.h"

#define CAMEL_SPOOLD_STORE_TYPE     (camel_spoold_store_get_type ())
#define CAMEL_SPOOLD_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SPOOLD_STORE_TYPE, CamelSpoolDStore))
#define CAMEL_SPOOLD_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SPOOLD_STORE_TYPE, CamelSpoolDStoreClass))
#define CAMEL_IS_SPOOLD_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SPOOLD_STORE_TYPE))


typedef struct {
	CamelSpoolStore parent_object;	

} CamelSpoolDStore;



typedef struct {
	CamelSpoolStoreClass parent_class;

} CamelSpoolDStoreClass;


/* public methods */

/* Standard Camel function */
CamelType camel_spoold_store_get_type (void);

const gchar *camel_spoold_store_get_toplevel_dir (CamelSpoolDStore *store);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_SPOOLD_STORE_H */



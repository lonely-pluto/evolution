/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* 
 * Copyright (C) 2005  Novell, Inc.
 *
 * Authors: Michael Zucchi <notzed@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <string.h>
#include <bonobo/bonobo-shlib-factory.h>
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-i18n.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-arg.h>

#include "evolution-mail-store.h"
#include "evolution-mail-session.h"

#include "e-corba-utils.h"

#include <camel/camel-store.h>
#include <camel/camel-session.h>

#include <e-util/e-account.h>

#define FACTORY_ID "OAFIID:GNOME_Evolution_Mail_Store_Factory:" BASE_VERSION
#define MAIL_STORE_ID  "OAFIID:GNOME_Evolution_Mail_Store:" BASE_VERSION

#define PARENT_TYPE bonobo_object_get_type ()

static BonoboObjectClass *parent_class = NULL;

#define _PRIVATE(o) (g_type_instance_get_private ((GTypeInstance *)o, evolution_mail_store_get_type()))

struct _EvolutionMailStorePrivate {
	struct _EAccount *account;

	CamelStore *store;
	EvolutionMailSession *session;
};

/* GObject methods */

static void
impl_dispose (GObject *object)
{
	struct _EvolutionMailStorePrivate *p = _PRIVATE(object);

	if (p->account) {
		g_object_unref(p->account);
		p->account = NULL;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Evolution.Mail.Store */

static CORBA_boolean
impl_getProperties(PortableServer_Servant _servant,
		   const GNOME_Evolution_Mail_PropertyNames* names,
		   GNOME_Evolution_Mail_Properties **propsp,
		   CORBA_Environment * ev)
{
	EvolutionMailStore *ems = (EvolutionMailStore *)bonobo_object_from_servant(_servant);
	int i;
	GNOME_Evolution_Mail_Properties *props;
	struct _EvolutionMailStorePrivate *p = _PRIVATE(ems);
	CORBA_boolean ok = CORBA_TRUE;

	*propsp = props = GNOME_Evolution_Mail_Properties__alloc();
	props->_length = names->_length;
	props->_maximum = props->_length;
	props->_buffer = GNOME_Evolution_Mail_Properties_allocbuf(props->_maximum);
	CORBA_sequence_set_release(props, CORBA_TRUE);

	for (i=0;i<names->_length;i++) {
		const CORBA_char *name = names->_buffer[i];
		GNOME_Evolution_Mail_Property *prop = &props->_buffer[i];
		char *val = NULL;

		printf("getting property '%s'\n", name);

		if (!strcmp(name, "name")) {
			if (p->account)
				val = p->account->name;
			else
				/* FIXME: name & i18n */
				val = "Local";
			e_mail_property_set_string(prop, name, val);
		} else if (!strcmp(name, "uid")) {
			if (p->account)
				val = p->account->uid;
			else
				val = "local@local";
			e_mail_property_set_string(prop, name, val);
		} else {
			e_mail_property_set_null(prop, name);
			ok = CORBA_FALSE;
		}
	}

	return ok;
}

static GNOME_Evolution_Mail_Folders *
impl_getFolders(PortableServer_Servant _servant,
		const CORBA_char * pattern,
		CORBA_Environment * ev)
{
	EvolutionMailStore *ems = (EvolutionMailStore *)bonobo_object_from_servant(_servant);
	struct _EvolutionMailStorePrivate *p = _PRIVATE(ems);
	CamelFolderInfo *fi;
	CamelException ex = { 0 };

	if (p->store == NULL) {
		if (p->account == NULL) {
			p->store = mail_component_peek_local_store(NULL);
			camel_object_ref(p->store);
		} else {
			const char *uri;

			uri = e_account_get_string(p->account, E_ACCOUNT_SOURCE_URL);
			if (uri && *uri) {
				p->store = camel_session_get_store(p->session->session, uri, &ex);
				if (camel_exception_is_set(&ex)) {
					camel_exception_clear(&ex);
					return NULL;
				}
			} else {
				return NULL;
			}
		}
	}

	fi = camel_store_get_folder_info(p->store, pattern, CAMEL_STORE_FOLDER_INFO_RECURSIVE|CAMEL_STORE_FOLDER_INFO_FAST, &ex);

	/* flatten folders ... */

	return NULL;
}

static void
impl_sendMessage(PortableServer_Servant _servant,
		 const Bonobo_Stream msg, const CORBA_char * from,
		 const CORBA_char * recipients,
		 CORBA_Environment * ev)
{
	EvolutionMailStore *ems = (EvolutionMailStore *)bonobo_object_from_servant(_servant);
	struct _EvolutionMailStorePrivate *p = _PRIVATE(ems);

	printf("Sending message from '%s' to '%s'\n", from, recipients);
	if (p->account == NULL) {
		printf("Local mail can only store ...\n");
	} else if (p->account->transport && p->account->transport->url) {
		printf("via '%s'\n", p->account->transport->url);
	} else {
		printf("Account not setup for sending '%s'\n", p->account->name);
	}
}

/* Initialization */

static void
evolution_mail_store_class_init (EvolutionMailStoreClass *klass)
{
	POA_GNOME_Evolution_Mail_Store__epv *epv = &klass->epv;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	epv->getProperties = impl_getProperties;
	epv->getFolders = impl_getFolders;
	epv->sendMessage = impl_sendMessage;

	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;

	g_type_class_add_private(klass, sizeof(struct _EvolutionMailStorePrivate));
}

static void
evolution_mail_store_init(EvolutionMailStore *component, EvolutionMailStoreClass *klass)
{
}

BONOBO_TYPE_FUNC_FULL (EvolutionMailStore, GNOME_Evolution_Mail_Store, PARENT_TYPE, evolution_mail_store)

EvolutionMailStore *
evolution_mail_store_new(struct _EvolutionMailSession *s, struct _EAccount *ea)
{
	EvolutionMailStore *ems = g_object_new (EVOLUTION_MAIL_TYPE_STORE, NULL);
	struct _EvolutionMailStorePrivate *p = _PRIVATE(ems);

	if (ea) {
		p->account = ea;
		g_object_ref(ea);
	}

	p->session = s;

	return ems;
}

#if 0
static BonoboObject *
factory (BonoboGenericFactory *factory,
	 const char *component_id,
	 void *closure)
{
	if (strcmp (component_id, MAIL_STORE_ID) == 0) {
		BonoboObject *object = BONOBO_OBJECT (g_object_new (EVOLUTION_MAIL_TYPE_STORE, NULL));
		bonobo_object_ref (object);
		return object;
	}
	
	g_warning (FACTORY_ID ": Don't know what to do with %s", component_id);

	return NULL;
}

BONOBO_ACTIVATION_SHLIB_FACTORY (FACTORY_ID, "Evolution Calendar component factory", factory, NULL)
#endif

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
#include <bonobo/bonobo-i18n.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-arg.h>
#include "evolution-mail-messageiterator.h"

#include <camel/camel-folder.h>

#include <libedataserver/e-account.h>

#define FACTORY_ID "OAFIID:GNOME_Evolution_Mail_MessageIterator_Factory:" BASE_VERSION
#define MAIL_MESSAGEITERATOR_ID  "OAFIID:GNOME_Evolution_Mail_MessageIterator:" BASE_VERSION

#define PARENT_TYPE bonobo_object_get_type ()

static BonoboObjectClass *parent_class = NULL;

#define _PRIVATE(o) (g_type_instance_get_private ((GTypeInstance *)o, evolution_mail_messageiterator_get_type()))

struct _EvolutionMailMessageIteratorPrivate {
	int index;
	CamelFolder *folder;
	char *expr;
	GPtrArray *search;
};

/* GObject methods */

static void
impl_dispose (GObject *object)
{
	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	struct _EvolutionMailMessageIteratorPrivate *p = _PRIVATE(object);

	if (*p->expr)
		camel_folder_search_free(p->folder, p->search);
	else
		camel_folder_free_uids(p->folder, p->search);

	g_free(p->expr);
	camel_object_unref(p->folder);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Evolution.Mail.MessageIterator */
static GNOME_Evolution_Mail_Messages *
impl_next(PortableServer_Servant _servant, const CORBA_long limit, CORBA_Environment * ev)
{
	EvolutionMailMessageIterator *emf = (EvolutionMailMessageIterator *)bonobo_object_from_servant(_servant);
	int i, j;
	GNOME_Evolution_Mail_Messages *msgs;
	struct _EvolutionMailMessageIteratorPrivate *p = _PRIVATE(emf);
	CamelException ex = { 0 };

	if (p->search == NULL) {
		if (*p->expr)
			p->search = camel_folder_search_by_expression(p->folder, p->expr, &ex);
		else
			p->search = camel_folder_get_uids(p->folder);

		if (camel_exception_is_set(&ex)) {
			camel_exception_clear(&ex);
			return NULL;
		}

		p->index = 0;
	}

	msgs = GNOME_Evolution_Mail_Messages__alloc();
	msgs->_maximum = MIN(limit, p->search->len - p->index);
	msgs->_buffer = GNOME_Evolution_Mail_Messages_allocbuf(msgs->_maximum);
	CORBA_sequence_set_release(msgs, CORBA_TRUE);

	j=0;
	for (i=p->index;i<p->search->len && j<msgs->_maximum;i++) {
		CamelMessageInfo *info = camel_folder_get_message_info(p->folder, p->search->pdata[i]);

		if (info) {
			msgs->_buffer[j].uid = CORBA_string_dup(camel_message_info_uid(info));
			msgs->_buffer[j].subject = CORBA_string_dup(camel_message_info_subject(info));
			msgs->_buffer[j].to = CORBA_string_dup(camel_message_info_to(info));
			msgs->_buffer[j].from = CORBA_string_dup(camel_message_info_from(info));
			j++;
			camel_message_info_free(info);
		}
	}

	msgs->_length = j;

	return msgs;
}

/* Initialization */

static void
evolution_mail_messageiterator_class_init (EvolutionMailMessageIteratorClass *klass)
{
	POA_GNOME_Evolution_Mail_MessageIterator__epv *epv = &klass->epv;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	epv->next = impl_next;

	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;

	g_type_class_add_private(klass, sizeof(struct _EvolutionMailMessageIteratorPrivate));
}

static void
evolution_mail_messageiterator_init(EvolutionMailMessageIterator *component, EvolutionMailMessageIteratorClass *klass)
{
}

BONOBO_TYPE_FUNC_FULL (EvolutionMailMessageIterator, GNOME_Evolution_Mail_MessageIterator, PARENT_TYPE, evolution_mail_messageiterator)

EvolutionMailMessageIterator *
evolution_mail_messageiterator_new(CamelFolder *folder, const char *expr)
{
	EvolutionMailMessageIterator *emf = g_object_new(evolution_mail_messageiterator_get_type(), NULL);
	struct _EvolutionMailMessageIteratorPrivate *p = _PRIVATE(emf);

	p->folder = folder;
	camel_object_ref(folder);
	p->expr = g_strdup(expr);

	return emf;
}

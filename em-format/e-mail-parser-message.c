/*
 * e-mail-parser-message.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-mail-format-extensions.h"

#include <glib/gi18n-lib.h>

#include <em-format/e-mail-parser-extension.h>
#include <em-format/e-mail-parser.h>
#include "e-mail-part-utils.h"
#include <libemail-engine/e-mail-utils.h>
#include <e-util/e-util.h>

#include <camel/camel.h>

#include <string.h>

typedef struct _EMailParserMessage {
	GObject parent;
} EMailParserMessage;

typedef struct _EMailParserMessageClass {
	GObjectClass parent_class;
} EMailParserMessageClass;

static void e_mail_parser_parser_extension_interface_init (EMailParserExtensionInterface *iface);
static void e_mail_parser_mail_extension_interface_init (EMailExtensionInterface *iface);

G_DEFINE_TYPE_EXTENDED (
	EMailParserMessage,
	e_mail_parser_message,
	G_TYPE_OBJECT,
	0,
	G_IMPLEMENT_INTERFACE (
		E_TYPE_MAIL_EXTENSION,
		e_mail_parser_mail_extension_interface_init)
	G_IMPLEMENT_INTERFACE (
		E_TYPE_MAIL_PARSER_EXTENSION,
		e_mail_parser_parser_extension_interface_init));

static const gchar *parser_mime_types[] = { "application/vnd.evolution.message", NULL };

static GSList *
empe_message_parse (EMailParserExtension *extension,
                    EMailParser *parser,
                    CamelMimePart *part,
                    GString *part_id,
                    GCancellable *cancellable)
{
	GSList *parts;
	CamelContentType *ct;
	gchar *mime_type;

	if (g_cancellable_is_cancelled (cancellable))
		return NULL;

	/* Headers */
	parts = g_slist_concat (NULL, e_mail_parser_parse_part_as (
					parser, part, part_id,
					"application/vnd.evolution.headers",
					cancellable));

	/* Attachment Bar */
	parts = g_slist_concat (parts, e_mail_parser_parse_part_as (
					parser, part, part_id,
					"application/vnd.evolution.widget.attachment-bar",
					cancellable));

	ct = camel_mime_part_get_content_type (part);
	mime_type = camel_content_type_simple (ct);

	/* Actual message body */
	parts = g_slist_concat (parts, e_mail_parser_parse_part_as (
					parser, part, part_id, mime_type,
					cancellable));

	g_free (mime_type);

	return parts;
}

static const gchar **
empe_message_mime_types (EMailExtension *extension)
{
	return parser_mime_types;
}

static void
e_mail_parser_message_class_init (EMailParserMessageClass *class)
{
}

static void
e_mail_parser_parser_extension_interface_init (EMailParserExtensionInterface *iface)
{
	iface->parse = empe_message_parse;
}

static void
e_mail_parser_mail_extension_interface_init (EMailExtensionInterface *iface)
{
	iface->mime_types = empe_message_mime_types;
}

static void
e_mail_parser_message_init (EMailParserMessage *parser)
{

}
/*
 * e-mail-parser-audio.c
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "e-mail-parser-audio.h"
#include "e-mail-part-audio.h"

#include <camel/camel.h>

#include <em-format/e-mail-extension-registry.h>
#include <em-format/e-mail-parser-extension.h>
#include <em-format/e-mail-part.h>

#include <libebackend/libebackend.h>

#define d(x)

typedef EMailParserExtension EMailParserAudio;
typedef EMailParserExtensionClass EMailParserAudioClass;

typedef EExtension EMailParserAudioLoader;
typedef EExtensionClass EMailParserAudioLoaderClass;

GType e_mail_parser_audio_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	EMailParserAudio,
	e_mail_parser_audio,
	E_TYPE_MAIL_PARSER_EXTENSION)

static const gchar *parser_mime_types[] = {
	"audio/ac3",
	"audio/x-ac3",
	"audio/basic",
	"audio/mpeg",
	"audio/x-mpeg",
	"audio/mpeg3",
	"audio/x-mpeg3",
	"audio/mp3",
	"audio/x-mp3",
	"audio/mp4",
	"audio/flac",
	"audio/x-flac",
	"audio/mod",
	"audio/x-mod",
	"audio/x-wav",
	"audio/microsoft-wav",
	"audio/x-wma",
	"audio/x-ms-wma",
	"application/ogg",
	"application/x-ogg",
	NULL
};

static void
mail_part_audio_free (EMailPart *mail_part)
{
	EMailPartAudio *ai_part = (EMailPartAudio *) mail_part;

	g_clear_object (&ai_part->play_button);
	g_clear_object (&ai_part->pause_button);
	g_clear_object (&ai_part->stop_button);

	if (ai_part->filename) {
		g_unlink (ai_part->filename);
		g_free (ai_part->filename);
		ai_part->filename = NULL;
	}

	if (ai_part->bus_id) {
		g_source_remove (ai_part->bus_id);
		ai_part->bus_id = 0;
	}

	if (ai_part->playbin) {
		gst_element_set_state (ai_part->playbin, GST_STATE_NULL);
		gst_object_unref (ai_part->playbin);
		ai_part->playbin = NULL;
	}
}

static gint
mail_parser_audio_parse (EMailParserExtension *extension,
                         EMailParser *parser,
                         CamelMimePart *part,
                         GString *part_id,
                         GCancellable *cancellable,
                         GQueue *out_mail_queue)
{
	EMailPartAudio *mail_part;
	GQueue work_queue = G_QUEUE_INIT;
	gint len;
	gint n_parts_added = 0;

	len = part_id->len;
	g_string_append (part_id, ".org-gnome-audio-button-panel");

	d (printf ("audio formatter: format classid %s\n", part_id->str));

	mail_part = (EMailPartAudio *) e_mail_part_subclass_new (
		part, part_id->str, sizeof (EMailPartAudio),
		(GFreeFunc) mail_part_audio_free);
	mail_part->parent.mime_type = camel_content_type_simple (
		camel_mime_part_get_content_type (part));
	mail_part->parent.is_attachment = TRUE;
	g_string_truncate (part_id, len);

	g_queue_push_tail (&work_queue, mail_part);
	n_parts_added++;

	e_mail_parser_wrap_as_attachment (
		parser, part, part_id, &work_queue);

	e_queue_transfer (&work_queue, out_mail_queue);

	return TRUE;
}

static void
e_mail_parser_audio_class_init (EMailParserExtensionClass *class)
{
	class->mime_types = parser_mime_types;
	class->flags = E_MAIL_PARSER_EXTENSION_INLINE_DISPOSITION;
	class->parse = mail_parser_audio_parse;
}

static void
e_mail_parser_audio_class_finalize (EMailParserExtensionClass *class)
{

}

static void
e_mail_parser_audio_init (EMailParserExtension *extension)
{
}

void
e_mail_parser_audio_type_register (GTypeModule *type_module)
{
	e_mail_parser_audio_register_type (type_module);
}

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Authors:
 *		Chris Toshok <toshok@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "eab-contact-display.h"

#include <string.h>
#include <glib/gi18n.h>

#include <webkit/webkit.h>

#include "e-contact-map.h"
#include "eab-contact-formatter.h"
#include "eab-gui-util.h"

#define EAB_CONTACT_DISPLAY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), EAB_TYPE_CONTACT_DISPLAY, EABContactDisplayPrivate))

#define TEXT_IS_RIGHT_TO_LEFT \
	(gtk_widget_get_default_direction () == GTK_TEXT_DIR_RTL)

struct _EABContactDisplayPrivate {
	EContact *contact;

	EABContactDisplayMode mode;
	gboolean show_maps;
};

enum {
	PROP_0,
	PROP_CONTACT,
	PROP_MODE,
	PROP_SHOW_MAPS
};

enum {
	SEND_MESSAGE,
	LAST_SIGNAL
};

static const gchar *ui =
"<ui>"
"  <popup name='context'>"
"    <placeholder name='custom-actions-1'>"
"      <menuitem action='contact-send-message'/>"
"    </placeholder>"
"    <placeholder name='custom-actions-2'>"
"      <menuitem action='contact-mailto-copy'/>"
"    </placeholder>"
"  </popup>"
"</ui>";

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (
	EABContactDisplay,
	eab_contact_display,
	E_TYPE_WEB_VIEW)

static void
contact_display_emit_send_message (EABContactDisplay *display,
                                   gint email_num)
{
	EDestination *destination;
	EContact *contact;

	g_return_if_fail (email_num >= 0);

	destination = e_destination_new ();
	contact = eab_contact_display_get_contact (display);
	e_destination_set_contact (destination, contact, email_num);
	g_signal_emit (display, signals[SEND_MESSAGE], 0, destination);
	g_object_unref (destination);
}

static void
action_contact_mailto_copy_cb (GtkAction *action,
                               EABContactDisplay *display)
{
	GtkClipboard *clipboard;
	EWebView *web_view;
	EContact *contact;
	GList *list;
	const gchar *text;
	const gchar *uri;
	gint index;

	web_view = E_WEB_VIEW (display);
	uri = e_web_view_get_selected_uri (web_view);
	g_return_if_fail (uri != NULL);

	index = atoi (uri + strlen ("internal-mailto:"));
	g_return_if_fail (index >= 0);

	contact = eab_contact_display_get_contact (display);
	list = e_contact_get (contact, E_CONTACT_EMAIL);
	text = g_list_nth_data (list, index);

	clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text (clipboard, text, -1);
	gtk_clipboard_store (clipboard);

	g_list_foreach (list, (GFunc) g_free, NULL);
	g_list_free (list);
}

static void
action_contact_send_message_cb (GtkAction *action,
                                EABContactDisplay *display)
{
	EWebView *web_view;
	const gchar *uri;
	gint index;

	web_view = E_WEB_VIEW (display);
	uri = e_web_view_get_selected_uri (web_view);
	g_return_if_fail (uri != NULL);

	index = atoi (uri + strlen ("internal-mailto:"));
	contact_display_emit_send_message (display, index);
}

static GtkActionEntry internal_mailto_entries[] = {

	{ "contact-mailto-copy",
	  "edit-copy",
	  N_("Copy _Email Address"),
	  "<Control>c",
	  N_("Copy the email address to the clipboard"),
	  G_CALLBACK (action_contact_mailto_copy_cb) },

	{ "contact-send-message",
	  "mail-message-new",
	  N_("_Send New Message To..."),
	  NULL,
	  N_("Send a mail message to this address"),
	  G_CALLBACK (action_contact_send_message_cb) }
};

static void
load_contact (EABContactDisplay *display)
{
	EABContactFormatter *formatter;
	GString *buffer;

	if (!display->priv->contact) {
		e_web_view_clear (E_WEB_VIEW (display));
		return;
	}

	formatter = eab_contact_formatter_new ();
	g_object_set (
		G_OBJECT (formatter),
		"display-mode", display->priv->mode,
		"render-maps", display->priv->show_maps,
		NULL);

	buffer = g_string_sized_new (1024);

	eab_contact_formatter_format_contact (
		formatter, display->priv->contact, buffer);
	e_web_view_load_string (E_WEB_VIEW (display), buffer->str);

	g_string_free (buffer, TRUE);

	g_object_unref (formatter);
}

static void
contact_display_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONTACT:
			eab_contact_display_set_contact (
				EAB_CONTACT_DISPLAY (object),
				g_value_get_object (value));
			return;

		case PROP_MODE:
			eab_contact_display_set_mode (
				EAB_CONTACT_DISPLAY (object),
				g_value_get_int (value));
			return;

		case PROP_SHOW_MAPS:
			eab_contact_display_set_show_maps (
				EAB_CONTACT_DISPLAY (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
contact_display_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONTACT:
			g_value_set_object (
				value, eab_contact_display_get_contact (
				EAB_CONTACT_DISPLAY (object)));
			return;

		case PROP_MODE:
			g_value_set_int (
				value, eab_contact_display_get_mode (
				EAB_CONTACT_DISPLAY (object)));
			return;

		case PROP_SHOW_MAPS:
			g_value_set_boolean (
				value, eab_contact_display_get_show_maps (
				EAB_CONTACT_DISPLAY (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
contact_display_dispose (GObject *object)
{
	EABContactDisplayPrivate *priv;

	priv = EAB_CONTACT_DISPLAY_GET_PRIVATE (object);

	if (priv->contact != NULL) {
		g_object_unref (priv->contact);
		priv->contact = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (eab_contact_display_parent_class)->dispose (object);
}

static void
contact_display_hovering_over_link (EWebView *web_view,
                                    const gchar *title,
                                    const gchar *uri)
{
	EWebViewClass *web_view_class;
	EABContactDisplay *display;
	EContact *contact;
	const gchar *name;
	gchar *message;

	if (uri == NULL || *uri == '\0')
		goto chainup;

	if (!g_str_has_prefix (uri, "internal-mailto:"))
		goto chainup;

	display = EAB_CONTACT_DISPLAY (web_view);
	contact = eab_contact_display_get_contact (display);

	name = e_contact_get_const (contact, E_CONTACT_FILE_AS);
	if (name == NULL)
		e_contact_get_const (contact, E_CONTACT_FULL_NAME);
	g_return_if_fail (name != NULL);

	message = g_strdup_printf (_("Click to mail %s"), name);
	e_web_view_status_message (web_view, message);
	g_free (message);

	return;

chainup:
	/* Chain up to parent's hovering_over_link() method. */
	web_view_class = E_WEB_VIEW_CLASS (eab_contact_display_parent_class);
	web_view_class->hovering_over_link (web_view, title, uri);
}

static void
contact_display_link_clicked (EWebView *web_view,
                              const gchar *uri)
{
	EABContactDisplay *display;
	gsize length;

	display = EAB_CONTACT_DISPLAY (web_view);

	length = strlen ("internal-mailto:");
	if (g_ascii_strncasecmp (uri, "internal-mailto:", length) == 0) {
		gint index;

		index = atoi (uri + length);
		contact_display_emit_send_message (display, index);
		return;
	}

	/* Chain up to parent's link_clicked() method. */
	E_WEB_VIEW_CLASS (eab_contact_display_parent_class)->
		link_clicked (web_view, uri);
}

#ifdef WITH_CONTACT_MAPS
/* XXX Clutter event handling workaround. Clutter-gtk propagates events down
 *     to parent widgets.  In this case it leads to GtkHTML scrolling up and
 *     down while user's trying to zoom in the champlain widget. This
 *     workaround stops the propagation from map widget down to GtkHTML. */
static gboolean
handle_map_scroll_event (GtkWidget *widget,
                         GdkEvent *event)
{
	return TRUE;
}

static GtkWidget *
contact_display_object_requested (WebKitWebView *web_view,
                                  gchar *mime_type,
                                  gchar *uri,
                                  GHashTable *param,
                                  EABContactDisplay *display)
{
	EContact *contact = display->priv->contact;
	const gchar *name = e_contact_get_const (contact, E_CONTACT_FILE_AS);
	const gchar *contact_uid = e_contact_get_const (contact, E_CONTACT_UID);
	gchar *full_name = NULL;
	EContactAddress *address = NULL;
	GtkWidget *map = NULL;

	if (strstr (mime_type, "work") != NULL) {
		address = e_contact_get (contact, E_CONTACT_ADDRESS_WORK);
		full_name = g_strconcat (name, " (", _("Work"), ")", NULL);
	} else if (strstr (mime_type, "home") != NULL) {
		address = e_contact_get (contact, E_CONTACT_ADDRESS_HOME);
		full_name = g_strconcat (name, " (", _("Home"), ")", NULL);
	}

	if (address) {
		map = e_contact_map_new ();
		gtk_widget_set_size_request (map, 250, 250);
		g_signal_connect (
			E_CONTACT_MAP (map), "contact-added",
			G_CALLBACK (e_contact_map_zoom_on_marker), NULL);
		g_signal_connect_swapped (
			E_CONTACT_MAP (map), "contact-added",
			G_CALLBACK (gtk_widget_show_all), map);
		g_signal_connect (
			GTK_CHAMPLAIN_EMBED (map), "scroll-event",
			G_CALLBACK (handle_map_scroll_event), NULL);

		/* No need to display photo in contact preview. */
		e_contact_map_add_marker (
			E_CONTACT_MAP (map), full_name,
			contact_uid, address, NULL);

		gtk_widget_show_all (map);

		e_contact_address_free (address);
	}

	g_free (full_name);

	return map;
}
#endif

static void
contact_display_load_status_changed (WebKitWebView *web_view,
                                     GParamSpec *pspec,
                                     gpointer user_data)
{
	WebKitLoadStatus load_status;
	WebKitDOMDocument *document;

	load_status = webkit_web_view_get_load_status (web_view);
	if (load_status != WEBKIT_LOAD_FINISHED)
		return;

	document = webkit_web_view_get_dom_document (web_view);
	eab_contact_formatter_bind_dom (document);
}

static void
contact_display_update_actions (EWebView *web_view)
{
	GtkActionGroup *action_group;
	gboolean scheme_is_internal_mailto;
	gboolean visible;
	const gchar *group_name;
	const gchar *uri;

	/* Chain up to parent's update_actions() method. */
	E_WEB_VIEW_CLASS (eab_contact_display_parent_class)->
		update_actions (web_view);

	uri = e_web_view_get_selected_uri (web_view);

	scheme_is_internal_mailto = (uri == NULL) ? FALSE :
		(g_ascii_strncasecmp (uri, "internal-mailto:", 16) == 0);

	/* Override how EWebView treats internal-mailto URIs. */
	group_name = "uri";
	action_group = e_web_view_get_action_group (web_view, group_name);
	visible = gtk_action_group_get_visible (action_group);
	visible &= !scheme_is_internal_mailto;
	gtk_action_group_set_visible (action_group, visible);

	group_name = "internal-mailto";
	visible = scheme_is_internal_mailto;
	action_group = e_web_view_get_action_group (web_view, group_name);
	gtk_action_group_set_visible (action_group, visible);
}

static void
eab_contact_display_class_init (EABContactDisplayClass *class)
{
	GObjectClass *object_class;
	EWebViewClass *web_view_class;

	g_type_class_add_private (class, sizeof (EABContactDisplayPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = contact_display_set_property;
	object_class->get_property = contact_display_get_property;
	object_class->dispose = contact_display_dispose;

	web_view_class = E_WEB_VIEW_CLASS (class);
	web_view_class->hovering_over_link = contact_display_hovering_over_link;
	web_view_class->link_clicked = contact_display_link_clicked;
	web_view_class->update_actions = contact_display_update_actions;

	g_object_class_install_property (
		object_class,
		PROP_CONTACT,
		g_param_spec_object (
			"contact",
			NULL,
			NULL,
			E_TYPE_CONTACT,
			G_PARAM_READWRITE));

	/* XXX Make this a real enum property. */
	g_object_class_install_property (
		object_class,
		PROP_MODE,
		g_param_spec_int (
			"mode",
			NULL,
			NULL,
			EAB_CONTACT_DISPLAY_RENDER_NORMAL,
			EAB_CONTACT_DISPLAY_RENDER_COMPACT,
			EAB_CONTACT_DISPLAY_RENDER_NORMAL,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_SHOW_MAPS,
		g_param_spec_boolean (
			"show-maps",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	signals[SEND_MESSAGE] = g_signal_new (
		"send-message",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (EABContactDisplayClass, send_message),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		E_TYPE_DESTINATION);
}

static void
eab_contact_display_init (EABContactDisplay *display)
{
	EWebView *web_view;
	GtkUIManager *ui_manager;
	GtkActionGroup *action_group;
	const gchar *domain = GETTEXT_PACKAGE;
	GError *error = NULL;

	display->priv = EAB_CONTACT_DISPLAY_GET_PRIVATE (display);

	web_view = E_WEB_VIEW (display);
	ui_manager = e_web_view_get_ui_manager (web_view);

#ifdef WITH_CONTACT_MAPS
	g_signal_connect (
		web_view, "create-plugin-widget",
		G_CALLBACK (contact_display_object_requested), display);
#endif
	e_signal_connect_notify (
		web_view, "notify::load-status",
		G_CALLBACK (contact_display_load_status_changed), NULL);
	g_signal_connect (
		web_view, "style-updated",
		G_CALLBACK (load_contact), NULL);

	e_web_view_install_request_handler (E_WEB_VIEW (display), E_TYPE_FILE_REQUEST);
	e_web_view_install_request_handler (E_WEB_VIEW (display), E_TYPE_STOCK_REQUEST);

	action_group = gtk_action_group_new ("internal-mailto");
	gtk_action_group_set_translation_domain (action_group, domain);
	gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);
	g_object_unref (action_group);

	gtk_action_group_add_actions (
		action_group, internal_mailto_entries,
		G_N_ELEMENTS (internal_mailto_entries), display);

	/* Because we are loading from a hard-coded string, there is
	 * no chance of I/O errors.  Failure here implies a malformed
	 * UI definition.  Full stop. */
	gtk_ui_manager_add_ui_from_string (ui_manager, ui, -1, &error);
	if (error != NULL)
		g_error ("%s", error->message);
}

GtkWidget *
eab_contact_display_new (void)
{
	return g_object_new (EAB_TYPE_CONTACT_DISPLAY, NULL);
}

EContact *
eab_contact_display_get_contact (EABContactDisplay *display)
{
	g_return_val_if_fail (EAB_IS_CONTACT_DISPLAY (display), NULL);

	return display->priv->contact;
}

void
eab_contact_display_set_contact (EABContactDisplay *display,
                                 EContact *contact)
{
	g_return_if_fail (EAB_IS_CONTACT_DISPLAY (display));

	if (display->priv->contact == contact)
		return;

	if (contact != NULL)
		g_object_ref (contact);

	if (display->priv->contact != NULL)
		g_object_unref (display->priv->contact);

	display->priv->contact = contact;

	load_contact (display);

	g_object_notify (G_OBJECT (display), "contact");
}

EABContactDisplayMode
eab_contact_display_get_mode (EABContactDisplay *display)
{
	g_return_val_if_fail (EAB_IS_CONTACT_DISPLAY (display), 0);

	return display->priv->mode;
}

void
eab_contact_display_set_mode (EABContactDisplay *display,
                              EABContactDisplayMode mode)
{
	g_return_if_fail (EAB_IS_CONTACT_DISPLAY (display));

	if (display->priv->mode == mode)
		return;

	display->priv->mode = mode;

	load_contact (display);

	g_object_notify (G_OBJECT (display), "mode");
}

gboolean
eab_contact_display_get_show_maps (EABContactDisplay *display)
{
	g_return_val_if_fail (EAB_IS_CONTACT_DISPLAY (display), FALSE);

	return display->priv->show_maps;
}

void
eab_contact_display_set_show_maps (EABContactDisplay *display,
                                   gboolean show_maps)
{
	g_return_if_fail (EAB_IS_CONTACT_DISPLAY (display));

	if (display->priv->show_maps == show_maps)
		return;

	display->priv->show_maps = show_maps;

	load_contact (display);

	g_object_notify (G_OBJECT (display), "show-maps");
}

/*
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
 * Copyright (C) 2011 Dan Vratil <dvratil@redhat.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <e-util/e-print.h>
#include <e-util/e-marshal.h>

#include <webkit/webkitdom.h>

#include "e-mail-printer.h"
#include "em-format-html-print.h"
#include "e-mail-display.h"

static gpointer parent_class = NULL;

enum {
        BUTTON_SELECT_ALL,
        BUTTON_SELECT_NONE,
        BUTTON_TOP,
        BUTTON_UP,
        BUTTON_DOWN,
        BUTTON_BOTTOM,
        BUTTONS_COUNT
};

#define w(x)

struct _EMailPrinterPrivate {
	EMFormatHTMLPrint *efhp;

        gboolean export_mode;

	GtkListStore *headers;

	WebKitWebView *webview; /* WebView to print from */
	gchar *uri;
	GtkWidget *buttons[BUTTONS_COUNT];
	GtkWidget *treeview;

	GtkPrintOperation *operation;
};

G_DEFINE_TYPE (
	EMailPrinter,
	e_mail_printer,
	G_TYPE_OBJECT);

enum {
	PROP_0,
	PROP_PRINT_FORMATTER
};

enum {
	SIGNAL_DONE,
	LAST_SIGNAL
};

enum {
	COLUMN_ACTIVE,
	COLUMN_HEADER_NAME,
	COLUMN_HEADER_VALUE,
	COLUMN_HEADER_STRUCT,
	LAST_COLUMN
};

static guint signals[LAST_SIGNAL];

static gint
emp_header_name_equal (const EMFormatHeader *h1,
                       const EMFormatHeader *h2)
{
	if ((h2->value == NULL) || (h1->value == NULL)) {
		return g_strcmp0 (h1->name, h2->name);
	} else {
		if ((g_strcmp0 (h1->name, h2->name) == 0) &&
		    (g_strcmp0 (h1->value, h2->value) == 0))
			return 0;
		else
			return 1;
	}
}

static void
emp_draw_footer (GtkPrintOperation *operation,
                 GtkPrintContext *context,
                 gint page_nr)
{
	PangoFontDescription *desc;
	PangoLayout *layout;
	gint n_pages;
	gdouble width, height;
	gchar *text;
	cairo_t *cr;

	cr = gtk_print_context_get_cairo_context (context);
	width = gtk_print_context_get_width (context);
	height = gtk_print_context_get_height (context);

	g_object_get (operation, "n-pages", &n_pages, NULL);
	text = g_strdup_printf (_("Page %d of %d"), page_nr + 1, n_pages);

	cairo_set_source_rgb (cr, 0.1, 0.1, 0.1);
	cairo_fill (cr);

	desc = pango_font_description_from_string ("Sans Regular 10");
	layout = gtk_print_context_create_pango_layout (context);
	pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
	pango_layout_set_font_description (layout, desc);
	pango_layout_set_text (layout, text, -1);
	pango_layout_set_width (layout, width * PANGO_SCALE);
	pango_font_description_free (desc);

	cairo_move_to (cr, 0, height + 5);
	pango_cairo_show_layout (cr, layout);

	g_object_unref (layout);
	g_free (text);
}

static void
emp_printing_done (GtkPrintOperation *operation,
                   GtkPrintOperationResult result,
                   gpointer user_data)
{
	EMailPrinter *emp = user_data;

	g_signal_emit (emp, signals[SIGNAL_DONE], 0, operation, result);
}

static void
emp_start_printing (GObject *object,
                    GParamSpec *pspec,
                    gpointer user_data)
{
	WebKitWebView *web_view;
	WebKitWebFrame *frame;
	WebKitLoadStatus load_status;
	EMailPrinter *emp = user_data;

	web_view = WEBKIT_WEB_VIEW (object);
	load_status = webkit_web_view_get_load_status (web_view);

	if (load_status != WEBKIT_LOAD_FINISHED)
		return;

	frame = webkit_web_view_get_main_frame (web_view);

	if (emp->priv->export_mode) {
		gtk_print_operation_set_export_filename (
			emp->priv->operation,
			emp->priv->efhp->export_filename);
		webkit_web_frame_print_full (
			frame, emp->priv->operation,
			GTK_PRINT_OPERATION_ACTION_EXPORT, NULL);
	} else {
		webkit_web_frame_print_full
		(frame, emp->priv->operation,
		 GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, NULL);
	}

}

static void
emp_run_print_operation (EMailPrinter *emp)
{
	EMFormat *emf;
	SoupSession *session;
	GHashTable *formatters;
	gchar *mail_uri;

	emf = EM_FORMAT (emp->priv->efhp);
	mail_uri = em_format_build_mail_uri (emf->folder, emf->message_uid, NULL, NULL);

	/* It's safe to assume that session exists and contains formatters table,
	 * because at least the message we are about to print now must be already
	 * there */
	session = webkit_get_default_session ();
	formatters = g_object_get_data (G_OBJECT (session), "formatters");
	g_hash_table_insert (formatters, g_strdup (mail_uri), emp->priv->efhp);

	/* Print_layout is a special EMPart created by EMFormatHTMLPrint */
	if (emp->priv->uri)
		g_free (emp->priv->uri);

        emp->priv->uri = g_strconcat (mail_uri, "?part_id=print_layout&__evo-load-images=1", NULL);

	if (emp->priv->webview == NULL) {
		emp->priv->webview = g_object_new (E_TYPE_MAIL_DISPLAY, NULL);
		e_web_view_set_enable_frame_flattening (E_WEB_VIEW (emp->priv->webview), FALSE);
		e_mail_display_set_force_load_images (
				E_MAIL_DISPLAY (emp->priv->webview), TRUE);
		g_object_ref_sink (emp->priv->webview);
                g_signal_connect (emp->priv->webview, "notify::load-status",
			G_CALLBACK (emp_start_printing), emp);

		w ({
			GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
			GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);
			gtk_container_add (GTK_CONTAINER (window), sw);
			gtk_container_add (GTK_CONTAINER (sw),
					   GTK_WIDGET (emp->priv->webview));
			gtk_widget_show_all (window);
		});
	}

	e_mail_display_set_formatter (E_MAIL_DISPLAY (emp->priv->webview),
				      (EMFormatHTML *) emp->priv->efhp);

	webkit_web_view_load_uri (emp->priv->webview, emp->priv->uri);

	g_free (mail_uri);
}

static void
set_header_visible (EMailPrinter *emp,
                    EMFormatHeader *header,
                    gint index,
                    gboolean visible)
{
	WebKitDOMDocument *document;
	WebKitDOMNodeList *headers;
	WebKitDOMElement *element;
	WebKitDOMCSSStyleDeclaration *style;

	document = webkit_web_view_get_dom_document (emp->priv->webview);
        headers = webkit_dom_document_get_elements_by_class_name (document, "header-item");

	g_return_if_fail (index < webkit_dom_node_list_get_length (headers));

	element = WEBKIT_DOM_ELEMENT (webkit_dom_node_list_item (headers, index));
	style = webkit_dom_element_get_style (element);
	webkit_dom_css_style_declaration_set_property (style,
                "display", (visible ? "table-row" : "none"), "", NULL);
}

static void
header_active_renderer_toggled_cb (GtkCellRendererToggle *renderer,
                                   gchar *path,
                                   EMailPrinter *emp)
{
	GtkTreeIter iter;
	GtkTreePath *p;
	gboolean active;
	EMFormatHeader *header;
	gint *indices;

	gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (emp->priv->headers),
		&iter, path);

	gtk_tree_model_get (GTK_TREE_MODEL (emp->priv->headers), &iter,
		COLUMN_ACTIVE, &active, -1);
	gtk_tree_model_get (GTK_TREE_MODEL (emp->priv->headers), &iter,
		COLUMN_HEADER_STRUCT, &header, -1);
	gtk_list_store_set (GTK_LIST_STORE (emp->priv->headers), &iter,
		COLUMN_ACTIVE, !active, -1);

	p = gtk_tree_path_new_from_string (path);
	indices = gtk_tree_path_get_indices (p);
	set_header_visible (emp, header, indices[0], !active);
	gtk_tree_path_free (p);
}

static void
emp_headers_tab_toggle_selection (GtkWidget *button,
                                  gpointer user_data)
{
	EMailPrinter *emp = user_data;
	GtkTreeIter iter;
	gboolean select;

	if (button == emp->priv->buttons[BUTTON_SELECT_ALL])
		select = TRUE;
	else if (button == emp->priv->buttons[BUTTON_SELECT_NONE])
		select = FALSE;
	else
		return;

	if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (emp->priv->headers), &iter))
		return;

	do {
		EMFormatHeader *header;
		GtkTreePath *path;
		gint *indices;

		gtk_tree_model_get (GTK_TREE_MODEL (emp->priv->headers), &iter,
			COLUMN_HEADER_STRUCT, &header, -1);
		gtk_list_store_set (GTK_LIST_STORE (emp->priv->headers), &iter,
			COLUMN_ACTIVE, select, -1);

		path = gtk_tree_model_get_path (GTK_TREE_MODEL (emp->priv->headers), &iter);
		indices = gtk_tree_path_get_indices (path);
		set_header_visible (emp, header, indices[0], select);
		gtk_tree_path_free (path);

	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (emp->priv->headers), &iter));
}

static void
emp_headers_tab_selection_changed (GtkTreeSelection *selection,
                                   gpointer user_data)
{
	EMailPrinter *emp = user_data;
	gboolean enabled;
	GList *selected_rows;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreePath *path;

	if (gtk_tree_selection_count_selected_rows (selection) == 0) {
		gtk_widget_set_sensitive (emp->priv->buttons[BUTTON_TOP], FALSE);
		gtk_widget_set_sensitive (emp->priv->buttons[BUTTON_UP], FALSE);
		gtk_widget_set_sensitive (emp->priv->buttons[BUTTON_DOWN], FALSE);
		gtk_widget_set_sensitive (emp->priv->buttons[BUTTON_BOTTOM], FALSE);

		return;
	}

	model = GTK_TREE_MODEL (emp->priv->headers);
	selected_rows = gtk_tree_selection_get_selected_rows (selection, &model);

	path = gtk_tree_path_copy (selected_rows->data);
	enabled = gtk_tree_path_prev (path);
	gtk_widget_set_sensitive (emp->priv->buttons[BUTTON_TOP], enabled);
	gtk_widget_set_sensitive (emp->priv->buttons[BUTTON_UP], enabled);

	gtk_tree_model_get_iter (model, &iter, g_list_last (selected_rows)->data);
	enabled = gtk_tree_model_iter_next (model, &iter);
	gtk_widget_set_sensitive (emp->priv->buttons[BUTTON_DOWN], enabled);
	gtk_widget_set_sensitive (emp->priv->buttons[BUTTON_BOTTOM], enabled);

	g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (selected_rows);
	gtk_tree_path_free (path);
}

static void
emp_headers_tab_move (GtkWidget *button,
                      gpointer user_data)
{
	EMailPrinter *emp = user_data;
	GtkTreeSelection *selection;
	GList *selected_rows, *references, *l;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeRowReference *selection_middle;
	gint *indices;

	WebKitDOMDocument *document;
	WebKitDOMNodeList *headers;
	WebKitDOMNode *header, *parent;

	model = GTK_TREE_MODEL (emp->priv->headers);
	selection = gtk_tree_view_get_selection  (GTK_TREE_VIEW (emp->priv->treeview));
	selected_rows = gtk_tree_selection_get_selected_rows (selection, &model);

        /* The order of header rows in the HMTL document should be in sync with
	   order of headers in the listview and in efhp->headers_list */
	document = webkit_web_view_get_dom_document (emp->priv->webview);
        headers = webkit_dom_document_get_elements_by_class_name (document, "header-item");

	l = g_list_nth (selected_rows, g_list_length (selected_rows) / 2);
	selection_middle = gtk_tree_row_reference_new (model, l->data);

	references = NULL;
	for (l = selected_rows; l; l = l->next) {
		references = g_list_prepend (references,
			gtk_tree_row_reference_new (model, l->data));
	}

	if (button == emp->priv->buttons[BUTTON_TOP]) {

		for (l = references; l; l = l->next) {
                        /* Move the rows in the view  */
			path = gtk_tree_row_reference_get_path (l->data);
			gtk_tree_model_get_iter (model, &iter, path);
			gtk_list_store_move_after (emp->priv->headers, &iter, NULL);

                        /* Move the header row in HTML document */
			indices = gtk_tree_path_get_indices (path);
			header = webkit_dom_node_list_item (headers, indices[0]);
			parent = webkit_dom_node_get_parent_node (header);
			webkit_dom_node_remove_child (parent, header, NULL);
			webkit_dom_node_insert_before (parent, header,
				webkit_dom_node_get_first_child (parent), NULL);

			gtk_tree_path_free (path);
		}

       } else if (button == emp->priv->buttons[BUTTON_UP]) {

		GtkTreeIter *iter_prev;
		WebKitDOMNode *node2;

		references = g_list_reverse (references);

		for (l = references; l; l = l->next) {

			path = gtk_tree_row_reference_get_path (l->data);
			gtk_tree_model_get_iter (model, &iter, path);
			iter_prev = gtk_tree_iter_copy (&iter);
			gtk_tree_model_iter_previous (model, iter_prev);

			gtk_list_store_move_before (emp->priv->headers, &iter, iter_prev);

			indices = gtk_tree_path_get_indices (path);
			header = webkit_dom_node_list_item (headers, indices[0]);
			node2 = webkit_dom_node_get_previous_sibling (header);
			parent = webkit_dom_node_get_parent_node (header);

			webkit_dom_node_remove_child (parent, header, NULL);
			webkit_dom_node_insert_before (parent, header, node2, NULL);

			gtk_tree_path_free (path);
			gtk_tree_iter_free (iter_prev);
		}

	} else if (button == emp->priv->buttons[BUTTON_DOWN]) {

		GtkTreeIter *iter_next;
		WebKitDOMNode *node2;

		for (l = references; l; l = l->next) {

			path = gtk_tree_row_reference_get_path (l->data);
			gtk_tree_model_get_iter (model, &iter, path);
			iter_next = gtk_tree_iter_copy (&iter);
			gtk_tree_model_iter_next (model, iter_next);

			gtk_list_store_move_after (emp->priv->headers, &iter, iter_next);

			indices = gtk_tree_path_get_indices (path);
			header = webkit_dom_node_list_item (headers, indices[0]);
			node2 = webkit_dom_node_get_next_sibling (header);
			parent = webkit_dom_node_get_parent_node (header);

			webkit_dom_node_remove_child (parent, header, NULL);
			webkit_dom_node_insert_before (parent, header,
				webkit_dom_node_get_next_sibling (node2), NULL);

			gtk_tree_path_free (path);
			gtk_tree_iter_free (iter_next);
		}

	} else if (button == emp->priv->buttons[BUTTON_BOTTOM]) {

		references = g_list_reverse (references);

		for (l = references; l; l = l->next) {
			path = gtk_tree_row_reference_get_path (l->data);
			gtk_tree_model_get_iter (model, &iter, path);
			gtk_list_store_move_before (emp->priv->headers, &iter, NULL);

                        /* Move the header row in HTML document */
			indices = gtk_tree_path_get_indices (path);
			header = webkit_dom_node_list_item (headers, indices[0]);
			parent = webkit_dom_node_get_parent_node (header);
			webkit_dom_node_remove_child (parent, header, NULL);
			webkit_dom_node_append_child (parent, header, NULL);

			gtk_tree_path_free (path);
		}
	};

	g_list_foreach (references, (GFunc) gtk_tree_row_reference_free, NULL);
	g_list_free (references);

        /* Keep the selection in middle of the screen */
	path = gtk_tree_row_reference_get_path (selection_middle);
	gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (emp->priv->treeview),
		path, COLUMN_ACTIVE, TRUE, 0.5, 0.5);
	gtk_tree_path_free (path);
	gtk_tree_row_reference_free (selection_middle);

	g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (selected_rows);

	emp_headers_tab_selection_changed (selection, user_data);
}

static GtkWidget *
emp_create_headers_tab (GtkPrintOperation *operation,
                        EMailPrinter *emp)
{
	GtkWidget *vbox, *hbox, *scw, *button;
	GtkTreeView *view;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
	gtk_box_pack_end (GTK_BOX (hbox), vbox, FALSE, FALSE, 5);

	emp->priv->treeview = gtk_tree_view_new_with_model (
		GTK_TREE_MODEL (emp->priv->headers));
	view = GTK_TREE_VIEW (emp->priv->treeview);
	selection = gtk_tree_view_get_selection (view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
        g_signal_connect (selection, "changed",
		G_CALLBACK (emp_headers_tab_selection_changed), emp);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled",
		G_CALLBACK (header_active_renderer_toggled_cb), emp);
	column = gtk_tree_view_column_new_with_attributes (
		_("Print"), renderer, 
		"active", COLUMN_ACTIVE, NULL);
	gtk_tree_view_append_column (view, column);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
		_("Header Name"), renderer, 
		"text", COLUMN_HEADER_NAME, NULL);
	gtk_tree_view_append_column (view, column);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
		_("Header Value"), renderer,
		"text", COLUMN_HEADER_VALUE, NULL);
	gtk_tree_view_append_column (view, column);

	scw = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (scw), GTK_WIDGET (view));
	gtk_box_pack_start (GTK_BOX (hbox), scw, TRUE, TRUE, 0);

	button = gtk_button_new_from_stock (GTK_STOCK_SELECT_ALL);
	emp->priv->buttons[BUTTON_SELECT_ALL] = button;
        g_signal_connect (button, "clicked",
		G_CALLBACK (emp_headers_tab_toggle_selection), emp);
	gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, TRUE, 5);

	button = gtk_button_new_from_stock (GTK_STOCK_CLEAR);
	emp->priv->buttons[BUTTON_SELECT_NONE] = button;
        g_signal_connect (button, "clicked",
		G_CALLBACK (emp_headers_tab_toggle_selection), emp);
	gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, TRUE, 5);

	button = gtk_button_new_from_stock (GTK_STOCK_GOTO_TOP);
	emp->priv->buttons[BUTTON_TOP] = button;
	gtk_widget_set_sensitive (button, FALSE);
        g_signal_connect (button, "clicked",
		G_CALLBACK (emp_headers_tab_move), emp);
	gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, TRUE, 5);

	button = gtk_button_new_from_stock (GTK_STOCK_GO_UP);
	emp->priv->buttons[BUTTON_UP] = button;
	gtk_widget_set_sensitive (button, FALSE);
        g_signal_connect (button, "clicked",
		G_CALLBACK (emp_headers_tab_move), emp);
	gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, TRUE, 5);

	button = gtk_button_new_from_stock (GTK_STOCK_GO_DOWN);
	emp->priv->buttons[BUTTON_DOWN] = button;
	gtk_widget_set_sensitive (button, FALSE);
        g_signal_connect (button, "clicked",
		G_CALLBACK (emp_headers_tab_move), emp);
	gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, TRUE, 5);

	button = gtk_button_new_from_stock (GTK_STOCK_GOTO_BOTTOM);
	emp->priv->buttons[BUTTON_BOTTOM] = button;
	gtk_widget_set_sensitive (button, FALSE);
        g_signal_connect (button, "clicked",
		G_CALLBACK (emp_headers_tab_move), emp);
	gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, TRUE, 5);

	gtk_print_operation_set_custom_tab_label (operation, _("Headers"));
	gtk_widget_show_all (hbox);

	return hbox;
}

static void
emp_set_formatter (EMailPrinter *emp,
                   EMFormatHTMLPrint *formatter)
{
	EMFormat *emf = (EMFormat *) formatter;
	CamelMediumHeader *header;
	GArray *headers;
	gint i;
	GtkTreeIter last_known;

	g_return_if_fail (EM_IS_FORMAT_HTML_PRINT (formatter));

	g_object_ref (formatter);

	if (emp->priv->efhp)
		g_object_unref (emp->priv->efhp);

	emp->priv->efhp = formatter;

	if (emp->priv->headers)
		g_object_unref (emp->priv->headers);
	emp->priv->headers = gtk_list_store_new (5,
		G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_INT);

	headers = camel_medium_get_headers (CAMEL_MEDIUM (emf->message));
	if (!headers)
		return;

	for (i = 0; i < headers->len; i++) {
		GtkTreeIter iter;
		GList *found_header;
		EMFormatHeader *emfh;

		header = &g_array_index (headers, CamelMediumHeader, i);
		emfh = em_format_header_new (header->name, header->value);

		found_header = g_queue_find_custom (&EM_FORMAT (formatter)->header_list,
				emfh, (GCompareFunc) emp_header_name_equal);

		if (!found_header) {
			emfh->flags |= EM_FORMAT_HTML_HEADER_HIDDEN;
			em_format_add_header_struct (EM_FORMAT (formatter), emfh);
			gtk_list_store_append (emp->priv->headers, &iter);
		} else {
			if (gtk_list_store_iter_is_valid (emp->priv->headers, &last_known))
				gtk_list_store_insert_after (emp->priv->headers, &iter, &last_known);
			else
				gtk_list_store_insert_after (emp->priv->headers, &iter, NULL);

			last_known = iter;
		}

		gtk_list_store_set (emp->priv->headers, &iter,
			COLUMN_ACTIVE, (found_header != NULL),
			COLUMN_HEADER_NAME, emfh->name,
			COLUMN_HEADER_VALUE, emfh->value,
			COLUMN_HEADER_STRUCT, emfh, -1);
	}

	camel_medium_free_headers (CAMEL_MEDIUM (emf->message), headers);
}

static void
emp_set_property (GObject *object,
                  guint property_id,
                  const GValue *value,
                  GParamSpec *pspec)
{
	EMailPrinter *emp = E_MAIL_PRINTER (object);

	switch (property_id) {

		case PROP_PRINT_FORMATTER:
			emp_set_formatter (emp, g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
emp_get_property (GObject *object,
                  guint property_id,
                  GValue *value,
                  GParamSpec *pspec)
{
	EMailPrinter *emp = E_MAIL_PRINTER (object);

	switch (property_id) {

		case PROP_PRINT_FORMATTER:
			g_value_set_object (value,
				e_mail_printer_get_print_formatter (emp));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
emp_finalize (GObject *object)
{
	EMailPrinterPrivate *priv = E_MAIL_PRINTER (object)->priv;

	if (priv->efhp) {
		g_object_unref (priv->efhp);
		priv->efhp = NULL;
	}

	if (priv->headers) {
		GtkTreeIter iter;

		if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->headers), &iter)) {
			do {
				EMFormatHeader *header = NULL;
				gtk_tree_model_get (GTK_TREE_MODEL (priv->headers), &iter,
					COLUMN_HEADER_STRUCT, &header, -1);
				em_format_header_free (header);
			} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (priv->headers), &iter));
		}
		g_object_unref (priv->headers);
		priv->headers = NULL;
	}

	if (priv->webview) {
		g_object_unref (priv->webview);
		priv->webview = NULL;
	}

	if (priv->uri) {
		g_free (priv->uri);
		priv->uri = NULL;
	}

	if (priv->operation) {
		g_object_unref (priv->operation);
		priv->operation = NULL;
	}

        /* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
e_mail_printer_class_init (EMailPrinterClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);
	g_type_class_add_private (klass, sizeof (EMailPrinterPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = emp_set_property;
	object_class->get_property = emp_get_property;
	object_class->finalize = emp_finalize;

	g_object_class_install_property (
		object_class,
		PROP_PRINT_FORMATTER,
		g_param_spec_object (
			"print-formatter",
			NULL,
			NULL,
			EM_TYPE_FORMAT_HTML_PRINT,
			G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	signals[SIGNAL_DONE] =	g_signal_new ("done",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (EMailPrinterClass, done),
		NULL, NULL,
		e_marshal_VOID__OBJECT_INT,
		G_TYPE_NONE, 2,
		GTK_TYPE_PRINT_OPERATION, G_TYPE_INT);
}

static void
e_mail_printer_init (EMailPrinter *emp)
{
	emp->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		emp, E_TYPE_MAIL_PRINTER, EMailPrinterPrivate);

	emp->priv->efhp = NULL;
	emp->priv->headers = NULL;
	emp->priv->webview = NULL;
}

EMailPrinter *
e_mail_printer_new (EMFormatHTML * source)
{
	EMailPrinter *emp;
	EMFormatHTMLPrint *efhp;

	efhp = em_format_html_print_new (source);

	emp = g_object_new (E_TYPE_MAIL_PRINTER,
		"print-formatter", efhp, NULL);

	g_object_unref (efhp);

	return emp;
}

void
e_mail_printer_print (EMailPrinter *emp,
                      gboolean export_mode,
                      GCancellable *cancellable)
{
	g_return_if_fail (E_IS_MAIL_PRINTER (emp));

	if (emp->priv->operation)
		g_object_unref (emp->priv->operation);
	emp->priv->operation = e_print_operation_new ();
	gtk_print_operation_set_unit (emp->priv->operation, GTK_UNIT_PIXEL);

	gtk_print_operation_set_show_progress (emp->priv->operation, TRUE);
	g_signal_connect (emp->priv->operation, "create-custom-widget",
		G_CALLBACK (emp_create_headers_tab), emp);
	g_signal_connect (emp->priv->operation, "done",
		G_CALLBACK (emp_printing_done), emp);
        g_signal_connect (emp->priv->operation, "draw-page",
		G_CALLBACK (emp_draw_footer), NULL);

	emp->priv->export_mode = export_mode;

	if (cancellable)
                g_signal_connect_swapped (cancellable, "cancelled",
			G_CALLBACK (gtk_print_operation_cancel), emp->priv->operation);

	emp_run_print_operation (emp);
}

const gchar *
e_mail_printer_get_export_filename (EMailPrinter *printer)
{
	g_return_val_if_fail (E_IS_MAIL_PRINTER (printer), NULL);

	if (!printer->priv->efhp)
	  return NULL;

	return printer->priv->efhp->export_filename;
}

void
e_mail_printer_set_export_filename (EMailPrinter *printer,
                                    const gchar *filename)
{
	g_return_if_fail (E_IS_MAIL_PRINTER (printer));
	g_return_if_fail (printer->priv->efhp != NULL);

	if (printer->priv->efhp->export_filename && *printer->priv->efhp->export_filename)
	  g_free (printer->priv->efhp->export_filename);

	printer->priv->efhp->export_filename = g_strdup (filename);
}

EMFormatHTMLPrint *
e_mail_printer_get_print_formatter (EMailPrinter *emp)
{
	g_return_val_if_fail (E_IS_MAIL_PRINTER (emp), NULL);

	return emp->priv->efhp;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2002 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "mail-accounts.h"

#define USE_ETABLE 0

#ifdef USE_ETABLE
#include <gal/e-table/e-table-memory-store.h>
#include <gal/e-table/e-table-scrolled.h>
#include <gal/e-table/e-cell-toggle.h>
#endif
#include <camel/camel-url.h>

#include <gtk/gtkliststore.h>
#include <gtk/gtktreeselection.h>

#include <bonobo/bonobo-generic-factory.h>

#include "mail.h"
#include "mail-config.h"
#include "mail-config-druid.h"
#include "mail-account-editor.h"
#include "mail-send-recv.h"

#include "art/mark.xpm"


static void mail_accounts_tab_class_init (MailAccountsTabClass *class);
static void mail_accounts_tab_init       (MailAccountsTab *prefs);
static void mail_accounts_tab_finalise   (GObject *obj);

static void mail_accounts_load (MailAccountsTab *tab);

static GdkPixbuf *disabled_pixbuf = NULL;
static GdkPixbuf *enabled_pixbuf = NULL;

static GtkVBoxClass *parent_class = NULL;


#define PREFS_WINDOW(prefs) GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (prefs), GTK_TYPE_WINDOW))


GType
mail_accounts_tab_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		GTypeInfo type_info = {
			sizeof (MailAccountsTabClass),
			NULL, NULL,
			(GClassInitFunc) mail_accounts_tab_class_init,
			NULL, NULL,
			sizeof (MailAccountsTab),
			0,
			(GInstanceInitFunc) mail_accounts_tab_init,
		};
		
		type = g_type_register_static (gtk_vbox_get_type (), "MailAccountsTab", &type_info, 0);
	}
	
	return type;
}

static void
mail_accounts_tab_class_init (MailAccountsTabClass *klass)
{
	GObjectClass *object_class;
	
	object_class = (GObjectClass *) klass;
	parent_class = g_type_class_ref(gtk_vbox_get_type ());
	
	object_class->finalize = mail_accounts_tab_finalise;
	/* override methods */
	
	
	/* setup static data */
	disabled_pixbuf = NULL;
	enabled_pixbuf = gdk_pixbuf_new_from_xpm_data ((const char **) mark_xpm);
}

static void
mail_accounts_tab_init (MailAccountsTab *prefs)
{
	prefs->druid = NULL;
	prefs->editor = NULL;
	
	gdk_pixbuf_render_pixmap_and_mask (enabled_pixbuf, &prefs->mark_pixmap, &prefs->mark_bitmap, 128);
}

static void
mail_accounts_tab_finalise (GObject *obj)
{
	MailAccountsTab *prefs = (MailAccountsTab *) obj;
	
	g_object_unref((prefs->gui));
	gdk_pixmap_unref (prefs->mark_pixmap);
	g_object_unref (prefs->mark_bitmap);
	
        ((GObjectClass *)(parent_class))->finalize (obj);
}

static void
account_add_finished (MailAccountsTab *prefs, GObject *deadbeef)
{
	/* Either Cancel or Finished was clicked in the druid so reload the accounts */
	prefs->druid = NULL;
	
#warning "GTK_OBJECT_DESTROYED"
#if 0	
	if (!GTK_OBJECT_DESTROYED (prefs))
#endif
		mail_accounts_load (prefs);
	
	g_object_unref (prefs);
}

static void
account_add_clicked (GtkButton *button, gpointer user_data)
{
	MailAccountsTab *prefs = (MailAccountsTab *) user_data;
	
	if (prefs->druid == NULL) {
		prefs->druid = (GtkWidget *) mail_config_druid_new (prefs->shell);
		g_object_weak_ref ((GObject *) prefs->druid,
				   (GWeakNotify) account_add_finished, prefs);
		
		gtk_widget_show (prefs->druid);
		g_object_ref (prefs);
	} else {
		gdk_window_raise (prefs->druid->window);
	}
}

static void
account_edit_finished (MailAccountsTab *prefs, GObject *deadbeef)
{
	prefs->editor = NULL;
	
#warning "GTK_OBJECT_DESTROYED"
#if 0
	if (!GTK_OBJECT_DESTROYED (prefs))
#endif
		mail_accounts_load (prefs);
	
	g_object_unref (prefs);
}

static void
account_edit_clicked (GtkButton *button, gpointer user_data)
{
	MailAccountsTab *prefs = (MailAccountsTab *) user_data;
	
	if (prefs->editor == NULL) {
		MailConfigAccount *account = NULL;
#if USE_ETABLE
		int row;

		row = e_table_get_cursor_row (prefs->table);
		if (row >=0)
			account = e_table_memory_get_data (E_TABLE_MEMORY (prefs->model), row);
#else
		GtkTreeSelection *selection;
		GtkTreeModel *model;
		GtkTreeIter iter;
		
		selection = gtk_tree_view_get_selection(prefs->table);
		if (gtk_tree_selection_get_selected(selection, &model, &iter))
			gtk_tree_model_get(model, &iter, 3, &account, -1);
#endif
		if (account) {
			GtkWidget *window;
			
			window = gtk_widget_get_ancestor (GTK_WIDGET (prefs), GTK_TYPE_WINDOW);
			prefs->editor = (GtkWidget *) mail_account_editor_new (account, GTK_WINDOW (window), prefs);
			g_object_weak_ref ((GObject *) prefs->editor, (GWeakNotify) account_edit_finished, prefs);
			gtk_widget_show (prefs->editor);
			g_object_ref (prefs);
		}
	} else {
		gdk_window_raise (prefs->editor->window);
	}
}

static void
account_delete_clicked (GtkButton *button, gpointer user_data)
{
	MailAccountsTab *prefs = user_data;
	const MailConfigAccount *account = NULL;
	GtkDialog *confirm;
	GtkButton *tmp;
	const GSList *list;
	int ans;
	
#if USE_ETABLE
	int row;

	row = e_table_get_cursor_row (prefs->table);
	if (row >=0)
		account = e_table_memory_get_data (E_TABLE_MEMORY (prefs->model), row);
#else
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;

	selection = gtk_tree_view_get_selection(prefs->table);
	if (gtk_tree_selection_get_selected(selection, &model, &iter))
		gtk_tree_model_get(model, &iter, 3, &account, -1);
#endif
	/* make sure we have a valid account selected and that we aren't editing anything... */
	if (account == NULL || prefs->editor != NULL)
		return;

	confirm = (GtkDialog *)gtk_message_dialog_new(PREFS_WINDOW (prefs),
						      GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
						      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
						      _("Are you sure you want to delete this account?"));
	tmp = (GtkButton *)gtk_button_new_from_stock(GTK_STOCK_YES);
	gtk_button_set_label(tmp, _("Delete"));
	gtk_dialog_add_action_widget(confirm, (GtkWidget *)tmp, GTK_RESPONSE_YES);
	tmp = (GtkButton *)gtk_button_new_from_stock(GTK_STOCK_NO);
	gtk_button_set_label(tmp, _("Don't delete"));
	gtk_dialog_add_action_widget(confirm, (GtkWidget *)tmp, GTK_RESPONSE_NO);
	ans = gtk_dialog_run(confirm);
	gtk_widget_destroy((GtkWidget *)confirm);
	g_object_unref(confirm);

	if (ans == GTK_RESPONSE_YES) {
		int len;
		
		/* remove it from the folder-tree in the shell */
		if (account->source && account->source->url && account->source->enabled)
			mail_remove_storage_by_uri (account->source->url);
		
		/* remove it from the config file */
		list = mail_config_remove_account ((MailConfigAccount *) account);
		
		mail_config_write ();
		
		mail_autoreceive_setup ();
		
#if USE_ETABLE
		e_table_memory_store_remove (E_TABLE_MEMORY_STORE (prefs->model), row);
#else
		gtk_list_store_remove((GtkListStore *)model, &iter);
#endif		
		
		len = list ? g_slist_length ((GSList *) list) : 0;
		if (len > 0) {
#if USE_ETABLE
			e_table_set_cursor_row (prefs->table, row >= len ? len - 1 : row);
#else
			gtk_tree_selection_select_iter(selection, &iter);
#endif
		} else {
			gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_edit), FALSE);
			gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_delete), FALSE);
			gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_default), FALSE);
			gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_able), FALSE);
		}
	}
}

static void
account_default_clicked (GtkButton *button, gpointer user_data)
{
	MailAccountsTab *prefs = user_data;
	const MailConfigAccount *account = NULL;
	int row;
	
#if USE_ETABLE
	row = e_table_get_cursor_row (prefs->table);
	if (row >= 0)
		account = e_table_memory_get_data (E_TABLE_MEMORY (prefs->model), row);

#else
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;

	selection = gtk_tree_view_get_selection(prefs->table);
	if (gtk_tree_selection_get_selected(selection, &model, &iter))
		gtk_tree_model_get(model, &iter, 3, &account, -1);
#endif

	if (account) {
		mail_config_set_default_account (account);
		
		mail_config_write ();
		
		mail_accounts_load (prefs);
	}
}

static void
account_able_clicked (GtkButton *button, gpointer user_data)
{
	MailAccountsTab *prefs = user_data;
	const MailConfigAccount *account = NULL;
#if USE_ETABLE
	int row;
	
	row = e_table_get_cursor_row (prefs->table);
	if (row >= 0) {
		account = e_table_memory_get_data (E_TABLE_MEMORY (prefs->model), row);
		account->source->enabled = !account->source->enabled;
	}
#else
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;

	selection = gtk_tree_view_get_selection(prefs->table);
	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		gtk_tree_model_get(model, &iter, 3, &account, -1);
		account->source->enabled = !account->source->enabled;
		gtk_list_store_set((GtkListStore *)model, &iter, 0, account->source->enabled, -1);
	}
#endif
	if (account) {
		/* if the account got disabled, remove it from the
		   folder-tree, otherwise add it to the folder-tree */
		if (account->source->url) {
			if (account->source->enabled)
				mail_load_storage_by_uri (prefs->shell, account->source->url, account->name);
			else
				mail_remove_storage_by_uri (account->source->url);
		}
		mail_autoreceive_setup ();
		
		mail_config_write ();
	}
}

#if USE_ETABLE
static void
account_cursor_change (ETable *table, int row, gpointer user_data)
{
	MailAccountsTab *prefs = user_data;
	
	if (row >= 0) {
		const MailConfigAccount *account;
		
		account = e_table_memory_get_data (E_TABLE_MEMORY (prefs->model), row);
		if (account->source && account->source->enabled)
			gtk_label_set_text (GTK_LABEL (GTK_BIN (prefs->mail_able)->child), _("Disable"));
		else
			gtk_label_set_text (GTK_LABEL (GTK_BIN (prefs->mail_able)->child), _("Enable"));
		
		gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_edit), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_delete), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_default), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_able), TRUE);
	} else {
		gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_edit), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_delete), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_default), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_able), FALSE);
		
		gtk_widget_grab_focus (GTK_WIDGET (prefs->mail_add));
	}
}

static void
account_double_click (ETable *table, int row, int col, GdkEvent *event, gpointer user_data)
{
	account_edit_clicked (NULL, user_data);
}
#else
static void
account_cursor_change (GtkTreeSelection *selection, MailAccountsTab *prefs)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	const MailConfigAccount *account;
	int state;

	state = gtk_tree_selection_get_selected(selection, &model, &iter);
	if (state) {
		gtk_tree_model_get(model, &iter, 3, &account, -1);
		if (account->source && account->source->enabled)
			gtk_button_set_label(prefs->mail_able, _("Disable"));
		else
			gtk_button_set_label(prefs->mail_able, _("Enable"));
		/* FIXME: how do we get double clicks?? */
#warning "how to get double-clicks from gtktreeview"
		/*if (event && event->type == GDK_2BUTTON_PRESS)
		  account_edit_clicked (NULL, user_data);*/
	} else {
		gtk_widget_grab_focus (GTK_WIDGET (prefs->mail_add));
	}

	gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_edit), state);
	gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_delete), state);
	gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_default), state);
	gtk_widget_set_sensitive (GTK_WIDGET (prefs->mail_able), state);
}
#endif


static void
mail_accounts_load (MailAccountsTab *prefs)
{
	const GSList *node;
	int row = 0;
	
#if USE_ETABLE
	e_table_memory_freeze (E_TABLE_MEMORY (prefs->model));
	
	e_table_memory_store_clear (E_TABLE_MEMORY_STORE (prefs->model));
#else
	GtkTreeIter iter;
	GtkListStore *model;
	const MailConfigAccount *default_account;
	char *name, *val;

	model = (GtkListStore *)gtk_tree_view_get_model(prefs->table);
	gtk_list_store_clear(model);

	default_account = mail_config_get_default_account ();
#endif
	
	node = mail_config_get_accounts ();
	while (node) {
		const MailConfigAccount *account;
		CamelURL *url;
		
		account = node->data;
		
		url = account->source && account->source->url ? camel_url_new (account->source->url, NULL) : NULL;
		
#if USE_ETABLE
		e_table_memory_store_insert_list (E_TABLE_MEMORY_STORE (prefs->model),
						  row, GINT_TO_POINTER (account->source->enabled),
						  account->name,
						  url && url->protocol ? url->protocol : U_("None"));
		
		e_table_memory_set_data (E_TABLE_MEMORY (prefs->model), row, (gpointer) account);
#else
		gtk_list_store_append(model, &iter);
		if (account == default_account) {
			/* translators: default account indicator */
			name = val = g_strdup_printf("%s %s", account->name, _("[Default]"));
		} else {
			val = account->name;
			name = NULL;
		}

		gtk_list_store_set(model, &iter,
				   0, account->source->enabled,
				   1, val,
				   2, url && url->protocol ? url->protocol : (char *) _("None"),
				   3, account,
				   -1);
		g_free(name);
#endif
		
		if (url)
			camel_url_free (url);
		
		node = node->next;
		row++;
	}
	
#if USE_ETABLE
	e_table_memory_thaw (E_TABLE_MEMORY (prefs->model));
#endif
}



GtkWidget *mail_accounts_etable_new (char *widget_name, char *string1, char *string2,
				     int int1, int int2);

#if USE_ETABLE
GtkWidget *
mail_accounts_etable_new (char *widget_name, char *string1, char *string2, int int1, int int2)
{
	ETableModel *model;
	ETableExtras *extras;
	GdkPixbuf *images[2];
	ETableMemoryStoreColumnInfo columns[] = {
		E_TABLE_MEMORY_STORE_INTEGER,
		E_TABLE_MEMORY_STORE_STRING,
		E_TABLE_MEMORY_STORE_STRING,
		E_TABLE_MEMORY_STORE_TERMINATOR,
	};
	
	extras = e_table_extras_new ();
	
	images[0] = disabled_pixbuf;   /* disabled */
	images[1] = enabled_pixbuf;    /* enabled */
	e_table_extras_add_cell (extras, "render_able", e_cell_toggle_new (0, 2, images));
	
	model = e_table_memory_store_new (columns);
	
	return e_table_scrolled_new_from_spec_file (model, extras, EVOLUTION_ETSPECDIR "/mail-accounts.etspec", NULL);
}
#else
GtkWidget *
mail_accounts_etable_new (char *widget_name, char *string1, char *string2, int int1, int int2)
{
	GtkWidget *table, *scrolled;
	char *titles[3];
	GtkListStore *model;
	GtkCellRenderer *renderer;
	GtkTreeSelection *selection;

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	model = gtk_list_store_new(4, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	table = gtk_tree_view_new_with_model((GtkTreeModel *)model);
	gtk_tree_view_insert_column_with_attributes((GtkTreeView *)table, -1, _("Enabled"),
						    gtk_cell_renderer_toggle_new(),
						    "active", 0,
						    NULL);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes((GtkTreeView *)table, -1, _("Account name"),
						    renderer,
						    "text", 1,
						    NULL);
	gtk_tree_view_insert_column_with_attributes((GtkTreeView *)table, -1, _("Protocol"),
						    renderer,
						    "text", 2,
						    NULL);	
	selection = gtk_tree_view_get_selection ((GtkTreeView *)table);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	gtk_tree_view_set_headers_visible((GtkTreeView *)table, TRUE);

	/* FIXME: column auto-resize? */
	/* Is this needed?
	   gtk_tree_view_column_set_alignment(gtk_tree_view_get_column(prefs->table, 0), 1.0);*/

	gtk_container_add (GTK_CONTAINER (scrolled), table);
	
	g_object_set_data(G_OBJECT(scrolled), "table", table);
	
	gtk_widget_show (scrolled);
	gtk_widget_show (table);
	
	return scrolled;
}
#endif

static void
mail_accounts_tab_construct (MailAccountsTab *prefs)
{
	GtkWidget *toplevel, *widget;
	GladeXML *gui;
	
	gui = glade_xml_new (EVOLUTION_GLADEDIR "/mail-config.glade", "accounts_tab", NULL);
	prefs->gui = gui;
	
	/* get our toplevel widget */
	toplevel = glade_xml_get_widget (gui, "toplevel");
	
	/* reparent */
	gtk_widget_ref (toplevel);
	gtk_container_remove (GTK_CONTAINER (toplevel->parent), toplevel);
	gtk_container_add (GTK_CONTAINER (prefs), toplevel);
	gtk_widget_unref (toplevel);
	
	widget = glade_xml_get_widget (gui, "etableMailAccounts");
	
#if USE_ETABLE
	prefs->table = e_table_scrolled_get_table (E_TABLE_SCROLLED (widget));
	prefs->model = prefs->table->model;
	
	g_signal_connect((prefs->table), "cursor_change",
			    account_cursor_change, prefs);
	
	g_signal_connect((prefs->table), "double_click",
			    account_double_click, prefs);
	
	mail_accounts_load (prefs);
#else
	prefs->table = GTK_TREE_VIEW (g_object_get_data(G_OBJECT(widget), "table"));
	g_signal_connect(gtk_tree_view_get_selection(prefs->table),
			 "changed", G_CALLBACK(account_cursor_change), prefs);
	
	mail_accounts_load (prefs);
#endif
	
	prefs->mail_add = GTK_BUTTON (glade_xml_get_widget (gui, "cmdAccountAdd"));
	g_signal_connect(prefs->mail_add, "clicked", G_CALLBACK(account_add_clicked), prefs);

	prefs->mail_edit = GTK_BUTTON (glade_xml_get_widget (gui, "cmdAccountEdit"));
	g_signal_connect(prefs->mail_edit, "clicked", G_CALLBACK(account_edit_clicked), prefs);
	
	prefs->mail_delete = GTK_BUTTON (glade_xml_get_widget (gui, "cmdAccountDelete"));
	g_signal_connect(prefs->mail_delete, "clicked", G_CALLBACK(account_delete_clicked), prefs);
	
	prefs->mail_default = GTK_BUTTON (glade_xml_get_widget (gui, "cmdAccountDefault"));
	g_signal_connect(prefs->mail_default, "clicked", G_CALLBACK(account_default_clicked), prefs);
	
	prefs->mail_able = GTK_BUTTON (glade_xml_get_widget (gui, "cmdAccountAble"));
	g_signal_connect(prefs->mail_able, "clicked", G_CALLBACK(account_able_clicked), prefs);
}


GtkWidget *
mail_accounts_tab_new (GNOME_Evolution_Shell shell)
{
	MailAccountsTab *new;
	
	new = (MailAccountsTab *) g_object_new (mail_accounts_tab_get_type (), NULL);
	mail_accounts_tab_construct (new);
	new->shell = shell;
	
	return (GtkWidget *) new;
}


void
mail_accounts_tab_apply (MailAccountsTab *prefs)
{
	/* nothing to do here... */
}

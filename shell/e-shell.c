/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-shell.c
 *
 * Copyright (C) 2000, 2001, 2002, 2003 Ximian, Inc.
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
 *
 * Author: Ettore Perazzoli
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-shell.h"

#include "e-util/e-dialog-utils.h"
#include "e-util/e-bconf-map.h"

#include "e-shell-constants.h"
#include "e-shell-offline-handler.h"
#include "e-shell-settings-dialog.h"
#include "e-shell-startup-wizard.h"

#include "e-shell-marshal.h"

#include "evolution-shell-component-utils.h"

#include "importer/intelligent.h"

#include <glib.h>

#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gdk/gdkx.h>

#include <X11/Xatom.h>

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>

/* (For the displayName stuff.)  */
#include <gdk/gdkprivate.h>
#include <X11/Xlib.h>

#include <bonobo-activation/bonobo-activation.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-moniker-util.h>

#include <gal/util/e-util.h>

#include <gconf/gconf-client.h>

#include <string.h>

#include "Evolution.h"


#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;

struct _EShellPrivate {
	/* IID for registering the object on OAF.  */
	char *iid;

	GList *windows;

	EUserCreatableItemsHandler *user_creatable_items_handler;
	/* EUriSchemaRegistry *uri_schema_registry; FIXME */
	EComponentRegistry *component_registry;

	/* Names for the types of the folders that have maybe crashed.  */
	/* FIXME TODO */
	GList *crash_type_names; /* char * */

	/* Line status.  */
	EShellLineStatus line_status;

	/* This object handles going off-line.  If the pointer is not NULL, it
	   means we have a going-off-line process in progress.  */
	EShellOfflineHandler *offline_handler;

	/* Settings Dialog */
	GtkWidget *settings_dialog;

	/* Whether the shell is succesfully initialized.  This is needed during
	   the start-up sequence, to avoid CORBA calls to do make wrong things
	   to happen while the shell is initializing.  */
	unsigned int is_initialized : 1;

	/* Wether the shell is working in "interactive" mode or not.
	   (Currently, it's interactive IIF there is at least one active
	   view.)  */
	unsigned int is_interactive : 1;

	/* Whether quit has been requested, and the shell is now waiting for
	   permissions from all the components to quit.  */
	unsigned int preparing_to_quit : 1;
};


/* Signals.  */

enum {
	NO_WINDOWS_LEFT,
	LINE_STATUS_CHANGED,
	NEW_WINDOW_CREATED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


/* Utility functions.  */

static gboolean
get_config_start_offline (void)
{
	GConfClient *client;
	gboolean value;

	client = gconf_client_get_default ();

	value = gconf_client_get_bool (client, "/apps/evolution/shell/start_offline", NULL);

	g_object_unref (client);

	return value;
}


/* Interactivity handling.  */

static void
set_interactive (EShell *shell,
		 gboolean interactive)
{
	/* FIXME TODO */
}


/* CORBA interface implementation.  */

static gboolean
raise_exception_if_not_ready (PortableServer_Servant servant,
			      CORBA_Environment *ev)
{
	EShell *shell;

	shell = E_SHELL (bonobo_object_from_servant (servant));

	if (! shell->priv->is_initialized) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Shell_NotReady, NULL);
		return TRUE;
	}

	return FALSE;
}

static void
impl_Shell_createNewWindow (PortableServer_Servant servant,
			    const CORBA_char *component_id,
			    CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;
	EShell *shell;
	EShellWindow *shell_window;

	if (raise_exception_if_not_ready (servant, ev))
		return;

	bonobo_object = bonobo_object_from_servant (servant);
	shell = E_SHELL (bonobo_object);

	if (component_id[0] == '\0')
		component_id = NULL;

	shell_window = e_shell_create_window (shell, component_id, NULL);
	if (shell_window == NULL) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Shell_ComponentNotFound, NULL);
		return;
	}
}

static void
impl_Shell_handleURI (PortableServer_Servant servant,
		      const CORBA_char *uri,
		      CORBA_Environment *ev)
{
	EShell *shell = E_SHELL (bonobo_object_from_servant (servant));
	EComponentInfo *component_info;
	const char *colon_p;
	char *schema;

	colon_p = strchr (uri, ':');
	if (colon_p == NULL)
		schema = g_strdup (uri);
	else
		schema = g_strndup (uri, colon_p - uri);

	component_info = e_component_registry_peek_info_for_uri_schema (shell->priv->component_registry, schema);
	g_free (schema);

	if (component_info == NULL) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION, ex_GNOME_Evolution_Shell_UnsupportedSchema, NULL);
		return;
	}

	GNOME_Evolution_Component_handleURI (component_info->iface, uri, ev);
}

static void
impl_Shell_setLineStatus (PortableServer_Servant servant,
			  CORBA_boolean online,
			  CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;
	EShell *shell;

	if (raise_exception_if_not_ready (servant, ev))
		return;

	bonobo_object = bonobo_object_from_servant (servant);
	shell = E_SHELL (bonobo_object);

	if (online)
		e_shell_go_online (shell, NULL);
	else
		e_shell_go_offline (shell, NULL);
}


/* EShellWindow handling and bookkeeping.  */

static int
window_delete_event_cb (GtkWidget *widget,
		      GdkEventAny *ev,
		      void *data)
{
	EShell *shell;

	g_assert (E_IS_SHELL_WINDOW (widget));
	shell = E_SHELL (data);

	return ! e_shell_request_close_window (shell, E_SHELL_WINDOW (widget));
}

static gboolean
notify_no_windows_left_idle_cb (void *data)
{
	EShell *shell;

	shell = E_SHELL (data);

	set_interactive (shell, FALSE);

	g_signal_emit (shell, signals [NO_WINDOWS_LEFT], 0);

	bonobo_object_unref (BONOBO_OBJECT (shell));

	return FALSE;
}

static void
window_weak_notify (void *data,
		    GObject *where_the_object_was)
{
	EShell *shell;
	int num_windows;

	shell = E_SHELL (data);

	num_windows = g_list_length (shell->priv->windows);

	/* If this is our last window, save settings now because in the callback
	   for no_windows_left shell->priv->windows will be NULL and settings won't
	   be saved because of that.  */
	if (num_windows == 1)
		e_shell_save_settings (shell);

	shell->priv->windows = g_list_remove (shell->priv->windows, where_the_object_was);

	if (shell->priv->windows == NULL) {
		bonobo_object_ref (BONOBO_OBJECT (shell));
		g_idle_add (notify_no_windows_left_idle_cb, shell);
	}
}

static EShellWindow *
create_window (EShell *shell,
	       const char *component_id,
	       EShellWindow *template_window)
{
	EShellPrivate *priv;
	EShellWindow *window;

	priv = shell->priv;

	window = E_SHELL_WINDOW (e_shell_window_new (shell, component_id));

	g_signal_connect (window, "delete_event", G_CALLBACK (window_delete_event_cb), shell);
	g_object_weak_ref (G_OBJECT (window), window_weak_notify, shell);

	shell->priv->windows = g_list_prepend (shell->priv->windows, window);

	g_signal_emit (shell, signals[NEW_WINDOW_CREATED], 0, window);

	return window;
}


/* GObject methods.  */

static void
impl_dispose (GObject *object)
{
	EShell *shell;
	EShellPrivate *priv;
	GList *p;

	shell = E_SHELL (object);
	priv = shell->priv;

	priv->is_initialized = FALSE;

	if (priv->user_creatable_items_handler != NULL) {
		g_object_unref (priv->user_creatable_items_handler);
		priv->user_creatable_items_handler = NULL;
	}

#if 0				/* FIXME */
	if (priv->uri_schema_registry != NULL) {
		g_object_unref (priv->uri_schema_registry);
		priv->uri_schema_registry = NULL;
	}
#endif

	if (priv->offline_handler != NULL) {
		g_object_unref (priv->offline_handler);
		priv->offline_handler = NULL;
	}

	for (p = priv->windows; p != NULL; p = p->next) {
		EShellWindow *window;

		window = E_SHELL_WINDOW (p->data);

		g_signal_handlers_disconnect_by_func (window, G_CALLBACK (window_delete_event_cb), shell);
		g_object_weak_unref (G_OBJECT (window), window_weak_notify, shell);

		gtk_object_destroy (GTK_OBJECT (window));
	}

	g_list_free (priv->windows);
	priv->windows = NULL;

	/* No unreffing for these as they are aggregate.  */
	/* bonobo_object_unref (BONOBO_OBJECT (priv->corba_storage_registry)); */

	if (priv->settings_dialog != NULL) {
		gtk_widget_destroy (priv->settings_dialog);
		priv->settings_dialog = NULL;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	EShell *shell;
	EShellPrivate *priv;

	shell = E_SHELL (object);
	priv = shell->priv;

	if (priv->iid != NULL)
		bonobo_activation_active_server_unregister (priv->iid,
							    bonobo_object_corba_objref (BONOBO_OBJECT (shell)));

	e_free_string_list (priv->crash_type_names);

	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}


/* Initialization.  */

static void
e_shell_class_init (EShellClass *klass)
{
	GObjectClass *object_class;
	POA_GNOME_Evolution_Shell__epv *epv;

	parent_class = g_type_class_ref(PARENT_TYPE);

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	signals[NO_WINDOWS_LEFT] =
		g_signal_new ("no_windows_left",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EShellClass, no_windows_left),
			      NULL, NULL,
			      e_shell_marshal_NONE__NONE,
			      G_TYPE_NONE, 0);

	signals[LINE_STATUS_CHANGED] =
		g_signal_new ("line_status_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EShellClass, line_status_changed),
			      NULL, NULL,
			      e_shell_marshal_NONE__INT,
			      G_TYPE_NONE, 1,
			      G_TYPE_INT);

	signals[NEW_WINDOW_CREATED] =
		g_signal_new ("new_window_created",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EShellClass, new_window_created),
			      NULL, NULL,
			      e_shell_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);

	epv = & klass->epv;
	epv->createNewWindow = impl_Shell_createNewWindow;
	epv->handleURI       = impl_Shell_handleURI;
	epv->setLineStatus   = impl_Shell_setLineStatus;
}

static void
e_shell_init (EShell *shell)
{
	EShellPrivate *priv;

	priv = g_new0 (EShellPrivate, 1);
	priv->line_status                  = E_SHELL_LINE_STATUS_OFFLINE;
	priv->component_registry           = e_component_registry_new ();
	priv->user_creatable_items_handler = e_user_creatable_items_handler_new (priv->component_registry);

	shell->priv = priv;
}

static gboolean
detect_version (GConfClient *gconf, int *major, int *minor, int *revision)
{
	char *val, *evolution_dir, *filename;
	struct stat st;
	
	evolution_dir = g_build_filename (g_get_home_dir (), "evolution", NULL);
	filename = g_build_filename (evolution_dir, "config.xmldb", NULL);
	
	val = gconf_client_get_string(gconf, "/apps/evolution/version", NULL);	
	if (val) {
		/* Since 1.4.0 We've been keeping the version key in gconf */
		sscanf(val, "%u.%u.%u", major, minor, revision);
		g_free(val);
	} else if (lstat (filename, &st) != 0 || !S_ISDIR (st.st_mode)) {
		/* If ~/evolution does not exit or is not a directory it must be a new installation */
		*major = 0;
		*minor = 0;
		*revision = 0;
	} else {
		xmlDocPtr config_doc = NULL;
		xmlNodePtr source;
		char *tmp;
		
		if (lstat(filename, &st) == 0
		    && S_ISREG(st.st_mode))
			config_doc = xmlParseFile (filename);

		tmp = NULL;
		if (config_doc
		    && (source = e_bconf_get_path (config_doc, "/Shell"))
		    && (tmp = e_bconf_get_value (source, "upgrade_from_1_0_to_1_2_performed"))
		    && tmp[0] == '1' ) {
			*major = 1;
			*minor = 2;
			*revision = 0;
		} else {
			*major = 1;
			*minor = 0;
			*revision = 0;
		}
		if (tmp)
			xmlFree(tmp);
		if (config_doc)
			xmlFreeDoc (config_doc);		
	}

	g_free (evolution_dir);
	g_free (filename);

	return TRUE;
}

static void
attempt_upgrade (EShell *shell)
{
	GConfClient *gconf_client;
	int major = 0, minor = 0, revision = 0;

	gconf_client = gconf_client_get_default ();

	if (!detect_version (gconf_client, &major, &minor, &revision)
	    || !e_shell_attempt_upgrade (shell, major, minor, revision)) 
		e_notice (NULL, GTK_MESSAGE_ERROR,
			  _("Warning: Evolution could not upgrade all your data from version %d.%d.%d.\n"
			    "The data hasn't been deleted, but it will not be seen by this version of Evolution.\n"),
			  major, minor, revision);

	gconf_client_set_string (gconf_client, "/apps/evolution/version", VERSION, NULL);
	g_object_unref (gconf_client);
}

/**
 * e_shell_construct:
 * @shell: An EShell object to construct
 * @iid: OAFIID for registering the shell into the name server
 * @startup_line_mode: How to set up the line mode (online or offline) initally.
 * 
 * Construct @shell so that it uses the specified @corba_object.
 *
 * Return value: The result of the operation.
 **/
EShellConstructResult
e_shell_construct (EShell *shell,
		   const char *iid,
		   EShellStartupLineMode startup_line_mode)
{
	EShellPrivate *priv;
	CORBA_Object corba_object;
	gboolean start_online;

	g_return_val_if_fail (shell != NULL, E_SHELL_CONSTRUCT_RESULT_INVALIDARG);
	g_return_val_if_fail (E_IS_SHELL (shell), E_SHELL_CONSTRUCT_RESULT_INVALIDARG);
	g_return_val_if_fail (startup_line_mode == E_SHELL_STARTUP_LINE_MODE_CONFIG
			      || startup_line_mode == E_SHELL_STARTUP_LINE_MODE_ONLINE
			      || startup_line_mode == E_SHELL_STARTUP_LINE_MODE_OFFLINE,
			      E_SHELL_CONSTRUCT_RESULT_INVALIDARG);
	
	priv = shell->priv;
	priv->iid = g_strdup (iid);

	/* Now we can register into OAF.  Notice that we shouldn't be
	   registering into OAF until we are sure we can complete.  */
	
	/* FIXME: Multi-display stuff.  */
	corba_object = bonobo_object_corba_objref (BONOBO_OBJECT (shell));
	if (bonobo_activation_active_server_register (iid, corba_object) != Bonobo_ACTIVATION_REG_SUCCESS)
		return E_SHELL_CONSTRUCT_RESULT_CANNOTREGISTER;

	while (gtk_events_pending ())
		gtk_main_iteration ();	

	attempt_upgrade(shell);

	if (e_shell_startup_wizard_create () == FALSE) {
		bonobo_object_unref (BONOBO_OBJECT (shell));
		exit (0);
	}

	priv->is_initialized = TRUE;

	switch (startup_line_mode) {
	case E_SHELL_STARTUP_LINE_MODE_CONFIG:
		start_online = ! get_config_start_offline ();
		break;
	case E_SHELL_STARTUP_LINE_MODE_ONLINE:
		start_online = TRUE;
		break;
	case E_SHELL_STARTUP_LINE_MODE_OFFLINE:
		start_online = FALSE;
		break;
	default:
		start_online = FALSE; /* Make compiler happy.  */
		g_assert_not_reached ();
	}

	if (start_online)
		e_shell_go_online (shell, NULL);

	return E_SHELL_CONSTRUCT_RESULT_OK;
}

/**
 * e_shell_new:
 * @start_online: Whether to start in on-line mode or not.
 * @construct_result_return: A pointer to an EShellConstructResult variable into
 * which the result of the operation will be stored.
 * 
 * Create a new EShell.
 * 
 * Return value: 
 **/
EShell *
e_shell_new (EShellStartupLineMode startup_line_mode,
	     EShellConstructResult *construct_result_return)
{
	EShell *new;
	EShellPrivate *priv;
	EShellConstructResult construct_result;

	new = g_object_new (e_shell_get_type (), NULL);

	construct_result = e_shell_construct (new, E_SHELL_OAFIID, startup_line_mode);

	if (construct_result != E_SHELL_CONSTRUCT_RESULT_OK) {
		*construct_result_return = construct_result;
		bonobo_object_unref (BONOBO_OBJECT (new));
		return NULL;
	}

	priv = new->priv;

	*construct_result_return = E_SHELL_CONSTRUCT_RESULT_OK;
	return new;
}


/**
 * e_shell_attempt_upgrade:
 * @shell: 
 * @from_version: 
 * 
 * Upgrade config and components from the specified version name.
 * 
 * Return value: %TRUE If it works, %FALSE if it fails (it should only fail if
 * upgrade from @from_version is unsupported).
 **/
gboolean
e_shell_attempt_upgrade (EShell *shell, int major, int minor, int revision)
{
	GSList *component_infos, *p;
	int current_major, current_minor, current_revision;
	gboolean success;

	g_return_val_if_fail (E_IS_SHELL (shell), FALSE);

	sscanf (VERSION, "%u.%u.%u", &current_major, &current_minor, &current_revision);

	if (! (current_major > major
	       || (current_major == major && current_minor > minor)
	       || (current_minor == current_minor && current_revision > revision))) {
		return TRUE;
	}

	success = TRUE;

	component_infos = e_component_registry_peek_list (shell->priv->component_registry);
	for (p = component_infos; p != NULL; p = p->next) {
		const EComponentInfo *info = p->data;
		CORBA_Environment ev;
		gboolean component_upgraded;

		CORBA_exception_init (&ev);

		component_upgraded = GNOME_Evolution_Component_upgradeFromVersion (info->iface, major, minor, revision, &ev);
		
		if (BONOBO_EX (&ev)) {
			char *exception_text;

			// Ignore components that do not implement this version, it might just mean that they don't need an
			// upgrade path.
			if (strcmp (ev._id, ex_CORBA_NO_IMPLEMENT) == 0) {
				CORBA_exception_free (&ev);
				continue;
			}

			exception_text = bonobo_exception_get_text (&ev);
			g_warning ("Upgrade of component \"%s\" from version %d.%d.%d failed with exception %s",
				   info->alias, major, minor, revision, exception_text);
			g_free (exception_text);
			CORBA_exception_free (&ev);
			success = FALSE;
		} else {
			CORBA_exception_free (&ev);
			if (! component_upgraded) {
				g_warning ("Component \"%s\" could not upgrade configuration from version %d.%d.%d",
					   info->alias, major, minor, revision);
				success = FALSE;
			}
		}
	}

	return success;
}


/**
 * e_shell_create_window:
 * @shell: The shell for which to create a new window.
 * @component_id: Id or alias of the component to display in the new window.
 * @template_window: Window from which to copy the window settings (can be %NULL).
 * 
 * Create a new window for @uri.
 * 
 * Return value: The new window.
 **/
EShellWindow *
e_shell_create_window (EShell *shell,
		       const char *component_id,
		       EShellWindow *template_window)
{
	EShellWindow *window;
	EShellPrivate *priv;

	/* FIXME need to actually copy settings from template_window.  */

	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	priv = shell->priv;

	window = create_window (shell, component_id, template_window);

	gtk_widget_show (GTK_WIDGET (window));

	set_interactive (shell, TRUE);

	return window;
}

gboolean
e_shell_request_close_window (EShell *shell,
			      EShellWindow *shell_window)
{
	g_return_val_if_fail (E_IS_SHELL (shell), FALSE);
	g_return_val_if_fail (E_IS_SHELL_WINDOW (shell_window), FALSE);

	e_shell_save_settings (shell);

	if (g_list_length (shell->priv->windows) != 1) {
		/* Not the last window.  */
		return TRUE;
	}

	if (shell->priv->preparing_to_quit)
		return FALSE;

	/* If it's the last window, save settings and ask for confirmation before
	   quitting. */

	e_shell_window_save_defaults (shell_window);

	if (e_shell_prepare_for_quit (shell))
		return TRUE;
	else
		return FALSE;
}


#if 0				/* FIXME */
/**
 * e_shell_peek_uri_schema_registry:
 * @shell: An EShell object.
 * 
 * Get the schema registry associated to @shell.
 * 
 * Return value: A pointer to the EUriSchemaRegistry associated to @shell.
 **/
EUriSchemaRegistry  *
e_shell_peek_uri_schema_registry (EShell *shell)
{
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	return shell->priv->uri_schema_registry;
}
#endif


/**
 * e_shell_peek_component_registry:
 * @shell: 
 * 
 * Get the component registry associated to @shell.
 * 
 * Return value: 
 **/
EComponentRegistry *
e_shell_peek_component_registry (EShell *shell)
{
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	return shell->priv->component_registry;
}


/**
 * e_shell_save_settings:
 * @shell: 
 * 
 * Save the settings for this shell.
 * 
 * Return value: %TRUE if it worked, %FALSE otherwise.  Even if %FALSE is
 * returned, it is possible that at least part of the settings for the windows
 * have been saved.
 **/
gboolean
e_shell_save_settings (EShell *shell)
{
	GConfClient *client;
	EShellPrivate *priv;
	gboolean is_offline;

	priv = shell->priv;

	is_offline = ( e_shell_get_line_status (shell) == E_SHELL_LINE_STATUS_OFFLINE );

	client = gconf_client_get_default ();
	gconf_client_set_bool (client, "/apps/evolution/shell/start_offline", is_offline, NULL);
	g_object_unref (client);

	return TRUE;
}

/**
 * e_shell_close_all_windows:
 * @shell: 
 * 
 * Destroy all the windows in @shell.
 **/
void
e_shell_close_all_windows (EShell *shell)
{
	EShellPrivate *priv;
	GList *p, *pnext;

	g_return_if_fail (shell != NULL);
	g_return_if_fail (E_IS_SHELL (shell));

	if (shell->priv->windows)
		e_shell_save_settings (shell); 

	priv = shell->priv;
	for (p = priv->windows; p != NULL; p = pnext) {
		pnext = p->next;

		/* Note that this will also remove the window from the list... Hence the
		   need for the pnext variable.  */
		gtk_widget_destroy (GTK_WIDGET (p->data));
	}
}


/**
 * e_shell_get_line_status:
 * @shell: A pointer to an EShell object.
 * 
 * Get the line status for @shell.
 * 
 * Return value: The current line status for @shell.
 **/
EShellLineStatus
e_shell_get_line_status (EShell *shell)
{
	g_return_val_if_fail (shell != NULL, E_SHELL_LINE_STATUS_OFFLINE);
	g_return_val_if_fail (E_IS_SHELL (shell), E_SHELL_LINE_STATUS_OFFLINE);

	return shell->priv->line_status;
}


/* Offline/online handling.  */

static void
offline_procedure_started_cb (EShellOfflineHandler *offline_handler,
			      void *data)
{
	EShell *shell;
	EShellPrivate *priv;

	shell = E_SHELL (data);
	priv = shell->priv;

	priv->line_status = E_SHELL_LINE_STATUS_GOING_OFFLINE;
	g_signal_emit (shell, signals[LINE_STATUS_CHANGED], 0, priv->line_status);
}

static void
offline_procedure_finished_cb (EShellOfflineHandler *offline_handler,
			       gboolean now_offline,
			       void *data)
{
	EShell *shell;
	EShellPrivate *priv;

	shell = E_SHELL (data);
	priv = shell->priv;

	if (now_offline)
		priv->line_status = E_SHELL_LINE_STATUS_OFFLINE;
	else
		priv->line_status = E_SHELL_LINE_STATUS_ONLINE;

	g_object_unref (priv->offline_handler);
	priv->offline_handler = NULL;

	g_signal_emit (shell, signals[LINE_STATUS_CHANGED], 0, priv->line_status);
}

/**
 * e_shell_go_offline:
 * @shell: 
 * @action_window: 
 * 
 * Make the shell go into off-line mode.
 **/
void
e_shell_go_offline (EShell *shell,
		    EShellWindow *action_window)
{
	EShellPrivate *priv;

	g_return_if_fail (shell != NULL);
	g_return_if_fail (E_IS_SHELL (shell));
	g_return_if_fail (action_window != NULL);
	g_return_if_fail (action_window == NULL || E_IS_SHELL_WINDOW (action_window));

	priv = shell->priv;

	if (priv->line_status != E_SHELL_LINE_STATUS_ONLINE)
		return;

	priv->offline_handler = e_shell_offline_handler_new (shell);

	g_signal_connect (priv->offline_handler, "offline_procedure_started",
			  G_CALLBACK (offline_procedure_started_cb), shell);
	g_signal_connect (priv->offline_handler, "offline_procedure_finished",
			  G_CALLBACK (offline_procedure_finished_cb), shell);

	e_shell_offline_handler_put_components_offline (priv->offline_handler, GTK_WINDOW (action_window));
}

/**
 * e_shell_go_online:
 * @shell: 
 * @action_window: 
 * 
 * Make the shell go into on-line mode.
 **/
void
e_shell_go_online (EShell *shell,
		   EShellWindow *action_window)
{
	EShellPrivate *priv;
	GSList *component_infos;
	GSList *p;

	g_return_if_fail (shell != NULL);
	g_return_if_fail (E_IS_SHELL (shell));
	g_return_if_fail (action_window == NULL || E_IS_SHELL_WINDOW (action_window));

	priv = shell->priv;

	component_infos = e_component_registry_peek_list (priv->component_registry);
	for (p = component_infos; p != NULL; p = p->next) {
		EComponentInfo *info = p->data;
		CORBA_Environment ev;
		GNOME_Evolution_Offline offline_interface;

		CORBA_exception_init (&ev);

		offline_interface = Bonobo_Unknown_queryInterface (info->iface, "IDL:GNOME/Evolution/Offline:1.0", &ev);
		if (ev._major != CORBA_NO_EXCEPTION || offline_interface == CORBA_OBJECT_NIL) {
			CORBA_exception_free (&ev);
			continue;
		}

		CORBA_exception_free (&ev);

		GNOME_Evolution_Offline_goOnline (offline_interface, &ev);
		if (ev._major != CORBA_NO_EXCEPTION)
			g_warning ("Error putting component `%s' online.", info->id);

		CORBA_exception_free (&ev);
	}

	priv->line_status = E_SHELL_LINE_STATUS_ONLINE;
	g_signal_emit (shell, signals[LINE_STATUS_CHANGED], 0, priv->line_status);
}


void
e_shell_send_receive (EShell *shell)
{
	GSList *component_list;
	GSList *p;

	g_return_if_fail (E_IS_SHELL (shell));

	component_list = e_component_registry_peek_list (shell->priv->component_registry);

	for (p = component_list; p != NULL; p = p->next) {
		EComponentInfo *info = p->data;
		CORBA_Environment ev;

		CORBA_exception_init (&ev);

		GNOME_Evolution_Component_sendAndReceive (info->iface, &ev);

		// Ignore errors, the components can decide to not implement
		// this interface.

		CORBA_exception_free (&ev);
	}
}


void
e_shell_show_settings (EShell *shell,
		       const char *type,
		       EShellWindow *shell_window)
{
	EShellPrivate *priv;
	
	g_return_if_fail (shell != NULL);
	g_return_if_fail (E_IS_SHELL (shell));

	priv = shell->priv;
	
	if (priv->settings_dialog != NULL) {
		gdk_window_show (priv->settings_dialog->window);
		gtk_widget_grab_focus (priv->settings_dialog);
		return;
	}
	
	priv->settings_dialog = e_shell_settings_dialog_new ();

	if (type != NULL)
		e_shell_settings_dialog_show_type (E_SHELL_SETTINGS_DIALOG (priv->settings_dialog), type);

	g_object_add_weak_pointer (G_OBJECT (priv->settings_dialog), (void **) & priv->settings_dialog);

	gtk_window_set_transient_for (GTK_WINDOW (priv->settings_dialog), GTK_WINDOW (shell_window));
	gtk_widget_show (priv->settings_dialog);
}


const char *
e_shell_construct_result_to_string (EShellConstructResult result)
{
	switch (result) {
	case E_SHELL_CONSTRUCT_RESULT_OK:
		return _("OK");
	case E_SHELL_CONSTRUCT_RESULT_INVALIDARG:
		return _("Invalid arguments");
	case E_SHELL_CONSTRUCT_RESULT_CANNOTREGISTER:
		return _("Cannot register on OAF");
	case E_SHELL_CONSTRUCT_RESULT_NOCONFIGDB:
		return _("Configuration Database not found");
	case E_SHELL_CONSTRUCT_RESULT_GENERICERROR:
		return _("Generic error");
	default:
		return _("Unknown error");
	}
}


gboolean
e_shell_prepare_for_quit (EShell *shell)
{
	EShellPrivate *priv;
	GSList *component_infos;
	GList *p;
	GSList *sp;
	CORBA_boolean can_quit;

	g_return_val_if_fail (E_IS_SHELL (shell), FALSE);

	priv = shell->priv;
	priv->preparing_to_quit = TRUE;

	/* Make all the windows insensitive so we have some modal-like
	   behavior.  */
	for (p = priv->windows; p != NULL; p = p->next)
		gtk_widget_set_sensitive (GTK_WIDGET (p->data), FALSE);

	component_infos = e_component_registry_peek_list (priv->component_registry);
	can_quit = TRUE;
	for (sp = component_infos; sp != NULL; sp = sp->next) {
		EComponentInfo *info = sp->data;
		CORBA_Environment ev;

		CORBA_exception_init (&ev);

		can_quit = GNOME_Evolution_Component_requestQuit (info->iface, &ev);
		if (BONOBO_EX (&ev)) {
			/* The component might not implement the interface, in which case we assume we can quit. */
			can_quit = TRUE;
		}

		CORBA_exception_free (&ev);

		if (! can_quit)
			break;
	}

	/* Restore all the windows to be sensitive.  */
	for (p = priv->windows; p != NULL; p = p->next)
		gtk_widget_set_sensitive (GTK_WIDGET (p->data), TRUE);

	priv->preparing_to_quit = FALSE;
	return can_quit;
}


EUserCreatableItemsHandler *
e_shell_peek_user_creatable_items_handler (EShell *shell)
{
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	return shell->priv->user_creatable_items_handler;
}


BONOBO_TYPE_FUNC_FULL (EShell, GNOME_Evolution_Shell, PARENT_TYPE, e_shell)

/*
 * e-source-config.c
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

#include "e-source-config.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#include <libebackend/e-extensible.h>
#include <libedataserver/e-source-authentication.h>
#include <libedataserver/e-source-backend.h>
#include <libedataserver/e-source-refresh.h>
#include <libedataserver/e-source-security.h>
#include <libedataserver/e-source-webdav.h>

#include <e-util/e-marshal.h>
#include <misc/e-interval-chooser.h>

#include "e-source-config-backend.h"

#define E_SOURCE_CONFIG_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_CONFIG, ESourceConfigPrivate))

typedef struct _Candidate Candidate;

struct _ESourceConfigPrivate {
	ESource *original_source;
	ESourceRegistry *registry;

	GHashTable *backends;
	GPtrArray *candidates;

	GtkWidget *type_label;
	GtkWidget *type_combo;
	GtkWidget *name_entry;
	GtkWidget *backend_box;
	GtkSizeGroup *size_group;

	gboolean complete;
};

struct _Candidate {
	GtkWidget *page;
	ESource *scratch_source;
	ESourceConfigBackend *backend;
};

enum {
	PROP_0,
	PROP_COMPLETE,
	PROP_ORIGINAL_SOURCE,
	PROP_REGISTRY
};

enum {
	CHECK_COMPLETE,
	COMMIT_CHANGES,
	INIT_CANDIDATE,
	RESIZE_WINDOW,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE (
	ESourceConfig,
	e_source_config,
	GTK_TYPE_BOX,
	G_IMPLEMENT_INTERFACE (
		E_TYPE_EXTENSIBLE, NULL))

static void
source_config_init_backends (ESourceConfig *config)
{
	GList *list, *iter;

	config->priv->backends = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);

	e_extensible_load_extensions (E_EXTENSIBLE (config));

	list = e_extensible_list_extensions (
		E_EXTENSIBLE (config), E_TYPE_SOURCE_CONFIG_BACKEND);

	for (iter = list; iter != NULL; iter = g_list_next (iter)) {
		ESourceConfigBackend *backend;
		ESourceConfigBackendClass *class;

		backend = E_SOURCE_CONFIG_BACKEND (iter->data);
		class = E_SOURCE_CONFIG_BACKEND_GET_CLASS (backend);

		if (class->backend_name != NULL)
			g_hash_table_insert (
				config->priv->backends,
				g_strdup (class->backend_name),
				g_object_ref (backend));
	}

	g_list_free (list);
}

static gint
source_config_compare_backends (ESourceConfigBackend *backend_a,
                                ESourceConfigBackend *backend_b,
                                ESourceConfig *config)
{
	ESourceConfigBackendClass *class_a;
	ESourceConfigBackendClass *class_b;
	ESourceRegistry *registry;
	ESource *source_a;
	ESource *source_b;
	const gchar *parent_uid_a;
	const gchar *parent_uid_b;
	const gchar *backend_name_a;
	const gchar *backend_name_b;
	gint result;

	registry = e_source_config_get_registry (config);

	class_a = E_SOURCE_CONFIG_BACKEND_GET_CLASS (backend_a);
	class_b = E_SOURCE_CONFIG_BACKEND_GET_CLASS (backend_b);

	parent_uid_a = class_a->parent_uid;
	parent_uid_b = class_b->parent_uid;

	backend_name_a = class_a->backend_name;
	backend_name_b = class_b->backend_name;

	if (g_strcmp0 (backend_name_a, backend_name_b) == 0)
		return 0;

	/* "On This Computer" always comes first. */

	if (g_strcmp0 (backend_name_a, "local") == 0)
		return -1;

	if (g_strcmp0 (backend_name_b, "local") == 0)
		return 1;

	source_a = e_source_registry_ref_source (registry, parent_uid_a);
	source_b = e_source_registry_ref_source (registry, parent_uid_b);

	g_return_val_if_fail (source_a != NULL, 1);
	g_return_val_if_fail (source_b != NULL, -1);

	result = e_source_compare_by_display_name (source_a, source_b);

	g_object_unref (source_a);
	g_object_unref (source_b);

	return result;
}

static void
source_config_add_candidate (ESourceConfig *config,
                             ESourceConfigBackend *backend)
{
	Candidate *candidate;
	GtkBox *backend_box;
	GtkLabel *type_label;
	GtkComboBoxText *type_combo;
	ESourceConfigBackendClass *class;
	ESourceRegistry *registry;
	ESourceBackend *extension;
	ESource *original_source;
	ESource *parent_source;
	GDBusObject *dbus_object;
	const gchar *display_name;
	const gchar *extension_name;
	const gchar *parent_uid;

	backend_box = GTK_BOX (config->priv->backend_box);
	type_label = GTK_LABEL (config->priv->type_label);
	type_combo = GTK_COMBO_BOX_TEXT (config->priv->type_combo);

	registry = e_source_config_get_registry (config);
	class = E_SOURCE_CONFIG_BACKEND_GET_CLASS (backend);

	original_source = e_source_config_get_original_source (config);

	if (original_source != NULL)
		parent_uid = e_source_get_parent (original_source);
	else
		parent_uid = class->parent_uid;

	/* Make sure the parent source exists.  This will either
	 * be a collection source or a built-in "stub" source. */
	parent_source = e_source_registry_ref_source (registry, parent_uid);
	if (parent_source == NULL)
		return;

	/* Some backends don't allow new sources to be created.
	 * The "contacts" calendar backend is one such example. */
	if (original_source == NULL) {
		if (!e_source_config_backend_allow_creation (backend))
			return;
	}

	candidate = g_slice_new (Candidate);
	candidate->backend = g_object_ref (backend);

	/* Skip passing a GError here.  If dbus_object is NULL, this should
	 * never fail.  If dbus_object is non-NULL, then its data should have
	 * been produced by a GKeyFile on the server-side, so the chances of
	 * it failing to load this time are slim. */
	if (original_source != NULL)
		dbus_object = e_source_ref_dbus_object (original_source);
	else
		dbus_object = NULL;
	candidate->scratch_source = e_source_new (dbus_object, NULL, NULL);
	if (dbus_object != NULL)
		g_object_unref (dbus_object);

	/* Do not show the page here. */
	candidate->page = g_object_ref_sink (gtk_vbox_new (FALSE, 6));
	gtk_box_pack_start (backend_box, candidate->page, FALSE, FALSE, 0);

	e_source_set_parent (candidate->scratch_source, parent_uid);

	extension_name =
		e_source_config_get_backend_extension_name (config);
	extension = e_source_get_extension (
		candidate->scratch_source, extension_name);
	e_source_backend_set_backend_name (extension, class->backend_name);

	g_ptr_array_add (config->priv->candidates, candidate);

	display_name = e_source_get_display_name (parent_source);
	gtk_combo_box_text_append_text (type_combo, display_name);
	gtk_label_set_text (type_label, display_name);

	/* Make sure the combo box has a valid active item before
	 * adding widgets.  Otherwise we'll get run-time warnings
	 * as property bindings are set up. */
	if (gtk_combo_box_get_active (GTK_COMBO_BOX (type_combo)) == -1)
		gtk_combo_box_set_active (GTK_COMBO_BOX (type_combo), 0);

	/* Bind the standard widgets to the new scratch source. */
	g_signal_emit (
		config, signals[INIT_CANDIDATE], 0,
		candidate->scratch_source);

	/* Insert any backend-specific widgets. */
	e_source_config_backend_insert_widgets (
		candidate->backend, candidate->scratch_source);

	g_signal_connect_swapped (
		candidate->scratch_source, "changed",
		G_CALLBACK (e_source_config_check_complete), config);

	/* Trigger the "changed" handler we just connected to set the
	 * initial "complete" state based on the widgets we just added. */
	e_source_changed (candidate->scratch_source);

	g_object_unref (parent_source);
}

static void
source_config_free_candidate (Candidate *candidate)
{
	g_object_unref (candidate->page);
	g_object_unref (candidate->scratch_source);
	g_object_unref (candidate->backend);

	g_slice_free (Candidate, candidate);
}

static Candidate *
source_config_get_active_candidate (ESourceConfig *config)
{
	GtkComboBox *type_combo;
	gint index;

	type_combo = GTK_COMBO_BOX (config->priv->type_combo);
	index = gtk_combo_box_get_active (type_combo);
	g_return_val_if_fail (index >= 0, NULL);

	return g_ptr_array_index (config->priv->candidates, index);
}

static void
source_config_type_combo_changed_cb (GtkComboBox *type_combo,
                                     ESourceConfig *config)
{
	Candidate *candidate;
	GPtrArray *array;
	gint index;

	array = config->priv->candidates;

	for (index = 0; index < array->len; index++) {
		candidate = g_ptr_array_index (array, index);
		gtk_widget_hide (candidate->page);
	}

	index = gtk_combo_box_get_active (type_combo);
	if (index == CLAMP (index, 0, array->len)) {
		candidate = g_ptr_array_index (array, index);
		gtk_widget_show (candidate->page);
	}

	e_source_config_resize_window (config);
}

static void
source_config_set_original_source (ESourceConfig *config,
                                   ESource *original_source)
{
	g_return_if_fail (config->priv->original_source == NULL);

	if (original_source != NULL)
		g_object_ref (original_source);

	config->priv->original_source = original_source;
}

static void
source_config_set_registry (ESourceConfig *config,
                            ESourceRegistry *registry)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (config->priv->registry == NULL);

	config->priv->registry = g_object_ref (registry);
}

static void
source_config_set_property (GObject *object,
                            guint property_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ORIGINAL_SOURCE:
			source_config_set_original_source (
				E_SOURCE_CONFIG (object),
				g_value_get_object (value));
			return;

		case PROP_REGISTRY:
			source_config_set_registry (
				E_SOURCE_CONFIG (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_config_get_property (GObject *object,
                            guint property_id,
                            GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_COMPLETE:
			g_value_set_boolean (
				value,
				e_source_config_check_complete (
				E_SOURCE_CONFIG (object)));
			return;

		case PROP_ORIGINAL_SOURCE:
			g_value_set_object (
				value,
				e_source_config_get_original_source (
				E_SOURCE_CONFIG (object)));
			return;

		case PROP_REGISTRY:
			g_value_set_object (
				value,
				e_source_config_get_registry (
				E_SOURCE_CONFIG (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_config_dispose (GObject *object)
{
	ESourceConfigPrivate *priv;

	priv = E_SOURCE_CONFIG_GET_PRIVATE (object);

	if (priv->original_source != NULL) {
		g_object_unref (priv->original_source);
		priv->original_source = NULL;
	}

	if (priv->registry != NULL) {
		g_object_unref (priv->registry);
		priv->registry = NULL;
	}

	if (priv->type_label != NULL) {
		g_object_unref (priv->type_label);
		priv->type_label = NULL;
	}

	if (priv->type_combo != NULL) {
		g_object_unref (priv->type_combo);
		priv->type_combo = NULL;
	}

	if (priv->name_entry != NULL) {
		g_object_unref (priv->name_entry);
		priv->name_entry = NULL;
	}

	if (priv->backend_box != NULL) {
		g_object_unref (priv->backend_box);
		priv->backend_box = NULL;
	}

	if (priv->size_group != NULL) {
		g_object_unref (priv->size_group);
		priv->size_group = NULL;
	}

	g_hash_table_remove_all (priv->backends);
	g_ptr_array_set_size (priv->candidates, 0);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_source_config_parent_class)->dispose (object);
}

static void
source_config_finalize (GObject *object)
{
	ESourceConfigPrivate *priv;

	priv = E_SOURCE_CONFIG_GET_PRIVATE (object);

	g_hash_table_destroy (priv->backends);
	g_ptr_array_free (priv->candidates, TRUE);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_config_parent_class)->finalize (object);
}

static void
source_config_constructed (GObject *object)
{
	ESourceConfig *config;
	ESource *original_source;

	config = E_SOURCE_CONFIG (object);
	original_source = e_source_config_get_original_source (config);

	if (original_source != NULL)
		e_source_config_insert_widget (
			config, NULL, _("Type:"),
			config->priv->type_label);
	else
		e_source_config_insert_widget (
			config, NULL, _("Type:"),
			config->priv->type_combo);

	e_source_config_insert_widget (
		config, NULL, _("Name:"),
		config->priv->name_entry);

	source_config_init_backends (config);
}

static void
source_config_realize (GtkWidget *widget)
{
	ESourceConfig *config;
	ESource *original_source;

	/* Chain up to parent's realize() method. */
	GTK_WIDGET_CLASS (e_source_config_parent_class)->realize (widget);

	/* Do this after constructed() so subclasses can fully
	 * initialize themselves before we add candidates. */

	config = E_SOURCE_CONFIG (widget);
	original_source = e_source_config_get_original_source (config);

	if (original_source != NULL) {
		ESourceBackend *extension;
		ESourceConfigBackend *backend;
		const gchar *backend_name;
		const gchar *extension_name;

		extension_name =
			e_source_config_get_backend_extension_name (config);
		extension = e_source_get_extension (
			original_source, extension_name);
		backend_name = e_source_backend_get_backend_name (extension);
		g_return_if_fail (backend_name != NULL);

		backend = g_hash_table_lookup (
			config->priv->backends, backend_name);
		g_return_if_fail (E_IS_SOURCE_CONFIG_BACKEND (backend));

		source_config_add_candidate (config, backend);

	} else {
		GList *list, *link;

		list = g_list_sort_with_data (
			g_hash_table_get_values (config->priv->backends),
			(GCompareDataFunc) source_config_compare_backends,
			config);

		for (link = list; link != NULL; link = g_list_next (link)) {
			ESourceConfigBackend *backend;

			backend = E_SOURCE_CONFIG_BACKEND (link->data);
			source_config_add_candidate (config, backend);
		}

		g_list_free (list);
	}
}

static void
source_config_init_candidate (ESourceConfig *config,
                              ESource *scratch_source)
{
	g_object_bind_property (
		scratch_source, "display-name",
		config->priv->name_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);
}

static gboolean
source_config_check_complete (ESourceConfig *config,
                              ESource *scratch_source)
{
	GtkEntry *name_entry;
	GtkComboBox *type_combo;
	const gchar *text;

	/* Make sure the Type: combo box has a valid item. */
	type_combo = GTK_COMBO_BOX (config->priv->type_combo);
	if (gtk_combo_box_get_active (type_combo) < 0)
		return FALSE;

	/* Make sure the Name: entry field is not empty. */
	name_entry = GTK_ENTRY (config->priv->name_entry);
	text = gtk_entry_get_text (name_entry);
	if (text == NULL || *text == '\0')
		return FALSE;

	return TRUE;
}

static void
source_config_commit_changes (ESourceConfig *config,
                              ESource *scratch_source)
{
	/* Placeholder so subclasses can safely chain up. */
}

static void
source_config_resize_window (ESourceConfig *config)
{
	GtkWidget *toplevel;

	/* Expand or shrink our parent window vertically to accommodate
	 * the newly selected backend's options.  Some backends have tons
	 * of options, some have few.  This avoids the case where you
	 * select a backend with tons of options and then a backend with
	 * few options and wind up with lots of unused vertical space. */

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (config));

	if (GTK_IS_WINDOW (toplevel)) {
		GtkWindow *window = GTK_WINDOW (toplevel);
		GtkAllocation allocation;

		gtk_widget_get_allocation (toplevel, &allocation);
		gtk_window_resize (window, allocation.width, 1);
	}
}

static gboolean
source_config_check_complete_accumulator (GSignalInvocationHint *ihint,
                                          GValue *return_accu,
                                          const GValue *handler_return,
                                          gpointer unused)
{
	gboolean v_boolean;

	/* Abort emission if a handler returns FALSE. */
	v_boolean = g_value_get_boolean (handler_return);
	g_value_set_boolean (return_accu, v_boolean);

	return v_boolean;
}

static void
e_source_config_class_init (ESourceConfigClass *class)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	g_type_class_add_private (class, sizeof (ESourceConfigPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_config_set_property;
	object_class->get_property = source_config_get_property;
	object_class->dispose = source_config_dispose;
	object_class->finalize = source_config_finalize;
	object_class->constructed = source_config_constructed;

	widget_class = GTK_WIDGET_CLASS (class);
	widget_class->realize = source_config_realize;

	class->init_candidate = source_config_init_candidate;
	class->check_complete = source_config_check_complete;
	class->commit_changes = source_config_commit_changes;
	class->resize_window = source_config_resize_window;

	g_object_class_install_property (
		object_class,
		PROP_COMPLETE,
		g_param_spec_boolean (
			"complete",
			"Complete",
			"Are the required fields complete?",
			FALSE,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_ORIGINAL_SOURCE,
		g_param_spec_object (
			"original-source",
			"Original Source",
			"The original ESource",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_REGISTRY,
		g_param_spec_object (
			"registry",
			"Registry",
			"Registry of ESources",
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	signals[CHECK_COMPLETE] = g_signal_new (
		"check-complete",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceConfigClass, check_complete),
		source_config_check_complete_accumulator, NULL,
		e_marshal_BOOLEAN__OBJECT,
		G_TYPE_BOOLEAN, 1,
		E_TYPE_SOURCE);

	signals[COMMIT_CHANGES] = g_signal_new (
		"commit-changes",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceConfigClass, commit_changes),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		E_TYPE_SOURCE);

	signals[INIT_CANDIDATE] = g_signal_new (
		"init-candidate",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceConfigClass, init_candidate),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		E_TYPE_SOURCE);

	signals[RESIZE_WINDOW] = g_signal_new (
		"resize-window",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceConfigClass, resize_window),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

static void
e_source_config_init (ESourceConfig *config)
{
	GPtrArray *candidates;
	GtkSizeGroup *size_group;
	PangoAttribute *attr;
	PangoAttrList *attr_list;
	GtkWidget *widget;

	/* The candidates array holds scratch ESources, one for each
	 * item in the "type" combo box.  Scratch ESources are never
	 * added to the registry, so backend extensions can make any
	 * changes they want to them.  Whichever scratch ESource is
	 * "active" (selected in the "type" combo box) when the user
	 * clicks OK wins and is written to disk.  The others are
	 * discarded. */
	candidates = g_ptr_array_new_with_free_func (
		(GDestroyNotify) source_config_free_candidate);

	/* The size group is used for caption labels. */
	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	gtk_box_set_spacing (GTK_BOX (config), 6);

	gtk_orientable_set_orientation (
		GTK_ORIENTABLE (config), GTK_ORIENTATION_VERTICAL);

	config->priv = E_SOURCE_CONFIG_GET_PRIVATE (config);
	config->priv->candidates = candidates;
	config->priv->size_group = size_group;

	/* Either the combo box or the label is shown, never both.
	 * But we create both widgets and keep them both up-to-date
	 * regardless just because it makes the logic simpler. */

	attr_list = pango_attr_list_new ();

	attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
	pango_attr_list_insert (attr_list, attr);

	widget = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_label_set_attributes (GTK_LABEL (widget), attr_list);
	config->priv->type_label = g_object_ref_sink (widget);
	gtk_widget_show (widget);

	widget = gtk_combo_box_text_new ();
	config->priv->type_combo = g_object_ref_sink (widget);
	gtk_widget_show (widget);

	widget = gtk_entry_new ();
	gtk_entry_set_activates_default (GTK_ENTRY (widget), TRUE);
	config->priv->name_entry = g_object_ref_sink (widget);
	gtk_widget_show (widget);

	/* The backend box holds backend-specific options.  Each backend
	 * gets a child widget.  Only one child widget is visible at once. */
	widget = gtk_vbox_new (FALSE, 12);
	gtk_box_pack_end (GTK_BOX (config), widget, TRUE, TRUE, 0);
	config->priv->backend_box = g_object_ref (widget);
	gtk_widget_show (widget);

	pango_attr_list_unref (attr_list);

	g_signal_connect (
		config->priv->type_combo, "changed",
		G_CALLBACK (source_config_type_combo_changed_cb), config);
}

GtkWidget *
e_source_config_new (ESourceRegistry *registry,
                     ESource *original_source)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	if (original_source != NULL)
		g_return_val_if_fail (E_IS_SOURCE (original_source), NULL);

	return g_object_new (
		E_TYPE_SOURCE_CONFIG, "registry", registry,
		"original-source", original_source, NULL);
}

void
e_source_config_insert_widget (ESourceConfig *config,
                               ESource *scratch_source,
                               const gchar *caption,
                               GtkWidget *widget)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *label;

	g_return_if_fail (E_IS_SOURCE_CONFIG (config));
	g_return_if_fail (GTK_IS_WIDGET (widget));

	if (scratch_source == NULL)
		vbox = GTK_WIDGET (config);
	else
		vbox = e_source_config_get_page (config, scratch_source);

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);

	g_object_bind_property (
		widget, "visible",
		hbox, "visible",
		G_BINDING_SYNC_CREATE);

	label = gtk_label_new (caption);
	gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
	gtk_size_group_add_widget (config->priv->size_group, label);
	gtk_widget_show (label);

	gtk_box_pack_start (GTK_BOX (hbox), widget, TRUE, TRUE, 0);
}

GtkWidget *
e_source_config_get_page (ESourceConfig *config,
                          ESource *scratch_source)
{
	Candidate *candidate;
	GtkWidget *page = NULL;
	GPtrArray *array;
	gint index;

	g_return_val_if_fail (E_IS_SOURCE_CONFIG (config), NULL);
	g_return_val_if_fail (E_IS_SOURCE (scratch_source), NULL);

	array = config->priv->candidates;

	for (index = 0; page == NULL && index < array->len; index++) {
		candidate = g_ptr_array_index (array, index);
		if (e_source_equal (scratch_source, candidate->scratch_source))
			page = candidate->page;
	}

	g_return_val_if_fail (GTK_IS_BOX (page), NULL);

	return page;
}

const gchar *
e_source_config_get_backend_extension_name (ESourceConfig *config)
{
	ESourceConfigClass *class;

	g_return_val_if_fail (E_IS_SOURCE_CONFIG (config), NULL);

	class = E_SOURCE_CONFIG_GET_CLASS (config);
	g_return_val_if_fail (class->get_backend_extension_name != NULL, NULL);

	return class->get_backend_extension_name (config);
}

gboolean
e_source_config_check_complete (ESourceConfig *config)
{
	Candidate *candidate;
	gboolean complete;

	g_return_val_if_fail (E_IS_SOURCE_CONFIG (config), FALSE);

	candidate = source_config_get_active_candidate (config);
	g_return_val_if_fail (candidate != NULL, FALSE);

	g_signal_emit (
		config, signals[CHECK_COMPLETE], 0,
		candidate->scratch_source, &complete);

	complete &= e_source_config_backend_check_complete (
		candidate->backend, candidate->scratch_source);

	/* XXX Emitting "notify::complete" may cause this function
	 *     to be called repeatedly by signal handlers.  The IF
	 *     check below should break any recursive cycles.  Not
	 *     very efficient but I think we can live with it. */

	if (complete != config->priv->complete) {
		config->priv->complete = complete;
		g_object_notify (G_OBJECT (config), "complete");
	}

	return complete;
}

ESource *
e_source_config_get_original_source (ESourceConfig *config)
{
	g_return_val_if_fail (E_IS_SOURCE_CONFIG (config), NULL);

	return config->priv->original_source;
}

ESourceRegistry *
e_source_config_get_registry (ESourceConfig *config)
{
	g_return_val_if_fail (E_IS_SOURCE_CONFIG (config), NULL);

	return config->priv->registry;
}

void
e_source_config_resize_window (ESourceConfig *config)
{
	g_return_if_fail (E_IS_SOURCE_CONFIG (config));

	g_signal_emit (config, signals[RESIZE_WINDOW], 0);
}

/* Helper for e_source_config_commit() */
static void
source_config_commit_cb (GObject *object,
                         GAsyncResult *result,
                         gpointer user_data)
{
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (user_data);

	e_source_registry_commit_source_finish (
		E_SOURCE_REGISTRY (object), result, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);

	g_simple_async_result_complete (simple);
	g_object_unref (simple);
}

void
e_source_config_commit (ESourceConfig *config,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GSimpleAsyncResult *simple;
	ESourceRegistry *registry;
	Candidate *candidate;

	g_return_if_fail (E_IS_SOURCE_CONFIG (config));

	registry = e_source_config_get_registry (config);

	candidate = source_config_get_active_candidate (config);
	g_return_if_fail (candidate != NULL);

	e_source_config_backend_commit_changes (
		candidate->backend, candidate->scratch_source);

	g_signal_emit (
		config, signals[COMMIT_CHANGES], 0,
		candidate->scratch_source);

	simple = g_simple_async_result_new (
		G_OBJECT (config), callback,
		user_data, e_source_config_commit);

	e_source_registry_commit_source (
		registry, candidate->scratch_source,
		cancellable, source_config_commit_cb, simple);
}

gboolean
e_source_config_commit_finish (ESourceConfig *config,
                               GAsyncResult *result,
                               GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (config),
		e_source_config_commit), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

void
e_source_config_add_refresh_interval (ESourceConfig *config,
                                      ESource *scratch_source)
{
	GtkWidget *widget;
	GtkWidget *container;
	ESourceExtension *extension;
	const gchar *extension_name;

	g_return_if_fail (E_IS_SOURCE_CONFIG (config));
	g_return_if_fail (E_IS_SOURCE (scratch_source));

	extension_name = E_SOURCE_EXTENSION_REFRESH;
	extension = e_source_get_extension (scratch_source, extension_name);

	widget = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
	e_source_config_insert_widget (config, scratch_source, NULL, widget);
	gtk_widget_show (widget);

	container = widget;

	widget = gtk_hbox_new (FALSE, 6);
	gtk_container_add (GTK_CONTAINER (container), widget);
	gtk_widget_show (widget);

	container = widget;

	/* Translators: This is the first of a sequence of widgets:
	 * "Refresh every [NUMERIC_ENTRY] [TIME_UNITS_COMBO]" */
	widget = gtk_label_new (_("Refresh every"));
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	widget = e_interval_chooser_new ();
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	g_object_bind_property (
		extension, "interval-minutes",
		widget, "interval-minutes",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);
}

void
e_source_config_add_secure_connection (ESourceConfig *config,
                                       ESource *scratch_source)
{
	GtkWidget *widget;
	ESourceExtension *extension;
	const gchar *extension_name;
	const gchar *label;

	g_return_if_fail (E_IS_SOURCE_CONFIG (config));
	g_return_if_fail (E_IS_SOURCE (scratch_source));

	extension_name = E_SOURCE_EXTENSION_SECURITY;
	extension = e_source_get_extension (scratch_source, extension_name);

	label = _("Use a secure connection");
	widget = gtk_check_button_new_with_label (label);
	e_source_config_insert_widget (config, scratch_source, NULL, widget);
	gtk_widget_show (widget);

	g_object_bind_property (
		extension, "secure",
		widget, "active",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);
}

void
e_source_config_add_secure_connection_for_webdav (ESourceConfig *config,
                                                  ESource *scratch_source)
{
	GtkWidget *widget1;
	GtkWidget *widget2;
	ESourceExtension *extension;
	const gchar *extension_name;
	const gchar *label;

	g_return_if_fail (E_IS_SOURCE_CONFIG (config));
	g_return_if_fail (E_IS_SOURCE (scratch_source));

	extension_name = E_SOURCE_EXTENSION_SECURITY;
	extension = e_source_get_extension (scratch_source, extension_name);

	label = _("Use a secure connection");
	widget1 = gtk_check_button_new_with_label (label);
	e_source_config_insert_widget (config, scratch_source, NULL, widget1);
	gtk_widget_show (widget1);

	g_object_bind_property (
		extension, "secure",
		widget1, "active",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	extension = e_source_get_extension (scratch_source, extension_name);

	label = _("Ignore invalid SSL certificate");
	widget2 = gtk_check_button_new_with_label (label);
	gtk_widget_set_margin_left (widget2, 24);
	e_source_config_insert_widget (config, scratch_source, NULL, widget2);
	gtk_widget_show (widget2);

	g_object_bind_property (
		widget1, "active",
		widget2, "sensitive",
		G_BINDING_SYNC_CREATE);

	g_object_bind_property (
		extension, "ignore-invalid-cert",
		widget2, "active",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);
}

void
e_source_config_add_user_entry (ESourceConfig *config,
                                ESource *scratch_source)
{
	GtkWidget *widget;
	ESource *original_source;
	ESourceExtension *extension;
	const gchar *extension_name;

	g_return_if_fail (E_IS_SOURCE_CONFIG (config));
	g_return_if_fail (E_IS_SOURCE (scratch_source));

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	extension = e_source_get_extension (scratch_source, extension_name);

	original_source = e_source_config_get_original_source (config);

	widget = gtk_entry_new ();
	e_source_config_insert_widget (
		config, scratch_source, _("User"), widget);
	gtk_widget_show (widget);

	g_object_bind_property (
		extension, "user",
		widget, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	/* If this is a new data source, initialize the
	 * GtkEntry to the user name of the current user. */
	if (original_source == NULL)
		gtk_entry_set_text (GTK_ENTRY (widget), g_get_user_name ());
}

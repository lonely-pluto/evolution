/*
 * e-mail-autoconfig.c
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

#include "e-mail-autoconfig.h"

#include <config.h>
#include <string.h>
#include <glib/gi18n-lib.h>

/* Stuff for DNS querying and message parsing. */
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>
#include <arpa/nameser.h>
#if defined(HAVE_ARPA_NAMESER_COMPAT_H) && !defined(GETSHORT)
#include <arpa/nameser_compat.h>
#endif

/* For error codes. */
#include <libsoup/soup.h>

#include <libedataserver/e-source-camel.h>
#include <libedataserver/e-source-mail-account.h>
#include <libedataserver/e-source-mail-transport.h>

#define E_MAIL_AUTOCONFIG_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_AUTOCONFIG, EMailAutoconfigPrivate))

#define AUTOCONFIG_BASE_URI \
	"http://api.gnome.org/evolution/autoconfig/1.1/"

/* XXX g_file_load_contents() on an "http://" URI returns error codes
 *     in the SOUP_HTTP_ERROR domain instead of the G_IO_ERROR domain.
 *     That is both undocumented and unexpected. */
#define ERROR_IS_NOT_FOUND(error) \
	(g_error_matches ((error), SOUP_HTTP_ERROR, SOUP_STATUS_NOT_FOUND))

typedef struct _ParserClosure ParserClosure;
typedef struct _ResolverClosure ResolverClosure;

struct _EMailAutoconfigPrivate {
	gchar *email_address;
	gchar *email_local_part;
	gchar *email_domain_part;
	gchar *markup_content;
};

struct _ParserClosure {
	CamelNetworkSettings *network_settings;
	const gchar *expected_type;
	const gchar *email_address;
	const gchar *email_local_part;
	const gchar *email_domain_part;
	gboolean in_server_element;
	gboolean settings_modified;
};

struct _ResolverClosure {
	volatile gint ref_count;
	GMainContext *main_context;
	GMainLoop *main_loop;
	gchar *domain_name;
	gchar *name_server;
	GError *error;
};

enum {
	PROP_0,
	PROP_EMAIL_ADDRESS
};

/* Forward Declarations */
static void	e_mail_autoconfig_initable_init	(GInitableIface *interface);

/* By default, the GAsyncInitable interface calls GInitable.init()
 * from a separate thread, so we only have to override GInitable. */
G_DEFINE_TYPE_WITH_CODE (
	EMailAutoconfig,
	e_mail_autoconfig,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE, e_mail_autoconfig_initable_init)
	G_IMPLEMENT_INTERFACE (
		G_TYPE_ASYNC_INITABLE, NULL))

static ResolverClosure *
resolver_closure_new (const gchar *domain_name)
{
	ResolverClosure *closure;

	closure = g_slice_new0 (ResolverClosure);
	closure->domain_name = g_strdup (domain_name);
	closure->main_context = g_main_context_new ();
	closure->main_loop = g_main_loop_new (closure->main_context, FALSE);
	closure->ref_count = 1;

	return closure;
}

static ResolverClosure *
resolver_closure_ref (ResolverClosure *closure)
{
	g_return_val_if_fail (closure != NULL, NULL);
	g_return_val_if_fail (closure->ref_count > 0, NULL);

	g_atomic_int_inc (&closure->ref_count);

	return closure;
}

static void
resolver_closure_unref (ResolverClosure *closure)
{
	g_return_if_fail (closure != NULL);
	g_return_if_fail (closure->ref_count > 0);

	if (g_atomic_int_dec_and_test (&closure->ref_count)) {
		g_main_context_unref (closure->main_context);
		g_main_loop_unref (closure->main_loop);
		g_free (closure->domain_name);
		g_free (closure->name_server);
		g_clear_error (&closure->error);
		g_slice_free (ResolverClosure, closure);
	}
}

static gboolean
mail_autoconfig_resolver_idle_quit (gpointer user_data)
{
	GMainLoop *main_loop = user_data;

	g_main_loop_quit (main_loop);

	return FALSE;
}

static void
mail_autoconfig_resolver_cancelled (GCancellable *cancellable,
                                    ResolverClosure *closure)
{
	GSource *source;

	source = g_idle_source_new ();
	g_source_set_callback (
		source,
		mail_autoconfig_resolver_idle_quit,
		g_main_loop_ref (closure->main_loop),
		(GDestroyNotify) g_main_loop_unref);
	g_source_attach (source, closure->main_context);
	g_source_unref (source);
}

static gpointer
mail_autoconfig_resolver_thread (gpointer user_data)
{
	ResolverClosure *closure = user_data;
	HEADER *header;
	guchar answer[1024];
	gchar namebuf[1024];
	guchar *end, *cp;
	gint count;
	gint length;
	gint herr;

	/* Query DNS for the MX record for the domain name given in the
	 * email address.  We need an authoritative name server for it. */

	length = res_query (
		closure->domain_name, C_IN, T_MX,
		answer, sizeof (answer));
	herr = h_errno;  /* h_errno is defined in <netdb.h> */

	/* Based heavily on _g_resolver_targets_from_res_query().
	 * The binary DNS message format is described in RFC 1035. */

	if (length <= 0) {
		if (length == 0 || herr == HOST_NOT_FOUND || herr == NO_DATA)
			g_set_error (
				&closure->error,
				G_RESOLVER_ERROR,
				G_RESOLVER_ERROR_NOT_FOUND,
				_("No mail exchanger record for '%s'"),
				closure->domain_name);
		else if (herr == TRY_AGAIN)
			g_set_error (
				&closure->error,
				G_RESOLVER_ERROR,
				G_RESOLVER_ERROR_TEMPORARY_FAILURE,
				_("Temporarily unable to resolve '%s'"),
				closure->domain_name);
		else
			g_set_error (
				&closure->error,
				G_RESOLVER_ERROR,
				G_RESOLVER_ERROR_INTERNAL,
				_("Error resolving '%s'"),
				closure->domain_name);
		goto exit;
	}

	header = (HEADER *) answer;
	cp = answer + sizeof (HEADER);
	end = answer + length;

	/* Skip the 'question' section. */
	count = ntohs (header->qdcount);
	while (count-- && cp < end) {
		cp += dn_expand (answer, end, cp, namebuf, sizeof (namebuf));
		cp += 2;			/* skip QTYPE */
		cp += 2;			/* skip QCLASS */
	}

	/* Skip the 'answers' section. */
	count = ntohs (header->ancount);
	while (count-- && cp < end) {
		guint16 rdlength;
		cp += dn_expand (answer, end, cp, namebuf, sizeof (namebuf));
		cp += 2;			/* skip TYPE */
		cp += 2;			/* skip CLASS */
		cp += 4;			/* skip TTL */
		GETSHORT (rdlength, cp);	/* read RDLENGTH */
		cp += rdlength;			/* skip RDATA */
	}

	/* Read the 'authority' section. */
	count = ntohs (header->nscount);
	while (count-- && cp < end) {
		guint16 type, qclass, rdlength;
		cp += dn_expand (answer, end, cp, namebuf, sizeof (namebuf));
		GETSHORT (type, cp);
		GETSHORT (qclass, cp);
		cp += 4;			/* skip TTL */
		GETSHORT (rdlength, cp);

		if (type != T_NS || qclass != C_IN) {
			cp += rdlength;
			continue;
		}

		cp += dn_expand (answer, end, cp, namebuf, sizeof (namebuf));

		/* Pick the first T_NS record we find. */
		closure->name_server = g_strdup (namebuf);
		break;
	}

	if (closure->name_server == NULL)
		g_set_error (
			&closure->error,
			G_RESOLVER_ERROR,
			G_RESOLVER_ERROR_NOT_FOUND,
			_("No authoritative name server for '%s'"),
			closure->domain_name);

exit:
	g_main_loop_quit (closure->main_loop);
	resolver_closure_unref (closure);

	return NULL;  /* return value is not used */
}

static gchar *
mail_autoconfig_resolve_authority (const gchar *domain,
                                   GCancellable *cancellable,
                                   GError **error)
{
	ResolverClosure *closure;
	GThread *resolver_thread;
	gchar *name_server = NULL;
	gulong cancel_id = 0;

	closure = resolver_closure_new (domain);

	/* DNS record lookup is not cancellable, so we run it in a
	 * separate thread.  We don't join with the thread, however,
	 * because if we get cancelled we want to return immediately.
	 * So use a reference count on the thread closure and always
	 * let the thread run to completion even if we're not around
	 * any longer to pick up the result. */
	resolver_thread = g_thread_create (
		mail_autoconfig_resolver_thread,
		resolver_closure_ref (closure),
		FALSE /* not joinable */, error);

	if (resolver_thread == NULL)
		return FALSE;

	if (G_IS_CANCELLABLE (cancellable))
		cancel_id = g_cancellable_connect (
			cancellable,
			G_CALLBACK (mail_autoconfig_resolver_cancelled),
			resolver_closure_ref (closure),
			(GDestroyNotify) resolver_closure_unref);

	g_main_loop_run (closure->main_loop);

	if (cancel_id > 0)
		g_cancellable_disconnect (cancellable, cancel_id);

	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		/* do nothing */

	} else if (closure->error != NULL) {
		g_warn_if_fail (closure->name_server == NULL);
		g_propagate_error (error, closure->error);
		closure->error = NULL;

	} else {
		g_warn_if_fail (closure->name_server != NULL);
		name_server = closure->name_server;
		closure->name_server = NULL;
	}

	resolver_closure_unref (closure);

	return name_server;
}

static gboolean
mail_autoconfig_lookup (EMailAutoconfig *autoconfig,
                        const gchar *domain,
                        GCancellable *cancellable,
                        GError **error)
{
	GFile *file;
	gchar *uri;
	gboolean success;

	uri = g_strconcat (AUTOCONFIG_BASE_URI, domain, NULL);
	file = g_file_new_for_uri (uri);
	g_free (uri);

	/* Just to make sure we don't leak. */
	g_free (autoconfig->priv->markup_content);
	autoconfig->priv->markup_content = NULL;

	success = g_file_load_contents (
		file, cancellable,
		&autoconfig->priv->markup_content,
		NULL, NULL, error);

	g_object_unref (file);

	return success;
}

static void
mail_autoconfig_parse_start_element (GMarkupParseContext *context,
                                     const gchar *element_name,
                                     const gchar **attribute_names,
                                     const gchar **attribute_values,
                                     gpointer user_data,
                                     GError **error)
{
	ParserClosure *closure = user_data;
	gboolean is_incoming_server;
	gboolean is_outgoing_server;

	is_incoming_server = g_str_equal (element_name, "incomingServer");
	is_outgoing_server = g_str_equal (element_name, "outgoingServer");

	if (is_incoming_server || is_outgoing_server) {
		const gchar *type = NULL;

		g_markup_collect_attributes (
			element_name,
			attribute_names,
			attribute_values,
			error,
			G_MARKUP_COLLECT_STRING,
			"type", &type,
			G_MARKUP_COLLECT_INVALID);

		closure->in_server_element =
			(g_strcmp0 (type, closure->expected_type) == 0);
	}
}

static void
mail_autoconfig_parse_end_element (GMarkupParseContext *context,
                                   const gchar *element_name,
                                   gpointer user_data,
                                   GError **error)
{
	ParserClosure *closure = user_data;
	gboolean is_incoming_server;
	gboolean is_outgoing_server;

	is_incoming_server = g_str_equal (element_name, "incomingServer");
	is_outgoing_server = g_str_equal (element_name, "outgoingServer");

	if (is_incoming_server || is_outgoing_server)
		closure->in_server_element = FALSE;
}

static void
mail_autoconfig_parse_text (GMarkupParseContext *context,
                            const gchar *text,
                            gsize text_length,
                            gpointer user_data,
                            GError **error)
{
	ParserClosure *closure = user_data;
	const gchar *element_name;
	GString *string;

	if (!closure->in_server_element)
		return;

	/* Perform the following text substitutions:
	 *
	 * %EMAILADDRESS%    :  closure->email_address
	 * %EMAILLOCALPART%  :  closure->email_local_part
	 * %EMAILDOMAIN%     :  closure->email_domain_part
	 */
	if (strchr (text, '%') == NULL)
		string = g_string_new (text);
	else {
		const gchar *cp = text;

		string = g_string_sized_new (256);
		while (*cp != '\0') {
			const gchar *variable;
			const gchar *substitute;

			if (*cp != '%') {
				g_string_append_c (string, *cp++);
				continue;
			}

			variable = "%EMAILADDRESS%";
			substitute = closure->email_address;

			if (strncmp (cp, variable, strlen (variable)) == 0) {
				g_string_append (string, substitute);
				cp += strlen (variable);
				continue;
			}

			variable = "%EMAILLOCALPART%";
			substitute = closure->email_local_part;

			if (strncmp (cp, variable, strlen (variable)) == 0) {
				g_string_append (string, substitute);
				cp += strlen (variable);
				continue;
			}

			variable = "%EMAILDOMAIN%";
			substitute = closure->email_domain_part;

			if (strncmp (cp, variable, strlen (variable)) == 0) {
				g_string_append (string, substitute);
				cp += strlen (variable);
				continue;
			}

			g_string_append_c (string, *cp++);
		}
	}

	element_name = g_markup_parse_context_get_element (context);

	if (g_str_equal (element_name, "hostname")) {
		camel_network_settings_set_host (
			closure->network_settings, string->str);
		closure->settings_modified = TRUE;

	} else if (g_str_equal (element_name, "username")) {
		camel_network_settings_set_user (
			closure->network_settings, string->str);
		closure->settings_modified = TRUE;

	} else if (g_str_equal (element_name, "port")) {
		glong port = strtol (string->str, NULL, 10);
		if (port == CLAMP (port, 1, G_MAXUINT16)) {
			camel_network_settings_set_port (
				closure->network_settings, (guint16) port);
			closure->settings_modified = TRUE;
		}

	} else if (g_str_equal (element_name, "socketType")) {
		if (g_str_equal (string->str, "plain")) {
			camel_network_settings_set_security_method (
				closure->network_settings,
				CAMEL_NETWORK_SECURITY_METHOD_NONE);
			closure->settings_modified = TRUE;
		} else if (g_str_equal (string->str, "SSL")) {
			camel_network_settings_set_security_method (
				closure->network_settings,
				CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT);
			closure->settings_modified = TRUE;
		} else if (g_str_equal (string->str, "STARTTLS")) {
			camel_network_settings_set_security_method (
				closure->network_settings,
				CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT);
			closure->settings_modified = TRUE;
		}
	}

	/* FIXME Not handling <authentication> elements.
	 *       Unclear how some map to SASL mechanisms. */

	g_string_free (string, TRUE);
}

static GMarkupParser mail_autoconfig_parser = {
	mail_autoconfig_parse_start_element,
	mail_autoconfig_parse_end_element,
	mail_autoconfig_parse_text
};

static gboolean
mail_autoconfig_set_details (EMailAutoconfig *autoconfig,
                             const gchar *expected_type,
                             ESource *source,
                             const gchar *extension_name)
{
	GMarkupParseContext *context;
	ESourceCamel *camel_ext;
	ESourceBackend *backend_ext;
	CamelSettings *settings;
	ParserClosure closure;
	const gchar *backend_name;
	const gchar *markup_content;
	gboolean success;

	if (!e_source_has_extension (source, extension_name))
		return FALSE;

	backend_ext = e_source_get_extension (source, extension_name);
	backend_name = e_source_backend_get_backend_name (backend_ext);
	extension_name = e_source_camel_get_extension_name (backend_name);
	camel_ext = e_source_get_extension (source, extension_name);

	settings = e_source_camel_get_settings (camel_ext);
	g_return_val_if_fail (CAMEL_IS_NETWORK_SETTINGS (settings), FALSE);

	markup_content = e_mail_autoconfig_get_markup_content (autoconfig);
	g_return_val_if_fail (markup_content != NULL, FALSE);

	closure.network_settings = CAMEL_NETWORK_SETTINGS (settings);
	closure.expected_type = expected_type;
	closure.in_server_element = FALSE;
	closure.settings_modified = FALSE;

	/* These are used for text substitutions. */
	closure.email_address = autoconfig->priv->email_address;
	closure.email_local_part = autoconfig->priv->email_local_part;
	closure.email_domain_part = autoconfig->priv->email_domain_part;

	context = g_markup_parse_context_new (
		&mail_autoconfig_parser, 0, &closure, (GDestroyNotify) NULL);

	success = g_markup_parse_context_parse (
		context, markup_content, strlen (markup_content), NULL);

	success &= g_markup_parse_context_end_parse (context, NULL);

	/* Did we actually configure anything? */
	success &= closure.settings_modified;

	g_markup_parse_context_free (context);

	return success;
}

static void
mail_autoconfig_set_email_address (EMailAutoconfig *autoconfig,
                                   const gchar *email_address)
{
	g_return_if_fail (email_address != NULL);
	g_return_if_fail (autoconfig->priv->email_address == NULL);

	autoconfig->priv->email_address = g_strdup (email_address);
}

static void
mail_autoconfig_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_EMAIL_ADDRESS:
			mail_autoconfig_set_email_address (
				E_MAIL_AUTOCONFIG (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_autoconfig_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_EMAIL_ADDRESS:
			g_value_set_string (
				value,
				e_mail_autoconfig_get_email_address (
				E_MAIL_AUTOCONFIG (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_autoconfig_finalize (GObject *object)
{
	EMailAutoconfigPrivate *priv;

	priv = E_MAIL_AUTOCONFIG_GET_PRIVATE (object);

	g_free (priv->email_address);
	g_free (priv->email_local_part);
	g_free (priv->email_domain_part);
	g_free (priv->markup_content);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_mail_autoconfig_parent_class)->finalize (object);
}

static gboolean
mail_autoconfig_initable_init (GInitable *initable,
                               GCancellable *cancellable,
                               GError **error)
{
	EMailAutoconfig *autoconfig;
	const gchar *email_address;
	const gchar *domain;
	const gchar *cp;
	gchar *name_server;
	gboolean success = FALSE;
	GError *local_error = NULL;

	autoconfig = E_MAIL_AUTOCONFIG (initable);
	email_address = e_mail_autoconfig_get_email_address (autoconfig);

	if (email_address == NULL) {
		g_set_error_literal (
			error, G_IO_ERROR,
			G_IO_ERROR_INVALID_ARGUMENT,
			_("No email address provided"));
		return FALSE;
	}

	cp = strchr (email_address, '@');
	if (cp == NULL) {
		g_set_error_literal (
			error, G_IO_ERROR,
			G_IO_ERROR_INVALID_ARGUMENT,
			_("Missing domain in email address"));
		return FALSE;
	}

	domain = cp + 1;

	autoconfig->priv->email_local_part =
		g_strndup (email_address, cp - email_address);
	autoconfig->priv->email_domain_part = g_strdup (domain);

	/* First try the email address domain verbatim. */
	success = mail_autoconfig_lookup (
		autoconfig, domain, cancellable, &local_error);

	g_warn_if_fail (
		(success && local_error == NULL) ||
		(!success && local_error != NULL));

	if (success)
		return TRUE;

	/* "404 Not Found" errors are non-fatal this time around. */
	if (ERROR_IS_NOT_FOUND (local_error)) {
		g_clear_error (&local_error);
	} else {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	/* Look up an authoritative name server for the email address
	 * domain according to its "mail exchanger" (MX) DNS record. */
	name_server = mail_autoconfig_resolve_authority (
		domain, cancellable, error);

	if (name_server == NULL)
		return FALSE;

	/* Widdle away segments of the name server domain until
	 * we find a match, or until we widdle down to nothing. */

	cp = name_server;
	while (cp != NULL && strchr (cp, '.') != NULL) {
		g_clear_error (&local_error);

		success = mail_autoconfig_lookup (
			autoconfig, cp, cancellable, &local_error);

		g_warn_if_fail (
			(success && local_error == NULL) ||
			(!success && local_error != NULL));

		if (success || !ERROR_IS_NOT_FOUND (local_error))
			break;

		cp = strchr (cp, '.');
		if (cp != NULL)
			cp++;
	}

	if (local_error != NULL)
		g_propagate_error (error, local_error);

	g_free (name_server);

	return success;
}

static void
e_mail_autoconfig_class_init (EMailAutoconfigClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EMailAutoconfigPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = mail_autoconfig_set_property;
	object_class->get_property = mail_autoconfig_get_property;
	object_class->finalize = mail_autoconfig_finalize;

	g_object_class_install_property (
		object_class,
		PROP_EMAIL_ADDRESS,
		g_param_spec_string (
			"email-address",
			"Email Address",
			"The address from which to query config data",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_mail_autoconfig_initable_init (GInitableIface *interface)
{
	interface->init = mail_autoconfig_initable_init;
}

static void
e_mail_autoconfig_init (EMailAutoconfig *autoconfig)
{
	autoconfig->priv = E_MAIL_AUTOCONFIG_GET_PRIVATE (autoconfig);
}

EMailAutoconfig *
e_mail_autoconfig_new_sync (const gchar *email_address,
                            GCancellable *cancellable,
                            GError **error)
{
	g_return_val_if_fail (email_address != NULL, NULL);

	return g_initable_new (
		E_TYPE_MAIL_AUTOCONFIG,
		cancellable, error,
		"email-address", email_address,
		NULL);
}

void
e_mail_autoconfig_new (const gchar *email_address,
                       gint io_priority,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	g_return_if_fail (email_address != NULL);

	g_async_initable_new_async (
		E_TYPE_MAIL_AUTOCONFIG,
		io_priority, cancellable,
		callback, user_data,
		"email-address", email_address,
		NULL);
}

EMailAutoconfig *
e_mail_autoconfig_finish (GAsyncResult *result,
                          GError **error)
{
	GObject *source_object;
	GObject *autoconfig;

	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	source_object = g_async_result_get_source_object (result);
	g_return_val_if_fail (source_object != NULL, NULL);

	autoconfig = g_async_initable_new_finish (
		G_ASYNC_INITABLE (source_object), result, error);

	g_object_unref (source_object);

	if (autoconfig == NULL)
		return NULL;

	return E_MAIL_AUTOCONFIG (autoconfig);
}

const gchar *
e_mail_autoconfig_get_email_address (EMailAutoconfig *autoconfig)
{
	g_return_val_if_fail (E_IS_MAIL_AUTOCONFIG (autoconfig), NULL);

	return autoconfig->priv->email_address;
}

const gchar *
e_mail_autoconfig_get_markup_content (EMailAutoconfig *autoconfig)
{
	g_return_val_if_fail (E_IS_MAIL_AUTOCONFIG (autoconfig), NULL);

	return autoconfig->priv->markup_content;
}

gboolean
e_mail_autoconfig_set_imap_details (EMailAutoconfig *autoconfig,
                                    ESource *imap_source)
{
	g_return_val_if_fail (E_IS_MAIL_AUTOCONFIG (autoconfig), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (imap_source), FALSE);

	return mail_autoconfig_set_details (
		autoconfig, "imap", imap_source,
		E_SOURCE_EXTENSION_MAIL_ACCOUNT);
}

gboolean
e_mail_autoconfig_set_pop3_details (EMailAutoconfig *autoconfig,
                                    ESource *pop3_source)
{
	g_return_val_if_fail (E_IS_MAIL_AUTOCONFIG (autoconfig), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (pop3_source), FALSE);

	return mail_autoconfig_set_details (
		autoconfig, "pop3", pop3_source,
		E_SOURCE_EXTENSION_MAIL_ACCOUNT);
}

gboolean
e_mail_autoconfig_set_smtp_details (EMailAutoconfig *autoconfig,
                                    ESource *smtp_source)
{
	g_return_val_if_fail (E_IS_MAIL_AUTOCONFIG (autoconfig), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (smtp_source), FALSE);

	return mail_autoconfig_set_details (
		autoconfig, "smtp", smtp_source,
		E_SOURCE_EXTENSION_MAIL_TRANSPORT);
}

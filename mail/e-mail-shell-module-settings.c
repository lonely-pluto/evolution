/*
 * e-mail-shell-module-settings.c
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
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include "e-mail-shell-module-settings.h"

#include <gconf/gconf-client.h>
#include <libedataserver/e-account-list.h>

#include "e-util/e-signature-list.h"
#include "mail/e-mail-label-list-store.h"

void
e_mail_shell_module_init_settings (EShell *shell)
{
	EShellSettings *shell_settings;
	gpointer object;

	shell_settings = e_shell_get_shell_settings (shell);

	/* XXX Default values should match the GConf schema.
	 *     Yes it's redundant, but we're stuck with GConf. */

	/*** Global Objects ***/

	e_shell_settings_install_property (
		g_param_spec_object (
			"mail-label-list-store",
			NULL,
			NULL,
			E_TYPE_MAIL_LABEL_LIST_STORE,
			G_PARAM_READWRITE));

	object = e_mail_label_list_store_new ();
	e_shell_settings_set_object (
		shell_settings, "mail-label-list-store", object);
	g_object_unref (object);

	/*** Mail Preferences ***/

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-address-compress",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-address-compress",
		"/apps/evolution/mail/display/address_compress");

	e_shell_settings_install_property (
		g_param_spec_int (
			"mail-address-count",
			NULL,
			NULL,
			G_MININT,
			G_MAXINT,
			0,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-address-count",
		"/apps/evolution/mail/display/address_count");

	e_shell_settings_install_property (
		g_param_spec_string (
			"mail-citation-color",
			NULL,
			NULL,
			"#737373",
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-citation-color",
		"/apps/evolution/mail/display/citation_colour");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-check-for-junk",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-check-for-junk",
		"/apps/evolution/mail/junk/check_incoming");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-confirm-expunge",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-confirm-expunge",
		"/apps/evolution/mail/prompts/expunge");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-confirm-unwanted-html",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-confirm-unwanted-html",
		"/apps/evolution/mail/prompts/unwanted_html");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-empty-trash-on-exit",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-empty-trash-on-exit",
		"/apps/evolution/mail/trash/empty_on_exit");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-enable-search-folders",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-enable-search-folders",
		"/apps/evolution/mail/display/enable_vfolders");

	e_shell_settings_install_property (
		g_param_spec_string (
			"mail-font-monospace",
			NULL,
			NULL,
			"",
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-font-monospace",
		"/apps/evolution/mail/display/fonts/monospace");

	e_shell_settings_install_property (
		g_param_spec_string (
			"mail-font-variable",
			NULL,
			NULL,
			"",
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-font-variable",
		"/apps/evolution/mail/display/fonts/variable");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-force-message-limit",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-force-message-limit",
		"/apps/evolution/mail/display/force_message_limit");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-magic-spacebar",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-magic-spacebar",
		"/apps/evolution/mail/display/magic_spacebar");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-mark-citations",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-mark-citations",
		"/apps/evolution/mail/display/mark_citations");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-mark-seen",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-mark-seen",
		"/apps/evolution/mail/display/mark_seen");

	e_shell_settings_install_property (
		g_param_spec_int (
			"mail-mark-seen-timeout",
			NULL,
			NULL,
			G_MININT,
			G_MAXINT,
			0,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-mark-seen-timeout",
		"/apps/evolution/mail/display/mark_seen_timeout");

	e_shell_settings_install_property (
		g_param_spec_int (
			"mail-message-text-part-limit",
			NULL,
			NULL,
			G_MININT,
			G_MAXINT,
			0,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-message-text-part-limit",
		"/apps/evolution/mail/display/message_text_part_limit");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-only-local-photos",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-only-local-photos",
		"/apps/evolution/mail/display/photo_local");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-prompt-delete-in-vfolder",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-prompt-delete-in-vfolder",
		"/apps/evolution/mail/prompts/delete_in_vfolder");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-show-animated-images",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-show-animated-images",
		"/apps/evolution/mail/display/animated_images");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-show-sender-photo",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-show-sender-photo",
		"/apps/evolution/mail/display/sender_photo");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"mail-use-custom-fonts",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "mail-use-custom-fonts",
		"/apps/evolution/mail/display/fonts/use_custom");


	/*** Composer Preferences ***/

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"composer-format-html",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "composer-format-html",
		"/apps/evolution/mail/composer/send_html");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"composer-inline-spelling",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "composer-inline-spelling",
		"/apps/evolution/mail/composer/inline_spelling");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"composer-magic-links",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "composer-magic-links",
		"/apps/evolution/mail/composer/magic_links");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"composer-magic-smileys",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "composer-magic-smileys",
		"/apps/evolution/mail/composer/magic_smileys");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"composer-outlook-filenames",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "composer-outlook-filenames",
		"/apps/evolution/mail/composer/outlook_filenames");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"composer-prompt-only-bcc",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "composer-prompt-only-bcc",
		"/apps/evolution/mail/prompts/only_bcc");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"composer-prompt-empty-subject",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "composer-prompt-empty-subject",
		"/apps/evolution/mail/prompts/empty_subject");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"composer-reply-start-bottom",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "composer-reply-start-bottom",
		"/apps/evolution/mail/composer/reply_start_bottom");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"composer-request-receipt",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "composer-request-receipt",
		"/apps/evolution/mail/composer/request_receipt");

	e_shell_settings_install_property (
		g_param_spec_string (
			"composer-spell-color",
			NULL,
			NULL,
			"#ff0000",
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "composer-spell-color",
		"/apps/evolution/mail/composer/spell_color");

	e_shell_settings_install_property (
		g_param_spec_boolean (
			"composer-top-signature",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	e_shell_settings_bind_to_gconf (
		shell_settings, "composer-top-signature",
		"/apps/evolution/mail/composer/top_signature");
}

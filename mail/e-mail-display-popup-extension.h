/*
 * e-mail-dispaly-popup-extension.h
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

#ifndef E_MAIL_DISPLAY_POPUP_EXTENSION_H
#define E_MAIL_DISPLAY_POPUP_EXTENSION_H

#include <glib-object.h>
#include <webkit/webkit.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_DISPLAY_POPUP_EXTENSION \
	(e_mail_display_popup_extension_get_type ())
#define E_MAIL_DISPLAY_POPUP_EXTENSION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_DISPLAY_POPUP_EXTENSION, EMailDisplayPopupExtension))
#define E_MAIL_DISPLAY_POPUP_EXTENSION_INTERFACE(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_DISPLAY_POPUP_EXTENSION, EMailDisplayPopupExtensionInterface))
#define E_IS_MAIL_DISPLAY_POPUP_EXTENSION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_DISPLAY_POPUP_EXTENSION))
#define E_IS_MAIL_DISPLAY_POPUP_EXTENSION_INTERFACE(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_DISPLAY_POPUP_EXTENSION))
#define E_MAIL_DISPLAY_POPUP_EXTENSION_GET_INTERFACE(obj) \
	(G_TYPE_INSTANCE_GET_INTERFACE \
	((obj), E_TYPE_MAIL_DISPLAY_POPUP_EXTENSION, EMailDisplayPopupExtensionInterface))

G_BEGIN_DECLS

typedef struct _EMailDisplayPopupExtension EMailDisplayPopupExtension;
typedef struct _EMailDisplayPopupExtensionInterface EMailDisplayPopupExtensionInterface;

struct _EMailDisplayPopupExtensionInterface {
	GTypeInterface parent_interface;

	void	(*update_actions)		(EMailDisplayPopupExtension *extension,
						 WebKitHitTestResult *context);
};

GType		e_mail_display_popup_extension_get_type	(void);

void		e_mail_display_popup_extension_update_actions
							(EMailDisplayPopupExtension *extension,
							 WebKitHitTestResult *context);

G_END_DECLS

#endif /* E_MAIL_DISPLAY_POPUP_EXTENSION_H */
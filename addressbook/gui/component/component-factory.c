/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* component-factory.c - Factory for Evolution's Addressbook component.
 *
 * Copyright (C) 2002 Ximian, Inc.
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
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#include <config.h>

#include <string.h>
#include "addressbook.h"
#include "addressbook-component.h"
#include "addressbook-config.h"
#include "eab-popup-control.h"
#include "eab-vcard-control.h"
#include "select-names/e-select-names-bonobo.h"
#if HAVE_NSS
#include "smime/gui/certificate-manager.h"
#endif
#include <bonobo/bonobo-shlib-factory.h>


#define FACTORY_ID "OAFIID:GNOME_Evolution_Addressbook_Factory_2"

#define VCARD_CONTROL_ID               "OAFIID:GNOME_Evolution_Addressbook_VCard_Control"
#define ADDRESSBOOK_CONTROL_ID         "OAFIID:GNOME_Evolution_Addressbook_Control"
#define COMPONENT_ID                   "OAFIID:GNOME_Evolution_Addressbook_Component"
#define ADDRESS_POPUP_ID               "OAFIID:GNOME_Evolution_Addressbook_AddressPopup"
#define SELECT_NAMES_ID                "OAFIID:GNOME_Evolution_Addressbook_SelectNames"
#define LDAP_STORAGE_CONFIG_CONTROL_ID "OAFIID:GNOME_Evolution_LDAPStorage_ConfigControl"
#define CERTIFICATE_MANAGER_CONFIG_CONTROL_ID "OAFIID:GNOME_Evolution_SMime_CertificateManager_ConfigControl"


static BonoboObject *
factory (BonoboGenericFactory *factory,
	 const char *component_id,
	 void *closure)
{
	printf ("asked to activate component_id `%s'\n", component_id);

	if (strcmp (component_id, VCARD_CONTROL_ID) == 0)
		return BONOBO_OBJECT (eab_vcard_control_new ());
	if (strcmp (component_id, ADDRESSBOOK_CONTROL_ID) == 0)
		return BONOBO_OBJECT (addressbook_new_control ());
	if (strcmp (component_id, COMPONENT_ID) == 0) {
		BonoboObject *object = BONOBO_OBJECT (addressbook_component_peek ());
		bonobo_object_ref (object);
		return object;
	}
	if (strcmp (component_id, ADDRESS_POPUP_ID) == 0)
		return BONOBO_OBJECT (eab_popup_control_new ());
	if (strcmp (component_id, LDAP_STORAGE_CONFIG_CONTROL_ID) == 0)
		return BONOBO_OBJECT (addressbook_config_control_new ());
	if (strcmp (component_id, SELECT_NAMES_ID) == 0)
		return BONOBO_OBJECT (e_select_names_bonobo_new ());
#if HAVE_NSS
        if (strcmp (component_id, CERTIFICATE_MANAGER_CONFIG_CONTROL_ID) == 0)
                return BONOBO_OBJECT (certificate_manager_config_control_new ());
#endif

	g_warning (FACTORY_ID ": Don't know what to do with %s", component_id);
	return NULL;
}

BONOBO_ACTIVATION_SHLIB_FACTORY (FACTORY_ID, "Evolution Addressbook component factory", factory, NULL)

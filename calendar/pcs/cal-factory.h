/* Evolution calendar factory
 *
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef CAL_FACTORY_H
#define CAL_FACTORY_H

#include <libgnome/gnome-defs.h>
#include <bonobo/bonobo-xobject.h>

#include "evolution-calendar.h"

BEGIN_GNOME_DECLS



#define CAL_FACTORY_TYPE            (cal_factory_get_type ())
#define CAL_FACTORY(obj)            (GTK_CHECK_CAST ((obj), CAL_FACTORY_TYPE, CalFactory))
#define CAL_FACTORY_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), CAL_FACTORY_TYPE,		\
				     CalFactoryClass))
#define IS_CAL_FACTORY(obj)         (GTK_CHECK_TYPE ((obj), CAL_FACTORY_TYPE))
#define IS_CAL_FACTORY_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), CAL_FACTORY_TYPE))

typedef struct _CalFactory CalFactory;
typedef struct _CalFactoryClass CalFactoryClass;

typedef struct _CalFactoryPrivate CalFactoryPrivate;

struct _CalFactory {
	BonoboXObject object;

	/* Private data */
	CalFactoryPrivate *priv;
};

struct _CalFactoryClass {
	BonoboXObjectClass parent_class;
	
	POA_GNOME_Evolution_Calendar_CalFactory__epv epv;

	/* Notification signals */
	void (* last_calendar_gone) (CalFactory *factory);
};

GtkType     cal_factory_get_type        (void);
CalFactory *cal_factory_new             (void);

gboolean    cal_factory_oaf_register    (CalFactory *factory, const char *iid);
void        cal_factory_register_method (CalFactory *factory,
					 const char *method,
					 GtkType     backend_type);
int         cal_factory_get_n_backends  (CalFactory *factory);

END_GNOME_DECLS

#endif

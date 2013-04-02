/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * anjuta-profile.h
 * Copyright (C) Naba Kumar  <naba@gnome.org>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _ANJUTA_PROFILE_H_
#define _ANJUTA_PROFILE_H_

#include <glib-object.h>
#include <gio/gio.h>
#include <libanjuta/anjuta-plugin-handle.h>
#include <libanjuta/anjuta-plugin-manager.h>

G_BEGIN_DECLS

#define ANJUTA_TYPE_PROFILE             (anjuta_profile_get_type ())
#define ANJUTA_PROFILE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), ANJUTA_TYPE_PROFILE, AnjutaProfile))
#define ANJUTA_PROFILE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), ANJUTA_TYPE_PROFILE, AnjutaProfileClass))
#define ANJUTA_IS_PROFILE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), ANJUTA_TYPE_PROFILE))
#define ANJUTA_IS_PROFILE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), ANJUTA_TYPE_PROFILE))
#define ANJUTA_PROFILE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), ANJUTA_TYPE_PROFILE, AnjutaProfileClass))

/**
 * ANJUTA_PROFILE_ERROR:
 *
 * Error domain for Anjuta profile. Errors in this domain will be from the
 * #AnjutaProfileError enumeration. See #GError for more information
 * on error domains.
 */
#define ANJUTA_PROFILE_ERROR            (anjuta_profile_error_quark())

/**
 * AnjutaProfileError:
 * @ANJUTA_PROFILE_ERROR_URI_READ_FAILED: Fail to read xml plugins list file.
 * @ANJUTA_PROFILE_ERROR_URI_WRITE_FAILED: Fail to write xml plugins list file.
 *
 * Error codes returned by anjuta profile functions.
 *
 */
typedef enum
{
	ANJUTA_PROFILE_ERROR_URI_READ_FAILED,
	ANJUTA_PROFILE_ERROR_URI_WRITE_FAILED
} AnjutaProfileError;

typedef struct _AnjutaProfileClass AnjutaProfileClass;
typedef struct _AnjutaProfile AnjutaProfile;
typedef struct _AnjutaProfilePriv AnjutaProfilePriv;

struct _AnjutaProfileClass
{
	GObjectClass parent_class;

	/* Signals */
	void(* plugin_added) (AnjutaProfile *self,
						  AnjutaPluginHandle *plugin);
	void(* plugin_removed) (AnjutaProfile *self,
							AnjutaPluginHandle *plugin);
	void(* changed) (AnjutaProfile *self, GList *plugins);
	void(* descoped) (AnjutaProfile *self);
	void(* scoped) (AnjutaProfile *self);
};

/**
 * AnjutaProfile:
 *
 * Stores a plugin list.
 */
struct _AnjutaProfile
{
	GObject parent_instance;
	AnjutaProfilePriv *priv;
};

GQuark anjuta_profile_error_quark (void);
GType anjuta_profile_get_type (void) G_GNUC_CONST;

AnjutaProfile* anjuta_profile_new (const gchar *name,
								   AnjutaPluginManager *plugin_manager);
const gchar *anjuta_profile_get_name (AnjutaProfile *profile);
void anjuta_profile_add_plugin (AnjutaProfile *profile,
								AnjutaPluginHandle *plugin);
void anjuta_profile_remove_plugin (AnjutaProfile *profile,
								   AnjutaPluginHandle *plugin);
gboolean anjuta_profile_add_plugins_from_xml (AnjutaProfile *profile,
											  GFile* profile_xml_file,
											  gboolean exclude_from_sync,
											  GError **error);
gboolean anjuta_profile_has_plugin (AnjutaProfile *profile,
									AnjutaPluginHandle *plugin);
GList* anjuta_profile_get_plugins (AnjutaProfile *profile);

void anjuta_profile_set_sync_file (AnjutaProfile *profile,
								  GFile *sync_file);
gboolean anjuta_profile_sync (AnjutaProfile *profile, GError **error);

G_END_DECLS

#endif /* _ANJUTA_PROFILE_H_ */

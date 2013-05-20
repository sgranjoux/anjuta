/*
 * AnjutaPluginDescription - Plugin meta data
 * anjuta-plugin-description.h Copyright (C) 2002 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef ANJUTA_PLUGIN_PARSER_H
#define ANJUTA_PLUGIN_PARSER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define ANJUTA_TYPE_PLUGIN_DESCRIPTION		(anjuta_project_property_get_type ())
#define ANJUTA_IS_PLUGIN_DESCIRPTION(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), ANJUTA_TYPE_PLUGIN_DESCRIPTION))

typedef struct _AnjutaPluginDescription AnjutaPluginDescription;

typedef void (*AnjutaPluginDescriptionSectionFunc) (AnjutaPluginDescription *df,
													const gchar *name,
													gpointer user_data);

/* If @key is %NULL, @value is a comment line. */
/* @value is raw, unescaped data. */
typedef void (*AnjutaPluginDescriptionLineFunc) (AnjutaPluginDescription *df,
												 const gchar *key,
												 const gchar *locale,
												 const gchar *value,
												 gpointer   data);

/**
 * AnjutaPluginDescriptionParseError:
 * @ANJUTA_PLUGIN_DESCRIPTION_PARSE_ERROR_INVALID_SYNTAX: Syntax of plugin file is invalid
 * @ANJUTA_PLUGIN_DESCRIPTION_PARSE_ERROR_INVALID_ESCAPES: Invalid escape sequence
 * @ANJUTA_PLUGIN_DESCRIPTION_PARSE_ERROR_INVALID_CHARS: Invalid characters
 *
 * Possible errors when parsing a plugin file
 */
typedef enum
{
  ANJUTA_PLUGIN_DESCRIPTION_PARSE_ERROR_INVALID_SYNTAX,
  ANJUTA_PLUGIN_DESCRIPTION_PARSE_ERROR_INVALID_ESCAPES,
  ANJUTA_PLUGIN_DESCRIPTION_PARSE_ERROR_INVALID_CHARS
} AnjutaPluginDescriptionParseError;

#define ANJUTA_PLUGIN_DESCRIPTION_PARSE_ERROR \
			anjuta_plugin_description_parse_error_quark()

GQuark anjuta_plugin_description_parse_error_quark (void);

GType anjuta_plugin_description_get_type (void);

AnjutaPluginDescription* anjuta_plugin_description_new (const gchar *filename,
														GError **error);

AnjutaPluginDescription* anjuta_plugin_description_new_from_string (gchar *data,
																	GError **error);

gchar* anjuta_plugin_description_to_string (AnjutaPluginDescription *df);

void anjuta_plugin_description_free (AnjutaPluginDescription *df);
AnjutaPluginDescription *anjuta_plugin_description_copy (AnjutaPluginDescription *df);

void anjuta_plugin_description_foreach_section (AnjutaPluginDescription *df,
												AnjutaPluginDescriptionSectionFunc func,
												gpointer user_data);

void anjuta_plugin_description_foreach_key (AnjutaPluginDescription *df,
											const gchar *section_name,
											gboolean include_localized,
											AnjutaPluginDescriptionLineFunc func,
											gpointer user_data);

/* Gets the raw text of the key, unescaped */
gboolean anjuta_plugin_description_get_raw (AnjutaPluginDescription *df,
										    const gchar *section_name,
											const gchar *keyname,
											const gchar *locale,
											gchar **val);

gboolean anjuta_plugin_description_get_integer (AnjutaPluginDescription *df,
												const gchar *section,
												const gchar *keyname,
												gint *val);

gboolean anjuta_plugin_description_get_boolean (AnjutaPluginDescription   *df,
											   const gchar *section,
											   const gchar *keyname,
											   gboolean *val);

gboolean anjuta_plugin_description_get_string (AnjutaPluginDescription   *df,
											   const gchar *section,
											   const gchar *keyname,
											   gchar **val);

gboolean anjuta_plugin_description_get_locale_string (AnjutaPluginDescription *df,
													  const gchar *section,
													  const gchar *keyname,
													  gchar **val);

gboolean anjuta_plugin_description_override (AnjutaPluginDescription *df,
                                             const gchar *section_name,
                                             const gchar *keyname,
                                             const gchar*val);

gboolean anjuta_plugin_description_remove (AnjutaPluginDescription *df,
                                           const gchar *section_name,
                                           const gchar *keyname);
G_END_DECLS

#endif /* ANJUTA_PLUGIN_PARSER_H */

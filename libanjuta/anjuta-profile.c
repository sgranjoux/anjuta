/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * anjuta-profile.c
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

/**
 * SECTION:anjuta-profile
 * @short_description: Profile is a collection of plugins
 * @see_also: #AnjutaProfileManager, #AnjutaPlugin
 * @stability: Unstable
 * @include: libanjuta/anjuta-profile.h
 * 
 * A anjuta profile contains the list of all plugins used in one Anjuta session.
 * It is possible to add and remove plugins,
 * check if one is included or get the whole list. The plugins list can be saved
 * into a xml file and loaded from it.
 *
 * A profile in an Anjuta session includes plugins from up to 3 different xml
 * sources:
 *	<variablelist>
 *    <varlistentry>
 *    <term>$prefix/share/anjuta/profiles/default.profile</term>
 *    <listitem>
 *    <para>
 *        This contains the system plugins. It is loaded in every profile and
 *        contains mandatory plugins for Anjuta. These plugins cannot be
 *        unloaded.
 *    </para>
 *    </listitem>
 *    </varlistentry>
 *    <varlistentry>
 *    <term>$project_dir/$project_name.anjuta</term>
 *    <listitem>
 *    <para>
 *        This contains the project plugins. It lists mandatory plugins for the
 *        project. This file is version controlled and distributed with the source
 *        code. Every user working on the project uses the same one. If there
 *        is no project loaded, no project plugins are loaded.
 *    </para>
 *    </listitem>
 *    </varlistentry>
 *    <varlistentry>
 *    <term>$project_dir/.anjuta/default.profile</term>
 *    <listitem>
 *    <para>
 *        This contains the user plugins. This is the only list of plugins
 *        which is updated when the user add or remove one plugin.
 *        If there is no project loaded, the user home directory is used
 *        instead of the project directory but this list is used only in this case.
 *        There is no global user plugins list.
 *    </para>
 *    </listitem>
 *    </varlistentry>
 * </variablelist>
 */

#include <glib/gi18n.h>
#include <glib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "anjuta-profile.h"
#include "anjuta-marshal.h"
#include "anjuta-debug.h"

enum
{
	PROP_0,
	PROP_PLUGIN_MANAGER,
	PROP_PROFILE_NAME,
	PROP_SYNC_FILE,
};

enum
{
	PLUGIN_ADDED,
	PLUGIN_REMOVED,
	CHANGED,
	DESCOPED,
	SCOPED,
	LAST_SIGNAL
};

typedef struct _AnjutaProfileXml AnjutaProfileXml;

struct _AnjutaProfileXml
{
	GFile *file;
	xmlDocPtr doc;
	gboolean exclude_from_sync;
	gboolean core_plugin;
	AnjutaProfileXml *next;
};


struct _AnjutaProfilePriv
{
	gchar *name;
	AnjutaPluginManager *plugin_manager;
	GHashTable *plugins_to_load;
	GHashTable *plugins_to_exclude_from_sync;
	GList *plugins_to_disable;
	GList *configuration;
	GList *config_keys;
	GFile *sync_file;
	AnjutaProfileXml *xml;
};

static GObjectClass* parent_class = NULL;
static guint profile_signals[LAST_SIGNAL] = { 0 };

GQuark 
anjuta_profile_error_quark (void)
{
	static GQuark quark = 0;
	
	if (quark == 0) {
		quark = g_quark_from_static_string ("anjuta-profile-quark");
	}
	
	return quark;
}

static void
anjuta_profile_init (AnjutaProfile *object)
{
	object->priv = g_new0 (AnjutaProfilePriv, 1);
	object->priv->plugins_to_load = g_hash_table_new (g_direct_hash,
	                                                  g_direct_equal);
	object->priv->plugins_to_exclude_from_sync = g_hash_table_new (g_direct_hash,
	                                                               g_direct_equal);
}

static void
anjuta_profile_finalize (GObject *object)
{
	AnjutaProfilePriv *priv = ANJUTA_PROFILE (object)->priv;
	g_free (priv->name);
	g_hash_table_destroy (priv->plugins_to_load);
	g_hash_table_destroy (priv->plugins_to_exclude_from_sync);
	g_list_free (priv->plugins_to_disable);
	g_list_free_full (priv->config_keys, (GDestroyNotify)g_free);
	g_list_free (priv->configuration);

	while (priv->xml != NULL)
	{
		AnjutaProfileXml *next;

		next = priv->xml->next;
		g_object_unref (priv->xml->file);
		g_free (priv->xml);
		priv->xml = next;
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
anjuta_profile_set_property (GObject *object, guint prop_id,
							 const GValue *value, GParamSpec *pspec)
{
	AnjutaProfilePriv *priv = ANJUTA_PROFILE (object)->priv;
	
	g_return_if_fail (ANJUTA_IS_PROFILE (object));

	switch (prop_id)
	{
	case PROP_PLUGIN_MANAGER:
		priv->plugin_manager = g_value_get_object (value);
		break;
	case PROP_PROFILE_NAME:
		g_return_if_fail (g_value_get_string (value) != NULL);
		g_free (priv->name);
		priv->name = g_strdup (g_value_get_string (value));
		break;
	case PROP_SYNC_FILE:
		if (priv->sync_file)
				g_object_unref (priv->sync_file);
		priv->sync_file = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
anjuta_profile_get_property (GObject *object, guint prop_id,
							 GValue *value, GParamSpec *pspec)
{
	AnjutaProfilePriv *priv = ANJUTA_PROFILE (object)->priv;
	
	g_return_if_fail (ANJUTA_IS_PROFILE (object));

	switch (prop_id)
	{
	case PROP_PLUGIN_MANAGER:
		g_value_set_object (value, priv->plugin_manager);
		break;
	case PROP_PROFILE_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_SYNC_FILE:
		g_value_set_object (value, priv->sync_file);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
anjuta_profile_plugin_added (AnjutaProfile *self,
							 AnjutaPluginHandle *plugin)
{
}

static void
anjuta_profile_plugin_removed (AnjutaProfile *self,
							   AnjutaPluginHandle *plugin)
{
}

static void
anjuta_profile_changed (AnjutaProfile *self)
{
	GError *error = NULL;
	anjuta_profile_sync (self, &error);
	if (error)
	{
		g_warning ("Failed to synchronize plugins profile '%s': %s",
				   self->priv->name, error->message);
		g_error_free (error);
	}
}

static void
anjuta_profile_class_init (AnjutaProfileClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));

	object_class->finalize = anjuta_profile_finalize;
	object_class->set_property = anjuta_profile_set_property;
	object_class->get_property = anjuta_profile_get_property;

	klass->plugin_added = anjuta_profile_plugin_added;
	klass->plugin_removed = anjuta_profile_plugin_removed;
	klass->changed = anjuta_profile_changed;

	g_object_class_install_property (object_class,
	                                 PROP_PLUGIN_MANAGER,
	                                 g_param_spec_object ("plugin-manager",
											  _("Plugin Manager"),
											  _("The plugin manager to use for resolving plugins"),
											  ANJUTA_TYPE_PLUGIN_MANAGER,
											  G_PARAM_READABLE |
											  G_PARAM_WRITABLE |
											  G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
	                                 PROP_PROFILE_NAME,
	                                 g_param_spec_string ("profile-name",
											  _("Profile Name"),
											  _("Name of the plugin profile"),
											  NULL,
											  G_PARAM_READABLE |
											  G_PARAM_WRITABLE |
											  G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
	                                 PROP_SYNC_FILE,
	                                 g_param_spec_object ("sync-file",
											  _("Synchronization file"),
											  _("File to syncronize the profile XML"),
											  G_TYPE_FILE,
											  G_PARAM_READABLE |
											  G_PARAM_WRITABLE |
											  G_PARAM_CONSTRUCT));

	/**
	 * AnjutaProfile::plugin-added:
	 * @profile: a #AnjutaProfile object.
	 * @plugin: the new plugin as a #AnjutaPluginHandle.
	 * 
	 * Emitted when a plugin is added in the list.
	 */
	profile_signals[PLUGIN_ADDED] =
		g_signal_new ("plugin-added",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (AnjutaProfileClass, plugin_added),
		              NULL, NULL,
		              anjuta_cclosure_marshal_VOID__POINTER,
		              G_TYPE_NONE, 1,
		              G_TYPE_POINTER);

	/**
	 * AnjutaProfile::plugin-removed:
	 * @profile: a #AnjutaProfile object.
	 * @plugin: the removed plugin as a #AnjutaPluginHandle.
	 * 
	 * Emitted when a plugin is removed from the list.
	 */
	profile_signals[PLUGIN_REMOVED] =
		g_signal_new ("plugin-removed",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (AnjutaProfileClass, plugin_removed),
		              NULL, NULL,
		              anjuta_cclosure_marshal_VOID__POINTER,
		              G_TYPE_NONE, 1,
		              G_TYPE_POINTER);
	
	/**
	 * AnjutaProfile::changed:
	 * @profile: a #AnjutaProfile object.
	 * 
	 * Emitted when a plugin is added or removed from the list.
	 */
	profile_signals[CHANGED] =
		g_signal_new ("changed",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (AnjutaProfileClass, changed),
		              NULL, NULL,
		              anjuta_cclosure_marshal_VOID__POINTER,
		              G_TYPE_NONE, 0);

	/**
	 * AnjutaProfile::profile-descoped:
	 * @profile: the old unloaded #AnjutaProfile
	 * 
	 * Emitted when a profile will be unloaded.
	 */
	profile_signals[DESCOPED] =
		g_signal_new ("descoped",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (AnjutaProfileClass,
									   descoped),
		              NULL, NULL,
					  g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);
	
	/**
	 * AnjutaProfileManager::profile-scoped:
	 * @profile_manager: a #AnjutaProfileManager object.
	 * @profile: the current loaded #AnjutaProfile.
	 * 
	 * Emitted when a new profile is loaded.
	 */
	profile_signals[SCOPED] =
		g_signal_new ("scoped",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (AnjutaProfileClass,
									   scoped),
		              NULL, NULL,
					  g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);
}

GType
anjuta_profile_get_type (void)
{
	static GType our_type = 0;

	if(our_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (AnjutaProfileClass), /* class_size */
			(GBaseInitFunc) NULL, /* base_init */
			(GBaseFinalizeFunc) NULL, /* base_finalize */
			(GClassInitFunc) anjuta_profile_class_init, /* class_init */
			(GClassFinalizeFunc) NULL, /* class_finalize */
			NULL /* class_data */,
			sizeof (AnjutaProfile), /* instance_size */
			0, /* n_preallocs */
			(GInstanceInitFunc) anjuta_profile_init, /* instance_init */
			NULL /* value_table */
		};

		our_type = g_type_register_static (G_TYPE_OBJECT, "AnjutaProfile",
		                                   &our_info, 0);
	}

	return our_type;
}

static void
on_plugin_activated (AnjutaPluginManager *plugin_manager,
					 AnjutaPluginHandle *plugin_handle,
					 GObject *plugin_object,
					 AnjutaProfile *profile)
{
	/* Add it current profile */
	gboolean exclude;
	AnjutaPluginDescription *desc;

	desc = anjuta_plugin_handle_get_description (plugin_handle);
	if (!anjuta_plugin_description_get_boolean (desc, "Anjuta Plugin", "ExcludeFromSession", &exclude) || !exclude)
	{
		anjuta_profile_add_plugin (profile, plugin_handle);
	}
}

static void
on_plugin_deactivated (AnjutaPluginManager *plugin_manager,
					   AnjutaPluginHandle *plugin_handle,
					   GObject *plugin_object,
					   AnjutaProfile *profile)
{
	/* Remove from current profile */
	anjuta_profile_remove_plugin (profile, plugin_handle);
}

/**
 * anjuta_profile_new:
 * @name: the new profile name.
 * @plugin_manager: the #AnjutaPluginManager used by this profile.
 * 
 * Create a new profile.
 *
 * Return value: the new #AnjutaProfile object.
 */
AnjutaProfile*
anjuta_profile_new (const gchar* name, AnjutaPluginManager *plugin_manager)
{
	GObject *profile;
	profile = g_object_new (ANJUTA_TYPE_PROFILE, "profile-name", name,
							"plugin-manager", plugin_manager, NULL);
	return ANJUTA_PROFILE (profile);
}

/**
 * anjuta_profile_get_name:
 * @profile: a #AnjutaProfile object.
 * 
 * Get the profile name.
 *
 * Return value: the profile name.
 */
const gchar*
anjuta_profile_get_name (AnjutaProfile *profile)
{
	AnjutaProfilePriv *priv;
	g_return_val_if_fail (ANJUTA_IS_PROFILE (profile), NULL);
	priv = ANJUTA_PROFILE (profile)->priv;
	return priv->name;
}

/**
 * anjuta_profile_add_plugin:
 * @profile: a #AnjutaProfile object.
 * @plugin: a #AnjutaPluginHandle.
 * 
 * Add one plugin into the profile plugin list.
 */
void
anjuta_profile_add_plugin (AnjutaProfile *profile,
						   AnjutaPluginHandle *plugin)
{
	AnjutaProfilePriv *priv;
	g_return_if_fail (ANJUTA_IS_PROFILE (profile));
	g_return_if_fail (plugin != NULL);
	priv = ANJUTA_PROFILE (profile)->priv;
	if (g_hash_table_lookup (priv->plugins_to_load, plugin) == NULL)
	{
		g_hash_table_add (priv->plugins_to_load, plugin);
		g_signal_emit_by_name (profile, "plugin-added", plugin);
		g_signal_emit_by_name (profile, "changed");
	}
}

/**
 * anjuta_profile_remove_plugin:
 * @profile: a #AnjutaProfile object.
 * @plugin: a #AnjutaPluginHandle.
 * 
 * Remove one plugin from the profile plugin list.
 */
void
anjuta_profile_remove_plugin (AnjutaProfile *profile, 
							  AnjutaPluginHandle *plugin)
{
	AnjutaProfilePriv *priv;
	g_return_if_fail (ANJUTA_IS_PROFILE (profile));
	g_return_if_fail (plugin != NULL);
	priv = ANJUTA_PROFILE (profile)->priv;
	if (g_hash_table_remove (priv->plugins_to_load, plugin))
	{
		g_hash_table_remove (priv->plugins_to_exclude_from_sync, plugin);
		g_signal_emit_by_name (profile, "plugin-removed", plugin);
		g_signal_emit_by_name (profile, "changed");
	}
}

/**
 * anjuta_profile_has_plugin:
 * @profile: a #AnjutaProfile object
 * @plugin: a #AnjutaPluginHandle 
 * 
 * Check if a plugin is included in the profile plugin list.
 *
 * Return value: TRUE if the plugin is included in the list.
 */
gboolean
anjuta_profile_has_plugin (AnjutaProfile *profile,
						   AnjutaPluginHandle *plugin)
{
	AnjutaProfilePriv *priv;
	g_return_val_if_fail (ANJUTA_IS_PROFILE (profile), FALSE);
	g_return_val_if_fail (plugin != NULL, FALSE);
	priv = ANJUTA_PROFILE (profile)->priv;

	return g_hash_table_lookup (priv->plugins_to_load, plugin) != NULL;
}

static gboolean
anjuta_profile_configure_plugins (AnjutaProfile *profile,
                                  GList *handles_list,
                                  GList *config_list)
{
	AnjutaProfilePriv *priv;
	GList *item;
	GList *config;

	g_return_val_if_fail (ANJUTA_IS_PROFILE (profile), FALSE);

	priv = ANJUTA_PROFILE (profile)->priv;
	for (config = config_list, item = handles_list; item != NULL; item = g_list_next (item), config = g_list_next (config))
	{
		GList *plugin;
		GList *set;

		for (plugin = g_list_first ((GList *)item->data); plugin != NULL; plugin = g_list_next (plugin))
		{
			AnjutaPluginHandle *handle = ANJUTA_PLUGIN_HANDLE (plugin->data);
			AnjutaPluginDescription *desc;

			desc = anjuta_plugin_handle_get_description (handle);
			for (set = g_list_first ((GList *)config->data); set != NULL; set = g_list_next (set))
			{
				gchar *group = (gchar *)set->data;
				gchar *key = group + strlen (group) + 1;
				gchar *value = key + strlen (key) + 1;

				anjuta_plugin_description_override (desc, group, key, value);
				priv->configuration = g_list_prepend (priv->configuration, group);
				priv->configuration = g_list_prepend (priv->configuration, handle);
			}
		}
		for (set = g_list_first ((GList *)config->data); set != NULL; set = g_list_delete_link (set, set))
		{
			priv->config_keys = g_list_prepend (priv->config_keys, set->data);
		}
	}
	g_list_free (config_list);

	return TRUE;
}


static gboolean
anjuta_profile_unconfigure_plugins (AnjutaProfile *profile)
{
	AnjutaProfilePriv *priv;
	GList *item;

	g_return_val_if_fail (ANJUTA_IS_PROFILE (profile), FALSE);

	priv = ANJUTA_PROFILE (profile)->priv;
	for (item = g_list_first (priv->configuration); item != NULL; item = g_list_delete_link (item, item))
	{
		AnjutaPluginHandle *handle = ANJUTA_PLUGIN_HANDLE (item->data);
		AnjutaPluginDescription *desc;
		gchar *group;
		gchar *key;

		item = g_list_delete_link (item, item);
		group = (gchar *)(item->data);
		key = group + strlen (group) + 1;

		desc = anjuta_plugin_handle_get_description (handle);
		anjuta_plugin_description_remove (desc, group, key);
	}
	priv->configuration = NULL;
	g_list_free_full (priv->config_keys, (GDestroyNotify)g_free);
	priv->config_keys = NULL;

	return TRUE;
}

static GList*
anjuta_profile_select_plugins (AnjutaProfile *profile,
							   GList *handles_list)
{
	GList *selected_plugins = NULL;
	GList *node = handles_list;
	AnjutaProfilePriv *priv;
	
	priv = profile->priv;
	
	while (node)
	{
		GList *descs = node->data;
		if (g_list_length (descs) == 1)
		{
			selected_plugins = g_list_prepend (selected_plugins, descs->data);
		}
		else
		{
			AnjutaPluginHandle* handle;
			handle = anjuta_plugin_manager_select (priv->plugin_manager,
			                                       _("Select a plugin"),
			                                       _("Please select a plugin from the list"),
			                                       descs);
			if (handle)
				selected_plugins = g_list_prepend (selected_plugins, handle);
		}
		node = g_list_next (node);
	}
	return g_list_reverse (selected_plugins);
}


/* Read profile from XML
 *---------------------------------------------------------------------------*/

/* Error during parsing */
static gboolean
set_parse_error (GError **error, GFile*file)
{
	gchar *uri = g_file_get_uri (file);

	g_error_free (*error);
	*error = g_error_new (ANJUTA_PROFILE_ERROR,
	                      ANJUTA_PROFILE_ERROR_URI_READ_FAILED,
	                      _("Failed to read '%s': XML parse error. "
	                        "Invalid or corrupted Anjuta plugins profile."),
	                      uri);
	g_free (uri);

	return FALSE;
}

static xmlDocPtr
load_profile_from_xml (GFile *file, GError **error)
{
	gchar *read_buf;
	gsize size;
	xmlDocPtr xml_doc;

	/* Read xml file */
	if (!g_file_load_contents (file, NULL, &read_buf, &size, NULL, error))
	{
		return NULL;
	}
	
	/* Parse xml file */
	xml_doc = xmlParseMemory (read_buf, size);
	g_free (read_buf);
	if (xml_doc != NULL)
	{
		xmlNodePtr xml_root;
		
		xml_root = xmlDocGetRootElement(xml_doc);
		if (xml_root ||
			(xml_root->name) ||
			xmlStrEqual(xml_root->name, (const xmlChar *)"anjuta"))
		{
			return xml_doc;
		}
		xmlFreeDoc(xml_doc);
	}
	set_parse_error (error, file);

	return NULL;
}

static GList *
parse_set (xmlNodePtr xml_node, GFile *file, GError **error)
{
	GList *config = NULL;
	gboolean parse_error = FALSE;
	xmlNodePtr xml_require_node;
	
	/* Read attribute conditions */
	for (xml_require_node = xml_node->xmlChildrenNode;
	     xml_require_node;
	     xml_require_node = xml_require_node->next)
	{
		xmlChar *group;
		xmlChar *attrib;
		xmlChar *value;

		if (!xml_require_node->name ||
		    !xmlStrEqual (xml_require_node->name,
		                  (const xmlChar*)"set"))
		{
			continue;
		}
		group = xmlGetProp (xml_require_node,
		                    (const xmlChar *)"group");
		attrib = xmlGetProp(xml_require_node,
		                    (const xmlChar *)"attribute");
		value = xmlGetProp(xml_require_node,
		                   (const xmlChar *)"value");

		if (group && attrib && value)
		{
			GString *str;

			str = g_string_new ((const gchar *)group);
			g_string_append_c (str, '\0');
			g_string_append (str, (const gchar *)attrib);
			g_string_append_c (str, '\0');
			g_string_append (str, (const gchar *)value);
			
			config = g_list_prepend (config, g_string_free (str, FALSE));
		}
		else
		{
			parse_error = TRUE;
			g_warning ("XML parse error: group, attribute and value should be defined in set");
		}
		if (group) xmlFree (group);
		if (attrib) xmlFree (attrib);
		if (value) xmlFree (value);
		if (parse_error) break;
	}
	
	if (parse_error)
	{
		set_parse_error (error, file);
	}

	return g_list_reverse (config);
}


static GList *
parse_requires (xmlNodePtr xml_node, AnjutaPluginManager *plugin_manager, GFile *file, GError **error)
{
	GList *plugin_handles = NULL;
	GList *groups = NULL;
	GList *attribs = NULL;
	GList *values = NULL;
	gboolean parse_error = FALSE;
	xmlNodePtr xml_require_node;
	
	/* Read attribute conditions */
	for (xml_require_node = xml_node->xmlChildrenNode;
	     xml_require_node;
	     xml_require_node = xml_require_node->next)
	{
		xmlChar *group;
		xmlChar *attrib;
		xmlChar *value;

		if (!xml_require_node->name ||
		    !xmlStrEqual (xml_require_node->name,
		                  (const xmlChar*)"require"))
		{
			continue;
		}
		group = xmlGetProp (xml_require_node,
		                    (const xmlChar *)"group");
		attrib = xmlGetProp(xml_require_node,
		                    (const xmlChar *)"attribute");
		value = xmlGetProp(xml_require_node,
		                   (const xmlChar *)"value");

		if (group && attrib && value)
		{
			groups = g_list_prepend (groups, group);
			attribs = g_list_prepend (attribs, attrib);
			values = g_list_prepend (values, value);
		}
		else
		{
			if (group) xmlFree (group);
			if (attrib) xmlFree (attrib);
			if (value) xmlFree (value);
			parse_error = TRUE;
			g_warning ("XML parse error: group, attribute and value should be defined in require");
			break;
		}
	}
	
	if (parse_error)
	{
		set_parse_error (error, file);
	}
	else
	{
		if (g_list_length (groups) == 0)
		{
			parse_error = TRUE;
			g_warning ("XML Error: No attributes to match given");
		}
		else
		{
			plugin_handles =
				anjuta_plugin_manager_list_query (plugin_manager,
				                                  groups,
				                                  attribs,
				                                  values);
		}
	}
	g_list_free_full (groups, (GDestroyNotify)xmlFree);
	g_list_free_full (attribs, (GDestroyNotify)xmlFree);
	g_list_free_full (values, (GDestroyNotify)xmlFree);

	
	return plugin_handles;
}

/* Read filter */
static gboolean
parse_disable_plugins (GHashTable *disable_plugins, xmlNodePtr xml_root, AnjutaPluginManager *plugin_manager, GFile *file, GError **error)
{
	xmlNodePtr xml_node;
	GError *parse_error = NULL;

	for (xml_node = xml_root->xmlChildrenNode; xml_node; xml_node = xml_node->next)
	{
		GList *plugin_handles = NULL;

		if (!xml_node->name ||
			!xmlStrEqual (xml_node->name, (const xmlChar*)"filter"))
		{
			continue;
		}

		/* Get all plugins fullfiling filter requirements */
		plugin_handles = parse_requires (xml_node, plugin_manager, file, &parse_error);
		if (parse_error != NULL)
		{
			g_propagate_error (error, parse_error);

			return FALSE;
		}
		else if (plugin_handles)
		{
			for (; plugin_handles != NULL; plugin_handles = g_list_delete_link (plugin_handles, plugin_handles))
			{
				g_hash_table_remove (disable_plugins, plugin_handles->data);
			}
		}
	}

	return TRUE;
}

/* Read plugins, return a list of plugin list */
static GList *
parse_plugins (GList **set_list, xmlNodePtr xml_root, AnjutaPluginManager *plugin_manager, GFile *file, GError **error)
{
	xmlNodePtr xml_node;
	GError *parse_error = NULL;
	GList *handles_list = NULL;
	GList *not_found_names = NULL;
	GList *not_found_urls = NULL;

	/* Read plugin list */
	for (xml_node = xml_root->xmlChildrenNode; xml_node; xml_node = xml_node->next)
	{
		xmlChar *name, *url, *mandatory_text;
		gboolean mandatory;
		GList *plugin_handles = NULL;

		if (!xml_node->name ||
		    !xmlStrEqual (xml_node->name, (const xmlChar*)"plugin"))
		{
			continue;
		}

		name = xmlGetProp (xml_node, (const xmlChar*)"name");
		url = xmlGetProp (xml_node, (const xmlChar*)"url");
		
		/* Ensure that both name is given */
		if (!name)
		{
			g_warning ("XML error: Plugin name should be present in plugin tag");
			set_parse_error (&parse_error, file);
			break;
		}
		if (!url)
			url = xmlCharStrdup ("http://anjuta.org/plugins/");

		/* Check if the plugin is mandatory */
		mandatory_text = xmlGetProp (xml_node, (const xmlChar*)"mandatory");
		mandatory = mandatory_text && (xmlStrcasecmp (mandatory_text, (const xmlChar *)"yes") == 0);
		xmlFree(mandatory_text);

		plugin_handles = parse_requires (xml_node, plugin_manager, file, &parse_error);
		if (parse_error != NULL) break;
		if (plugin_handles)
		{
			GList *set = parse_set (xml_node, file, &parse_error);
			if (parse_error != NULL) break;

			handles_list = g_list_prepend (handles_list, plugin_handles);
			*set_list = g_list_prepend (*set_list, set);
		}
		else if (mandatory)
		{
			not_found_names = g_list_prepend (not_found_names, g_strdup ((const gchar *)name));
			not_found_urls = g_list_prepend (not_found_urls, g_strdup ((const gchar *)url));
		}
	}
	
	if (parse_error != NULL)
	{
		g_propagate_error (error, parse_error);
		g_list_free_full (handles_list, (GDestroyNotify)g_list_free);
		handles_list = NULL;
	}
	else if (not_found_names)
	{
		/*
	 	* FIXME: Present a nice dialog box to promt the user to download
		* the plugin from corresponding URLs, install them and proceed.
 		*/	
		GList *node_name, *node_url;
		GString *mesg = g_string_new ("");
			
		not_found_names = g_list_reverse (not_found_names);
		not_found_urls = g_list_reverse (not_found_urls);
		
		node_name = not_found_names;
		node_url = not_found_urls;
		while (node_name)
		{
			/* <Pluginname>: Install it from <some location on the web> */
			g_string_append_printf (mesg, _("%s: Install it from '%s'\n"),
											(char *)node_name->data,
											(char*)node_url->data);
			node_name = g_list_next (node_name);
			node_url = g_list_next (node_url);
		}
		g_set_error (error, ANJUTA_PROFILE_ERROR,
					 ANJUTA_PROFILE_ERROR_PLUGIN_MISSING,
					 _("Failed to read '%s': Following mandatory plugins are missing"),
					 mesg->str);
		g_string_free (mesg, TRUE);

		g_list_foreach (not_found_names, (GFunc)g_free, NULL);
		g_list_free (not_found_names);
		g_list_foreach (not_found_urls, (GFunc)g_free, NULL);
		g_list_free (not_found_urls);

		g_list_free_full (handles_list, (GDestroyNotify)g_list_free);
		handles_list = NULL;
	}

	return handles_list;
}

static gboolean
anjuta_profile_read_xml (AnjutaProfile *profile,
                         GError **error)
{
	AnjutaProfilePriv *priv;
	AnjutaProfileXml *xml;
	xmlNodePtr xml_root;
	GError *parse_error = NULL;
	GList *disable_list;
	GHashTable *disable_hash;
	guint disable_size = 0;

	/* Check if there are new XML files */
	priv = profile->priv;
	if (priv->xml == NULL) return TRUE;
	
	/* Read all xml file */
	for (xml = priv->xml; xml != NULL; xml = xml->next)
	{
		xml->doc = load_profile_from_xml (xml->file, &parse_error);
		if (parse_error != NULL)
		{
			g_propagate_error (error, parse_error);

			return FALSE;
		}
	}

	/* Get all disable plugins */
	if (priv->plugins_to_disable == NULL)
	{
		disable_list = anjuta_plugin_manager_list_query (priv->plugin_manager, NULL, NULL, NULL);
		disable_size = g_list_length (disable_list);
	}
	else
	{
		disable_list = priv->plugins_to_disable;
	}
	disable_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (; disable_list != NULL; disable_list = g_list_delete_link (disable_list, disable_list))
	{
		g_hash_table_add (disable_hash, disable_list->data);
	}
	for (xml = priv->xml; xml != NULL; xml = xml->next)
	{
		/* Parse filter in xml file */
		xml_root = xmlDocGetRootElement(xml->doc);
		parse_disable_plugins (disable_hash, xml_root, priv->plugin_manager, xml->file, &parse_error);
		if (parse_error != NULL) break;
	}
	if (disable_size == g_hash_table_size (disable_hash))
	{
		/* No filter, keep all plugins */
		priv->plugins_to_disable = NULL;
	}
	else
	{
		/* Filter some plugins */
		priv->plugins_to_disable = g_hash_table_get_keys (disable_hash);
		anjuta_plugin_manager_set_disable_plugins (priv->plugin_manager, priv->plugins_to_disable, TRUE);
	}
	g_hash_table_destroy (disable_hash);
	if (parse_error != NULL) return FALSE;


	/* Get all plugins to load */
	for (xml = priv->xml; xml != NULL; xml = xml->next)
	{
		GList *handles_list;
		GList *plugin_list;
		GList *set_list = NULL;
		
		/* Parse plugin in xml file */
		xml_root = xmlDocGetRootElement(xml->doc);
		handles_list = parse_plugins (&set_list, xml_root, priv->plugin_manager, xml->file, &parse_error);
		if (parse_error != NULL) break;

		anjuta_profile_configure_plugins (profile, handles_list, set_list);

		plugin_list = anjuta_profile_select_plugins (profile, handles_list);
		g_list_foreach (handles_list, (GFunc)g_list_free, NULL);
		g_list_free (handles_list);
		for (; plugin_list != NULL; plugin_list = g_list_delete_link (plugin_list, plugin_list))
		{
			g_hash_table_add (priv->plugins_to_load, plugin_list->data);
			if (xml->exclude_from_sync) g_hash_table_add (priv->plugins_to_exclude_from_sync, plugin_list->data);
			anjuta_plugin_handle_set_core_plugin ((AnjutaPluginHandle *)plugin_list->data, xml->core_plugin);
		}
	}

	/* Remove xml object */
	while (priv->xml != NULL)
	{
		AnjutaProfileXml *next;

		next = priv->xml->next;
		g_object_unref (priv->xml->file);
		xmlFreeDoc(priv->xml->doc);
		g_free (priv->xml);
		priv->xml = next;
	}

	if (parse_error != NULL) g_propagate_error (error, parse_error);

	return parse_error == NULL;
}



/* Public functions
 *---------------------------------------------------------------------------*/

/**
 * anjuta_profile_add_plugins_from_xml:
 * @profile: a #AnjutaProfile object.
 * @profile_xml_file: xml file containing plugin list.
 * @exclude_from_sync: TRUE if these plugins shouldn't be saved in user session.
 * @core_plugin: %TRUE if these plugins should never be unloaded.
 * @error: error propagation and reporting.
 * 
 * Add all plugins inscribed in the xml file into the profile plugin list.
 *
 * Return value: TRUE on success, FALSE otherwise.
 */
gboolean
anjuta_profile_add_plugins_from_xml (AnjutaProfile *profile,
                                     GFile* profile_xml_file,
                                     gboolean exclude_from_sync,
                                     gboolean core_plugin,
                                     GError **error)
{
	AnjutaProfilePriv *priv;
	AnjutaProfileXml *xml;
	AnjutaProfileXml **last;
	
	g_return_val_if_fail (ANJUTA_IS_PROFILE (profile), FALSE);
	
	priv = profile->priv;

	/* Just save the file name, the xml wil be loaded later after unloading the
	 * previous profile if needed */

	xml = g_new (AnjutaProfileXml, 1);
	xml->file = g_object_ref (profile_xml_file);
	xml->doc = NULL;
	xml->exclude_from_sync = exclude_from_sync;
	xml->core_plugin = core_plugin;
	xml->next = NULL;
	for (last = &(priv->xml); *last != NULL; last = &((*last)->next));
	*last = xml;

	return TRUE;
}

/**
 * anjuta_profile_to_xml :
 * @profile: a #AnjutaProfile object.
 * 
 * Return a string in xml format containing the list of saved plugins.
 *
 * Return value: a newly-allocated string that must be freed with g_free().
 */
static gchar*
anjuta_profile_to_xml (AnjutaProfile *profile)
{
	GList *node;
	GString *str;
	AnjutaProfilePriv *priv;
	
	g_return_val_if_fail (ANJUTA_IS_PROFILE (profile), FALSE);
	priv = profile->priv;
	
	str = g_string_new ("<?xml version=\"1.0\"?>\n<anjuta>\n");
	for (node = g_hash_table_get_keys (priv->plugins_to_load); node != NULL; node = g_list_delete_link (node, node))
	{
		AnjutaPluginHandle *handle;
		AnjutaPluginDescription *desc;
		gboolean user_activatable = TRUE;
		gchar *name = NULL, *plugin_id = NULL;
		
		if (g_hash_table_lookup (priv->plugins_to_exclude_from_sync, node->data))
		{
			/* Do not save plugin in the exclude list */
			continue;
		}
		handle = (AnjutaPluginHandle *)node->data;
		desc = anjuta_plugin_handle_get_description(handle);
		if (anjuta_plugin_description_get_boolean (desc, "Anjuta Plugin",
												  "UserActivatable", &user_activatable)
				&& !user_activatable)
		{
			/* Do not save plugins that are auto activated */
			continue;
		}
			
		/* Do not use the _locale_ version because it's not in UI */
		anjuta_plugin_description_get_string (desc, "Anjuta Plugin",
											  "Name", &name);
		DEBUG_PRINT("Saving plugin: %s", name);
		if (!name)
			name = g_strdup ("Unknown");
			
		if (anjuta_plugin_description_get_string (desc, "Anjuta Plugin",
												  "Location", &plugin_id))
		{
			g_string_append (str, "    <plugin name=\"");
			g_string_append (str, name);
			g_string_append (str, "\" mandatory=\"no\">\n");
			g_string_append (str, "        <require group=\"Anjuta Plugin\"\n");
			g_string_append (str, "                 attribute=\"Location\"\n");
			g_string_append (str, "                 value=\"");
			g_string_append (str, plugin_id);
			g_string_append (str, "\"/>\n");
			g_string_append (str, "    </plugin>\n");
				
			g_free (plugin_id);
		}
		g_free (name);
	}
	g_string_append (str, "</anjuta>\n");
	
	return g_string_free (str, FALSE);
}

/**
 * anjuta_profile_set_sync_file:
 * @profile: a #AnjutaProfile object.
 * @sync_file: file used to save profile.
 * 
 * Define the file used to save plugins list.
 */

void
anjuta_profile_set_sync_file (AnjutaProfile *profile, GFile *sync_file)
{
	AnjutaProfilePriv *priv;
	
	g_return_if_fail (ANJUTA_IS_PROFILE (profile));
	
	priv = profile->priv;
	
	if (priv->sync_file)
		g_object_unref (priv->sync_file);
	priv->sync_file = sync_file;
	if (priv->sync_file);
		g_object_ref (priv->sync_file);
}

/**
 * anjuta_profile_sync:
 * @profile: a #AnjutaProfile object.
 * @error: error propagation and reporting.
 * 
 * Save the current plugins list in the xml file set with anjuta_profile_set_sync_file().
 *
 * Return value: TRUE on success, FALSE otherwise.
 */
gboolean
anjuta_profile_sync (AnjutaProfile *profile, GError **error)
{
	gboolean ok;
	gchar *xml_buffer;
	AnjutaProfilePriv *priv;
	GError* file_error = NULL;
	
	g_return_val_if_fail (ANJUTA_IS_PROFILE (profile), FALSE);
	priv = profile->priv;
	
	if (!priv->sync_file)
		return FALSE;
	
	xml_buffer = anjuta_profile_to_xml (profile);
	ok = g_file_replace_contents (priv->sync_file, xml_buffer, strlen(xml_buffer),
								  NULL, FALSE, G_FILE_CREATE_NONE,
								  NULL, NULL, &file_error);
	if (!ok && g_error_matches (file_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
	{
		/* Try to create parent directory */
		GFile* parent = g_file_get_parent (priv->sync_file);
		if (g_file_make_directory (parent, NULL, NULL))
		{
			g_clear_error (&file_error);
			ok = g_file_replace_contents (priv->sync_file, xml_buffer, strlen(xml_buffer),
										  NULL, FALSE, G_FILE_CREATE_NONE,
										  NULL, NULL, &file_error);
		}
		g_object_unref (parent);
	}
	g_free (xml_buffer);
	if (file_error != NULL) g_propagate_error (error, file_error);
	
	return ok;
}

/**
 * anjuta_profile_load:
 * @profile: a #AnjutaProfile object.
 * @error: error propagation and reporting.
 * 
 * Load the profile
 *
 * Return value: TRUE on success, FALSE otherwise.
 */
gboolean
anjuta_profile_load (AnjutaProfile *profile, GError **error)
{
	AnjutaProfilePriv *priv;
	GList *active_plugins, *node;
	GHashTable *active_hash;

	/* Read XML file if needed */
	if (!anjuta_profile_read_xml (profile, error)) return FALSE;
	priv = profile->priv;

	/* Deactivate plugins that are already active, but are not requested to be
	 * active */
	active_plugins = anjuta_plugin_manager_get_active_plugins (priv->plugin_manager);
	for (node = active_plugins; node != NULL; node = g_list_next (node))
	{
		AnjutaPluginHandle *handle = (AnjutaPluginHandle *)node->data;
		
		if (!anjuta_plugin_handle_is_core_plugin (handle) &&
		    !g_hash_table_lookup (priv->plugins_to_load, handle))
		{
			anjuta_plugin_manager_unload_plugin_by_handle (priv->plugin_manager,
			                                               handle);
		}
	}

	/* Prepare active plugins hash */
	active_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (node = active_plugins; node != NULL; node = g_list_next (node))
	{
		g_hash_table_add (active_hash, node->data);
	}
	g_list_free (active_plugins);

	/* Prepare the plugins to activate */
	active_plugins = g_hash_table_get_keys (priv->plugins_to_load);
	for (node = active_plugins; node != NULL;)
	{
		AnjutaPluginHandle *handle = (AnjutaPluginHandle *)node->data;
		GList *next = g_list_next (node);
		
		if (g_hash_table_lookup (active_hash, handle) != NULL)
		{
			active_plugins = g_list_delete_link (active_plugins, node);
		}
		node = next;
	}
	g_hash_table_destroy (active_hash);

	/* Now activate the plugins */
	if (active_plugins != NULL)
	{
		anjuta_plugin_manager_activate_plugins (priv->plugin_manager,
		                                        active_plugins);
		g_list_free (active_plugins);
	}

	/* Enable profile synchronization */
	g_signal_connect (priv->plugin_manager, "plugin-activated",
	                  G_CALLBACK (on_plugin_activated), profile);
	g_signal_connect (priv->plugin_manager, "plugin-deactivated",
	                  G_CALLBACK (on_plugin_deactivated), profile);

	g_signal_emit_by_name (profile, "scoped");


	return TRUE;
}

/**
 * anjuta_profile_load:
 * @profile: a #AnjutaProfile object.
 * @error: error propagation and reporting.
 * 
 * Unload the profile
 *
 * Return value: TRUE on success, FALSE otherwise.
 */
gboolean
anjuta_profile_unload (AnjutaProfile *profile, GError **error)
{
	AnjutaProfilePriv *priv;

	/* Disable profile synchronization while the profile is being activated */
	priv = profile->priv;
	g_signal_handlers_disconnect_by_func (priv->plugin_manager,
	                                      G_CALLBACK (on_plugin_activated),
	                                      profile);
	g_signal_handlers_disconnect_by_func (priv->plugin_manager,
	                                      G_CALLBACK (on_plugin_deactivated),
	                                      profile);

	/* Remove profile configuration */
	anjuta_profile_unconfigure_plugins (profile);

	/* Re-enable disabled plugins */
	anjuta_plugin_manager_set_disable_plugins (priv->plugin_manager, priv->plugins_to_disable, FALSE);
	
	/* Emit pre-change for the last profile */
	if (profile)
	{
		g_signal_emit_by_name (profile, "descoped");
	}

	return FALSE;
}

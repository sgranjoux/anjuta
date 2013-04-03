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
	PROP_PROFILE_PLUGINS,
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

struct _AnjutaProfilePriv
{
	gchar *name;
	AnjutaPluginManager *plugin_manager;
	GList *plugins;
	GHashTable *plugins_hash;
	GHashTable *plugins_to_exclude_from_sync;
	GFile *sync_file;
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
	object->priv->plugins_hash = g_hash_table_new (g_direct_hash,
												   g_direct_equal);
	object->priv->plugins_to_exclude_from_sync =
		g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
anjuta_profile_finalize (GObject *object)
{
	AnjutaProfilePriv *priv = ANJUTA_PROFILE (object)->priv;
	g_free (priv->name);
	if (priv->plugins)
		g_list_free (priv->plugins);
	g_hash_table_destroy (priv->plugins_hash);
	g_hash_table_destroy (priv->plugins_to_exclude_from_sync);
	
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
	case PROP_PROFILE_PLUGINS:
		if (priv->plugins)
			g_list_free (priv->plugins);
		if (g_value_get_pointer (value))
			priv->plugins = g_list_copy (g_value_get_pointer (value));
		else
			priv->plugins = NULL;
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
	case PROP_PROFILE_PLUGINS:
		g_value_set_pointer (value, priv->plugins);
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
anjuta_profile_changed (AnjutaProfile *self, GList *plugins)
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
	                                 PROP_PROFILE_PLUGINS,
	                                 g_param_spec_pointer ("plugins",
											  _("Profile Plugins"),
											  _("List of plugins for this profile"),
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
	 * @plugin_list: the new plugins list.
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
		              G_TYPE_NONE, 1,
		              G_TYPE_POINTER);

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
	if (priv->plugins == NULL || g_list_find (priv->plugins, plugin) == NULL)
	{
		priv->plugins = g_list_prepend (priv->plugins, plugin);
		g_signal_emit_by_name (profile, "plugin-added", plugin);
		g_signal_emit_by_name (profile, "changed", priv->plugins);
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
	if (priv->plugins && g_list_find (priv->plugins, plugin) != NULL)
	{
		priv->plugins = g_list_remove (priv->plugins, plugin);
		g_signal_emit_by_name (profile, "plugin-removed", plugin);
		g_signal_emit_by_name (profile, "changed", priv->plugins);
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
	return (priv->plugins != NULL &&
			g_list_find (priv->plugins, plugin) != NULL);
}

/**
 * anjuta_profile_get_plugins:
 * @profile: a #AnjutaProfile object.
 * 
 * Get the profile current plugins list.
 *
 * Return value: the plugins list.
 */
GList*
anjuta_profile_get_plugins (AnjutaProfile *profile)
{
	AnjutaProfilePriv *priv;
	g_return_val_if_fail (ANJUTA_IS_PROFILE (profile), FALSE);
	priv = ANJUTA_PROFILE (profile)->priv;
	return priv->plugins;
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

static GList *
anjuta_profile_read_plugins_from_xml (AnjutaProfile *profile,
									  GFile *file,
									  GError **error)
{
	gchar *read_buf;
	gsize size;
	xmlDocPtr xml_doc;
	GList *handles_list = NULL;
	GList *not_found_names = NULL;
	GList *not_found_urls = NULL;
	gboolean parse_error;

	/* Read xml file */
	if (!g_file_load_contents (file, NULL, &read_buf, &size, NULL, error))
	{
		return NULL;
	}
	
	/* Parse xml file */
	parse_error = TRUE;
	xml_doc = xmlParseMemory (read_buf, size);
	g_free (read_buf);
	if (xml_doc != NULL)
	{
		xmlNodePtr xml_root;
		
		xml_root = xmlDocGetRootElement(xml_doc);
		if (xml_root &&
			(xml_root->name) &&
			xmlStrEqual(xml_root->name, (const xmlChar *)"anjuta"))
		{
			xmlNodePtr xml_node;

			parse_error = FALSE;
			for (xml_node = xml_root->xmlChildrenNode; xml_node; xml_node = xml_node->next)
			{
				GList *groups = NULL;
				GList *attribs = NULL;
				GList *values = NULL;
				xmlChar *name, *url, *mandatory_text;
				xmlNodePtr xml_require_node;
				gboolean mandatory;

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
					parse_error = TRUE;
					break;
				}
				if (!url)
					url = xmlCharStrdup ("http://anjuta.org/plugins/");
									 
				/* Check if the plugin is mandatory */
				mandatory_text = xmlGetProp (xml_node, (const xmlChar*)"mandatory");
				mandatory = mandatory_text && (xmlStrcasecmp (mandatory_text, (const xmlChar *)"yes") == 0);
				xmlFree(mandatory_text);
										 
				/* For all plugin attribute conditions */
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

				if (!parse_error)
				{
					if (g_list_length (groups) == 0)
					{
						parse_error = TRUE;
						g_warning ("XML Error: No attributes to match given");
					}
					else
					{
						GList *plugin_handles;
						
						plugin_handles =
							anjuta_plugin_manager_list_query (profile->priv->plugin_manager,
															  groups,
															  attribs,
															  values);
						if (plugin_handles)
						{
							handles_list = g_list_prepend (handles_list, plugin_handles);
						}
						else if (mandatory)
						{
							not_found_names = g_list_prepend (not_found_names, g_strdup ((const gchar *)name));
							not_found_urls = g_list_prepend (not_found_urls, g_strdup ((const gchar *)url));
						}
					}
				}
				g_list_foreach (groups, (GFunc)xmlFree, NULL);
				g_list_foreach (attribs, (GFunc)xmlFree, NULL);
				g_list_foreach (values, (GFunc)xmlFree, NULL);
				g_list_free (groups);
				g_list_free (attribs);
				g_list_free (values);
				xmlFree (name);
				xmlFree (url);
			}
		}
		xmlFreeDoc(xml_doc);
	}
	
	if (parse_error)
	{
		/* Error during parsing */
		gchar *uri = g_file_get_uri (file);

		g_set_error (error, ANJUTA_PROFILE_ERROR,
					 ANJUTA_PROFILE_ERROR_URI_READ_FAILED,
					 _("Failed to read '%s': XML parse error. "
					   "Invalid or corrupted Anjuta plugins profile."),
			 		uri);
		g_free (uri);
		
		g_list_foreach (handles_list, (GFunc)g_list_free, NULL);
		g_list_free (handles_list);
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
		gchar *uri = g_file_get_uri (file);
			
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
					 ANJUTA_PROFILE_ERROR_URI_READ_FAILED,
					 _("Failed to read '%s': Following mandatory plugins are missing:\n%s"),
					 uri, mesg->str);
		g_free (uri);
		g_string_free (mesg, TRUE);
		
		g_list_foreach (handles_list, (GFunc)g_list_free, NULL);
		g_list_free (handles_list);
		handles_list = NULL;
	}
	g_list_foreach (not_found_names, (GFunc)g_free, NULL);
	g_list_free (not_found_names);
	g_list_foreach (not_found_urls, (GFunc)g_free, NULL);
	g_list_free (not_found_urls);

	return handles_list;
}

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
	GList *handles_list = NULL;
	
	g_return_val_if_fail (ANJUTA_IS_PROFILE (profile), FALSE);
	
	priv = profile->priv;
	handles_list = anjuta_profile_read_plugins_from_xml (profile, profile_xml_file, error);
	
	if (handles_list)
	{
		GList *selected_plugins = NULL;
		GList *node;
		
		/* Now everything okay. Select the plugins */
		handles_list = g_list_reverse (handles_list);
		selected_plugins =
			anjuta_profile_select_plugins (profile, handles_list);
		g_list_foreach (handles_list, (GFunc)g_list_free, NULL);
		g_list_free (handles_list);
		
		node = selected_plugins;
		while (node)
		{
			anjuta_plugin_handle_set_core_plugin ((AnjutaPluginHandle *)node->data, core_plugin);
			if (exclude_from_sync)
			{
				g_hash_table_insert (priv->plugins_to_exclude_from_sync,
									 node->data, node->data);
			}
			
			/* Insure no duplicate plugins are added */
			if (g_hash_table_lookup (priv->plugins_hash, node->data) == NULL)
			{
				priv->plugins = g_list_append (priv->plugins, node->data);
				g_hash_table_insert (priv->plugins_hash,
									 node->data, node->data);
			}
			node = g_list_next (node);
		}
		g_list_free (selected_plugins);
	}
	
	return handles_list != NULL;
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
	for (node = priv->plugins; node != NULL; node = g_list_next (node))
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

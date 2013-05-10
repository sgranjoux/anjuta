/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * anjuta-plugin-manager.c
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
 * SECTION:anjuta-plugin-manager
 * @short_description: Plugins management and activation
 * @see_also: #AnjutaPlugin, #AnjutaProfileManager
 * @stability: Unstable
 * @include: libanjuta/anjuta-plugin-manager.h
 * 
 */
 
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#include <libanjuta/anjuta-plugin-manager.h>
#include <libanjuta/anjuta-marshal.h>
#include <libanjuta/anjuta-debug.h>
#include <libanjuta/anjuta-plugin-handle.h>
#include <libanjuta/anjuta-plugin.h>
#include <libanjuta/anjuta-c-plugin-factory.h>
#include <libanjuta/interfaces/ianjuta-plugin-factory.h>
#include <libanjuta/interfaces/ianjuta-preferences.h>


enum
{
	PROP_0,

	PROP_SHELL,
	PROP_STATUS,
	PROP_PROFILES,
	PROP_AVAILABLE_PLUGINS,
	PROP_ACTIVATED_PLUGINS
};

enum
{
	PROFILE_PUSHED,
	PROFILE_POPPED,
	PLUGINS_TO_LOAD,
	PLUGINS_TO_UNLOAD,
	PLUGIN_ACTIVATED,
	PLUGIN_DEACTIVATED,

	LAST_SIGNAL
};

struct _AnjutaPluginManagerPriv
{
	GObject      *shell;
	AnjutaStatus *status;
	GList        *plugin_dirs;
	GList        *available_plugins;
	
	/* Indexes => plugin handles */
	GHashTable   *plugins_by_interfaces;
	GHashTable   *plugins_by_name;
	GHashTable   *plugins_by_description;
	
	/* Plugins that are currently activated */
	GHashTable   *activated_plugins;
	
	/* Plugins that have been previously loaded but current deactivated */
	GHashTable   *plugins_cache;
	
	/* Remember plugin selection */
	GHashTable   *remember_plugins;
	
	/* disable plugins */
	GHashTable   *disable_plugins;
};

/* Available plugins page treeview */
enum {
	COL_ACTIVABLE,
	COL_ENABLED,
	COL_ICON,
	COL_NAME,
	COL_PLUGIN,
	N_COLS
};

/* Remembered plugins page treeview */
enum {
	COL_REM_ICON,
	COL_REM_NAME,
	COL_REM_PLUGIN_KEY,
	N_REM_COLS
};

/* Plugin class types */

static AnjutaCPluginFactory *anjuta_plugin_factory = NULL;

static GObjectClass* parent_class = NULL;
static guint plugin_manager_signals[LAST_SIGNAL] = { 0 };

static void plugin_set_update (AnjutaPluginManager *plugin_manager,
                               AnjutaPluginHandle* selected_plugin,
                               gboolean load);

static IAnjutaPluginFactory* get_plugin_factory (AnjutaPluginManager *plugin_manager,
                                                 const gchar *language, GError **error);

GQuark 
anjuta_plugin_manager_error_quark (void)
{
	static GQuark quark = 0;
	
	if (quark == 0) {
		quark = g_quark_from_static_string ("anjuta-plugin-manager-quark");
	}
	return quark;
}

/** Dependency Resolution **/

static gboolean
collect_cycle (AnjutaPluginManager *plugin_manager,
			   AnjutaPluginHandle *base_plugin, AnjutaPluginHandle *cur_plugin, 
			   GList **cycle)
{
	AnjutaPluginManagerPriv *priv;
	GList *l;
	
	priv = plugin_manager->priv;
	
	for (l = anjuta_plugin_handle_get_dependency_names (cur_plugin);
		 l != NULL; l = l->next)
	{
		AnjutaPluginHandle *dep = g_hash_table_lookup (priv->plugins_by_name,
													   l->data);
		if (dep)
		{
			if (dep == base_plugin)
			{
				*cycle = g_list_prepend (NULL, dep);
				/* DEBUG_PRINT ("%s", anjuta_plugin_handle_get_name (dep)); */
				return TRUE;
			}
			else
			{
				if (collect_cycle (plugin_manager, base_plugin, dep, cycle))
				{
					*cycle = g_list_prepend (*cycle, dep);
					/* DEBUG_PRINT ("%s", anjuta_plugin_handle_get_name (dep)); */
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}

static void
add_dependency (AnjutaPluginHandle *dependent, AnjutaPluginHandle *dependency)
{
	g_hash_table_insert (anjuta_plugin_handle_get_dependents (dependency),
						 dependent, dependency);
	g_hash_table_insert (anjuta_plugin_handle_get_dependencies (dependent),
						 dependency, dependent);
}

static void
child_dep_foreach_cb (gpointer key, gpointer value, gpointer user_data)
{
	add_dependency (ANJUTA_PLUGIN_HANDLE (user_data),
					ANJUTA_PLUGIN_HANDLE (key));
}

/* Resolves dependencies for a single module recursively.  Shortcuts if 
 * the module has already been resolved.  Returns a list representing
 * any cycles found, or NULL if no cycles are found.  If a cycle is found,
 * the graph is left unresolved.
 */
static GList*
resolve_for_module (AnjutaPluginManager *plugin_manager,
					AnjutaPluginHandle *plugin, int pass)
{
	AnjutaPluginManagerPriv *priv;
	GList *l;
	GList *ret = NULL;

	priv = plugin_manager->priv;
	
	if (anjuta_plugin_handle_get_checked (plugin))
	{
		return NULL;
	}

	if (anjuta_plugin_handle_get_resolve_pass (plugin) == pass)
	{
		GList *cycle = NULL;
		g_warning ("cycle found: %s on pass %d",
				   anjuta_plugin_handle_get_name (plugin),
				   anjuta_plugin_handle_get_resolve_pass (plugin));
		collect_cycle (plugin_manager, plugin, plugin, &cycle);
		return cycle;
	}
	
	if (anjuta_plugin_handle_get_resolve_pass (plugin) != -1)
	{
		return NULL;
	}	

	anjuta_plugin_handle_set_can_load (plugin, TRUE);
	anjuta_plugin_handle_set_resolve_pass (plugin, pass);
		
	for (l = anjuta_plugin_handle_get_dependency_names (plugin);
		 l != NULL; l = l->next)
	{
		char *dep = l->data;
		AnjutaPluginHandle *child = 
			g_hash_table_lookup (priv->plugins_by_name, dep);
		if (child)
		{
			ret = resolve_for_module (plugin_manager, child, pass);
			if (ret)
			{
				break;
			}
			
			/* Add the dependency's dense dependency list 
			 * to the current module's dense dependency list */
			g_hash_table_foreach (anjuta_plugin_handle_get_dependencies (child),
					      child_dep_foreach_cb, plugin);
			add_dependency (plugin, child);

			/* If the child can't load due to dependency problems,
			 * the current module can't either */
			anjuta_plugin_handle_set_can_load (plugin,
					anjuta_plugin_handle_get_can_load (child));
		} else {
			g_warning ("Dependency %s not found.\n", dep);
			anjuta_plugin_handle_set_can_load (plugin, FALSE);
			ret = NULL;
		}
	}
	anjuta_plugin_handle_set_checked (plugin, TRUE);
	
	return ret;
}

/* Clean up the results of a resolving run */
static void
unresolve_dependencies (AnjutaPluginManager *plugin_manager)
{
	AnjutaPluginManagerPriv *priv;
	GList *l;
	
	priv = plugin_manager->priv;
	
	for (l = priv->available_plugins; l != NULL; l = l->next)
	{
		AnjutaPluginHandle *plugin = l->data;
		anjuta_plugin_handle_unresolve_dependencies (plugin);
	}	
}

/* done upto here */

static void
prune_modules (AnjutaPluginManager *plugin_manager, GList *modules)
{
	AnjutaPluginManagerPriv *priv;
	GList *l;
	
	priv = plugin_manager->priv;
	
	for (l = modules; l != NULL; l = l->next) {
		AnjutaPluginHandle *plugin = l->data;
	
		g_hash_table_remove (priv->plugins_by_name,
							 anjuta_plugin_handle_get_id (plugin));
		priv->available_plugins = g_list_remove (priv->available_plugins, plugin);
	}
}

static int
dependency_compare (AnjutaPluginHandle *plugin_a,
					AnjutaPluginHandle *plugin_b)
{
	int a = g_hash_table_size (anjuta_plugin_handle_get_dependencies (plugin_a));
	int b = g_hash_table_size (anjuta_plugin_handle_get_dependencies (plugin_b));
	
	return a - b;
}

/* Resolves the dependencies of the priv->available_plugins list.  When this
 * function is complete, the following will be true: 
 *
 * 1) The dependencies and dependents hash tables of the modules will
 * be filled.
 * 
 * 2) Cycles in the graph will be removed.
 * 
 * 3) Modules which cannot be loaded due to failed dependencies will
 * be marked as such.
 *
 * 4) priv->available_plugins will be sorted such that no module depends on a
 * module after it.
 *
 * If a cycle in the graph is found, it is pruned from the tree and 
 * returned as a list stored in the cycles list.
 */
static void 
resolve_dependencies (AnjutaPluginManager *plugin_manager, GList **cycles)
{
	AnjutaPluginManagerPriv *priv;
	GList *cycle = NULL;
	GList *l;
	
	priv = plugin_manager->priv;
	*cycles = NULL;
	
	/* Try resolving dependencies.  If there is a cycle, prune the
	 * cycle and try to resolve again */
	do
	{
		int pass = 1;
		cycle = NULL;
		for (l = priv->available_plugins; l != NULL && !cycle; l = l->next) {
			cycle = resolve_for_module (plugin_manager, l->data, pass++);
			cycle = NULL;
		}
		if (cycle) {
			*cycles = g_list_prepend (*cycles, cycle);
			prune_modules (plugin_manager, cycle);
			unresolve_dependencies (plugin_manager);
		}
	} while (cycle);

	/* Now that there is a fully resolved dependency tree, sort
	 * priv->available_plugins to create a valid load order */
	priv->available_plugins = g_list_sort (priv->available_plugins, 
										   (GCompareFunc)dependency_compare);
}

/* Plugins loading */

static gboolean
str_has_suffix (const char *haystack, const char *needle)
{
	const char *h, *n;

	if (needle == NULL) {
		return TRUE;
	}
	if (haystack == NULL) {
		return needle[0] == '\0';
	}
		
	/* Eat one character at a time. */
	h = haystack + strlen(haystack);
	n = needle + strlen(needle);
	do {
		if (n == needle) {
			return TRUE;
		}
		if (h == haystack) {
			return FALSE;
		}
	} while (*--h == *--n);
	return FALSE;
}

static void
load_plugin (AnjutaPluginManager *plugin_manager,
			 const gchar *plugin_desc_path)
{
	AnjutaPluginManagerPriv *priv;
	AnjutaPluginHandle *plugin_handle;
	
	g_return_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager));
	priv = plugin_manager->priv;
	
	plugin_handle = anjuta_plugin_handle_new (plugin_desc_path);
	if (plugin_handle)
	{
		if (g_hash_table_lookup (priv->plugins_by_name,
								 anjuta_plugin_handle_get_id (plugin_handle)))
		{
			g_object_unref (plugin_handle);
		}
		else
		{
			GList *node;
			/* Available plugin */
			priv->available_plugins = g_list_prepend (priv->available_plugins,
													  plugin_handle);
			/* Index by id */
			g_hash_table_insert (priv->plugins_by_name,
								 (gchar *)anjuta_plugin_handle_get_id (plugin_handle),
								 plugin_handle);
			
			/* Index by description */
			g_hash_table_insert (priv->plugins_by_description,
								 anjuta_plugin_handle_get_description (plugin_handle),
								 plugin_handle);
			
			/* Index by interfaces exported by this plugin */
			node = anjuta_plugin_handle_get_interfaces (plugin_handle);
			while (node)
			{
				GList *objs;
				gchar *iface;
				GList *obj_node;
				gboolean found;
				
				iface = node->data;
				objs = (GList*)g_hash_table_lookup (priv->plugins_by_interfaces, iface);
				
				obj_node = objs;
				found = FALSE;
				while (obj_node)
				{
					if (obj_node->data == plugin_handle)
					{
						found = TRUE;
						break;
					}
					obj_node = g_list_next (obj_node);
				}
				if (!found)
				{
					g_hash_table_steal (priv->plugins_by_interfaces, iface);
					objs = g_list_prepend (objs, plugin_handle);
					g_hash_table_insert (priv->plugins_by_interfaces, iface, objs);
				}
				node = g_list_next (node);
			}
		}
	}
	return;
}

static void
load_plugins_from_directory (AnjutaPluginManager* plugin_manager,
							 const gchar *dirname)
{
	DIR *dir;
	struct dirent *entry;
	
	dir = opendir (dirname);
	
	if (!dir)
	{
		return;
	}
	
	for (entry = readdir (dir); entry != NULL; entry = readdir (dir))
	{
		if (str_has_suffix (entry->d_name, ".plugin"))
		{
			gchar *pathname;
			pathname = g_strdup_printf ("%s/%s", dirname, entry->d_name);
			load_plugin (plugin_manager,pathname);
			g_free (pathname);
		}
	}
	closedir (dir);
}

/* Plugin activation and deactivation */

static void
on_plugin_activated (AnjutaPlugin *plugin_object, AnjutaPluginHandle *plugin)
{
	AnjutaPluginManager *plugin_manager;
	AnjutaPluginManagerPriv *priv;
	
	/* FIXME: Pass plugin_manager directly in signal arguments */
	plugin_manager = anjuta_shell_get_plugin_manager (plugin_object->shell, NULL);
	
	g_return_if_fail(plugin_manager != NULL);
	
	priv = plugin_manager->priv;

	g_hash_table_insert (priv->activated_plugins, plugin,
						 g_object_ref (plugin_object));
	g_hash_table_remove (priv->plugins_cache, plugin);
	
	g_signal_emit_by_name (plugin_manager, "plugin-activated",
						   plugin,
						   plugin_object);
}

static void
on_plugin_deactivated (AnjutaPlugin *plugin_object, AnjutaPluginHandle *plugin)
{
	AnjutaPluginManager *plugin_manager;
	AnjutaPluginManagerPriv *priv;
	
	/* FIXME: Pass plugin_manager directly in signal arguments */
	plugin_manager = anjuta_shell_get_plugin_manager (plugin_object->shell, NULL);
	
	g_return_if_fail (plugin_manager != NULL);
	
	priv = plugin_manager->priv;
	
	g_hash_table_insert (priv->plugins_cache, plugin, g_object_ref (plugin_object));
	g_hash_table_remove (priv->activated_plugins, plugin);
	
	g_signal_emit_by_name (plugin_manager, "plugin-deactivated",
						   plugin,
						   plugin_object);
}

static AnjutaPlugin*
activate_plugin (AnjutaPluginManager *plugin_manager,
				 AnjutaPluginHandle *handle, GError **error)
{
	AnjutaPluginManagerPriv *priv;
	IAnjutaPluginFactory* factory;
	AnjutaPlugin *plugin;
	const gchar *language;
	
	priv = plugin_manager->priv;

	language = anjuta_plugin_handle_get_language (handle);
	
	factory = get_plugin_factory (plugin_manager, language, error);
	if (factory == NULL) return NULL;
	
	plugin = ianjuta_plugin_factory_new_plugin (factory, handle, ANJUTA_SHELL (priv->shell), error);
	
	if (plugin == NULL)
	{
		return NULL;
	}
	g_signal_connect (plugin, "activated",
					  G_CALLBACK (on_plugin_activated), handle);
	g_signal_connect (plugin, "deactivated",
					  G_CALLBACK (on_plugin_deactivated), handle);
	
	return plugin;
}

/**
 * anjuta_plugin_manager_unload_all_plugins:
 * @plugin_manager: A #AnjutaPluginManager object
 * 
 * Unload all plugins. Do not take care of the dependencies because all plugins
 * are unloaded anyway.
 */
void
anjuta_plugin_manager_unload_all_plugins (AnjutaPluginManager *plugin_manager)
{
	AnjutaPluginManagerPriv *priv;
	
	priv = plugin_manager->priv;
	if (g_hash_table_size (priv->activated_plugins) > 0 ||
		g_hash_table_size (priv->plugins_cache) > 0)
	{
		if (g_hash_table_size (priv->activated_plugins) > 0)
		{
			GList *node;
			for (node = g_list_last (priv->available_plugins); node; node = g_list_previous (node))
			{
				AnjutaPluginHandle *selected_plugin = node->data;
				AnjutaPlugin *plugin;

				plugin = g_hash_table_lookup (priv->activated_plugins, selected_plugin);
				if (plugin)
				{
					DEBUG_PRINT ("Deactivating plugin: %s",
					             anjuta_plugin_handle_get_id (selected_plugin));
					anjuta_plugin_deactivate (plugin);
				}
			}
			g_hash_table_remove_all (priv->activated_plugins);
		}
		if (g_hash_table_size (priv->plugins_cache) > 0)
		{
			GList *node;

			for (node = g_list_last (priv->available_plugins); node; node = g_list_previous (node))
			{
				AnjutaPluginHandle *selected_plugin = node->data;

				g_hash_table_remove (priv->plugins_cache, selected_plugin);
			}
			g_hash_table_remove_all (priv->plugins_cache);
		}
	}
}

/* Return true if plugin should be unloaded when plugin_to_unloaded is unloaded.
 * It can be because plugin is or need plugin_to_unload. */
static gboolean 
should_unload (GHashTable *activated_plugins, AnjutaPluginHandle *plugin_to_unload,
			   AnjutaPluginHandle *plugin)
{
	GObject *plugin_obj = g_hash_table_lookup (activated_plugins, plugin);
	
	if (!plugin_obj)
		return FALSE;
	
	if (plugin_to_unload == plugin)
		return TRUE;
	
	gboolean dependent = 
		GPOINTER_TO_INT (g_hash_table_lookup (anjuta_plugin_handle_get_dependents (plugin_to_unload),
											  plugin));
	return dependent;
}

/* Return true if plugin should be loaded when plugin_to_loaded is loaded.
 * It can be because plugin_to_load is or need plugin. */
static gboolean 
should_load (GHashTable *activated_plugins, AnjutaPluginHandle *plugin_to_load,
			 AnjutaPluginHandle *plugin)
{
	GObject *plugin_obj = g_hash_table_lookup (activated_plugins, plugin);
	
	if (plugin_obj)
		return FALSE;
	
	if (plugin_to_load == plugin)
		return anjuta_plugin_handle_get_can_load (plugin);
	
	gboolean dependency = 
		GPOINTER_TO_INT (g_hash_table_lookup (anjuta_plugin_handle_get_dependencies (plugin_to_load),
						 					  plugin));
	return (dependency && anjuta_plugin_handle_get_can_load (plugin));
}

static AnjutaPluginHandle *
plugin_for_iter (GtkListStore *store, GtkTreeIter *iter)
{
	AnjutaPluginHandle *plugin;
	
	gtk_tree_model_get (GTK_TREE_MODEL (store), iter, COL_PLUGIN, &plugin, -1);
	return plugin;
}

static void
update_enabled (GtkTreeModel *model, GHashTable *activated_plugins)
{
	GtkTreeIter iter;
	
	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			AnjutaPluginHandle *plugin;
			GObject *plugin_obj;
			gboolean installed;
			
			plugin = plugin_for_iter(GTK_LIST_STORE(model), &iter);
			plugin_obj = g_hash_table_lookup (activated_plugins, plugin);
			installed = (plugin_obj != NULL) ? TRUE : FALSE;
			gtk_tree_model_get (model, &iter, COL_PLUGIN, &plugin, -1);
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
								COL_ENABLED, installed, -1);
		} while (gtk_tree_model_iter_next (model, &iter));
	}
}

static void
plugin_set_update (AnjutaPluginManager *plugin_manager,
				 AnjutaPluginHandle* selected_plugin,
				 gboolean load)
{
	AnjutaPluginManagerPriv *priv;
	gboolean loaded;
	GList *l;
	
	priv = plugin_manager->priv;

	/* Plugins can be loaded or unloaded implicitely because they need or are
	 * needed by another plugin so it is possible that we try to load or unload
	 * respectively an already loaded or already unloaded plugin. */
	loaded = g_hash_table_lookup (priv->activated_plugins, selected_plugin) != NULL;
	if ((load && loaded) || (!load && !loaded)) return;
	
	if (priv->status)
		anjuta_status_busy_push (priv->status);
	
	if (!load)
	{
		/* visit priv->available_plugins in reverse order when unloading, so
		 * that plugins are unloaded in the right order */
		for (l = g_list_last(priv->available_plugins); l != NULL; l = l->prev)
		{
			AnjutaPluginHandle *plugin = l->data;
			if (should_unload (priv->activated_plugins, selected_plugin, plugin))
			{
				AnjutaPlugin *plugin_obj = ANJUTA_PLUGIN (g_hash_table_lookup (priv->activated_plugins, plugin));
				if (!anjuta_plugin_deactivate (plugin_obj))
				{
					anjuta_util_dialog_info (GTK_WINDOW (priv->shell),
								 dgettext (GETTEXT_PACKAGE, "Plugin '%s' does not want to be deactivated"),
								 anjuta_plugin_handle_get_name (plugin));
				}
			}
		}
	}
	else
	{
		for (l = priv->available_plugins; l != NULL; l = l->next)
		{
			AnjutaPluginHandle *plugin = l->data;
			if (should_load (priv->activated_plugins, selected_plugin, plugin))
			{
				AnjutaPlugin *plugin_obj;
				GError *error = NULL;
				plugin_obj = g_hash_table_lookup (priv->plugins_cache, plugin);
				if (plugin_obj)
					g_object_ref (plugin_obj);
				else
				{
					plugin_obj = activate_plugin (plugin_manager, plugin,
												  &error);
				}
				
				if (plugin_obj)
				{
					anjuta_plugin_activate (ANJUTA_PLUGIN (plugin_obj));
					g_object_unref (plugin_obj);
				}
				else
				{
					if (error)
					{
						gchar* message = g_strdup_printf (dgettext (GETTEXT_PACKAGE, "Could not load %s\n"
							"This usually means that your installation is corrupted. The "
							"error message leading to this was:\n%s"), 
														  anjuta_plugin_handle_get_name (selected_plugin),
														  error->message);
						anjuta_util_dialog_error (GTK_WINDOW(plugin_manager->priv->shell),
												  message);
						g_error_free (error);
						g_free(message);
					}
				}
			}
		}
	}
	if (priv->status)
		anjuta_status_busy_pop (priv->status);

	return;
}

static void
plugin_toggled (GtkCellRendererToggle *cell, char *path_str, gpointer data)
{
	AnjutaPluginManager *plugin_manager;
	AnjutaPluginManagerPriv *priv;
	GtkListStore *store = GTK_LIST_STORE (data);
	GtkTreeIter iter;
	GtkTreePath *path;
	AnjutaPluginHandle *plugin;
	gboolean enabled;
	GList *activated_plugins;
	GList *node;
	AnjutaPlugin* plugin_object;
	
	path = gtk_tree_path_new_from_string (path_str);
	
	plugin_manager = g_object_get_data (G_OBJECT (store), "plugin-manager");
	priv = plugin_manager->priv;
	
	gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path);
	gtk_tree_model_get (GTK_TREE_MODEL (store), &iter,
			    COL_ENABLED, &enabled,
			    COL_PLUGIN, &plugin,
			    -1);
	
	/* Activate one plugin can force the loading of other ones, instead of
	 * searching which plugins have to be activated, we just unmerge all 
	 * current plugins and merge all plugins after the modification */
	
	/* unmerge all plugins */
	activated_plugins = g_hash_table_get_values (priv->activated_plugins);
	for (node = g_list_first (activated_plugins); node != NULL; node = g_list_next (node))
	{
		plugin_object = (AnjutaPlugin *)node->data;
		if (plugin_object &&
			IANJUTA_IS_PREFERENCES(plugin_object))
		{
			ianjuta_preferences_unmerge (IANJUTA_PREFERENCES (plugin_object),
									   anjuta_shell_get_preferences (ANJUTA_SHELL (priv->shell), NULL),
									   NULL);
		}
	}
	g_list_free (activated_plugins);
	
	plugin_set_update (plugin_manager, plugin, !enabled);
	
	/* Make sure that it appears in the preferences. This method
		can only be called when the preferences dialog is active so
		it should be save
	*/
	activated_plugins = g_hash_table_get_values (priv->activated_plugins);
	for (node = g_list_first (activated_plugins); node != NULL; node = g_list_next (node))
	{
		plugin_object = (AnjutaPlugin *)node->data;
		if (plugin_object &&
			IANJUTA_IS_PREFERENCES(plugin_object))
		{
			ianjuta_preferences_merge (IANJUTA_PREFERENCES (plugin_object),
									   anjuta_shell_get_preferences (ANJUTA_SHELL (priv->shell), NULL),
									   NULL);
		}
	}
	g_list_free (activated_plugins);

	update_enabled (GTK_TREE_MODEL (store), priv->activated_plugins);
	gtk_tree_path_free (path);
}

#if 0
static void
selection_changed (GtkTreeSelection *selection, GtkListStore *store)
{
	GtkTreeIter iter;
	
	if (gtk_tree_selection_get_selected (selection, NULL,
					     &iter)) {
		GtkTextBuffer *buffer;
		
		GtkWidget *txt = g_object_get_data (G_OBJECT (store),
						    "AboutText");
		
		GtkWidget *image = g_object_get_data (G_OBJECT (store),
						      "Icon");
		AnjutaPluginHandle *plugin = plugin_for_iter (store, &iter);
		
		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (txt));
		gtk_text_buffer_set_text (buffer, plugin->about, -1);

		if (plugin->icon_path) {
			gtk_image_set_from_file (GTK_IMAGE (image), 
						 plugin->icon_path);
			gtk_widget_show (GTK_WIDGET (image));
		} else {
			gtk_widget_hide (GTK_WIDGET (image));
		}
	}
}
#endif

static GtkWidget *
create_plugin_tree (void)
{
	GtkListStore *store;
	GtkWidget *tree;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	store = gtk_list_store_new (N_COLS,
								G_TYPE_BOOLEAN,
								G_TYPE_BOOLEAN,
								GDK_TYPE_PIXBUF,
								G_TYPE_STRING,
								G_TYPE_POINTER);
	tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer), "toggled",
			  G_CALLBACK (plugin_toggled), store);
	column = gtk_tree_view_column_new_with_attributes (dgettext (GETTEXT_PACKAGE, "Load"),
													   renderer,
													   "active", 
													   COL_ENABLED,
													   "activatable",
													   COL_ACTIVABLE,
													   NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);
	gtk_tree_view_column_set_sizing (column,
									 GTK_TREE_VIEW_COLUMN_AUTOSIZE);

	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "pixbuf",
										COL_ICON);
	renderer = gtk_cell_renderer_text_new ();
	g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup",
										COL_NAME);
	gtk_tree_view_column_set_sizing (column,
									 GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_title (column, dgettext (GETTEXT_PACKAGE, "Available Plugins"));
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);
	gtk_tree_view_set_expander_column (GTK_TREE_VIEW (tree), column);

	g_object_unref (store);
	return tree;
}

/* Sort function for plugins */
static gint
sort_plugins(gconstpointer a, gconstpointer b)
{
	g_return_val_if_fail (a != NULL, 0);
	g_return_val_if_fail (b != NULL, 0);
	
	AnjutaPluginHandle* plugin_a = ANJUTA_PLUGIN_HANDLE (a);
	AnjutaPluginHandle* plugin_b = ANJUTA_PLUGIN_HANDLE (b);
	
	return strcmp (anjuta_plugin_handle_get_name (plugin_a),
				   anjuta_plugin_handle_get_name (plugin_b));
}

/* If show_all == FALSE, show only user activatable plugins
 * If show_all == TRUE, show all plugins
 */
static void
populate_plugin_model (AnjutaPluginManager *plugin_manager,
					   GtkListStore *store,
					   GHashTable *plugins_to_show,
					   GHashTable *activated_plugins,
					   gboolean show_all)
{
	AnjutaPluginManagerPriv *priv;
	GList *sorted_plugins, *l;
	
	priv = plugin_manager->priv;
	gtk_list_store_clear (store);

	sorted_plugins = g_list_copy (priv->available_plugins);
	sorted_plugins = g_list_sort (sorted_plugins, sort_plugins);
	
	for (l = sorted_plugins; l != NULL; l = l->next)
	{
		AnjutaPluginHandle *plugin = l->data;
		
		/* If plugins to show is NULL, show all available plugins */
		if (plugins_to_show == NULL ||
			g_hash_table_lookup (plugins_to_show, plugin))
		{
			
			gboolean enable = FALSE;
			if (g_hash_table_lookup (activated_plugins, plugin))
				enable = TRUE;
			
			if (anjuta_plugin_handle_get_name (plugin) &&
			    anjuta_plugin_handle_get_description (plugin) &&
			    (anjuta_plugin_handle_get_user_activatable (plugin) ||
			     show_all) &&
			    (g_hash_table_lookup (plugin_manager->priv->disable_plugins, plugin) == NULL))
			{
				GtkTreeIter iter;
				gchar *text;
				
				text = g_markup_printf_escaped ("<span size=\"larger\" weight=\"bold\">%s</span>\n%s",
												anjuta_plugin_handle_get_name (plugin),
												anjuta_plugin_handle_get_about (plugin));

				gtk_list_store_append (store, &iter);
				gtk_list_store_set (store, &iter,
									COL_ACTIVABLE,
									anjuta_plugin_handle_get_user_activatable (plugin),
									COL_ENABLED, enable,
									COL_NAME, text,
									COL_PLUGIN, plugin,
									-1);
				if (anjuta_plugin_handle_get_icon_path (plugin))
				{
					GdkPixbuf *icon;
					icon = gdk_pixbuf_new_from_file_at_size (anjuta_plugin_handle_get_icon_path (plugin),
															 32, 32, NULL);
					if (icon) {
						gtk_list_store_set (store, &iter,
											COL_ICON, icon, -1);
						g_object_unref (icon);
					}
				}
				g_free (text);
			}
		}
	}

	g_list_free (sorted_plugins);
}

static GtkWidget *
create_remembered_plugins_tree (void)
{
	GtkListStore *store;
	GtkWidget *tree;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	store = gtk_list_store_new (N_REM_COLS, GDK_TYPE_PIXBUF, G_TYPE_STRING,
								G_TYPE_STRING);
	tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "pixbuf",
										COL_REM_ICON);
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup",
										COL_REM_NAME);
	gtk_tree_view_column_set_sizing (column,
									 GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_title (column, dgettext (GETTEXT_PACKAGE, "Preferred plugins"));
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);
	gtk_tree_view_set_expander_column (GTK_TREE_VIEW (tree), column);
	
	g_object_unref (store);
	return tree;
}

static void
foreach_remembered_plugin (gpointer key, gpointer value, gpointer user_data)
{
	AnjutaPluginHandle *handle = (AnjutaPluginHandle *) value;
	GtkListStore *store = GTK_LIST_STORE (user_data);
	AnjutaPluginManager *manager = g_object_get_data (G_OBJECT (store),
													  "plugin-manager");
	
	if (anjuta_plugin_handle_get_name (handle) &&
		anjuta_plugin_handle_get_description (handle))
	{
		GtkTreeIter iter;
		gchar *text;
		
		text = g_markup_printf_escaped ("<span size=\"larger\" weight=\"bold\">%s</span>\n%s",
										anjuta_plugin_handle_get_name (handle),
										anjuta_plugin_handle_get_about (handle));

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
							COL_REM_NAME, text,
							COL_REM_PLUGIN_KEY, key,
							-1);
		if (anjuta_plugin_handle_get_icon_path (handle))
		{
			GdkPixbuf *icon;
			icon = gdk_pixbuf_new_from_file_at_size (anjuta_plugin_handle_get_icon_path (handle),
													 32, 32, NULL);
			if (icon) {
				gtk_list_store_set (store, &iter,
									COL_REM_ICON, icon, -1);
				g_object_unref (icon);
			}
		}
		g_free (text);
	}
}

static void
populate_remembered_plugins_model (AnjutaPluginManager *plugin_manager,
								   GtkListStore *store)
{
	AnjutaPluginManagerPriv *priv = plugin_manager->priv;
	gtk_list_store_clear (store);
	g_hash_table_foreach (priv->remember_plugins, foreach_remembered_plugin,
						  store);
}

static void
on_show_all_plugins_toggled (GtkToggleButton *button, GtkListStore *store)
{
	AnjutaPluginManager *plugin_manager;
	
	plugin_manager = g_object_get_data (G_OBJECT (button), "__plugin_manager");
	
	populate_plugin_model (plugin_manager, store, NULL,
						   plugin_manager->priv->activated_plugins,
						   !gtk_toggle_button_get_active (button));
}

static void
on_forget_plugin_clicked (GtkWidget *button, GtkTreeView *view)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (view);
	if (gtk_tree_selection_get_selected (selection, &model, &iter))
	{
		gchar *plugin_key;
		AnjutaPluginManager *manager = g_object_get_data (G_OBJECT (model),
														  "plugin-manager");
		gtk_tree_model_get (model, &iter, COL_REM_PLUGIN_KEY, &plugin_key, -1);
		g_hash_table_remove (manager->priv->remember_plugins, plugin_key);
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
		g_free (plugin_key);
	}
}

static void
on_forget_plugin_sel_changed (GtkTreeSelection *selection,
							  GtkWidget *button)
{
	GtkTreeIter iter;
	
	if (gtk_tree_selection_get_selected (selection, NULL, &iter))
		gtk_widget_set_sensitive (button, TRUE);
	else
		gtk_widget_set_sensitive (button, FALSE);
}

GtkWidget *
anjuta_plugin_manager_get_plugins_page (AnjutaPluginManager *plugin_manager)
{
	GtkWidget *vbox;
	GtkWidget *checkbutton;
	GtkWidget *tree;
	GtkWidget *scrolled;
	GtkWidget *toolbar;
	GtkToolItem *toolitem;
	GtkListStore *store;
	
	/* Plugins page */
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
									     GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
									GTK_POLICY_NEVER,
									GTK_POLICY_AUTOMATIC);
	gtk_style_context_set_junction_sides (gtk_widget_get_style_context (scrolled), GTK_JUNCTION_BOTTOM);
	gtk_box_pack_start (GTK_BOX (vbox), scrolled, TRUE, TRUE, 0);

	toolbar = gtk_toolbar_new ();
	gtk_style_context_add_class (gtk_widget_get_style_context (toolbar), GTK_STYLE_CLASS_INLINE_TOOLBAR);
	gtk_style_context_set_junction_sides (gtk_widget_get_style_context (toolbar), GTK_JUNCTION_TOP);
	gtk_box_pack_start (GTK_BOX (vbox), toolbar, FALSE, FALSE, 0);
	gtk_widget_show (toolbar);

	toolitem = gtk_tool_item_new ();
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), GTK_TOOL_ITEM (toolitem), 0);
	gtk_widget_show (GTK_WIDGET(toolitem));

	checkbutton = gtk_check_button_new_with_label (dgettext (GETTEXT_PACKAGE, "Only show user activatable plugins"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbutton), TRUE);
	gtk_container_add (GTK_CONTAINER (toolitem), checkbutton);
	
	tree = create_plugin_tree ();
	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (tree), TRUE);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tree), FALSE);
	store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (tree)));

	populate_plugin_model (plugin_manager, store, NULL,
						   plugin_manager->priv->activated_plugins, FALSE);
	
	gtk_container_add (GTK_CONTAINER (scrolled), tree);
	g_object_set_data (G_OBJECT (store), "plugin-manager", plugin_manager);

	
	g_object_set_data (G_OBJECT (checkbutton), "__plugin_manager", plugin_manager);
	g_signal_connect (G_OBJECT (checkbutton), "toggled",
					  G_CALLBACK (on_show_all_plugins_toggled),
					  store);
	gtk_widget_show_all (vbox);
	return vbox;
}

GtkWidget *
anjuta_plugin_manager_get_remembered_plugins_page (AnjutaPluginManager *plugin_manager)
{
	GtkWidget *vbox;
	GtkWidget *tree;
	GtkWidget *scrolled;
	GtkListStore *store;
	GtkWidget *hbox;
	GtkWidget *display_label;
	GtkWidget *forget_button;
	GtkTreeSelection *selection;

	/* Remembered plugin */
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);
	
	display_label = gtk_label_new (dgettext (GETTEXT_PACKAGE, "These are the plugins selected by you "
									 "when Anjuta prompted to choose one of "
									 "many suitable plugins. Removing the "
									 "preferred plugin will let Anjuta prompt "
									 "you again to choose different plugin."));
	gtk_label_set_line_wrap (GTK_LABEL (display_label), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox), display_label, FALSE, FALSE, 0);

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
									     GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
									GTK_POLICY_AUTOMATIC,
									GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start (GTK_BOX (vbox), scrolled, TRUE, TRUE, 0);
	
	tree = create_remembered_plugins_tree ();
	store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (tree)));

	gtk_container_add (GTK_CONTAINER (scrolled), tree);
	g_object_set_data (G_OBJECT (store), "plugin-manager", plugin_manager);
	populate_remembered_plugins_model (plugin_manager, store);
	
	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
	forget_button = gtk_button_new_with_label (dgettext (GETTEXT_PACKAGE, "Forget selected plugin"));
	gtk_widget_set_sensitive (forget_button, FALSE);
	gtk_box_pack_end (GTK_BOX (hbox), forget_button, FALSE, FALSE, 0);
	
	g_signal_connect (forget_button, "clicked",
					  G_CALLBACK (on_forget_plugin_clicked),
					  tree);
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree));
	g_signal_connect (selection, "changed",
					  G_CALLBACK (on_forget_plugin_sel_changed),
					  forget_button);
	gtk_widget_show_all (vbox);
	return vbox;
}

static GList *
property_to_list (const char *value)
{
	GList *l = NULL;
	char **split_str;
	char **p;
	
	split_str = g_strsplit (value, ",", -1);
	for (p = split_str; *p != NULL; p++) {
		l = g_list_prepend (l, g_strdup (g_strstrip (*p)));
	}
	g_strfreev (split_str);
	return l;
}

static IAnjutaPluginFactory*
get_plugin_factory (AnjutaPluginManager *plugin_manager,
								 	const gchar *language,
									GError **error)
{
	AnjutaPluginManagerPriv *priv;
	AnjutaPluginHandle *plugin;
	GList *loader_plugins, *node;
	GList *valid_plugins;
	GObject *obj = NULL;

	g_return_val_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager), G_TYPE_INVALID);

	
	if ((language == NULL) || (g_ascii_strcasecmp (language, "C") == 0))
	{
		/* Support of C plugin is built-in */
		return IANJUTA_PLUGIN_FACTORY (anjuta_plugin_factory);
	}
	
	priv = plugin_manager->priv;
	plugin = NULL;
		
	/* Find all plugins implementing the IAnjutaPluginLoader interface. */
	loader_plugins = g_hash_table_lookup (priv->plugins_by_interfaces, "IAnjutaPluginLoader");

	/* Create a list of loader supporting this language */
	node = loader_plugins;
	valid_plugins = NULL;
	while (node)
	{
		AnjutaPluginDescription *desc;
		gchar *val;
		GList *vals = NULL;
		GList *l_node;
		gboolean found;
		
		plugin = node->data;

		desc = anjuta_plugin_handle_get_description (plugin);
		if (anjuta_plugin_description_get_string (desc, "Plugin Loader", "SupportedLanguage", &val))		
		{
			if (val != NULL)
			{	
				vals = property_to_list (val);
				g_free (val);
			}
		}
		
		found = FALSE;
		l_node = vals;
		while (l_node)
		{
			if (!found && (g_ascii_strcasecmp (l_node->data, language) == 0))
			{
				found = TRUE;
			}
			g_free (l_node->data);
			l_node = g_list_next (l_node);
		}
		g_list_free (vals);

		if (found)
		{
			valid_plugins = g_list_prepend (valid_plugins, plugin);
		}
		
		node = g_list_next (node);
	}
	
	/* Find the first installed plugin from the valid plugins */
	node = valid_plugins;
	while (node)
	{
		plugin = node->data;
		obj = g_hash_table_lookup (priv->activated_plugins, plugin);
		if (obj) break;
		node = g_list_next (node);
	}

	/* If no plugin is installed yet, do something */
	if ((obj == NULL) && valid_plugins && g_list_length (valid_plugins) == 1)
	{
		/* If there is just one plugin, consider it selected */
		plugin = valid_plugins->data;
		
		/* Install and return it */
		plugin_set_update (plugin_manager, plugin, TRUE);
		obj = g_hash_table_lookup (priv->activated_plugins, plugin);
	}
	else if ((obj == NULL) && valid_plugins)
	{
		/* Prompt the user to select one of these plugins */

		GList *handles = NULL;
		node = valid_plugins;
		while (node)
		{
			plugin = node->data;
			handles = g_list_prepend (handles, plugin);
			node = g_list_next (node);
		}
		handles = g_list_reverse (handles);
		obj = anjuta_plugin_manager_select_and_activate (plugin_manager,
								  dgettext (GETTEXT_PACKAGE, "Select a plugin"),
								  dgettext (GETTEXT_PACKAGE, "Please select a plugin to activate"),
								  handles);
		g_list_free (handles);
	}
	g_list_free (valid_plugins);

	if (obj != NULL)
	{
		return IANJUTA_PLUGIN_FACTORY (obj);
	}
	
	/* No plugin implementing this interface found */
	g_set_error (error, ANJUTA_PLUGIN_MANAGER_ERROR,
					 ANJUTA_PLUGIN_MANAGER_MISSING_FACTORY,
					 dgettext (GETTEXT_PACKAGE, "No plugin is able to load other plugins in %s"), language);
	
	return NULL;
}

static void
on_is_active_plugins_foreach (gpointer key, gpointer data, gpointer user_data)
{
	AnjutaPluginHandle *handle = ANJUTA_PLUGIN_HANDLE (key);
	gchar const **search_iface = (gchar const **)user_data;
	
	if (*search_iface != NULL)
	{
		GList *interfaces;
		GList *found;
			
 		interfaces = anjuta_plugin_handle_get_interfaces (handle);
		
		for (found = g_list_first (interfaces); found != NULL; found = g_list_next (found))
		{
		}
		
		found = g_list_find_custom (interfaces, *search_iface, (GCompareFunc)strcmp);
		
		if (found != NULL) *search_iface = NULL;
	}
}

/**
 * anjuta_plugin_manager_is_active_plugin:
 * @plugin_manager: A #AnjutaPluginManager object
 * @iface_name: The interface implemented by the object to be found
 * 
 * Searches if a currently loaded plugins implements
 * the given interface.
 *
 * Return value: True is the plugin is currently loaded.
 */

gboolean
anjuta_plugin_manager_is_active_plugin (AnjutaPluginManager *plugin_manager,
								  const gchar *iface_name)
{
	const gchar *search_iface = iface_name;

	g_return_val_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager), FALSE);
	
	g_hash_table_foreach (plugin_manager->priv->activated_plugins,
						  on_is_active_plugins_foreach,
						  &search_iface);
	
	return search_iface == NULL;
}

/**
 * anjuta_plugin_manager_get_plugin:
 * @plugin_manager: A #AnjutaPluginManager object
 * @iface_name: The interface implemented by the object to be found
 * 
 * Searches the currently available plugins to find the one which
 * implements the given interface as primary interface and returns it. If
 * the plugin is not yet loaded, it will be loaded and activated.
 * It only searches
 * from the pool of plugin objects loaded in this shell and can only search
 * by primary interface. If there are more objects implementing this primary
 * interface, user might be prompted to select one from them (and might give
 * the option to use it as default for future queries). A typical usage of this
 * function is:
 * <programlisting>
 * GObject *docman =
 *     anjuta_plugin_manager_get_plugin (plugin_manager, "IAnjutaDocumentManager", error);
 * </programlisting>
 * Notice that this function takes the interface name string as string, unlike
 * anjuta_plugins_get_interface() which takes the type directly.
 * If no plugin implementing this interface can be found, returns NULL.
 *
 * Return value: The plugin object (subclass of #AnjutaPlugin) which implements
 * the given interface or NULL. See #AnjutaPlugin for more detail on interfaces
 * implemented by plugins.
 */
GObject *
anjuta_plugin_manager_get_plugin (AnjutaPluginManager *plugin_manager,
								  const gchar *iface_name)
{
	AnjutaPluginManagerPriv *priv;
	AnjutaPluginHandle *plugin;
	GList *valid_plugins, *node;
	
	g_return_val_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager), NULL);
	g_return_val_if_fail (iface_name != NULL, NULL);
	
	priv = plugin_manager->priv;
	plugin = NULL;
		
	/* Find all plugins implementing this (primary) interface. */
	valid_plugins = g_hash_table_lookup (priv->plugins_by_interfaces, iface_name);
	
	/* Find the first installed plugin from the valid plugins */
	node = valid_plugins;
	while (node)
	{
		GObject *obj;
		plugin = node->data;
		obj = g_hash_table_lookup (priv->activated_plugins, plugin);
		if (obj)
			return obj;
		node = g_list_next (node);
	}

	/* Filter disable plugins */
	valid_plugins = g_list_copy (valid_plugins);
	node = valid_plugins;
	while (node)
	{
		GList *next = g_list_next (node);
		
		if (g_hash_table_lookup (priv->disable_plugins, node->data) != NULL)
		{
			valid_plugins = g_list_delete_link (valid_plugins, node);
		}
		node = next;
	}
	
	/* If no plugin is installed yet, do something */
	if (valid_plugins &&
	    (g_list_length (valid_plugins) == 1))
	{
		/* If there is just one plugin, consider it selected */
		GObject *obj;
		plugin = valid_plugins->data;
		g_list_free (valid_plugins);
		
		/* Install and return it */
		plugin_set_update (plugin_manager, plugin, TRUE);
		obj = g_hash_table_lookup (priv->activated_plugins, plugin);
		
		return obj;
	}
	else if (valid_plugins)
	{
		/* Prompt the user to select one of these plugins */
		GObject *obj;
		obj = anjuta_plugin_manager_select_and_activate (plugin_manager,
									  dgettext (GETTEXT_PACKAGE, "Select a plugin"),
									  dgettext (GETTEXT_PACKAGE, "<b>Please select a plugin to activate</b>"),
									  valid_plugins);
		g_list_free (valid_plugins);
		return obj;
	}
	
	/* No plugin implementing this interface found */
	return NULL;
}

/**
 * anjuta_plugin_manager_get_plugin_by_handle:
 * @plugin_manager: A #AnjutaPluginManager object
 * @handle: A #AnjutaPluginHandle
 * 
 * Searches the currently available plugins to find the one with the
 * specified handle. If the plugin is not yet loaded, it will be loaded
 * and activated.
 *
 * Return value: The plugin object (subclass of #AnjutaPlugin)
 */
GObject *
anjuta_plugin_manager_get_plugin_by_handle (AnjutaPluginManager *plugin_manager,
                                            AnjutaPluginHandle *handle)
{
	AnjutaPluginManagerPriv *priv;
	GObject *obj;

	g_return_val_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager), NULL);
	g_return_val_if_fail (handle != NULL, NULL);

	priv = plugin_manager->priv;
	obj = g_hash_table_lookup (priv->activated_plugins, handle);
	if (obj == NULL)
	{
		plugin_set_update (plugin_manager, handle, TRUE);
		obj = g_hash_table_lookup (priv->activated_plugins, handle);
	}

	return obj;
}

static void
on_activated_plugins_foreach (gpointer key, gpointer data, gpointer user_data)
{
	AnjutaPluginHandle *plugin = ANJUTA_PLUGIN_HANDLE (key);
	GList **active_plugins = (GList **)user_data;
	*active_plugins = g_list_prepend (*active_plugins,
						plugin);
}

static void
on_activated_plugin_objects_foreach (gpointer key, gpointer data, gpointer user_data)
{
	GList **active_plugins = (GList **)user_data;
	*active_plugins = g_list_prepend (*active_plugins,
						data);
}

GList*
anjuta_plugin_manager_get_active_plugins (AnjutaPluginManager *plugin_manager)
{
	GList *active_plugins = NULL;

	g_return_val_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager), NULL);
	g_hash_table_foreach (plugin_manager->priv->activated_plugins,
						  on_activated_plugins_foreach,
						  &active_plugins);
	return g_list_reverse (active_plugins);
}

GList* 
anjuta_plugin_manager_get_active_plugin_objects (AnjutaPluginManager *plugin_manager)
{
	GList *active_plugins = NULL;
	
	g_return_val_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager), NULL);
	g_hash_table_foreach (plugin_manager->priv->activated_plugins,
						  on_activated_plugin_objects_foreach,
						  &active_plugins);
	return g_list_reverse (active_plugins);
}

/**
 * anjuta_plugin_manager_unload_plugin_by_handle:
 * @plugin_manager: A #AnjutaPluginManager object
 * @handle: A #AnjutaPluginHandle
 * 
 * Unload the plugin corresponding to the given handle. If the plugin is
 * already unloaded, nothing will be done.
 *
 * Return value: %TRUE is the plugin is unloaded. %FALSE if a corresponding 
 * plugin does not exist or if the plugin cannot be unloaded. 
 */
gboolean
anjuta_plugin_manager_unload_plugin_by_handle (AnjutaPluginManager *plugin_manager,
                                               AnjutaPluginHandle *handle)
{
	AnjutaPluginManagerPriv *priv;

	g_return_val_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager), FALSE);
	g_return_val_if_fail (handle != NULL, FALSE);

	priv = plugin_manager->priv;
	plugin_set_update (plugin_manager, handle, FALSE);

	/* Check if the plugin has been indeed unloaded */
	return g_hash_table_lookup (priv->activated_plugins, handle) == NULL;
}

static gboolean
find_plugin_for_object (gpointer key, gpointer value, gpointer data)
{
	if (value == data)
	{
		g_object_set_data (G_OBJECT (data), "__plugin_plugin", key);
		return TRUE;
	}
	return FALSE;
}

/**
 * anjuta_plugin_manager_unload_plugin:
 * @plugin_manager: A #AnjutaPluginManager object
 * @plugin_object: A #AnjutaPlugin object
 * 
 * Unload the corresponding plugin. The plugin has to be loaded. 
 *
 * Return value: %TRUE if the plugin has been unloaded. %FALSE if the plugin is
 * already or cannot be unloaded. 
 */
gboolean
anjuta_plugin_manager_unload_plugin (AnjutaPluginManager *plugin_manager,
									 GObject *plugin_object)
{
	AnjutaPluginManagerPriv *priv;
	AnjutaPluginHandle *plugin;
	
	g_return_val_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager), FALSE);
	g_return_val_if_fail (ANJUTA_IS_PLUGIN (plugin_object), FALSE);
	
	priv = plugin_manager->priv;
	
	plugin = NULL;

	/* Find the plugin that correspond to this plugin object */
	g_hash_table_find (priv->activated_plugins, find_plugin_for_object,
					   plugin_object);
	plugin = g_object_get_data (G_OBJECT (plugin_object), "__plugin_plugin");
	
	if (plugin)
	{
		plugin_set_update (plugin_manager, plugin, FALSE);
		
		/* Check if the plugin has been indeed unloaded */
		if (!g_hash_table_lookup (priv->activated_plugins, plugin))
			return TRUE;
		else
			return FALSE;
	}
	g_warning ("No plugin found with object \"%p\".", plugin_object);
	return FALSE;
}

GList*
anjuta_plugin_manager_list_query (AnjutaPluginManager *plugin_manager,
							 GList *secs,
							 GList *anames,
							 GList *avalues)
{
	AnjutaPluginManagerPriv *priv;
	GList *selected_plugins = NULL;
	const gchar *sec;
	const gchar *aname;
	const gchar *avalue;
	GList *available;
	
	g_return_val_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager), NULL);
	
	priv = plugin_manager->priv;
	available = priv->available_plugins;
	
	if (secs == NULL)
	{
		/* If no query is given, select all plugins */
		while (available)
		{
			AnjutaPluginHandle *plugin = available->data;
			if (g_hash_table_lookup (plugin_manager->priv->disable_plugins, plugin) == NULL)
				selected_plugins = g_list_prepend (selected_plugins, plugin);
			available = g_list_next (available);
		}
		return g_list_reverse (selected_plugins);
	}
	
	g_return_val_if_fail (secs != NULL, NULL);
	g_return_val_if_fail (anames != NULL, NULL);
	g_return_val_if_fail (avalues != NULL, NULL);
	
	for (;available; available = g_list_next (available))
	{
		GList* s_node = secs;
		GList* n_node = anames;
		GList* v_node = avalues;
		
		gboolean satisfied = FALSE;
		
		AnjutaPluginHandle *plugin = available->data;
		AnjutaPluginDescription *desc =
			anjuta_plugin_handle_get_description (plugin);
		
		if (g_hash_table_lookup (plugin_manager->priv->disable_plugins, plugin) != NULL)
			continue;

		while (s_node)
		{
			gchar *val;
			GList *vals;
			GList *node;
			gboolean found = FALSE;
			
			satisfied = TRUE;
			
			sec = s_node->data;
			aname = n_node->data;
			avalue = v_node->data;
			
			if (!anjuta_plugin_description_get_string (desc, sec, aname, &val))
			{
				satisfied = FALSE;
				break;
			}
			
			vals = property_to_list (val);
			g_free (val);
			
			node = vals;
			while (node)
			{
				if (strchr(node->data, '*') != NULL)
				{
					// Star match.
					gchar **segments;
					gchar **seg_ptr;
					const gchar *cursor;
					
					segments = g_strsplit (node->data, "*", -1);
					
					seg_ptr = segments;
					cursor = avalue;
					while (*seg_ptr != NULL)
					{
						if (strlen (*seg_ptr) > 0) {
							cursor = strstr (cursor, *seg_ptr);
							if (cursor == NULL)
								break;
						}
						cursor += strlen (*seg_ptr);
						seg_ptr++;
					}
					if (*seg_ptr == NULL)
						found = TRUE;
					g_strfreev (segments);
				}
				else if (g_ascii_strcasecmp (node->data, avalue) == 0)
				{
					// String match.
					found = TRUE;
				}
				g_free (node->data);
				node = g_list_next (node);
			}
			g_list_free (vals);
			if (!found)
			{
				satisfied = FALSE;
				break;
			}
			s_node = g_list_next (s_node);
			n_node = g_list_next (n_node);
			v_node = g_list_next (v_node);
		}
		if (satisfied)
		{
			selected_plugins = g_list_prepend (selected_plugins, plugin);
			/* DEBUG_PRINT ("Satisfied, Adding %s",
						 anjuta_plugin_handle_get_name (plugin));*/
		}
	}
	
	return g_list_reverse (selected_plugins);
}

GList*
anjuta_plugin_manager_query (AnjutaPluginManager *plugin_manager,
							 const gchar *section_name,
							 const gchar *attribute_name,
							 const gchar *attribute_value,
							 ...)
{
	va_list var_args;
	GList *secs = NULL;
	GList *anames = NULL;
	GList *avalues = NULL;
	const gchar *sec;
	const gchar *aname;
	const gchar *avalue;
	GList *selected_plugins;
	
	
	if (section_name == NULL)
	{
		/* If no query is given, select all plugins */
		return anjuta_plugin_manager_list_query (plugin_manager, NULL, NULL, NULL);
	}
	
	g_return_val_if_fail (section_name != NULL, NULL);
	g_return_val_if_fail (attribute_name != NULL, NULL);
	g_return_val_if_fail (attribute_value != NULL, NULL);
	
	secs = g_list_prepend (secs, g_strdup (section_name));
	anames = g_list_prepend (anames, g_strdup (attribute_name));
	avalues = g_list_prepend (avalues, g_strdup (attribute_value));
	
	va_start (var_args, attribute_value);
	do
	{
		sec = va_arg (var_args, const gchar *);
		if (sec)
		{
			aname = va_arg (var_args, const gchar *);
			if (aname)
			{
				avalue = va_arg (var_args, const gchar *);
				if (avalue)
				{
					secs = g_list_prepend (secs, g_strdup (sec));
					anames = g_list_prepend (anames, g_strdup (aname));
					avalues = g_list_prepend (avalues, g_strdup (avalue));
				}
			}
		}
	}
	while (sec);
	va_end (var_args);
	
	secs = g_list_reverse (secs);
	anames = g_list_reverse (anames);
	avalues = g_list_reverse (avalues);
	
	selected_plugins = anjuta_plugin_manager_list_query (plugin_manager,
														 secs,
														 anames,
														 avalues);
	
	anjuta_util_glist_strings_free (secs);
	anjuta_util_glist_strings_free (anames);
	anjuta_util_glist_strings_free (avalues);
	
	return selected_plugins;
}

enum {
	PIXBUF_COLUMN,
	PLUGIN_COLUMN,
	PLUGIN_HANDLE_COLUMN,
	N_COLUMNS
};

static void
on_plugin_list_row_activated (GtkTreeView *tree_view,
							  GtkTreePath *path,
                              GtkTreeViewColumn *column,
                              GtkDialog *dialog)
{
	gtk_dialog_response (dialog, GTK_RESPONSE_OK);
}


static void
on_plugin_list_show (GtkTreeView *view,
                     GtkDirectionType direction,
                     GtkDialog *dialog)
{
	GtkTreeSelection *selection;
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));

	g_signal_emit_by_name (G_OBJECT (selection), "changed", GTK_DIALOG(dialog), NULL);
}


static void
on_plugin_list_selection_changed (GtkTreeSelection *tree_selection,
								  GtkDialog *dialog)
{
	GtkContainer *action_area;
	GList *list;
	GtkButton *bt = NULL;

	action_area = GTK_CONTAINER (gtk_dialog_get_action_area (dialog));
	list = gtk_container_get_children (action_area);
	for (; list; list = list->next) {
		bt = list->data;
		if (!strcmp("gtk-ok", gtk_button_get_label (bt)))
		   break;
	}
	if (bt && gtk_tree_selection_get_selected (tree_selection, NULL, NULL))
		gtk_widget_set_sensitive ((GtkWidget *) bt, TRUE);
	else
		gtk_widget_set_sensitive ((GtkWidget *) bt, FALSE);
	g_list_free(list);
}

/*
 * anjuta_plugin_manager_select:
 * @plugin_manager: #AnjutaPluginManager object
 * @title: Title of the dialog
 * @description: label shown on the dialog
 * @plugin_handles: List of #AnjutaPluginHandle
 *
 * Show a dialog where the user can choose between the given plugins
 *
 * Returns: The chosen plugin handle
 */
AnjutaPluginHandle *
anjuta_plugin_manager_select (AnjutaPluginManager *plugin_manager,
							  gchar *title, gchar *description,
							  GList *plugin_handles)
{
	AnjutaPluginManagerPriv *priv;
	AnjutaPluginHandle *handle;
	GtkWidget *dlg;
	GtkTreeModel *model;
	GtkWidget *view;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	GList *node;
	GtkWidget *label;
	GtkWidget *content_area;
	GtkWidget *sc;
	GtkWidget *remember_checkbox;
	gint response;
	GtkTreeIter selected;
	GtkTreeSelection *selection;
	GtkTreeModel *store;
	GList *selection_ids = NULL;
	GString *remember_key = g_string_new ("");
	
	g_return_val_if_fail (title != NULL, NULL);
	g_return_val_if_fail (description != NULL, NULL);
	g_return_val_if_fail (plugin_handles != NULL, NULL);

	priv = plugin_manager->priv;
	
	if (g_list_length (plugin_handles) <= 0)
		return NULL;
		
	dlg = gtk_dialog_new_with_buttons (title, GTK_WINDOW (priv->shell),
									   GTK_DIALOG_DESTROY_WITH_PARENT,
									   GTK_STOCK_CANCEL,
									   GTK_RESPONSE_CANCEL,
									   GTK_STOCK_OK, GTK_RESPONSE_OK,
									   NULL);
	gtk_window_set_default_size (GTK_WINDOW (dlg), 400, 300);

	label = gtk_label_new (description);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_widget_show (label);
	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dlg));
	gtk_box_pack_start (GTK_BOX (content_area), label,
			    FALSE, FALSE, 5);
	
	sc = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_show (sc);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sc),
										 GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sc),
									GTK_POLICY_AUTOMATIC,
									GTK_POLICY_AUTOMATIC);
	
	gtk_box_pack_start (GTK_BOX (content_area), sc,
			    TRUE, TRUE, 5);
	
	model = GTK_TREE_MODEL (gtk_list_store_new (N_COLUMNS, GDK_TYPE_PIXBUF,
										   G_TYPE_STRING, G_TYPE_POINTER));
	view = gtk_tree_view_new_with_model (model);
	gtk_widget_show (view);
	gtk_container_add (GTK_CONTAINER (sc), view);
	
	column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_sizing (column,
									 GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_title (column, dgettext (GETTEXT_PACKAGE, "Available Plugins"));

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "pixbuf",
										PIXBUF_COLUMN);

	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup",
										PLUGIN_COLUMN);

	gtk_tree_view_append_column (GTK_TREE_VIEW (view), column);
	gtk_tree_view_set_expander_column (GTK_TREE_VIEW (view), column);
	
	g_signal_connect (view, "row-activated",
					  G_CALLBACK (on_plugin_list_row_activated),
					  GTK_DIALOG(dlg));
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
	g_signal_connect(selection, "changed",
					 G_CALLBACK(on_plugin_list_selection_changed),
					 GTK_DIALOG(dlg));
	g_signal_connect(view, "focus",
					 G_CALLBACK(on_plugin_list_show),
					 GTK_DIALOG(dlg));

	remember_checkbox =
		gtk_check_button_new_with_label (dgettext (GETTEXT_PACKAGE, "Remember this selection"));
	gtk_container_set_border_width (GTK_CONTAINER (remember_checkbox), 10);
	gtk_widget_show (remember_checkbox);
	gtk_box_pack_start (GTK_BOX (content_area), remember_checkbox,
						FALSE, FALSE, 0);
	
	node = plugin_handles;
	while (node)
	{
		const gchar *filename;
		GdkPixbuf *icon_pixbuf = NULL;
		const gchar *name = NULL;
		AnjutaPluginDescription *desc;

		handle = (AnjutaPluginHandle*)node->data;

		filename = anjuta_plugin_handle_get_icon_path (handle);
		if (filename != NULL)
		{
			icon_pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
			if (!icon_pixbuf)
				g_warning ("Plugin does not define Icon: No such file %s",
				           filename);
		}
		else
		{
			g_warning ("Plugin does not define Icon attribute");
		}

		name = anjuta_plugin_handle_get_name (handle);
		desc = anjuta_plugin_handle_get_description (handle);
		if ((name != NULL) && (desc != NULL))
		{
			gchar *plugin_desc;
			GtkTreeIter iter;
			gchar *text;
			
			if (!anjuta_plugin_description_get_locale_string (desc,
			                                                  "Anjuta Plugin",
			                                                  "Description",
			                                                  &plugin_desc))
			{
				g_warning ("Plugin does not define Description attribute");
			}
			text = g_markup_printf_escaped ("<span size=\"larger\" weight=\"bold\">%s</span>\n%s", name, plugin_desc);
			g_free (plugin_desc);

			gtk_list_store_append (GTK_LIST_STORE (model), &iter);
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
								PLUGIN_COLUMN, text,
								PLUGIN_HANDLE_COLUMN, handle, -1);
			if (icon_pixbuf) {
				gtk_list_store_set (GTK_LIST_STORE (model), &iter,
									PIXBUF_COLUMN, icon_pixbuf, -1);
			}
			g_free (text);

			selection_ids = g_list_prepend (selection_ids, (gpointer)anjuta_plugin_handle_get_id (handle));
		}
		else
		{
			g_warning ("Plugin does not define Name attribute");
		}

		if (icon_pixbuf)
			g_object_unref (icon_pixbuf);

		node = g_list_next (node);
	}
	
	/* Prepare remembering key */
	selection_ids = g_list_sort (selection_ids,
	                             (GCompareFunc)strcmp);
	node = selection_ids;
	while (node)
	{
		g_string_append (remember_key, (gchar*)node->data);
		g_string_append (remember_key, ",");
		node = g_list_next (node);
	}
	g_list_free (selection_ids);
	
	/* Find if the selection is remembered */
	handle = g_hash_table_lookup (priv->remember_plugins, remember_key->str);
	if (handle)
	{
		g_string_free (remember_key, TRUE);
		gtk_widget_destroy (dlg);
		return handle;
	}
	
	/* Prompt dialog */
	response = gtk_dialog_run (GTK_DIALOG (dlg));
	switch (response)
	{
	case GTK_RESPONSE_OK:
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
		if (gtk_tree_selection_get_selected (selection, &store,
											 &selected))
		{
			gtk_tree_model_get (model, &selected,
								PLUGIN_HANDLE_COLUMN, &handle, -1);
			if (handle)
			{
				/* Remember selection */
				if (gtk_toggle_button_get_active
					(GTK_TOGGLE_BUTTON (remember_checkbox)))
				{
					/* DEBUG_PRINT ("Remembering selection '%s'",
								 remember_key->str);*/
					g_hash_table_insert (priv->remember_plugins,
										 g_strdup (remember_key->str), handle);
				}
				g_string_free (remember_key, TRUE);
				gtk_widget_destroy (dlg);
				return handle;
			}
		}
		break;
	}
	g_string_free (remember_key, TRUE);
	gtk_widget_destroy (dlg);
	return NULL;
}

GObject*
anjuta_plugin_manager_select_and_activate (AnjutaPluginManager *plugin_manager,
										   gchar *title,
										   gchar *description,
										   GList *plugin_handles)
{
	AnjutaPluginHandle *handle;
	GObject *plugin = NULL;	

	g_return_val_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager), NULL);
	
	handle = anjuta_plugin_manager_select (plugin_manager, title, description,
	                                       plugin_handles);
	plugin = anjuta_plugin_manager_get_plugin_by_handle (plugin_manager, handle);

	return plugin;
}

/*
 * anjuta_plugin_manager_get_plugin_handle:
 * @plugin_manager: #AnjutaPluginManager object
 * @plugin: #AnjutaPlugin object
 *
 * Get the handle corresponding to the plugin or %NULL if the plugin is not
 * activated.
 *
 * Returns: A #AnjutaPluginHandle or %NULL.
 */
AnjutaPluginHandle*
anjuta_plugin_manager_get_plugin_handle (AnjutaPluginManager *plugin_manager,
											  GObject *plugin)
{
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, plugin_manager->priv->activated_plugins);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		if (G_OBJECT(value) == plugin)
		{
			return ANJUTA_PLUGIN_HANDLE (key);
		}
	}

	return NULL;
}


/* Plugin manager */

static void
anjuta_plugin_manager_init (AnjutaPluginManager *object)
{
	object->priv = g_new0 (AnjutaPluginManagerPriv, 1);
	object->priv->plugins_by_name = g_hash_table_new (g_str_hash, g_str_equal);
	object->priv->plugins_by_interfaces = g_hash_table_new_full (g_str_hash,
											   g_str_equal,
											   NULL,
											   (GDestroyNotify) g_list_free);
	object->priv->plugins_by_description = g_hash_table_new (g_direct_hash,
														   g_direct_equal);
	object->priv->activated_plugins = g_hash_table_new_full (g_direct_hash, g_direct_equal,
	                                                         NULL, g_object_unref);
	object->priv->plugins_cache = g_hash_table_new_full (g_direct_hash, g_direct_equal,
	                                                     NULL, g_object_unref);
	object->priv->remember_plugins = g_hash_table_new_full (g_str_hash,
															g_str_equal,
															NULL, NULL);
	object->priv->disable_plugins = g_hash_table_new (g_direct_hash,
	                                                  g_direct_equal);
}

static void
anjuta_plugin_manager_dispose (GObject *object)
{
	AnjutaPluginManagerPriv *priv;
	priv = ANJUTA_PLUGIN_MANAGER (object)->priv;

	if (priv->available_plugins)
	{
		g_list_foreach (priv->available_plugins, (GFunc)g_object_unref, NULL);
		g_list_free (priv->available_plugins);
		priv->available_plugins = NULL;
	}
	if (priv->activated_plugins)
	{
		g_hash_table_destroy (priv->activated_plugins);
		priv->activated_plugins = NULL;
	}
	if (priv->plugins_cache)
	{
		g_hash_table_destroy (priv->plugins_cache);
		priv->plugins_cache = NULL;
	}
	if (priv->disable_plugins)
	{
		g_hash_table_destroy (priv->disable_plugins);
		priv->disable_plugins = NULL;
	}
	if (priv->plugins_by_name)
	{
		g_hash_table_destroy (priv->plugins_by_name);
		priv->plugins_by_name = NULL;
	}
	if (priv->plugins_by_description)
	{
		g_hash_table_destroy (priv->plugins_by_description);
		priv->plugins_by_description = NULL;
	}
	if (priv->plugins_by_interfaces)
	{
		g_hash_table_destroy (priv->plugins_by_interfaces);
		priv->plugins_by_interfaces = NULL;
	}
	if (priv->plugin_dirs)
	{
		g_list_foreach (priv->plugin_dirs, (GFunc)g_free, NULL);
		g_list_free (priv->plugin_dirs);
		priv->plugin_dirs = NULL;
	}
#if 0
	if (anjuta_c_plugin_factory)
	{
		g_object_unref (anjuta_c_plugin_factory);
		anjuta_c_plugin_factory = NULL;
	}
#endif
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
anjuta_plugin_manager_set_property (GObject *object, guint prop_id,
									const GValue *value, GParamSpec *pspec)
{
	AnjutaPluginManagerPriv *priv;
	
	g_return_if_fail (ANJUTA_IS_PLUGIN_MANAGER (object));
	priv = ANJUTA_PLUGIN_MANAGER (object)->priv;
	
	switch (prop_id)
	{
	case PROP_STATUS:
		priv->status = g_value_get_object (value);
		break;
	case PROP_SHELL:
		priv->shell = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
anjuta_plugin_manager_get_property (GObject *object, guint prop_id,
									GValue *value, GParamSpec *pspec)
{
	AnjutaPluginManagerPriv *priv;
	
	g_return_if_fail (ANJUTA_IS_PLUGIN_MANAGER (object));
	priv = ANJUTA_PLUGIN_MANAGER (object)->priv;
	
	switch (prop_id)
	{
	case PROP_SHELL:
		g_value_set_object (value, priv->shell);
		break;
	case PROP_STATUS:
		g_value_set_object (value, priv->status);
		break;
	case PROP_AVAILABLE_PLUGINS:
		g_value_set_pointer (value, priv->available_plugins);
		break;
	case PROP_ACTIVATED_PLUGINS:
		g_value_set_pointer (value, priv->activated_plugins);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}
static void
anjuta_plugin_manager_plugin_activated (AnjutaPluginManager *self,
										AnjutaPluginHandle* handle,
										GObject *plugin)
{
	/* TODO: Add default signal handler implementation here */
}

static void
anjuta_plugin_manager_plugin_deactivated (AnjutaPluginManager *self,
										  AnjutaPluginHandle* handle,
										  GObject *plugin)
{
	/* TODO: Add default signal handler implementation here */
}

static void
anjuta_plugin_manager_class_init (AnjutaPluginManagerClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));

	object_class->dispose = anjuta_plugin_manager_dispose;
	object_class->set_property = anjuta_plugin_manager_set_property;
	object_class->get_property = anjuta_plugin_manager_get_property;

	klass->plugin_activated = anjuta_plugin_manager_plugin_activated;
	klass->plugin_deactivated = anjuta_plugin_manager_plugin_deactivated;

	g_object_class_install_property (object_class,
	                                 PROP_PROFILES,
	                                 g_param_spec_pointer ("profiles",
	                                                       dgettext (GETTEXT_PACKAGE, "Profiles"),
	                                                       dgettext (GETTEXT_PACKAGE, "Current stack of profiles"),
	                                                       G_PARAM_READABLE));
	g_object_class_install_property (object_class,
	                                 PROP_AVAILABLE_PLUGINS,
	                                 g_param_spec_pointer ("available-plugins",
	                                                       dgettext (GETTEXT_PACKAGE, "Available plugins"),
	                                                       dgettext (GETTEXT_PACKAGE, "Currently available plugins found in plugin paths"),
	                                                       G_PARAM_READABLE));

	g_object_class_install_property (object_class,
	                                 PROP_ACTIVATED_PLUGINS,
	                                 g_param_spec_pointer ("activated-plugins",
	                                                       dgettext (GETTEXT_PACKAGE, "Activated plugins"),
	                                                       dgettext (GETTEXT_PACKAGE, "Currently activated plugins"),
	                                                       G_PARAM_READABLE));
	g_object_class_install_property (object_class,
	                                 PROP_SHELL,
	                                 g_param_spec_object ("shell",
														  dgettext (GETTEXT_PACKAGE, "Anjuta Shell"),
														  dgettext (GETTEXT_PACKAGE, "Anjuta shell for which the plugins are made"),
														  G_TYPE_OBJECT,
														  G_PARAM_READABLE |
														  G_PARAM_WRITABLE |
														  G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
	                                 PROP_STATUS,
	                                 g_param_spec_object ("status",
														  dgettext (GETTEXT_PACKAGE, "Anjuta Status"),
														  dgettext (GETTEXT_PACKAGE, "Anjuta status to use in loading and unloading of plugins"),
														  ANJUTA_TYPE_STATUS,
														  G_PARAM_READABLE |
														  G_PARAM_WRITABLE |
														  G_PARAM_CONSTRUCT));
	
	plugin_manager_signals[PLUGIN_ACTIVATED] =
		g_signal_new ("plugin-activated",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (AnjutaPluginManagerClass,
									   plugin_activated),
		              NULL, NULL,
					  anjuta_cclosure_marshal_VOID__POINTER_OBJECT,
		              G_TYPE_NONE, 2,
		              G_TYPE_POINTER, ANJUTA_TYPE_PLUGIN);

	plugin_manager_signals[PLUGIN_DEACTIVATED] =
		g_signal_new ("plugin-deactivated",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (AnjutaPluginManagerClass,
									   plugin_deactivated),
		              NULL, NULL,
					  anjuta_cclosure_marshal_VOID__POINTER_OBJECT,
		              G_TYPE_NONE, 2,
		              G_TYPE_POINTER, ANJUTA_TYPE_PLUGIN);
}

GType
anjuta_plugin_manager_get_type (void)
{
	static GType our_type = 0;

	if(our_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (AnjutaPluginManagerClass), /* class_size */
			(GBaseInitFunc) NULL, /* base_init */
			(GBaseFinalizeFunc) NULL, /* base_finalize */
			(GClassInitFunc) anjuta_plugin_manager_class_init, /* class_init */
			(GClassFinalizeFunc) NULL, /* class_finalize */
			NULL /* class_data */,
			sizeof (AnjutaPluginManager), /* instance_size */
			0, /* n_preallocs */
			(GInstanceInitFunc) anjuta_plugin_manager_init, /* instance_init */
			NULL /* value_table */
		};
		our_type = g_type_register_static (G_TYPE_OBJECT,
										   "AnjutaPluginManager",
		                                   &our_info, 0);
	}

	return our_type;
}

AnjutaPluginManager*
anjuta_plugin_manager_new (GObject *shell, AnjutaStatus *status,
						   GList* plugins_directories)
{
	GObject *manager_object;
	AnjutaPluginManager *plugin_manager;
	GList *cycles = NULL;
	const char *gnome2_path;
	char **pathv;
	char **p;
	GList *node;
	GList *plugin_dirs = NULL;

	/* Initialize the anjuta plugin system */
	manager_object = g_object_new (ANJUTA_TYPE_PLUGIN_MANAGER,
								   "shell", shell, "status", status, NULL);
	plugin_manager = ANJUTA_PLUGIN_MANAGER (manager_object);
	
	if (anjuta_plugin_factory == NULL)
	{
		anjuta_plugin_factory = anjuta_c_plugin_factory_new ();
	}
	
	gnome2_path = g_getenv ("GNOME2_PATH");
	if (gnome2_path) {
		pathv = g_strsplit (gnome2_path, ":", 1);
	
		for (p = pathv; *p != NULL; p++) {
			char *path = g_strdup (*p);
			plugin_dirs = g_list_prepend (plugin_dirs, path);
		}
		g_strfreev (pathv);
	}
	
	node = plugins_directories;
	while (node) {
		if (!node->data)
			continue;
		char *path = g_strdup (node->data);
		plugin_dirs = g_list_prepend (plugin_dirs, path);
		node = g_list_next (node);
	}
	plugin_dirs = g_list_reverse (plugin_dirs);
	/* load_plugins (); */

	node = plugin_dirs;
	while (node)
	{
		load_plugins_from_directory (plugin_manager, (char*)node->data);
		node = g_list_next (node);
	}
	resolve_dependencies (plugin_manager, &cycles);
	g_list_foreach(plugin_dirs, (GFunc) g_free, NULL);
	g_list_free(plugin_dirs);
	return plugin_manager;
}

void
anjuta_plugin_manager_activate_plugins (AnjutaPluginManager *plugin_manager,
										GList *plugins_to_activate)
{
	AnjutaPluginManagerPriv *priv;
	GList *node;
	
	priv = plugin_manager->priv;
	
	/* Freeze shell operations */
	anjuta_shell_freeze (ANJUTA_SHELL (priv->shell), NULL);
	if (plugins_to_activate)
	{
		anjuta_status_progress_add_ticks (ANJUTA_STATUS (priv->status),
										  g_list_length (plugins_to_activate));
	}
	node = plugins_to_activate;
	while (node)
	{
		AnjutaPluginHandle *handle;
		const gchar *filename;
		GdkPixbuf *icon_pixbuf = NULL;
		const gchar *name;
		gchar*label= NULL;
		
		handle = node->data;

		filename = anjuta_plugin_handle_get_icon_path (handle);
		if (filename != NULL)
		{
			icon_pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
			if (!icon_pixbuf)
				g_warning ("Plugin does not define Icon: No such file %s",
				           filename);
		}

		name = anjuta_plugin_handle_get_name (handle);
		if (name != NULL)
		{
			label = g_strconcat (dgettext (GETTEXT_PACKAGE, "Loading:"), " ", name, "...", NULL);
		}

		anjuta_status_progress_tick (ANJUTA_STATUS (priv->status),
									 icon_pixbuf, label);
		g_free (label);
		if (icon_pixbuf)
			g_object_unref (icon_pixbuf);

		/* Activate the plugin */
		anjuta_plugin_manager_get_plugin_by_handle (plugin_manager, handle);
		
		node = g_list_next (node);
	}
	
	/* Thaw shell operations */
	anjuta_shell_thaw (ANJUTA_SHELL (priv->shell), NULL);
}

static void
on_collect (gpointer key, gpointer value, gpointer user_data)
{
	const gchar *id;
	gchar *query = (gchar*) key;
	AnjutaPluginHandle *handle = (AnjutaPluginHandle *) value;
	GString *write_buffer = (GString *) user_data;
	
	id = anjuta_plugin_handle_get_id (handle);
	g_string_append_printf (write_buffer, "%s=%s;", query, id);
}

/**
 * anjuta_plugin_manager_get_remembered_plugins:
 * @plugin_manager: A #AnjutaPluginManager object
 * 
 * Get the list of plugins loaded when there is a choice between several
 * ones without asking the user.
 *
 * The list format is returned as a string with the format detailed in 
 * anjuta_plugin_manager_set_remembered_plugins().
 *
 * Return value: a newly-allocated string that must be freed with g_free().
 */

gchar*
anjuta_plugin_manager_get_remembered_plugins (AnjutaPluginManager *plugin_manager)
{
	AnjutaPluginManagerPriv *priv;
	GString *write_buffer = g_string_new ("");
	
	g_return_val_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager), FALSE);
	
	priv = plugin_manager->priv;
	g_hash_table_foreach (priv->remember_plugins, on_collect,
						  write_buffer);
	return g_string_free (write_buffer, FALSE);
}

/**
 * anjuta_plugin_manager_set_remembered_plugins:
 * @plugin_manager: A #AnjutaPluginManager object
 * @remembered_plugins: A list of prefered plugins
 * 
 * Set the list of plugins loaded when there is a choice between several
 * ones without asking the user.
 * The list is a string composed of elements separated by ';'. Each element
 * is defined with "key=value", where key is the list of possible plugins and
 * the value is the choosen plugin.
 *
 * By the example the following element
 * <programlisting>
 *   anjuta-symbol-browser:SymbolBrowserPlugin,anjuta-symbol-db:SymbolDBPlugin,=anjuta-symbol-db:SymbolDBPlugin;
 * </programlisting>
 * means if Anjuta has to choose between SymbolBrowserPlugin and
 * SymbolDBPlugin, it will choose SymbolDBPlugin.
 */
void
anjuta_plugin_manager_set_remembered_plugins (AnjutaPluginManager *plugin_manager,
											  const gchar *remembered_plugins)
{
	AnjutaPluginManagerPriv *priv;
	gchar **strv_lines, **line_idx;
	
	g_return_if_fail (ANJUTA_IS_PLUGIN_MANAGER (plugin_manager));
	g_return_if_fail (remembered_plugins != NULL);
	
	priv = plugin_manager->priv;

	g_hash_table_remove_all (priv->remember_plugins);
	
	strv_lines = g_strsplit (remembered_plugins, ";", -1);
	line_idx = strv_lines;
	while (*line_idx)
	{
		gchar **strv_keyvals;
		strv_keyvals = g_strsplit (*line_idx, "=", -1);
		if (strv_keyvals && strv_keyvals[0] && strv_keyvals[1])
		{
			AnjutaPluginHandle *handle;
			handle = g_hash_table_lookup (priv->plugins_by_name,
										  strv_keyvals[1]);
			if (handle)
			{
				g_hash_table_insert (priv->remember_plugins,
									 g_strdup (strv_keyvals[0]), handle);
			}
			g_strfreev (strv_keyvals);
		}
		line_idx++;
	}
	g_strfreev (strv_lines);
}

/**
 * anjuta_plugin_manager_set_disable_plugins:
 * @plugin_manager: A #AnjutaPluginManager object
 * @plugins_list: A list of plugins to disable or reenable
 * @hide: %TRUE to disable, %FALSE to re-enable plugins in the list
 * 
 * Disable or re-enable plugins. By default, all plugins are enabled but they
 * can be disabled and they will not be proposed when a plugin is requested.
 */
void
anjuta_plugin_manager_set_disable_plugins (AnjutaPluginManager *plugin_manager,
                                           GList *plugin_handles,
                                           gboolean disable)
{
	GList *item;

	if (disable)
	{
		for (item = g_list_first (plugin_handles); item != NULL; item = g_list_next (item))
		{
			g_hash_table_add (plugin_manager->priv->disable_plugins, item->data);
		}
	}
	else
	{
		for (item = g_list_first (plugin_handles); item != NULL; item = g_list_next (item))
		{
			g_hash_table_remove (plugin_manager->priv->disable_plugins, item->data);
		}
	}
}

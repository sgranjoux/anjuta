/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * plugin.c
 * Copyright (C) Johannes Schmid 2007 <jhs@gnome.org>
 * 
 * plugin.c is free software.
 * 
 * You may redistribute it and/or modify it under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * 
 * plugin.c is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with plugin.c.  If not, write to:
 * 	The Free Software Foundation, Inc.,
 * 	51 Franklin Street, Fifth Floor
 * 	Boston, MA  02110-1301, USA.
 */

#include <config.h>
#include <gio/gio.h>
#include <libanjuta/anjuta-shell.h>
#include <libanjuta/anjuta-debug.h>
#include <libanjuta/anjuta-preferences.h>
#include <libanjuta/interfaces/ianjuta-file-loader.h>
#include <libanjuta/interfaces/ianjuta-file-manager.h>
#include <libanjuta/interfaces/ianjuta-vcs.h>
#include <libanjuta/interfaces/ianjuta-project-manager.h>
#include <libanjuta/interfaces/ianjuta-preferences.h>
#include <libanjuta/interfaces/ianjuta-document.h>
#include <libanjuta/interfaces/ianjuta-document-manager.h>
#include <libanjuta/interfaces/ianjuta-file.h>
#include "plugin.h"

#define UI_FILE PACKAGE_DATA_DIR"/ui/file-manager.xml"
#define ICON_FILE "anjuta-file-manager-plugin-48.png"
#define FILE_MANAGER_BUILDER PACKAGE_DATA_DIR"/glade/file-manager.ui"
#define FILE_MANAGER_BUILDER_ROOT "filemanager_prefs"

#define PREF_SCHEMA "org.gnome.anjuta.file-manager"
#define PREF_ROOT "filemanager-root"
#define PREF_FILTER_BINARY "filemanager-filter-binary"
#define PREF_FILTER_HIDDEN "filemanager-filter-hidden"
#define PREF_FILTER_BACKUP "filemanager-filter-backup"
#define PREF_FILTER_UNVERSIONED "filemanager-filter-unversioned"
#define PREF_SELECT_CURRENT_DOCUMENT "filemanager-select-current-document"

#define REGISTER_NOTIFY(key, func) \
	notify_id = anjuta_preferences_notify_add (prefs, \
											   key, func, file_manager); \
	file_manager->notify_ids = g_list_prepend (file_manager->notify_ids, \
										       GUINT_TO_POINTER(notify_id));

static gpointer parent_class;

typedef struct _ScrollPosition ScrollPosition;
struct _ScrollPosition
{
	GtkWidget* window;
	gdouble hvalue;
	gdouble vvalue;
};

static void
on_show_in_file_manager (GtkAction* action, AnjutaFileManager* file_manager)
{
	IAnjutaDocumentManager* docman;
	IAnjutaDocument* document;
	GFile* file;

	docman = anjuta_shell_get_interface (ANJUTA_PLUGIN (file_manager)->shell,
										 IAnjutaDocumentManager, NULL);
	g_return_if_fail (docman);

	document = ianjuta_document_manager_get_current_document (docman, NULL);
	if (!IANJUTA_IS_FILE (document))
		return;

	file = ianjuta_file_get_file (IANJUTA_FILE (document), NULL);
	if (!file)
		return;

	file_view_set_selected (file_manager->fv, file);
	g_object_unref (file);

	anjuta_shell_present_widget (ANJUTA_PLUGIN (file_manager)->shell,
								 file_manager->sw, NULL);
}

static void
on_file_manager_rename (GtkAction* action, AnjutaFileManager* file_manager)
{
	file_view_rename (file_manager->fv);
}

static GtkActionEntry popup_actions[] = 
{
	{
		"ActionPopupFileManagerRename", NULL,
		N_("_Rename"), NULL, N_("Rename file or directory"),
		G_CALLBACK (on_file_manager_rename)
	},
	{
		"ActionPopupShowInFileManager", NULL,
		N_("_Show in File manager"), NULL, N_("Show in File manager"),
		G_CALLBACK (on_show_in_file_manager)
	}
};

static void
file_manager_set_default_uri (AnjutaFileManager* file_manager)
{
	gchar* path = g_settings_get_string (file_manager->settings, PREF_ROOT);
		
	if (path)
	{
		GFile *base_path;

		base_path = g_file_new_for_path (path);
		g_object_set (G_OBJECT (file_manager->fv), "base-path", base_path, NULL);
		g_object_unref (base_path);

		file_manager->have_project = FALSE;
	}
	g_free(path);
}

static void 
refresh_signal_cb(IAnjutaVcs* ivcs, gpointer data) 
{
    AnjutaFileManager* file_manager = ANJUTA_FILE_MANAGER(data);
	file_view_refresh_vcs (file_manager->fv);
}

static IAnjutaVcs*
get_vcs_plugin(AnjutaFileManager* file_manager, const gchar* root_uri)
{
	typedef struct
	{
		const gchar* file;
		const gchar* name;
	} VcsSystem;
	VcsSystem vcs_systems[] = {
		{".svn", "Subversion"},
		{".git", "Git"},
		{NULL, NULL}
	};
	
	gint i;
	const gchar* vcs_system = NULL;
	IAnjutaVcs* ivcs = NULL;
	for (i = 0; vcs_systems[i].name != NULL; i++)
	{
		gchar* vcs_uri = g_build_filename (root_uri, vcs_systems[i].file, NULL);
		GFile* vcs_file = g_file_new_for_uri (vcs_uri);
		if (g_file_query_exists(vcs_file, NULL))
		{
			vcs_system = vcs_systems[i].name;
		}
		g_free (vcs_uri);
		g_object_unref (vcs_file);
		if (vcs_system)
			break;
	}
	       
	if (vcs_system)
	{
		/* Load current language editor support plugins */
		AnjutaPluginManager* plugin_manager = anjuta_shell_get_plugin_manager (ANJUTA_PLUGIN(file_manager)->shell, NULL);
		GList* plugin_handles = anjuta_plugin_manager_query (plugin_manager,
														   "Anjuta Plugin",
														   "Interfaces",
														   "IAnjutaVcs",
														   "Vcs",
														   "System",
														   vcs_system, NULL);
		if (plugin_handles)
		{
			ivcs = IANJUTA_VCS(anjuta_plugin_manager_get_plugin_by_handle (plugin_manager,
			                                                               (AnjutaPluginHandle *)plugin_handles->data));

			g_signal_connect (G_OBJECT (ivcs), "status_changed",
			                  G_CALLBACK (refresh_signal_cb),
			                  file_manager);
			
			g_list_free (plugin_handles);
		}
	}
	return ivcs;
}

static void
current_document_added (AnjutaPlugin *plugin, const gchar *name,
						const GValue *value, gpointer user_data)
{
	AnjutaFileManager* file_manager = (AnjutaFileManager*) plugin;
	IAnjutaDocument *current_document;
	GFile *file;

	if (!g_settings_get_boolean (file_manager->settings, PREF_SELECT_CURRENT_DOCUMENT))
		return;

	current_document = g_value_get_object (value);
	if (!IANJUTA_IS_FILE (current_document))
		return;

	file = ianjuta_file_get_file (IANJUTA_FILE (current_document), NULL);
	if (!file)
		return;

	ianjuta_file_manager_set_selected (IANJUTA_FILE_MANAGER (plugin), file, NULL);
	g_object_unref (file);
}

static void
current_document_removed (AnjutaPlugin *plugin, const gchar *name,
						  gpointer user_data)
{
}

static void
project_root_added (AnjutaPlugin *plugin, const gchar *name,
					const GValue *value, gpointer user_data)
{
	AnjutaFileManager* file_manager;
	GtkTreeModelSort *tree_model;
	FileModel *file_model;
	const gchar *root_uri;

	file_manager = (AnjutaFileManager*) plugin;
	tree_model = GTK_TREE_MODEL_SORT (
	    gtk_tree_view_get_model (GTK_TREE_VIEW (file_manager->fv)));
	file_model = FILE_MODEL (gtk_tree_model_sort_get_model (tree_model));
	
	root_uri = g_value_get_string (value);
	if (root_uri)
	{
		GFile *base_path;

		base_path = g_file_new_for_uri (root_uri);
		g_object_set (G_OBJECT(file_manager->fv), "base-path", base_path, NULL);
		g_object_unref (base_path);
		
		file_model_set_ivcs (file_model, get_vcs_plugin (file_manager,
														 root_uri));
		
		file_view_refresh (file_manager->fv);
		file_manager->have_project = TRUE;
	}
	else
	{
		file_model_set_ivcs(file_model, NULL);
		file_manager_set_default_uri (file_manager);
		file_view_refresh (file_manager->fv);
	}
}

static void
project_root_removed (AnjutaPlugin *plugin, const gchar *name,
					  gpointer user_data)
{
	AnjutaFileManager* file_manager = (AnjutaFileManager*) plugin;
	file_manager_set_default_uri (file_manager);
	file_view_refresh (file_manager->fv);
}

static void
on_file_view_current_file_changed (AnjutaFileView* view, GFile* file,
								   AnjutaFileManager* file_manager)
{
	if (file)
	{
		GValue* value;
		value = g_new0 (GValue, 1);
		g_value_init (value, G_TYPE_FILE);
		g_value_set_object (value, file);
		anjuta_shell_add_value (ANJUTA_PLUGIN (file_manager)->shell,
								IANJUTA_FILE_MANAGER_SELECTED_FILE, value, NULL);
		g_value_unset (value);
	}
	else
	{
		anjuta_shell_remove_value (ANJUTA_PLUGIN(file_manager)->shell,
								   IANJUTA_FILE_MANAGER_SELECTED_FILE, NULL);		
	}
}

static void
on_file_view_open_file (AnjutaFileView* view, GFile* file,
						AnjutaFileManager* file_manager)
{
	IAnjutaFileLoader *loader;
	g_return_if_fail (file != NULL);
	loader = anjuta_shell_get_interface (ANJUTA_PLUGIN (file_manager)->shell,
										 IAnjutaFileLoader, NULL);
	g_return_if_fail (loader != NULL);
		
	ianjuta_file_loader_load (loader, file, FALSE, NULL);
}

static void
on_file_view_show_popup_menu (AnjutaFileView* view, GFile* file,
							  gboolean is_dir, guint button,
							  guint32 time, AnjutaFileManager* file_manager)
{
	GtkWidget *popup;
	GtkWidget *rename;
	AnjutaUI* ui = anjuta_shell_get_ui (ANJUTA_PLUGIN(file_manager)->shell, 
										NULL);
	popup = gtk_ui_manager_get_widget (GTK_UI_MANAGER (ui),
									   "/PopupFileManager");
	rename = gtk_ui_manager_get_widget (GTK_UI_MANAGER (ui),
									   "/PopupFileManager/PopupFileManagerRename");
	/* TODO */
	gtk_widget_hide (rename);
	
	g_return_if_fail (GTK_IS_WIDGET (popup));
	gtk_menu_popup (GTK_MENU (popup), NULL, NULL, NULL, NULL, button, time);
}

static void 
on_notify_root(GSettings* settings,
               const gchar* key,
               gpointer user_data)
{
	AnjutaFileManager* file_manager = (AnjutaFileManager*) user_data;
	if (!file_manager->have_project)
	{
		file_manager_set_default_uri (file_manager);
		file_view_refresh (file_manager->fv);
	}
}

static void 
on_notify(GSettings* settings,
          const gchar* key,
          gpointer user_data)
{
	AnjutaFileManager* file_manager = (AnjutaFileManager*) user_data;
	GtkTreeModel* sort_model = gtk_tree_view_get_model (GTK_TREE_VIEW (file_manager->fv));
	GtkTreeModel* file_model = gtk_tree_model_sort_get_model (GTK_TREE_MODEL_SORT(sort_model));
	
	g_object_set (G_OBJECT (file_model),
				  "filter_binary", g_settings_get_boolean (settings, PREF_FILTER_BINARY),
				  "filter_hidden", g_settings_get_boolean (settings, PREF_FILTER_HIDDEN),
				  "filter_backup", g_settings_get_boolean (settings, PREF_FILTER_BACKUP),
				  "filter_unversioned", g_settings_get_boolean (settings, PREF_FILTER_UNVERSIONED), NULL);
	file_view_refresh (file_manager->fv);
}

static gboolean
file_manager_activate (AnjutaPlugin *plugin)
{
	AnjutaUI *ui;
	AnjutaFileManager *file_manager;
	
	DEBUG_PRINT ("%s", "AnjutaFileManager: Activating AnjutaFileManager plugin ...");
	file_manager = (AnjutaFileManager*) plugin;
	
	/* Add all UI actions and merge UI */
	ui = anjuta_shell_get_ui (plugin->shell, NULL);
	
	/* Add action group */
	file_manager->action_group = 
		anjuta_ui_add_action_group_entries (ui, "ActionGroupFileManager",
											_("File manager popup actions"),
											popup_actions, G_N_ELEMENTS (popup_actions),
											GETTEXT_PACKAGE, FALSE,
											plugin);
	
	file_manager->uiid = anjuta_ui_merge (ui, UI_FILE);
	
	file_manager->sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (file_manager->sw),
								    GTK_POLICY_AUTOMATIC,
								    GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (file_manager->sw),
										 GTK_SHADOW_IN);
	
	file_manager->fv = ANJUTA_FILE_VIEW (file_view_new ());
	
	g_signal_connect (G_OBJECT (file_manager->fv), "file-open",
					  G_CALLBACK (on_file_view_open_file), file_manager);
	g_signal_connect (G_OBJECT(file_manager->fv), "show-popup-menu",
					  G_CALLBACK (on_file_view_show_popup_menu), file_manager);
	g_signal_connect (G_OBJECT(file_manager->fv), "current-file-changed",
					  G_CALLBACK (on_file_view_current_file_changed),
					  file_manager);
	file_manager_set_default_uri (file_manager);
	file_view_refresh (file_manager->fv);
	
	gtk_container_add (GTK_CONTAINER (file_manager->sw), 
					   GTK_WIDGET (file_manager->fv));
	
	gtk_widget_show_all (file_manager->sw);
	
	anjuta_shell_add_widget (plugin->shell, file_manager->sw,
							 "AnjutaFileManager",
							 _("Files"), GTK_STOCK_OPEN,
							 ANJUTA_SHELL_PLACEMENT_LEFT, NULL);
	
	file_manager->root_watch_id =
		anjuta_plugin_add_watch (plugin, IANJUTA_PROJECT_MANAGER_PROJECT_ROOT_URI,
								 project_root_added,
								 project_root_removed, NULL);

	file_manager->current_document_watch_id =
		anjuta_plugin_add_watch (plugin, IANJUTA_DOCUMENT_MANAGER_CURRENT_DOCUMENT,
								 current_document_added, current_document_removed, NULL);
	

	g_signal_connect (file_manager->settings, "changed::" PREF_ROOT, 
	                  G_CALLBACK (on_notify_root), file_manager);
	g_signal_connect (file_manager->settings, "changed::" PREF_FILTER_BINARY, 
	                  G_CALLBACK (on_notify), file_manager);
	g_signal_connect (file_manager->settings, "changed::" PREF_FILTER_HIDDEN, 
	                  G_CALLBACK (on_notify), file_manager);
	g_signal_connect (file_manager->settings, "changed::" PREF_FILTER_BACKUP, 
	                  G_CALLBACK (on_notify), file_manager);
	g_signal_connect (file_manager->settings, "changed::" PREF_FILTER_UNVERSIONED, 
	                  G_CALLBACK (on_notify), file_manager);
	on_notify (file_manager->settings, NULL, file_manager);
	
	return TRUE;
}

static gboolean
file_manager_deactivate (AnjutaPlugin *plugin)
{
	AnjutaUI *ui;
	AnjutaFileManager *file_manager;

	DEBUG_PRINT ("%s", "AnjutaFileManager: Dectivating AnjutaFileManager plugin ...");
	
	file_manager = (AnjutaFileManager*) plugin;
	ui = anjuta_shell_get_ui (plugin->shell, NULL);
	
	anjuta_plugin_remove_watch (plugin, file_manager->root_watch_id, TRUE);
	anjuta_plugin_remove_watch (plugin, file_manager->current_document_watch_id, FALSE);
	anjuta_ui_remove_action_group (ui, ((AnjutaFileManager*)plugin)->action_group);
	anjuta_ui_unmerge (ui, ((AnjutaFileManager*)plugin)->uiid);
	
	anjuta_shell_remove_widget (plugin->shell, file_manager->sw, NULL);
	return TRUE;
}

static void
file_manager_finalize (GObject *obj)
{		
	/* Finalization codes here */
	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
file_manager_dispose (GObject *obj)
{
	AnjutaFileManager *plugin = (AnjutaFileManager*)obj;
	g_object_unref (plugin->settings);

	/* Disposition codes */
	G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
file_manager_instance_init (GObject *obj)
{
	AnjutaFileManager *plugin = (AnjutaFileManager*)obj;
	
	plugin->settings = g_settings_new (PREF_SCHEMA);
	plugin->have_project = FALSE;
	
	plugin->uiid = 0;
}

static void
file_manager_class_init (GObjectClass *klass) 
{
	AnjutaPluginClass *plugin_class = ANJUTA_PLUGIN_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	plugin_class->activate = file_manager_activate;
	plugin_class->deactivate = file_manager_deactivate;
	
	klass->finalize = file_manager_finalize;
	klass->dispose = file_manager_dispose;
}

static void
ifile_manager_set_root (IAnjutaFileManager *ifile_manager, const gchar *root,
						GError **err)
{
	AnjutaFileManager* file_manager = (AnjutaFileManager*) ifile_manager;
	GFile *base_path = g_file_new_for_uri (root);
	g_object_set (G_OBJECT(file_manager->fv), "base-path", base_path, NULL);
	g_object_unref (base_path);
}

static void
ifile_manager_set_selected (IAnjutaFileManager *ifile_manager,
							GFile* selected, GError **err)
{
	AnjutaFileManager* file_manager = (AnjutaFileManager*) ifile_manager;

	file_view_set_selected (file_manager->fv, selected);
}

static GFile*
ifile_manager_get_selected (IAnjutaFileManager *ifile_manager, GError **err)
{
	AnjutaFileManager* file_manager = (AnjutaFileManager*) ifile_manager;
	return file_view_get_selected (file_manager->fv);
}

static void
ifile_manager_iface_init (IAnjutaFileManagerIface *iface)
{
	iface->set_root = ifile_manager_set_root;
	iface->get_selected = ifile_manager_get_selected;
	iface->set_selected = ifile_manager_set_selected;
}

static void
ipreferences_merge (IAnjutaPreferences* ipref,
					AnjutaPreferences* prefs,
					GError** e)
{
	GError* error = NULL;
	GtkBuilder* bxml = gtk_builder_new ();
	AnjutaFileManager* fm = (AnjutaFileManager*) ipref;

	if (!gtk_builder_add_from_file (bxml, FILE_MANAGER_BUILDER, &error))
	{
		g_warning ("Couldn't load builder file: %s", error->message);
		g_error_free (error);
	}

	anjuta_preferences_add_from_builder (prefs, bxml, fm->settings, FILE_MANAGER_BUILDER_ROOT, _("File Manager"),
								 ICON_FILE);
}

static void
ipreferences_unmerge (IAnjutaPreferences* ipref,
					  AnjutaPreferences* prefs,
					  GError** e)
{
	anjuta_preferences_remove_page (prefs, _("File Manager"));
}

static void
ipreferences_iface_init (IAnjutaPreferencesIface* iface)
{
	iface->merge = ipreferences_merge;
	iface->unmerge = ipreferences_unmerge;
}

ANJUTA_PLUGIN_BEGIN (AnjutaFileManager, file_manager);
ANJUTA_PLUGIN_ADD_INTERFACE (ifile_manager, IANJUTA_TYPE_FILE_MANAGER);
ANJUTA_PLUGIN_ADD_INTERFACE (ipreferences, IANJUTA_TYPE_PREFERENCES);
ANJUTA_PLUGIN_END

ANJUTA_SIMPLE_PLUGIN (AnjutaFileManager, file_manager);

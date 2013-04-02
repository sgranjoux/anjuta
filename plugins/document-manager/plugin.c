/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
    plugin.c
    Copyright (C) 2000-2008 Naba Kumar

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <config.h>
#include <libanjuta/anjuta-shell.h>
#include <libanjuta/anjuta-debug.h>
#include <libanjuta/anjuta-encodings.h>

#include <libanjuta/interfaces/ianjuta-document-manager.h>
#include <libanjuta/interfaces/ianjuta-project-manager.h>
#include <libanjuta/interfaces/ianjuta-file.h>
#include <libanjuta/interfaces/ianjuta-editor.h>
#include <libanjuta/interfaces/ianjuta-editor-selection.h>
#include <libanjuta/interfaces/ianjuta-editor-convert.h>
#include <libanjuta/interfaces/ianjuta-editor-view.h>
#include <libanjuta/interfaces/ianjuta-editor-line-mode.h>
#include <libanjuta/interfaces/ianjuta-editor-factory.h>
#include <libanjuta/interfaces/ianjuta-editor-folds.h>
#include <libanjuta/interfaces/ianjuta-editor-comment.h>
#include <libanjuta/interfaces/ianjuta-editor-zoom.h>
#include <libanjuta/interfaces/ianjuta-editor-goto.h>
#include <libanjuta/interfaces/ianjuta-editor-search.h>
#include <libanjuta/interfaces/ianjuta-editor-assist.h>
#include <libanjuta/interfaces/ianjuta-file-savable.h>
#include <libanjuta/interfaces/ianjuta-editor-language.h>
#include <libanjuta/interfaces/ianjuta-language.h>
#include <libanjuta/interfaces/ianjuta-preferences.h>
#include <libanjuta/interfaces/ianjuta-document.h>

#include "anjuta-docman.h"
#include "action-callbacks.h"
#include "plugin.h"
#include "search-box.h"
#include "search-files.h"
#include "anjuta-bookmarks.h"

#define UI_FILE PACKAGE_DATA_DIR"/ui/anjuta-document-manager.xml"
#define PREFS_BUILDER PACKAGE_DATA_DIR"/glade/anjuta-document-manager.ui"
#define ICON_FILE "anjuta-document-manager-plugin-48.png"

#define PREF_SCHEMA "org.gnome.anjuta.document-manager"

#define ANJUTA_PIXMAP_BOOKMARK_TOGGLE     "anjuta-bookmark-toggle"
#define ANJUTA_PIXMAP_BOOKMARK_PREV		  "anjuta-bookmark-prev"
#define ANJUTA_PIXMAP_BOOKMARK_NEXT		  "anjuta-bookmark-next"
#define ANJUTA_PIXMAP_BOOKMARK_CLEAR	  "anjuta-bookmark-clear"

#define ANJUTA_PIXMAP_FOLD_TOGGLE         "anjuta-fold-toggle"
#define ANJUTA_PIXMAP_FOLD_CLOSE          "anjuta-fold-close"
#define ANJUTA_PIXMAP_FOLD_OPEN           "anjuta-fold-open"

#define ANJUTA_PIXMAP_BLOCK_SELECT        "anjuta-block-select"
#define ANJUTA_PIXMAP_BLOCK_START         "anjuta-block-start"
#define ANJUTA_PIXMAP_BLOCK_END           "anjuta-block-end"

#define ANJUTA_PIXMAP_INDENT_INC          "anjuta-indent-more"
#define ANJUTA_PIXMAP_INDENT_DCR          "anjuta-indent-less"

#define ANJUTA_PIXMAP_GOTO_LINE			  "anjuta-go-line"

#define ANJUTA_PIXMAP_HISTORY_NEXT				  "anjuta-go-history-next"
#define ANJUTA_PIXMAP_HISTORY_PREV				  "anjuta-go-history-prev"

#define ANJUTA_PIXMAP_AUTOCOMPLETE        "anjuta-complete-auto"

/* Stock icons */
#define ANJUTA_STOCK_FOLD_TOGGLE              "anjuta-fold-toggle"
#define ANJUTA_STOCK_FOLD_OPEN                "anjuta-fold-open"
#define ANJUTA_STOCK_FOLD_CLOSE               "anjuta-fold-close"
#define ANJUTA_STOCK_BLOCK_SELECT             "anjuta-block-select"
#define ANJUTA_STOCK_INDENT_INC               "anjuta-indent-inc"
#define ANJUTA_STOCK_INDENT_DCR               "anjuta-indect-dcr"
#define ANJUTA_STOCK_PREV_BRACE               "anjuta-prev-brace"
#define ANJUTA_STOCK_NEXT_BRACE               "anjuta-next-brace"
#define ANJUTA_STOCK_BLOCK_START              "anjuta-block-start"
#define ANJUTA_STOCK_BLOCK_END                "anjuta-block-end"
#define ANJUTA_STOCK_BOOKMARK_TOGGLE          "anjuta-bookmark-toggle"
#define ANJUTA_STOCK_BOOKMARK_PREV            "anjuta-bookmark-previous"
#define ANJUTA_STOCK_BOOKMARK_NEXT            "anjuta-bookmark-next"
#define ANJUTA_STOCK_BOOKMARK_CLEAR           "anjuta-bookmark-clear"
#define ANJUTA_STOCK_GOTO_LINE				  "anjuta-goto-line"
#define ANJUTA_STOCK_HISTORY_NEXT			  "anjuta-history-next"
#define ANJUTA_STOCK_HISTORY_PREV			  "anjuta-history-prev"
#define ANJUTA_STOCK_MATCH_NEXT				  "anjuta-match-next"
#define ANJUTA_STOCK_MATCH_PREV				  "anjuta-match-prev"
#define ANJUTA_STOCK_AUTOCOMPLETE             "anjuta-autocomplete"


static gpointer parent_class;

/* Shortcuts implementation */
enum {
	m___ = 0,
	mS__ = GDK_SHIFT_MASK,
	m_C_ = GDK_CONTROL_MASK,
	m__M = GDK_MOD1_MASK,
	mSC_ = GDK_SHIFT_MASK | GDK_CONTROL_MASK,
};

enum {
	ID_FIRSTBUFFER = 1, /* Note: the value mustn't be 0 ! */
};

typedef struct {
	gint modifiers;
	guint gdk_key;
	gint id;
} ShortcutMapping;

static ShortcutMapping global_keymap[] = {
	{ m__M, GDK_KEY_1, ID_FIRSTBUFFER },
	{ m__M, GDK_KEY_2, ID_FIRSTBUFFER + 1},
	{ m__M, GDK_KEY_3, ID_FIRSTBUFFER + 2},
	{ m__M, GDK_KEY_4, ID_FIRSTBUFFER + 3},
	{ m__M, GDK_KEY_5, ID_FIRSTBUFFER + 4},
	{ m__M, GDK_KEY_6, ID_FIRSTBUFFER + 5},
	{ m__M, GDK_KEY_7, ID_FIRSTBUFFER + 6},
	{ m__M, GDK_KEY_8, ID_FIRSTBUFFER + 7},
	{ m__M, GDK_KEY_9, ID_FIRSTBUFFER + 8},
	{ m__M, GDK_KEY_0, ID_FIRSTBUFFER + 9},
	{ 0,   0,		 0 }
};

static GtkActionEntry actions_file[] = {
  { "ActionFileSave", GTK_STOCK_SAVE, N_("_Save"), "<control>s",
	N_("Save current file"), G_CALLBACK (on_save_activate)},
  { "ActionFileSaveAs", GTK_STOCK_SAVE_AS, N_("Save _As…"),  "<shift><control>s",
	N_("Save the current file with a different name"),
    G_CALLBACK (on_save_as_activate)},
  { "ActionFileSaveAll", GTK_STOCK_SAVE, N_("Save A_ll"), "<shift><control>l",
	N_("Save all currently open files, except new files"),
    G_CALLBACK (on_save_all_activate)},
  { "ActionFileClose", GTK_STOCK_CLOSE, N_("_Close File"), "<control>w",
	N_("Close current file"),
    G_CALLBACK (on_close_file_activate)},
  { "ActionFileCloseAll", GTK_STOCK_CLOSE, N_("Close All"), "<shift><control>w",
	N_("Close all files"),
    G_CALLBACK (on_close_all_file_activate)},
  { "ActionFileCloseOther", GTK_STOCK_CLOSE, N_("Close Others"), "<shift><control>o",
	N_("Close other documents"),
    G_CALLBACK (on_close_other_file_activate)},
  { "ActionFileReload", GTK_STOCK_REVERT_TO_SAVED, N_("Reload F_ile"), NULL,
	N_("Reload current file"),
    G_CALLBACK (on_reload_file_activate)},
  { "ActionMenuFileRecentFiles", NULL, N_("Recent _Files"),  NULL, NULL, NULL},	/* menu title */
};

static GtkActionEntry actions_print[] = {
  { "ActionPrintFile", GTK_STOCK_PRINT, N_("_Print…"), "<control>p",
	N_("Print the current file"), G_CALLBACK (on_print_activate)},
  { "ActionPrintPreview",
#ifdef GTK_STOCK_PRINT_PREVIEW
	GTK_STOCK_PRINT_PREVIEW
#else
	NULL
#endif
	, N_("_Print Preview"), NULL,
	N_("Preview the current file in print format"),
    G_CALLBACK (on_print_preview_activate)},
};

static GtkActionEntry actions_transform[] = {
  { "ActionMenuEditTransform", NULL, N_("_Transform"), NULL, NULL, NULL}, /* menu title */
  { "ActionEditMakeSelectionUppercase", NULL, N_("_Make Selection Uppercase"), NULL,
	N_("Make the selected text uppercase"),
    G_CALLBACK (on_editor_command_upper_case_activate)},
  { "ActionEditMakeSelectionLowercase", NULL, N_("Make Selection Lowercase"), NULL,
	N_("Make the selected text lowercase"),
    G_CALLBACK (on_editor_command_lower_case_activate)},
  { "ActionEditConvertCRLF", NULL, N_("Convert EOL to CRLF"), NULL,
	N_("Convert End Of Line characters to DOS EOL (CRLF)"),
    G_CALLBACK (on_editor_command_eol_crlf_activate)},
  { "ActionEditConvertLF", NULL, N_("Convert EOL to LF"), NULL,
	N_("Convert End Of Line characters to Unix EOL (LF)"),
    G_CALLBACK (on_editor_command_eol_lf_activate)},
  { "ActionEditConvertCR", NULL, N_("Convert EOL to CR"), NULL,
	N_("Convert End Of Line characters to Mac OS EOL (CR)"),
    G_CALLBACK (on_editor_command_eol_cr_activate)},
  { "ActionEditConvertEOL", NULL, N_("Convert EOL to Majority EOL"), NULL,
	N_("Convert End Of Line characters to the most common EOL found in the file"),
    G_CALLBACK (on_transform_eolchars1_activate)},
};

static GtkActionEntry actions_select[] = {
	{ "ActionMenuEditSelect",  NULL, N_("_Select"), NULL, NULL, NULL}, /* menu title */
	{ "ActionEditSelectAll",
		GTK_STOCK_SELECT_ALL, N_("Select _All"), "<control>a",
		N_("Select all text in the editor"),
		G_CALLBACK (on_editor_command_select_all_activate)},
	{ "ActionEditSelectBlock", ANJUTA_STOCK_BLOCK_SELECT, N_("Select _Code Block"),
		"<shift><control>b", N_("Select the current code block"),
		G_CALLBACK (on_editor_command_select_block_activate)}
};

static GtkActionEntry actions_comment[] = {
  { "ActionMenuEditComment", NULL, N_("Co_mment"), NULL, NULL, NULL},
	/* Block comment: Uses line-comment (comment that affects only single line
	such as '//' or '#') and comments a block of lines. */
  { "ActionEditCommentBlock", NULL, N_("_Block Comment/Uncomment"), NULL,
	N_("Block comment the selected text"),
    G_CALLBACK (on_comment_block)},
	/* Box comment: Uses stream-comment to comment a block of lines, usually with
	some decorations, to give an appearance of box. */
  { "ActionEditCommentBox", NULL, N_("Bo_x Comment/Uncomment"), NULL,
	N_("Box comment the selected text"),
    G_CALLBACK (on_comment_box)},
	/* Stream comment: Uses 'stream comment' (comment that affects a stream of
	characters -- has start and end comment code) and comments any code from
	arbitrary start position to arbitrary end position (can be in middle of
	lines). */
  { "ActionEditCommentStream", NULL, N_("_Stream Comment/Uncomment"), NULL,
	N_("Stream comment the selected text"),
    G_CALLBACK (on_comment_stream)},
};

static GtkActionEntry actions_navigation[] = {
  { "ActionMenuGoto", NULL, N_("_Go to"), NULL, NULL, NULL},/* menu title */
  { "ActionEditGotoLine", ANJUTA_STOCK_GOTO_LINE, N_("_Line Number…"),
	"<control><alt>g", N_("Go to a particular line in the editor"),
    G_CALLBACK (on_goto_line_no1_activate)},
  { "ActionEditGotoMatchingBrace", GTK_STOCK_JUMP_TO, N_("Matching _Brace"),
	"<control><alt>m", N_("Go to the matching brace in the editor"),
    G_CALLBACK (on_editor_command_match_brace_activate)},
  { "ActionEditGotoBlockStart", ANJUTA_STOCK_BLOCK_START, N_("_Start of Block"),
	"<control><alt>s", N_("Go to the start of the current block"),
    G_CALLBACK (on_goto_block_start1_activate)},
  { "ActionEditGotoBlockEnd", ANJUTA_STOCK_BLOCK_END, N_("_End of Block"),
	"<control><alt>e", N_("Go to the end of the current block"),
    G_CALLBACK (on_goto_block_end1_activate)},
  { "ActionEditGotoHistoryPrev", ANJUTA_STOCK_HISTORY_PREV, N_("Previous _History"),
	"<alt>Left", N_("Go to previous history"),
    G_CALLBACK (on_prev_history)},
  { "ActionEditGotoHistoryNext", ANJUTA_STOCK_HISTORY_NEXT, N_("Next Histor_y"),
	"<alt>Right", N_("Go to next history"),
    G_CALLBACK (on_next_history)}
};

static GtkActionEntry actions_search[] = {
  { "ActionMenuEditSearch", NULL, N_("_Search"), NULL, NULL, NULL},
  { "ActionEditSearchQuickSearch", GTK_STOCK_FIND, N_("_Quick Search"),
	"<control>f", N_("Quick editor embedded search"),
    G_CALLBACK (on_show_search)},
  { "ActionEditSearchQuickSearchAgain", GTK_STOCK_FIND, N_("Find _Next"),
	"<control>g", N_("Search for next appearance of term."),
    G_CALLBACK (on_repeat_quicksearch)},
  { "ActionEditSearchReplace", GTK_STOCK_FIND, N_("Find and R_eplace…"),
	"<control>h", N_("Search and replace"),
    G_CALLBACK (on_search_and_replace)},
  { "ActionEditSearchFindPrevious", GTK_STOCK_FIND, N_("Find _Previous"),
	"<control><shift>g",
	N_("Repeat the last Find command"),
	G_CALLBACK (on_search_previous)},
  { "ActionSearchboxPopupClearHighlight", GTK_STOCK_FIND, N_("Clear Highlight"),
	NULL, N_("Clear all highlighted text"),
	G_CALLBACK (on_search_popup_clear_highlight)},
  { "ActionEditFindFiles", GTK_STOCK_FIND_AND_REPLACE, N_("Find in Files"),
	NULL, N_("Search in project files"),
	G_CALLBACK (on_search_find_in_files)}
};

static GtkToggleActionEntry actions_searchbox_popup[] = {
  { "ActionSearchboxPopupCaseCheck", GTK_STOCK_FIND,
    N_("Case Sensitive"), NULL,
    N_("Match case in search results."),
	G_CALLBACK (on_search_popup_case_sensitive_toggle)},
  { "ActionSearchboxPopupHighlightAll", GTK_STOCK_FIND,
    N_("Highlight All"), NULL,
    N_("Highlight all occurrences"),
	G_CALLBACK (on_search_popup_highlight_toggle)},
  { "ActionSearchboxRegexSearch", GTK_STOCK_FIND,
	N_("Regular Expression"), NULL,
	N_("Search using regular expressions"),
	G_CALLBACK (on_search_popup_regex_search)}
};


static GtkActionEntry actions_edit[] = {
  { "ActionMenuEdit", NULL, N_("_Edit"), NULL, NULL, NULL},/* menu title */
  { "ActionMenuViewEditor", NULL, N_("_Editor"), NULL, NULL, NULL},
  { "ActionViewEditorAddView",
#ifdef GTK_STOCK_EDIT
	GTK_STOCK_EDIT
#else
	NULL
#endif
	, N_("_Add Editor View"), NULL,
	N_("Add one more view of current document"),
    G_CALLBACK (on_editor_add_view_activate)},
  { "ActionViewEditorRemoveView", NULL, N_("_Remove Editor View"), NULL,
	N_("Remove current view of the document"),
    G_CALLBACK (on_editor_remove_view_activate)},
  { "ActionEditUndo", GTK_STOCK_UNDO, N_("U_ndo"), "<control>z",
	N_("Undo the last action"),
    G_CALLBACK (on_editor_command_undo_activate)},
  { "ActionEditRedo", GTK_STOCK_REDO, N_("_Redo"), "<shift><control>z",
	N_("Redo the last undone action"),
    G_CALLBACK (on_editor_command_redo_activate)},
  { "ActionEditCut", GTK_STOCK_CUT, N_("C_ut"), "<control>x",
	N_("Cut the selected text from the editor to the clipboard"),
    G_CALLBACK (on_editor_command_cut_activate)},
  { "ActionEditCopy", GTK_STOCK_COPY, N_("_Copy"), "<control>c",
	N_("Copy the selected text to the clipboard"),
    G_CALLBACK (on_editor_command_copy_activate)},
  { "ActionEditPaste", GTK_STOCK_PASTE, N_("_Paste"), "<control>v",
	N_("Paste the content of clipboard at the current position"),
    G_CALLBACK (on_editor_command_paste_activate)},
  { "ActionEditClear",
#ifdef GTK_STOCK_DISCARD
	GTK_STOCK_DISCARD
#else
	GTK_STOCK_CLEAR
#endif
	, N_("_Clear"), NULL,
	N_("Delete the selected text from the editor"),
    G_CALLBACK (on_editor_command_clear_activate)},
	{ "ActionEditAutocomplete", ANJUTA_STOCK_AUTOCOMPLETE,
	 N_("_Auto-Complete"), "<control>Return",
	 N_("Auto-complete the current word"), G_CALLBACK (on_autocomplete_activate)
	}
};


static GtkActionEntry actions_zoom[] = {
  { "ActionViewEditorZoomIn", GTK_STOCK_ZOOM_IN, N_("Zoom In"), "<control>KP_Add",
	N_("Zoom in: Increase font size"),
    G_CALLBACK (on_zoom_in_text_activate)},
  { "ActionViewEditorZoomOut", GTK_STOCK_ZOOM_OUT, N_("Zoom Out"), "<control>KP_Subtract",
	N_("Zoom out: Decrease font size"),
    G_CALLBACK (on_zoom_out_text_activate)}
};

static GtkActionEntry actions_style[] = {
  { "ActionMenuFormatStyle", NULL, N_("_Highlight Mode"), NULL, NULL, NULL}/* menu title */
};

static GtkActionEntry actions_format[] = {
  { "ActionFormatFoldCloseAll", ANJUTA_STOCK_FOLD_CLOSE, N_("_Close All Folds"),
	NULL, N_("Close all code folds in the editor"),
    G_CALLBACK (on_editor_command_close_folds_all_activate)},
  { "ActionFormatFoldOpenAll", ANJUTA_STOCK_FOLD_OPEN, N_("_Open All Folds"),
	NULL, N_("Open all code folds in the editor"),
    G_CALLBACK (on_editor_command_open_folds_all_activate)},
  { "ActionFormatFoldToggle", ANJUTA_STOCK_FOLD_TOGGLE, N_("_Toggle Current Fold"),
	NULL, N_("Toggle current code fold in the editor"),
    G_CALLBACK (on_editor_command_toggle_fold_activate)},
};

static GtkActionEntry actions_documents[] = {
  {"ActionMenuDocuments", NULL, N_("_Documents"), NULL, NULL, NULL},
  { "ActionDocumentsPrevious", GTK_STOCK_GO_BACK, N_("Previous Document"),
	"<control>Page_Up", N_("Switch to previous document"),
    G_CALLBACK (on_previous_document)},
  { "ActionDocumentsNext", GTK_STOCK_GO_FORWARD, N_("Next Document"),
	"<control>Page_Down", N_("Switch to next document"),
    G_CALLBACK (on_next_document)},
};

static GtkActionEntry actions_bookmarks[] = {
	{ "ActionMenuBookmark", NULL, N_("Bookmar_k"), NULL, NULL, NULL},
 	{ "ActionBookmarkToggle", GTK_STOCK_ADD, N_("_Toggle Bookmark"),
		 "<control>k", N_("Toggle bookmark at the current line position"),
	G_CALLBACK (on_bookmark_toggle_activate)},
	{ "ActionBookmarkPrev", ANJUTA_STOCK_BOOKMARK_PREV, N_("_Previous Bookmark"),
	"<control>comma", N_("Jump to the previous bookmark in the file"),
	G_CALLBACK (on_bookmark_prev_activate)},
 	{ "ActionBookmarkNext", ANJUTA_STOCK_BOOKMARK_NEXT, N_("_Next Bookmark"),
 	"<control>period", N_("Jump to the next bookmark in the file"),
 	G_CALLBACK (on_bookmark_next_activate)},
 	{ "ActionBookmarksClear", ANJUTA_STOCK_BOOKMARK_CLEAR, N_("_Clear All Bookmarks"),
 	NULL, N_("Clear bookmarks"),
 	G_CALLBACK (on_bookmarks_clear_activate)},
 };

struct ActionGroupInfo {
	GtkActionEntry *group;
	gint size;
	gchar *name;
	gchar *label;
};

struct ActionToggleGroupInfo {
	GtkToggleActionEntry *group;
	gint size;
	gchar *name;
	gchar *label;
};

static struct ActionGroupInfo action_groups[] = {
	{ actions_file, G_N_ELEMENTS (actions_file), "ActionGroupEditorFile",  N_("Editor file operations")},
	{ actions_print, G_N_ELEMENTS (actions_print), "ActionGroupEditorPrint",  N_("Editor print operations")},
	{ actions_transform, G_N_ELEMENTS (actions_transform), "ActionGroupEditorTransform", N_("Editor text transformation") },
	{ actions_select, G_N_ELEMENTS (actions_select), "ActionGroupEditorSelect", N_("Editor text selection") },
//	{ actions_insert, G_N_ELEMENTS (actions_insert), "ActionGroupEditorInsert", N_("Editor text insertions") },
	{ actions_comment, G_N_ELEMENTS (actions_comment), "ActionGroupEditorComment", N_("Editor code commenting") },
	{ actions_navigation, G_N_ELEMENTS (actions_navigation), "ActionGroupEditorNavigate", N_("Editor navigations") },
	{ actions_edit, G_N_ELEMENTS (actions_edit), "ActionGroupEditorEdit", N_("Editor edit operations") },
	{ actions_zoom, G_N_ELEMENTS (actions_zoom), "ActionGroupEditorZoom", N_("Editor zoom operations") },
	{ actions_style, G_N_ELEMENTS (actions_style), "ActionGroupEditorStyle", N_("Editor syntax highlighting styles") },
	{ actions_format, G_N_ELEMENTS (actions_format), "ActionGroupEditorFormat", N_("Editor text formating") },
	{ actions_search, G_N_ELEMENTS (actions_search), "ActionGroupEditorSearch", N_("Simple searching") },
	{ actions_documents, G_N_ELEMENTS (actions_documents), "ActionGroupDocuments", N_("Documents") },
	{ actions_bookmarks, G_N_ELEMENTS (actions_bookmarks), "ActionGroupBookmarks", N_("Bookmarks") }
};

static struct ActionToggleGroupInfo action_toggle_groups[] = {
	{ actions_searchbox_popup, G_N_ELEMENTS(actions_searchbox_popup),
	  "ActionGroupEditorSearchOptions", N_("Toggle search options")	}
};

// void pref_set_style_combo(DocmanPlugin *plugin);

#define MAX_TITLE_LENGTH 80

static void
on_editor_lang_changed (IAnjutaEditor* editor, const gchar* language,
                        DocmanPlugin* plugin);

static gchar*
get_directory_display_name (DocmanPlugin* plugin,
							GFile* file)
{
	gchar* dir;
	gchar* display_uri = g_file_get_parse_name (file);
	gchar* display_dir;
	dir = anjuta_util_uri_get_dirname (display_uri);

	display_dir = anjuta_util_str_middle_truncate (dir,
												   MAX (20, MAX_TITLE_LENGTH));
	g_free (display_uri);
	g_free (dir);
	return display_dir;
}

static void
update_title (DocmanPlugin* doc_plugin)
{
	IAnjutaDocument *doc;
	AnjutaStatus *status;
	gchar *title;

	doc = anjuta_docman_get_current_document (ANJUTA_DOCMAN (doc_plugin->docman));

	if (doc)
	{
		gchar* real_filename;
		gchar *dir;
		const gchar *filename;
		filename = ianjuta_document_get_filename (doc, NULL);
		GFile* file;
		file = ianjuta_file_get_file (IANJUTA_FILE (doc), NULL);
		if (file)
		{
			dir = get_directory_display_name (doc_plugin, file);
			g_object_unref (file);
		}
		else
		{
			dir = NULL;
		}
		if (ianjuta_file_savable_is_dirty(IANJUTA_FILE_SAVABLE (doc), NULL))
		{
			gchar* dirty_name = g_strconcat ("*", filename, NULL);
			real_filename = dirty_name;
		}
		else
			real_filename = g_strdup (filename);

		if (doc_plugin->project_name)
		{
			if (dir)
				title = g_strdup_printf ("%s (%s) - %s", real_filename, dir,
										 doc_plugin->project_name);
			else
				title = g_strdup_printf ("%s - %s", real_filename,
										 doc_plugin->project_name);
		}
		else
		{
			if (dir)
				title = g_strdup_printf ("%s (%s)", real_filename, dir);
			else
				title = g_strdup_printf ("%s", real_filename);
		}
		g_free (real_filename);
		g_free (dir);
	}
	else
	{
		title = g_strdup (doc_plugin->project_name);
	}
	status = anjuta_shell_get_status (ANJUTA_PLUGIN (doc_plugin)->shell, NULL);
	/* NULL title is ok */
	anjuta_status_set_title (status, title);
	g_free (title);
}

static void
value_added_project_root_uri (AnjutaPlugin *plugin, const gchar *name,
							  const GValue *value, gpointer user_data)
{
	DocmanPlugin *doc_plugin;
	const gchar *root_uri;

	doc_plugin = ANJUTA_PLUGIN_DOCMAN (plugin);

	DEBUG_PRINT ("%s", "Project added");

	g_free (doc_plugin->project_name);
	g_free (doc_plugin->project_path);
	doc_plugin->project_name = NULL;
	doc_plugin->project_path = NULL;

	if (doc_plugin->search_files)
		search_files_update_project (SEARCH_FILES(doc_plugin->search_files));
	
	root_uri = g_value_get_string (value);
	if (root_uri)
	{
		GFile* file = g_file_new_for_uri (root_uri);
		gchar* path = g_file_get_path (file);

		doc_plugin->project_name = g_file_get_basename (file);
		doc_plugin->project_path = path;

		if (doc_plugin->project_name)
		{
			update_title (doc_plugin);
		}
		g_object_unref (file);

		anjuta_docman_project_path_updated (ANJUTA_DOCMAN (doc_plugin->docman));
	}
}

static void
value_removed_project_root_uri (AnjutaPlugin *plugin, const gchar *name,
								gpointer user_data)
{
	DocmanPlugin *doc_plugin;

	doc_plugin = ANJUTA_PLUGIN_DOCMAN (plugin);

	DEBUG_PRINT ("%s", "Project removed");

	g_free (doc_plugin->project_name);
	g_free (doc_plugin->project_path);
	doc_plugin->project_name = NULL;
	doc_plugin->project_path = NULL;

	if (doc_plugin->search_files)
		search_files_update_project (SEARCH_FILES(doc_plugin->search_files));

	update_title(doc_plugin);
	anjuta_docman_project_path_updated (ANJUTA_DOCMAN (doc_plugin->docman));
}

static void
ui_give_shorter_names (AnjutaPlugin *plugin)
{
	AnjutaUI *ui;
	GtkAction *action;

	ui = anjuta_shell_get_ui (ANJUTA_PLUGIN (plugin)->shell, NULL);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorFile",
								   "ActionFileSave");
	g_object_set (G_OBJECT (action), "short-label", _("Save"),
				  "is-important", TRUE, NULL);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorFile",
								   "ActionFileReload");
	g_object_set (G_OBJECT (action), "short-label", _("Reload"), NULL);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorEdit",
								   "ActionEditUndo");
	g_object_set (G_OBJECT (action), "is-important", TRUE, NULL);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorNavigate",
								   "ActionEditGotoLine");
	g_object_set (G_OBJECT (action), "short-label", _("Go to"), NULL);
}

static void
update_document_ui_enable_all (AnjutaPlugin *plugin)
{
	AnjutaUI *ui;
	gint i, j;
	GtkAction *action;

	ui = anjuta_shell_get_ui (ANJUTA_PLUGIN (plugin)->shell, NULL);
	for (i = 0; i < G_N_ELEMENTS (action_groups); i++)
	{
		for (j = 0; j < action_groups[i].size; j++)
		{
			action = anjuta_ui_get_action (ui, action_groups[i].name,
										   action_groups[i].group[j].name);
			if (action_groups[i].group[j].callback)
			{
				g_object_set (G_OBJECT (action), "sensitive", TRUE, NULL);
			}
		}
	}
}

static void
update_document_ui_disable_all (AnjutaPlugin *plugin)
{
	AnjutaUI *ui;
	gint i, j;
	GtkAction *action;

	ui = anjuta_shell_get_ui (ANJUTA_PLUGIN (plugin)->shell, NULL);
	for (i = 0; i < G_N_ELEMENTS (action_groups); i++)
	{
		for (j = 0; j < action_groups[i].size; j++)
		{
			/* Never disable find in files. */
			if (!strcmp (action_groups[i].group[j].name, "ActionEditFindFiles"))
				continue;

			action = anjuta_ui_get_action (ui, action_groups[i].name,
										   action_groups[i].group[j].name);
			if (action_groups[i].group[j].callback)
			{
				g_object_set (G_OBJECT (action), "sensitive", FALSE, NULL);
			}
		}
	}
}

static void
update_document_ui_undo_items (AnjutaPlugin *plugin, IAnjutaDocument* doc)
{
	AnjutaUI *ui;
	GtkAction *action;

	ui = anjuta_shell_get_ui (plugin->shell, NULL);

	action = anjuta_ui_get_action (ui, "ActionGroupEditorEdit",
								   "ActionEditUndo");
	g_object_set (G_OBJECT (action), "sensitive",
				  ianjuta_document_can_undo (doc, NULL), NULL);

	action = anjuta_ui_get_action (ui, "ActionGroupEditorEdit",
								   "ActionEditRedo");
	g_object_set (G_OBJECT (action), "sensitive",
				  ianjuta_document_can_redo (doc, NULL), NULL);
}

static void
update_document_ui_save_items (AnjutaPlugin *plugin, IAnjutaDocument *doc)
{
	AnjutaUI *ui;
	GtkAction *action;

	ui = anjuta_shell_get_ui (plugin->shell, NULL);

	if (anjuta_docman_get_current_document (ANJUTA_DOCMAN(ANJUTA_PLUGIN_DOCMAN(plugin)->docman)) ==
		doc)
	{
		action = anjuta_ui_get_action (ui, "ActionGroupEditorFile",
		                               "ActionFileSave");
		g_object_set (G_OBJECT (action), "sensitive",
		              ianjuta_file_savable_is_dirty(IANJUTA_FILE_SAVABLE(doc), NULL),
		              NULL);
	}
}
static void
update_document_ui_interface_items (AnjutaPlugin *plugin, IAnjutaDocument *doc)
{
	AnjutaUI *ui;
	GtkAction *action;
	gboolean flag;

	ui = anjuta_shell_get_ui (plugin->shell, NULL);

	/* IAnjutaEditorLanguage */
	flag = IANJUTA_IS_EDITOR_LANGUAGE (doc);

	action = anjuta_ui_get_action (ui, "ActionGroupEditorStyle",
								   "ActionMenuFormatStyle");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	/* IAnjutaDocument */
	flag = IANJUTA_IS_DOCUMENT (doc);

	action = anjuta_ui_get_action (ui, "ActionGroupEditorEdit",
								   "ActionEditCut");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	action = anjuta_ui_get_action (ui, "ActionGroupEditorEdit",
								   "ActionEditCopy");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	action = anjuta_ui_get_action (ui, "ActionGroupEditorEdit",
								   "ActionEditPaste");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	action = anjuta_ui_get_action (ui, "ActionGroupEditorEdit",
								   "ActionEditClear");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	/* IAnjutaEditorSelection */
	flag = IANJUTA_IS_EDITOR_SELECTION (doc);

	action = anjuta_ui_get_action (ui, "ActionGroupEditorSelect",
								   "ActionEditSelectAll");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	action = anjuta_ui_get_action (ui, "ActionGroupEditorSelect",
								   "ActionEditSelectBlock");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	/* IAnjutaEditorConvert */
	flag = IANJUTA_IS_EDITOR_CONVERT (doc);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorTransform",
								   "ActionEditMakeSelectionUppercase");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	action = anjuta_ui_get_action (ui, "ActionGroupEditorTransform",
								   "ActionEditMakeSelectionLowercase");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	/* IAnjutaEditorLineMode */
	flag = IANJUTA_IS_EDITOR_LINE_MODE (doc);

	action = anjuta_ui_get_action (ui, "ActionGroupEditorTransform",
								   "ActionEditConvertCRLF");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorTransform",
								   "ActionEditConvertLF");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorTransform",
								   "ActionEditConvertCR");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorTransform",
								   "ActionEditConvertEOL");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	/* IAnjutaEditorView */
	flag = IANJUTA_IS_EDITOR_VIEW (doc);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorEdit",
								   "ActionViewEditorAddView");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorEdit",
								   "ActionViewEditorRemoveView");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	/* IAnjutaEditorFolds */
	flag = IANJUTA_IS_EDITOR_FOLDS (doc);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorFormat",
								   "ActionFormatFoldCloseAll");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	flag = IANJUTA_IS_EDITOR_FOLDS (doc);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorFormat",
								   "ActionFormatFoldOpenAll");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	flag = IANJUTA_IS_EDITOR_FOLDS (doc);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorFormat",
								   "ActionFormatFoldToggle");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	/* IAnjutaEditorComment */
	flag = IANJUTA_IS_EDITOR_COMMENT (doc);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorComment",
								   "ActionMenuEditComment");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	/* IAnjutaEditorZoom */
	flag = IANJUTA_IS_EDITOR_ZOOM (doc);
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorZoom",
								   "ActionViewEditorZoomIn");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorZoom",
								   "ActionViewEditorZoomOut");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	/* IAnjutaEditorGoto */
	flag = IANJUTA_IS_EDITOR_GOTO (doc);
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorNavigate",
								   "ActionEditGotoBlockStart");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorNavigate",
								   "ActionEditGotoBlockEnd");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorNavigate",
								   "ActionEditGotoMatchingBrace");
	g_object_set (G_OBJECT (action), "visible", flag, "sensitive", flag, NULL);

	/* IAnjutaEditorSearch */
	flag = IANJUTA_IS_EDITOR_SEARCH (doc);
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorSearch",
								   "ActionEditSearchQuickSearch");
	g_object_set (G_OBJECT (action), "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorSearch",
								   "ActionEditSearchQuickSearchAgain");
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorSearch",
								   "ActionEditSearchFindPrevious");
	g_object_set (G_OBJECT (action), "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui, "ActionGroupEditorSearch",
									"ActionEditSearchReplace");
	g_object_set (G_OBJECT (action), "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorSearch",
								   "ActionSearchboxPopupClearHighlight");
	g_object_set (G_OBJECT (action), "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorNavigate",
								   "ActionEditGotoLine");
	g_object_set (G_OBJECT (action), "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorSearchOptions",
								   "ActionSearchboxPopupCaseCheck");
	g_object_set (G_OBJECT (action), "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorSearchOptions",
								   "ActionSearchboxPopupHighlightAll");
	g_object_set (G_OBJECT (action), "sensitive", flag, NULL);
	action = anjuta_ui_get_action (ui,  "ActionGroupEditorSearchOptions",
								   "ActionSearchboxRegexSearch");
	g_object_set (G_OBJECT (action), "sensitive", flag, NULL);

	/* IAnjutaEditorAssist */
	flag = IANJUTA_IS_EDITOR_ASSIST (doc);

	/* Enable autocompletion action */
	action = anjuta_ui_get_action (ui,
	                               "ActionGroupEditorEdit",
	                               "ActionEditAutocomplete");
	g_object_set (G_OBJECT (action), "visible", flag,
	              "sensitive", flag, NULL);
}

static void
update_document_ui (AnjutaPlugin *plugin, IAnjutaDocument *doc)
{
	if (doc == NULL)
	{
		update_document_ui_disable_all (plugin);
		return;
	}
	update_document_ui_enable_all (plugin);
	update_document_ui_save_items (plugin, doc);
	update_document_ui_interface_items (plugin, doc);
}

static void
on_document_update_save_ui (IAnjutaDocument *doc,
							AnjutaPlugin *plugin)
{
	update_document_ui_save_items (plugin, doc);
	update_title(ANJUTA_PLUGIN_DOCMAN (plugin));
}

static void
register_stock_icons (AnjutaPlugin *plugin)
{
	static gboolean registered = FALSE;
	if (registered)
		return;
	registered = TRUE;

	/* Register stock icons */
	BEGIN_REGISTER_ICON (plugin);
	REGISTER_ICON (ICON_FILE, "editor-plugin-icon");
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_FOLD_TOGGLE, ANJUTA_STOCK_FOLD_TOGGLE);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_FOLD_OPEN, ANJUTA_STOCK_FOLD_OPEN);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_FOLD_CLOSE, ANJUTA_STOCK_FOLD_CLOSE);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_INDENT_DCR, ANJUTA_STOCK_INDENT_DCR);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_INDENT_INC, ANJUTA_STOCK_INDENT_INC);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_BLOCK_SELECT, ANJUTA_STOCK_BLOCK_SELECT);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_BOOKMARK_TOGGLE, ANJUTA_STOCK_BOOKMARK_TOGGLE);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_BOOKMARK_PREV, ANJUTA_STOCK_BOOKMARK_PREV);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_BOOKMARK_NEXT, ANJUTA_STOCK_BOOKMARK_NEXT);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_BOOKMARK_CLEAR, ANJUTA_STOCK_BOOKMARK_CLEAR);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_BLOCK_START, ANJUTA_STOCK_BLOCK_START);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_BLOCK_END, ANJUTA_STOCK_BLOCK_END);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_GOTO_LINE, ANJUTA_STOCK_GOTO_LINE);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_HISTORY_NEXT, ANJUTA_STOCK_HISTORY_NEXT);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_HISTORY_PREV, ANJUTA_STOCK_HISTORY_PREV);
	REGISTER_ICON_FULL (ANJUTA_PIXMAP_AUTOCOMPLETE, ANJUTA_STOCK_AUTOCOMPLETE);
	END_REGISTER_ICON;
}

#define TEXT_ZOOM_FACTOR           "text-zoom-factor"

static void
update_status (DocmanPlugin *plugin, IAnjutaEditor *te)
{
	AnjutaStatus *status;

	if (te)
	{
		gchar *edit /*, *mode*/;
		guint line, col, zoom;

		status = anjuta_shell_get_status (ANJUTA_PLUGIN (plugin)->shell, NULL);
		if (status == NULL)
			return;

		line = ianjuta_editor_get_lineno (te, NULL);
		col = ianjuta_editor_get_column (te, NULL);

		if (ianjuta_editor_get_overwrite (te, NULL))
		{
			edit = g_strdup (_("OVR"));
		}
		else
		{
			edit = g_strdup (_("INS"));
		}

		if (IANJUTA_IS_EDITOR_ZOOM(te))
		{
			zoom = g_settings_get_int (plugin->settings, TEXT_ZOOM_FACTOR);
			anjuta_status_set_default (status, _("Zoom"), "%d", zoom);
		}
		else
			anjuta_status_set_default (status, _("Zoom"), NULL);

		anjuta_status_set_default (status, _("Line"), "%04d", line);
		anjuta_status_set_default (status, _("Col"), "%03d", col);
		anjuta_status_set_default (status, _("Mode"), edit);
		// anjuta_status_set_default (status, _("EOL"), mode);

		g_free (edit);
		/* g_free (mode); */
	}
	else
	{
		status = anjuta_shell_get_status (ANJUTA_PLUGIN (plugin)->shell, NULL);
		if (status)
		{
			anjuta_status_set (status, "");
		}
	}
}

static void
on_document_update_ui (IAnjutaDocument *doc, DocmanPlugin *plugin)
{
	IAnjutaDocument *curdoc;

	curdoc = anjuta_docman_get_current_document (ANJUTA_DOCMAN (plugin->docman));
	update_document_ui_undo_items (ANJUTA_PLUGIN(plugin), curdoc);

	if (IANJUTA_IS_EDITOR (curdoc) && curdoc == doc)
	{
		update_status (plugin, IANJUTA_EDITOR (curdoc));
	}
}

/* Remove all instances of c from the string s. */
static void remove_char(gchar *s, gchar c)
{
	gchar *t = s;
	for (; *s ; ++s)
		if (*s != c)
			*t++ = *s;
	*t = '\0';
}

/* Compare two strings, ignoring _ characters which indicate mnemonics.
 * Returns -1, 0, or 1, just like strcmp(). */
static gint
menu_name_compare(const gchar *s, const char *t)
{
	gchar *s1 = g_utf8_strdown(s, -1);
	gchar *t1 = g_utf8_strdown(t, -1);
	remove_char(s1, '_');
	remove_char(t1, '_');
	gint result = g_utf8_collate (s1, t1);
	g_free(s1);
	g_free(t1);
	return result;
}

static gint
compare_language_name(const gchar *lang1, const gchar *lang2, IAnjutaEditorLanguage *manager)
{
	const gchar *fullname1 = ianjuta_editor_language_get_language_name (manager, lang1, NULL);
	const gchar *fullname2 = ianjuta_editor_language_get_language_name (manager, lang2, NULL);
	return menu_name_compare(fullname1, fullname2);
}

static GtkWidget*
create_highlight_submenu (DocmanPlugin *plugin, IAnjutaEditor *editor)
{
	const GList *languages, *node;
	GList *sorted_languages;
	GtkWidget *submenu;
	GtkWidget *auto_menuitem;
	submenu = gtk_menu_new ();

	if (!editor || !IANJUTA_IS_EDITOR_LANGUAGE (editor))
		return NULL;

	languages = ianjuta_editor_language_get_supported_languages (IANJUTA_EDITOR_LANGUAGE (editor), NULL);
	if (!languages)
		return NULL;

	/* Automatic highlight menu */
	auto_menuitem = gtk_radio_menu_item_new_with_mnemonic (NULL, _("Automatic"));
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(auto_menuitem), TRUE);
	g_signal_connect (G_OBJECT (auto_menuitem), "activate",
					  G_CALLBACK (on_force_hilite_activate),
					  plugin);
	g_object_set_data(G_OBJECT(auto_menuitem), "language_code", "auto-detect");
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), auto_menuitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), gtk_separator_menu_item_new());

	/* Sort languages so they appear alphabetically in the menu. */
	sorted_languages = g_list_copy((GList *) languages);
	sorted_languages = g_list_sort_with_data(sorted_languages, (GCompareDataFunc) compare_language_name,
											 IANJUTA_EDITOR_LANGUAGE (editor));

	node = sorted_languages;
	while (node)
	{
		const gchar *lang = node->data;
		const gchar *name = ianjuta_editor_language_get_language_name (IANJUTA_EDITOR_LANGUAGE (editor), lang, NULL);

		/* Should fix #493583 */
		if (name != NULL)
		{
			GtkWidget *menuitem;
			menuitem = gtk_radio_menu_item_new_with_mnemonic_from_widget (GTK_RADIO_MENU_ITEM(auto_menuitem), name);
			g_object_set_data_full (G_OBJECT (menuitem), "language_code",
									g_strdup (lang),
									(GDestroyNotify)g_free);
			g_signal_connect (G_OBJECT (menuitem), "activate",
							  G_CALLBACK (on_force_hilite_activate),
							  plugin);
			gtk_menu_shell_append (GTK_MENU_SHELL (submenu), menuitem);
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(menuitem), FALSE);
		}
		node = g_list_next (node);
	}
	g_list_free(sorted_languages);

	gtk_widget_show_all (submenu);
	return submenu;
}

static void
on_support_plugin_deactivated (AnjutaPlugin* plugin, DocmanPlugin* docman_plugin)
{
	docman_plugin->support_plugins = g_list_remove (docman_plugin->support_plugins, plugin);
}

static GList*
load_new_support_plugins (DocmanPlugin* docman_plugin, GList* new_plugin_handle,
						  AnjutaPluginManager* plugin_manager)
{
	GList* node;
	GList* needed_plugins = NULL;
	for (node = new_plugin_handle; node != NULL; node = g_list_next (node))
	{
		AnjutaPluginHandle *handle = (AnjutaPluginHandle *)node->data;
		GObject* new_plugin = anjuta_plugin_manager_get_plugin_by_handle (plugin_manager,
		                                                                  handle);
		GList* item = g_list_find (docman_plugin->support_plugins, new_plugin);
		if (!item)
		{
			DEBUG_PRINT ("Loading plugin: %s", anjuta_plugin_handle_get_id (handle));
			g_signal_connect (new_plugin, "deactivated",
							  G_CALLBACK (on_support_plugin_deactivated), docman_plugin);
		}
		needed_plugins = g_list_append (needed_plugins, new_plugin);
	}
	return needed_plugins;
}

static void
unload_unused_support_plugins (DocmanPlugin* docman_plugin,
							   GList* needed_plugins)
{
	GList* plugins = g_list_copy (docman_plugin->support_plugins);
	GList* node;
	DEBUG_PRINT ("Unloading plugins");
	for (node = plugins; node != NULL; node = g_list_next (node))
	{
		AnjutaPlugin* plugin = ANJUTA_PLUGIN (node->data);
		DEBUG_PRINT ("Checking plugin: %p", plugin);
		if (g_list_find (needed_plugins, plugin) == NULL)
		{
			DEBUG_PRINT ("%s", "Unloading plugin");
			anjuta_plugin_deactivate (plugin);
		}
	}
	g_list_free (plugins);
}

static void
update_language_plugin (AnjutaDocman *docman, IAnjutaDocument *doc,
                        AnjutaPlugin *plugin)
{
	DocmanPlugin* docman_plugin = ANJUTA_PLUGIN_DOCMAN (plugin);

	DEBUG_PRINT("%s", "Beginning language support");
	if (doc && IANJUTA_IS_EDITOR_LANGUAGE (doc))
	{
		AnjutaPluginManager *plugin_manager;
		IAnjutaLanguage *lang_manager;
		GList *new_support_plugins, *needed_plugins, *node;
		const gchar *language;

		lang_manager = anjuta_shell_get_interface (plugin->shell,
		                                           IAnjutaLanguage,
		                                           NULL);
		if (!lang_manager)
		{
			g_warning ("Could not load language manager!");
			return;
		}

		g_signal_handlers_block_by_func (doc, on_editor_lang_changed, plugin);
		language = ianjuta_language_get_name_from_editor (lang_manager,
		                                                  IANJUTA_EDITOR_LANGUAGE (doc),
		                                                  NULL);
		g_signal_handlers_unblock_by_func (doc, on_editor_lang_changed, plugin);

		if (!language)
		{
			unload_unused_support_plugins (docman_plugin, NULL);
			return;
		}

		/* Load current language editor support plugins */
		plugin_manager = anjuta_shell_get_plugin_manager (plugin->shell, NULL);
		new_support_plugins = anjuta_plugin_manager_query (plugin_manager,
		                                                    "Anjuta Plugin",
		                                                    "Interfaces",
		                                                    "IAnjutaLanguageSupport",
		                                                    "Language Support",
		                                                    "Languages",
		                                                    language, NULL);

		/* Load new plugins */
		needed_plugins =
			load_new_support_plugins (docman_plugin, new_support_plugins,
			                          plugin_manager);

		/* Unload unused plugins */
		unload_unused_support_plugins (docman_plugin, needed_plugins);

		/* Update list */
		g_list_free (docman_plugin->support_plugins);
		docman_plugin->support_plugins = needed_plugins;

		g_list_free (new_support_plugins);
	}
	else
	{
		unload_unused_support_plugins (docman_plugin, NULL);
	}
}

static void
on_document_changed (AnjutaDocman *docman, IAnjutaDocument *doc,
					 AnjutaPlugin *plugin)
{
	DocmanPlugin *docman_plugin;
	update_document_ui (plugin, doc);

	docman_plugin = ANJUTA_PLUGIN_DOCMAN (plugin);
	if (doc)
	{
		GValue value = {0, };
		g_value_init (&value, G_TYPE_OBJECT);
		g_value_set_object (&value, doc);
		anjuta_shell_add_value (plugin->shell,
								IANJUTA_DOCUMENT_MANAGER_CURRENT_DOCUMENT,
								&value, NULL);
		g_value_unset(&value);
	}
	else
	{
		anjuta_shell_remove_value (plugin->shell, IANJUTA_DOCUMENT_MANAGER_CURRENT_DOCUMENT,
								   NULL);
	}


	if (doc && IANJUTA_IS_EDITOR (doc))
	{
		update_status (docman_plugin, IANJUTA_EDITOR (doc));
		update_language_plugin (docman, doc, plugin);
	}
	else
	{
		update_status (docman_plugin, NULL);
		update_language_plugin (docman, NULL, plugin);
	}
	update_title (ANJUTA_PLUGIN_DOCMAN (plugin));
}

static void
on_editor_lang_changed (IAnjutaEditor* editor,
                        const gchar* language,
                        DocmanPlugin* plugin)
{
	update_language_plugin (ANJUTA_DOCMAN (plugin->docman),
	                        IANJUTA_DOCUMENT (editor),
	                        ANJUTA_PLUGIN(plugin));
}

#define EDITOR_TABS_RECENT_FIRST   "docman-tabs-recent-first"
#define EDITOR_TABS_POS            "docman-tabs-pos"
#define EDITOR_SHOW_DROP_DOWN      "docman-show-drop-down"
#define EDITOR_TABS_HIDE           "docman-tabs-hide"
#define EDITOR_TABS_ORDERING       "docman-tabs-ordering"
#define AUTOSAVE_TIMER             "docman-autosave-timer"
#define SAVE_AUTOMATIC             "docman-automatic-save"

static void
docman_plugin_set_tab_pos (DocmanPlugin *ep)
{

	if (g_settings_get_boolean (ep->settings, EDITOR_SHOW_DROP_DOWN))
	{
		anjuta_docman_set_open_documents_mode (ANJUTA_DOCMAN (ep->docman),
											   ANJUTA_DOCMAN_OPEN_DOCUMENTS_MODE_COMBO);
	}
	else if (g_settings_get_boolean (ep->settings, EDITOR_TABS_HIDE))
	{
		anjuta_docman_set_open_documents_mode (ANJUTA_DOCMAN (ep->docman),
											   ANJUTA_DOCMAN_OPEN_DOCUMENTS_MODE_NONE);
	}
	else
	{
		gchar *tab_pos;
		GtkPositionType pos;

		anjuta_docman_set_open_documents_mode (ANJUTA_DOCMAN (ep->docman),
											   ANJUTA_DOCMAN_OPEN_DOCUMENTS_MODE_TABS);
		tab_pos = g_settings_get_string (ep->settings, EDITOR_TABS_POS);

		pos = GTK_POS_TOP;
		if (tab_pos)
		{
			if (strcasecmp (tab_pos, "top") == 0)
				pos = GTK_POS_TOP;
			else if (strcasecmp (tab_pos, "left") == 0)
				pos = GTK_POS_LEFT;
			else if (strcasecmp (tab_pos, "right") == 0)
				pos = GTK_POS_RIGHT;
			else if (strcasecmp (tab_pos, "bottom") == 0)
				pos = GTK_POS_BOTTOM;
			g_free (tab_pos);
		}
		anjuta_docman_set_tab_pos (ANJUTA_DOCMAN (ep->docman), pos);
	}
}

static void
on_document_removed (AnjutaDocman *docman, IAnjutaDocument *doc,
					 AnjutaPlugin *plugin)
{
	/* Emit document-removed signal */
	g_signal_emit_by_name (plugin, "document-removed", doc);
}

static void
on_document_added (AnjutaDocman *docman, IAnjutaDocument *doc,
				   AnjutaPlugin *plugin)
{
	GtkWidget *highlight_submenu, *highlight_menu;
	DocmanPlugin *docman_plugin;

	docman_plugin = ANJUTA_PLUGIN_DOCMAN (plugin);
	g_signal_connect (G_OBJECT (doc), "update_ui",
					  G_CALLBACK (on_document_update_ui),
					  docman_plugin);
	g_signal_connect (G_OBJECT (doc), "update-save-ui",
					  G_CALLBACK (on_document_update_save_ui),
					  plugin);

	/* Present the vbox as this is the widget that was added to the shell */
	anjuta_shell_present_widget (plugin->shell,
								 GTK_WIDGET (docman_plugin->vbox), NULL);

	if (IANJUTA_IS_EDITOR (doc))
	{
		IAnjutaEditor *te;

		te = IANJUTA_EDITOR (doc);

		/* Check for language changes */
		g_signal_connect (G_OBJECT (doc), "language-changed",
		                  G_CALLBACK (on_editor_lang_changed),
		                  docman_plugin);


		/* Create Highlight submenu */
		highlight_submenu =
			create_highlight_submenu (docman_plugin, te);
		if (highlight_submenu)
		{
			highlight_menu =
				gtk_ui_manager_get_widget (GTK_UI_MANAGER (docman_plugin->ui),
						"/MenuMain/MenuView/MenuViewEditor/MenuFormatStyle");
			gtk_menu_item_set_submenu (GTK_MENU_ITEM (highlight_menu),
									   highlight_submenu);
		}
	}

	/* Emit document-added signal */
	g_signal_emit_by_name (docman_plugin, "document-added", doc);
}

static gboolean
on_window_key_press_event (AnjutaShell *shell,
						   GdkEventKey *event,
						  DocmanPlugin *plugin)
{
	gint i;

	g_return_val_if_fail (event != NULL, FALSE);

	for (i = 0; global_keymap[i].id; i++)
		if (event->keyval == global_keymap[i].gdk_key &&
		    (event->state & global_keymap[i].modifiers) == global_keymap[i].modifiers)
			break;

	if (!global_keymap[i].id)
		return FALSE;

	if (global_keymap[i].id >= ID_FIRSTBUFFER &&
			    global_keymap[i].id <= (ID_FIRSTBUFFER + 9))
	{
		gint page_req = global_keymap[i].id - ID_FIRSTBUFFER;

		if (!anjuta_docman_set_page (ANJUTA_DOCMAN(plugin->docman), page_req))
			return FALSE;
	}
	else
		return FALSE;

	/* Note: No reason for a shortcut to do more than one thing a time */
	g_signal_stop_emission_by_name (G_OBJECT (ANJUTA_PLUGIN(plugin)->shell),
									"key-press-event");

	return TRUE;
}

static void
on_session_load (AnjutaShell *shell, AnjutaSessionPhase phase,
				 AnjutaSession *session, DocmanPlugin *plugin)
{
	if (phase != ANJUTA_SESSION_PHASE_NORMAL)
		return;

	anjuta_bookmarks_session_load (ANJUTA_BOOKMARKS (plugin->bookmarks),
								   session);
}

static void
on_session_save (AnjutaShell *shell, AnjutaSessionPhase phase,
				 AnjutaSession *session, DocmanPlugin *plugin)
{
	GList *docwids, *node, *files;

	if (phase != ANJUTA_SESSION_PHASE_NORMAL)
		return;

	files = anjuta_session_get_string_list (session, "File Loader", "Files"); /* probably NULL */
	/* buffers list is ordered last-opened to first-opened */
	docwids = anjuta_docman_get_all_doc_widgets (ANJUTA_DOCMAN (plugin->docman));
	if (docwids)
	{
		for (node = docwids; node != NULL; node = g_list_next (node))
		{
			/* only editor-documents are logged here. glade files etc handled elsewhere */
			if (IANJUTA_IS_EDITOR (node->data))
			{
				IAnjutaEditor *te;
				GFile* file;

				te = IANJUTA_EDITOR (node->data);
				file = ianjuta_file_get_file (IANJUTA_FILE (te), NULL);
				if (file)
				{
					/* Save line locations also */
					gchar *line_number;

					line_number = g_strdup_printf ("%d", ianjuta_editor_get_lineno (te, NULL));
					files = g_list_prepend (files, anjuta_session_get_relative_uri_from_file (session, file, line_number));
					g_free (line_number);
				}
			}
		}
		g_list_free (docwids);
	}
	if (files)
	{
		anjuta_session_set_string_list (session, "File Loader", "Files", files);
		g_list_foreach (files, (GFunc)g_free, NULL);
		g_list_free (files);
	}

	anjuta_bookmarks_session_save (ANJUTA_BOOKMARKS (plugin->bookmarks),
								   session);
}

static gboolean
on_save_prompt_save_editor (AnjutaSavePrompt *save_prompt,
							gpointer item, gpointer user_data)
{
	DocmanPlugin *plugin;

	plugin = ANJUTA_PLUGIN_DOCMAN (user_data);
	return anjuta_docman_save_document (ANJUTA_DOCMAN (plugin->docman),
										IANJUTA_DOCUMENT (item),
										GTK_WIDGET (save_prompt));
}

static void
on_save_prompt (AnjutaShell *shell, AnjutaSavePrompt *save_prompt,
				DocmanPlugin *plugin)
{
	GList *buffers, *node;

	buffers = anjuta_docman_get_all_doc_widgets (ANJUTA_DOCMAN (plugin->docman));
	if (buffers)
	{
		for (node = buffers; node; node = g_list_next (node))
		{
			IAnjutaFileSavable *editor = IANJUTA_FILE_SAVABLE (node->data);
			if (ianjuta_file_savable_is_dirty (editor, NULL))
			{
				const gchar *name;
				gchar *uri = NULL;
				GFile* file;

				name = ianjuta_document_get_filename (IANJUTA_DOCUMENT (editor), NULL);
				file = ianjuta_file_get_file (IANJUTA_FILE (editor), NULL);
				if (file)
				{
					uri = g_file_get_uri (file);
					g_object_unref (file);
				}
				anjuta_save_prompt_add_item (save_prompt, name, uri, editor,
											 on_save_prompt_save_editor, plugin);
				g_free (uri);
			}
		}
		g_list_free (buffers);
	}
}

static void
on_notify_prefs (AnjutaPreferences* prefs,
                 const gchar* key, gpointer user_data)
{
	DocmanPlugin *ep = ANJUTA_PLUGIN_DOCMAN (user_data);
	docman_plugin_set_tab_pos (ep);
}

static gboolean
on_docman_auto_save (gpointer data)
{
	gboolean retval;
	DocmanPlugin *plugin;
	AnjutaDocman *docman;
	AnjutaStatus* status;
	IAnjutaDocument *doc;
	GList *buffers, *node;

	plugin = ANJUTA_PLUGIN_DOCMAN (data);
	docman = ANJUTA_DOCMAN (plugin->docman);
	if (!docman)
		return FALSE;

	if (g_settings_get_boolean (plugin->settings, SAVE_AUTOMATIC) == FALSE)
	{
		plugin->autosave_on = FALSE;
		return FALSE;
	}

	status = anjuta_shell_get_status (docman->shell, NULL);

	retval = TRUE;
	buffers = anjuta_docman_get_all_doc_widgets (docman);
	if (buffers)
	{
		for (node = buffers; node != NULL; node = g_list_next (node))
		{
			doc = IANJUTA_DOCUMENT (node->data);
			if (ianjuta_file_savable_is_dirty (IANJUTA_FILE_SAVABLE (doc), NULL) &&
				!ianjuta_file_savable_is_conflict (IANJUTA_FILE_SAVABLE (doc), NULL))
			{
				GFile* file = ianjuta_file_get_file (IANJUTA_FILE (doc), NULL);
				if (file)
				{
					GError *err = NULL;
					g_object_unref (file);

					ianjuta_file_savable_save (IANJUTA_FILE_SAVABLE (doc), &err);
					if (err)
					{
						gchar *fullmsg;
						const gchar *filename = ianjuta_document_get_filename (doc, NULL); /* this may fail, too */
						fullmsg = g_strdup_printf (_("Autosave failed for %s"), filename);
						anjuta_status (status, fullmsg, 3);
						g_free (fullmsg);
						g_error_free (err);
						retval = FALSE;
					}
				}
			}
		}
		g_list_free (buffers);
	}

	if (retval)
	{
		anjuta_status (status, _("Autosave completed"), 3);
	}
	return retval;
}

static void
on_notify_timer (GSettings* settings,
                 const gchar* key,
                 gpointer user_data)
{
	DocmanPlugin *plugin;
	gint auto_save_timer;
	gboolean auto_save;

	plugin = ANJUTA_PLUGIN_DOCMAN (user_data);

	auto_save_timer = g_settings_get_int(settings, AUTOSAVE_TIMER);
	auto_save = g_settings_get_boolean(settings, SAVE_AUTOMATIC);

	if (auto_save)
	{
		if (plugin->autosave_on)
		{
			if (auto_save_timer != plugin->autosave_it)
			{
				g_source_remove (plugin->autosave_id);
				plugin->autosave_id =
					g_timeout_add_seconds (auto_save_timer * 60,
										   on_docman_auto_save, plugin);
			}
		}
		else
		{
			plugin->autosave_id =
				g_timeout_add_seconds (auto_save_timer * 60,
									   on_docman_auto_save, plugin);
		}
		plugin->autosave_it = auto_save_timer;
		plugin->autosave_on = TRUE;
	}
	else if (plugin->autosave_on)
	{
		g_source_remove (plugin->autosave_id);
		plugin->autosave_on = FALSE;
	}
}

static void
prefs_init (DocmanPlugin *ep)
{
	docman_plugin_set_tab_pos (ep);

	g_signal_connect (ep->settings, "changed::" EDITOR_SHOW_DROP_DOWN,
	                  G_CALLBACK (on_notify_prefs), ep);
	g_signal_connect (ep->settings, "changed::" EDITOR_TABS_HIDE,
	                  G_CALLBACK (on_notify_prefs), ep);
	g_signal_connect (ep->settings, "changed::" EDITOR_TABS_POS,
	                  G_CALLBACK (on_notify_prefs), ep);
	g_signal_connect (ep->settings, "changed::" AUTOSAVE_TIMER,
	                  G_CALLBACK (on_notify_timer), ep);
	g_signal_connect (ep->settings, "changed::" SAVE_AUTOMATIC,
	                  G_CALLBACK (on_notify_timer), ep);

	on_notify_timer(ep->settings, NULL, ep);
}

static gboolean
activate_plugin (AnjutaPlugin *plugin)
{
	GtkWidget *docman, *popup_menu;
	AnjutaUI *ui;
	DocmanPlugin *dplugin;
	GtkActionGroup *group;
	gint i;
	static gboolean initialized = FALSE;
	GList *actions, *act;
	
	DEBUG_PRINT ("%s", "DocmanPlugin: Activating document manager plugin…");

	dplugin = ANJUTA_PLUGIN_DOCMAN (plugin);
	dplugin->ui = anjuta_shell_get_ui (plugin->shell, NULL);

	ui = dplugin->ui;
	docman = anjuta_docman_new (dplugin);
	gtk_widget_show (docman);
	dplugin->docman = docman;

	ANJUTA_DOCMAN(docman)->shell = anjuta_plugin_get_shell(plugin);
	g_signal_connect (G_OBJECT (docman), "document-added",
					  G_CALLBACK (on_document_added), plugin);
	g_signal_connect (G_OBJECT (docman), "document-removed",
					  G_CALLBACK (on_document_removed), plugin);
	g_signal_connect (G_OBJECT (docman), "document-changed",
					  G_CALLBACK (on_document_changed), plugin);
	g_signal_connect (G_OBJECT (plugin->shell), "key-press-event",
					  G_CALLBACK (on_window_key_press_event), plugin);

	if (!initialized)
	{
		register_stock_icons (plugin);
	}
	dplugin->action_groups = NULL;
	/* Add all our editor actions */
	for (i = 0; i < G_N_ELEMENTS (action_groups); i++)
	{
		group = anjuta_ui_add_action_group_entries (ui,
													action_groups[i].name,
													_(action_groups[i].label),
													action_groups[i].group,
													action_groups[i].size,
													GETTEXT_PACKAGE, TRUE,
													plugin);
		dplugin->action_groups =
			g_list_prepend (dplugin->action_groups, group);
		actions = gtk_action_group_list_actions (group);
		act = actions;
		while (act) {
			g_object_set_data (G_OBJECT (act->data), "Plugin", dplugin);
			act = g_list_next (act);
		}
	}
	for (i = 0; i < G_N_ELEMENTS (action_toggle_groups); i++)
	{
		group = anjuta_ui_add_toggle_action_group_entries (ui,
												action_toggle_groups[i].name,
												_(action_toggle_groups[i].label),
												action_toggle_groups[i].group,
												action_toggle_groups[i].size,
												GETTEXT_PACKAGE, TRUE, plugin);
		dplugin->action_groups =
			g_list_prepend (dplugin->action_groups, group);
		actions = gtk_action_group_list_actions (group);
		act = actions;
		while (act) {
			g_object_set_data (G_OBJECT (act->data), "Plugin", dplugin);
			act = g_list_next (act);
		}
	}

	/* Add UI */
	dplugin->uiid = anjuta_ui_merge (ui, UI_FILE);
	dplugin->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show (dplugin->vbox);
	gtk_box_pack_start (GTK_BOX(dplugin->vbox), docman, TRUE, TRUE, 0);

	/* Create the default searchbox instance. */
	dplugin->search_box = search_box_new (ANJUTA_DOCMAN (docman));
	gtk_box_pack_end (GTK_BOX (dplugin->vbox), dplugin->search_box, FALSE, FALSE, 0);

	anjuta_shell_add_widget_full (plugin->shell, dplugin->vbox,
							 "AnjutaDocumentManager", _("Documents"),
							 "editor-plugin-icon",
							 ANJUTA_SHELL_PLACEMENT_CENTER,
							 TRUE, NULL);
	anjuta_shell_present_widget (plugin->shell, dplugin->vbox, NULL);

	ui_give_shorter_names (plugin);
	update_document_ui (plugin, NULL);
	
	/* Setup popup menu */
	popup_menu = gtk_ui_manager_get_widget (GTK_UI_MANAGER (ui),
											"/PopupDocumentManager");
	g_assert (popup_menu != NULL && GTK_IS_MENU (popup_menu));
	anjuta_docman_set_popup_menu (ANJUTA_DOCMAN (docman), popup_menu);


	/* Connect to save session */
	g_signal_connect (G_OBJECT (plugin->shell), "save-session",
					  G_CALLBACK (on_session_save), plugin);
	/* Connect to load session */
	g_signal_connect (G_OBJECT (plugin->shell), "load-session",
					  G_CALLBACK (on_session_load), plugin);

	/* Connect to save prompt */
	g_signal_connect (G_OBJECT (plugin->shell), "save-prompt",
					  G_CALLBACK (on_save_prompt), plugin);

	/* Add bookmarks widget */
	dplugin->bookmarks = G_OBJECT(anjuta_bookmarks_new (dplugin));

	dplugin->project_watch_id =
		anjuta_plugin_add_watch (plugin, IANJUTA_PROJECT_MANAGER_PROJECT_ROOT_URI,
								 value_added_project_root_uri,
								 value_removed_project_root_uri, NULL);
	dplugin->project_name = NULL;

	prefs_init(ANJUTA_PLUGIN_DOCMAN (plugin));
	initialized = TRUE;

	return TRUE;
}

static gboolean
deactivate_plugin (AnjutaPlugin *plugin)
{
	// GtkIconFactory *icon_factory;
	DocmanPlugin *eplugin;
	AnjutaUI *ui;
	GList *node;

	DEBUG_PRINT ("%s", "DocmanPlugin: Deactivating document manager plugin…");

	eplugin = ANJUTA_PLUGIN_DOCMAN (plugin);

	g_signal_handlers_disconnect_by_func (G_OBJECT (plugin->shell),
										  G_CALLBACK (on_session_save), plugin);
	g_signal_handlers_disconnect_by_func (G_OBJECT (plugin->shell),
										  G_CALLBACK (on_save_prompt), plugin);

	ui = anjuta_shell_get_ui (plugin->shell, NULL);

	g_signal_handlers_disconnect_by_func (G_OBJECT (eplugin->docman),
										  G_CALLBACK (on_document_changed),
										  plugin);
	g_signal_handlers_disconnect_by_func (G_OBJECT (plugin->shell),
										  G_CALLBACK (on_window_key_press_event),
										  plugin);

	/* Remove autosave timeout if one is scheduled. */
	if (eplugin->autosave_on)
	{
		g_source_remove (eplugin->autosave_id);
		eplugin->autosave_on = FALSE;
	}

	on_document_changed (ANJUTA_DOCMAN (eplugin->docman), NULL, plugin);

	/* Widget is removed from the container when destroyed */
	gtk_widget_destroy (eplugin->docman);
	g_object_unref (eplugin->bookmarks);
	anjuta_ui_unmerge (ui, eplugin->uiid);
	node = eplugin->action_groups;
	while (node)
	{
		GtkActionGroup *group = GTK_ACTION_GROUP (node->data);
		anjuta_ui_remove_action_group (ui, group);
		node = g_list_next (node);
	}
	g_list_free (eplugin->action_groups);

	/* FIXME: */
	/* Unregister stock icons */
	/* Unregister preferences */

	eplugin->docman = NULL;
	eplugin->uiid = 0;
	eplugin->action_groups = NULL;

	return TRUE;
}

static void
dispose (GObject *obj)
{
	DocmanPlugin *eplugin = ANJUTA_PLUGIN_DOCMAN (obj);

	g_object_unref (eplugin->settings);

	G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
finalize (GObject *obj)
{
	/* Finalization codes here */
	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
docman_plugin_instance_init (GObject *obj)
{
	DocmanPlugin *plugin = ANJUTA_PLUGIN_DOCMAN (obj);
	plugin->uiid = 0;
	plugin->notify_ids = NULL;
	plugin->support_plugins = NULL;
	plugin->settings = g_settings_new (PREF_SCHEMA);
}

static void
docman_plugin_class_init (GObjectClass *klass)
{
	AnjutaPluginClass *plugin_class = ANJUTA_PLUGIN_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	plugin_class->activate = activate_plugin;
	plugin_class->deactivate = deactivate_plugin;
	klass->dispose = dispose;
	klass->finalize = finalize;
}

/* Implement IAnjutaDocumentManager interfaces */
static GFile*
ianjuta_docman_get_file (IAnjutaDocumentManager *plugin,
		const gchar *filename, GError **e)
{
	AnjutaDocman *docman;
	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));
	return anjuta_docman_get_file (docman, filename);
}

static IAnjutaDocument*
ianjuta_docman_get_document_for_file (IAnjutaDocumentManager *plugin,
		GFile* file, GError **e)
{
	AnjutaDocman *docman;
	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));
	return anjuta_docman_get_document_for_file (docman, file);
}

static IAnjutaDocument*
ianjuta_docman_get_current_document (IAnjutaDocumentManager *plugin, GError **e)
{
	AnjutaDocman *docman;
	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));
	return anjuta_docman_get_current_document (docman);
}

static void
ianjuta_docman_set_current_document (IAnjutaDocumentManager *plugin,
								   IAnjutaDocument *doc, GError **e)
{
	AnjutaDocman *docman;
	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));
	anjuta_docman_set_current_document (docman, doc);
}

static GList*
ianjuta_docman_get_doc_widgets (IAnjutaDocumentManager *plugin, GError **e)
{
	AnjutaDocman *docman;
	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));
	return anjuta_docman_get_all_doc_widgets (docman);
}

static IAnjutaEditor*
ianjuta_docman_goto_file_line (IAnjutaDocumentManager *plugin,
							   GFile* file, gint linenum, GError **e)
{
	AnjutaDocman *docman;
	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));
	return anjuta_docman_goto_file_line (docman, file, linenum);
}

static IAnjutaEditor*
ianjuta_docman_goto_file_line_mark (IAnjutaDocumentManager *plugin,
		GFile* file, gint linenum, gboolean mark, GError **e)
{
	AnjutaDocman *docman;
	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));
	return anjuta_docman_goto_file_line_mark (docman, file, linenum, mark);
}

/**
 * anjuta_docman_add_buffer:
 * @plugin:
 * @filename:
 * @content file text, a 0-terminated utf-8 string
 * @r: store for pointer to error data struct
 *
 * Return value: the new editor
 */
static IAnjutaEditor*
ianjuta_docman_add_buffer (IAnjutaDocumentManager *plugin,
						   const gchar* filename, const gchar *content,
						   GError **e)
{
	AnjutaDocman *docman;
	IAnjutaEditor *te;
	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));
	te = anjuta_docman_add_editor (docman, NULL, filename);
	if (te)
	{
		if (content != NULL && *content != '\0')
			ianjuta_editor_append (te, content, -1, NULL);

		return IANJUTA_EDITOR (te);
	}
	return NULL;
}

static void
ianjuta_docman_add_document (IAnjutaDocumentManager *plugin,
							IAnjutaDocument* document,
							GError **e)
{
	AnjutaDocman *docman;
	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));

	anjuta_docman_add_document(docman, document, NULL);
}

static gboolean
ianjuta_docman_remove_document(IAnjutaDocumentManager *plugin,
							  IAnjutaDocument *doc,
							  gboolean save_before, GError **e)
{
	gboolean ret_val;
	AnjutaDocman *docman;

	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));

	if (save_before)
	{
		ret_val = anjuta_docman_save_document(docman, doc,
								 GTK_WIDGET (ANJUTA_PLUGIN (plugin)->shell));
	}
	else
		ret_val = TRUE;

	if (ret_val)
		anjuta_docman_remove_document (docman, doc);

	return ret_val;
}

static void
ianjuta_docman_add_bookmark (IAnjutaDocumentManager* plugin,
									  GFile* file,
									  gint  line,
									  GError **e)
{
	AnjutaBookmarks* bookmarks = ANJUTA_BOOKMARKS (ANJUTA_PLUGIN_DOCMAN(plugin)->bookmarks);
	anjuta_bookmarks_add_file (bookmarks, file, line, NULL);
}

static void
ianjuta_document_manager_iface_init (IAnjutaDocumentManagerIface *iface)
{
	iface->add_buffer = ianjuta_docman_add_buffer;
	iface->add_document = ianjuta_docman_add_document;
	iface->find_document_with_file = ianjuta_docman_get_document_for_file;
	iface->get_current_document = ianjuta_docman_get_current_document;
	iface->get_doc_widgets = ianjuta_docman_get_doc_widgets;
	iface->get_file = ianjuta_docman_get_file;
	iface->goto_file_line = ianjuta_docman_goto_file_line;
	iface->goto_file_line_mark = ianjuta_docman_goto_file_line_mark;
	iface->remove_document = ianjuta_docman_remove_document;
	iface->set_current_document = ianjuta_docman_set_current_document;
	iface->add_bookmark = ianjuta_docman_add_bookmark;
}

/* Implement IAnjutaFile interface */
static void
ifile_open (IAnjutaFile* plugin, GFile* file, GError** e)
{
	AnjutaDocman *docman;

	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));
	anjuta_docman_goto_file_line (docman, file, -1);
}

static GFile*
ifile_get_file (IAnjutaFile* plugin, GError** e)
{
	AnjutaDocman *docman;
	IAnjutaDocument *doc;

	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));
	doc = anjuta_docman_get_current_document (docman);
	if (doc != NULL)
		return ianjuta_file_get_file (IANJUTA_FILE (doc), NULL);
	else
		return NULL;
}

static void
ifile_iface_init (IAnjutaFileIface *iface)
{
	iface->open = ifile_open;
	iface->get_file = ifile_get_file;
}

/* Implement IAnjutaFileSavable interface */
static void
isaveable_save (IAnjutaFileSavable* plugin, GError** e)
{
	/* Save all buffers whose path is known */
	AnjutaDocman *docman;
	GList *docwids, *node;

	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));
	docwids = anjuta_docman_get_all_doc_widgets (docman);
	if (docwids)
	{
		for (node = docwids; node != NULL; node = g_list_next (node))
		{
			IAnjutaDocument *doc;
			doc = IANJUTA_DOCUMENT (node->data);
			/* known-uri test occurs downstream */
			/* CHECKME test here and perform save-as when appropriate ? */
			if (ianjuta_file_savable_is_dirty (IANJUTA_FILE_SAVABLE (doc), NULL))
			{
				ianjuta_file_savable_save (IANJUTA_FILE_SAVABLE (doc), NULL);
			}
		}
		g_list_free (docwids);
	}
}

static void
isavable_save_as (IAnjutaFileSavable* plugin, GFile* file, GError** e)
{
	DEBUG_PRINT("%s", "save_as: Not implemented in DocmanPlugin");
}

static gboolean
isavable_is_dirty (IAnjutaFileSavable* plugin, GError **e)
{
	/* Is any editor unsaved */
	AnjutaDocman *docman;
	IAnjutaDocument *doc;
	GList *buffers, *node;
	gboolean retval;

	retval = FALSE;
	docman = ANJUTA_DOCMAN ((ANJUTA_PLUGIN_DOCMAN (plugin)->docman));
	buffers = anjuta_docman_get_all_doc_widgets (docman);
	if (buffers)
	{
		for (node = buffers; node; node = g_list_next (node))
		{
			doc = IANJUTA_DOCUMENT (node->data);
			if (ianjuta_file_savable_is_dirty (IANJUTA_FILE_SAVABLE (doc), NULL))
			{
				retval = TRUE;
				break;
			}
		}
		g_list_free (buffers);
	}
	return retval;
}

static void
isavable_set_dirty(IAnjutaFileSavable* plugin, gboolean dirty, GError** e)
{
	DEBUG_PRINT("%s", "set_dirty: Not implemented in DocmanPlugin");
}

static void
isavable_iface_init (IAnjutaFileSavableIface *iface)
{
	iface->save = isaveable_save;
	iface->save_as = isavable_save_as;
	iface->set_dirty = isavable_set_dirty;
	iface->is_dirty = isavable_is_dirty;
}

static void
ipreferences_merge(IAnjutaPreferences* ipref, AnjutaPreferences* prefs, GError** e)
{
	GError* error = NULL;
	GtkBuilder* bxml = gtk_builder_new ();
	DocmanPlugin* doc_plugin = ANJUTA_PLUGIN_DOCMAN (ipref);
	GObject *show_tabs_radio, *tabs_settings_box;

	/* Add preferences */
	if (!gtk_builder_add_from_file (bxml, PREFS_BUILDER, &error))
	{
		g_warning ("Couldn't load builder file: %s", error->message);
		g_error_free (error);
	}

	show_tabs_radio = gtk_builder_get_object (bxml, "show-tabs-radio");
	tabs_settings_box = gtk_builder_get_object (bxml, "tabs-settings-box");
	g_object_bind_property (show_tabs_radio, "active", tabs_settings_box, "sensitive", 0);

	anjuta_preferences_add_from_builder (prefs, bxml,
	                                     doc_plugin->settings,
	                                     "Documents", _("Documents"),  ICON_FILE);

	g_object_unref (G_OBJECT (bxml));
}

static void
ipreferences_unmerge(IAnjutaPreferences* ipref, AnjutaPreferences* prefs, GError** e)
{
	/*	DocmanPlugin* plugin = ANJUTA_PLUGIN_DOCMAN (ipref);*/
	anjuta_preferences_remove_page(prefs, _("Documents"));
}

static void
ipreferences_iface_init(IAnjutaPreferencesIface* iface)
{
	iface->merge = ipreferences_merge;
	iface->unmerge = ipreferences_unmerge;
}

ANJUTA_PLUGIN_BEGIN (DocmanPlugin, docman_plugin);
ANJUTA_PLUGIN_ADD_INTERFACE(ianjuta_document_manager, IANJUTA_TYPE_DOCUMENT_MANAGER);
ANJUTA_PLUGIN_ADD_INTERFACE(ifile, IANJUTA_TYPE_FILE);
ANJUTA_PLUGIN_ADD_INTERFACE(isavable, IANJUTA_TYPE_FILE_SAVABLE);
ANJUTA_PLUGIN_ADD_INTERFACE(ipreferences, IANJUTA_TYPE_PREFERENCES);
ANJUTA_PLUGIN_END;

ANJUTA_SIMPLE_PLUGIN (DocmanPlugin, docman_plugin);

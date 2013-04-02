/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
    queue.c
    Copyright (C) 2005 S�bastien Granjoux

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

/*
 * Keep all debugger commands in a queue and send them one by one to the
 * debugger (implementing IAnjutaDebugger).
 *---------------------------------------------------------------------------*/

#include <config.h>

#include "queue.h"

/*#define DEBUG*/
#include <libanjuta/anjuta-debug.h>
#include <libanjuta/interfaces/ianjuta-message-manager.h>
#include <libanjuta/interfaces/ianjuta-debugger-register.h>
#include <libanjuta/interfaces/ianjuta-debugger-memory.h>
#include <libanjuta/interfaces/ianjuta-debugger-instruction.h>
#include <libanjuta/interfaces/ianjuta-debugger-breakpoint.h>
#include <libanjuta/interfaces/ianjuta-debugger-variable.h>


/* Contants defintion
 *---------------------------------------------------------------------------*/

#define ICON_FILE "anjuta-debug-manager.plugin.png"

/* Private type
 *---------------------------------------------------------------------------*/

struct _DmaDebuggerQueue {
	GObject parent;

	AnjutaPlugin* plugin;
	IAnjutaDebugger* debugger;
	guint support;

	/* Command queue */
	GQueue *queue;
	DmaQueueCommand *last;
	GList *insert_command;		/* Insert command at the head of the list */
	
	IAnjutaDebuggerState debugger_state;
	IAnjutaDebuggerState queue_state;
	gboolean stop_on_sharedlib;

	/* View for debugger messages */
	IAnjutaMessageView* log;
	
	gboolean busy;	
};

struct _DmaDebuggerQueueClass {
	GObjectClass parent;
 };

/* Call backs
 *---------------------------------------------------------------------------*/

/* Queue function
 *---------------------------------------------------------------------------*/


/* Cancel all commands those cannot handle this unexpected state
 * Return TRUE if the state of the queue need to be changed too
 */

static gboolean
dma_queue_cancel_unexpected (DmaDebuggerQueue *self, IAnjutaDebuggerState state)
{
	GList* node = g_queue_peek_head_link(self->queue);

	/* IANJUTA_DEBUGGER_BUSY is used as a do nothing marker*/
	if (state == IANJUTA_DEBUGGER_BUSY) return FALSE;
	
	/* Cancel all commands in queue with the flag */
	while (node != NULL)
	{
		GList* next = g_list_next (node);
		DmaQueueCommand* cmd = (DmaQueueCommand *)node->data;

		if (!dma_command_is_valid_in_state(cmd, state))
		{
			/* Command is not allowed in this state, cancel it */
			dma_command_cancel (cmd);
			g_queue_delete_link (self->queue, node);
		}
		else if (dma_command_is_going_to_state (cmd) != IANJUTA_DEBUGGER_BUSY)
		{
			/* A command setting the state is kept,
		   	debugger state is known again afterward, queue state is kept too */
			
			return FALSE;
		}
		node = next;
	}
	/* End in this unexpected state */
	self->queue_state = state;
		
	return TRUE;
}

static void
dma_debugger_queue_clear (DmaDebuggerQueue *self)
{
	g_queue_foreach (self->queue, (GFunc)dma_command_free, NULL);
	/* Do not use g_queue_clear yet as it is defined only in GLib 2.14 */
	while (g_queue_pop_head(self->queue) != NULL);
	if (self->last != NULL)
	{
		DEBUG_PRINT("clear command %x", dma_command_get_type (self->last));
		dma_command_free (self->last);
		self->last = NULL;
	}
	
	/* Queue is empty so has the same state than debugger */
	self->queue_state = self->debugger_state;
	
	g_list_free (self->insert_command);
	self->insert_command = NULL;
}
		
static void
dma_queue_emit_debugger_state_change (DmaDebuggerQueue *self, IAnjutaDebuggerState state, GError* err)
{
	enum
	{
		NO_SIGNAL,
		DEBUGGER_STOPPED_SIGNAL,
		DEBUGGER_STARTED_SIGNAL,
		PROGRAM_LOADED_SIGNAL,
		PROGRAM_UNLOADED_SIGNAL,
		PROGRAM_STARTED_SIGNAL,
		PROGRAM_EXITED_SIGNAL,
		PROGRAM_RUNNING_SIGNAL,
		PROGRAM_STOPPED_SIGNAL
	} signal = NO_SIGNAL;
	
	DEBUG_PRINT("change debugger state new %d old %d", state, self->debugger_state);

	switch (state)
	{
	case IANJUTA_DEBUGGER_BUSY:
		/* Debugger is busy, nothing to do */
		g_return_if_reached();
		return;
	case IANJUTA_DEBUGGER_STOPPED:
		self->stop_on_sharedlib = FALSE;
		signal = DEBUGGER_STOPPED_SIGNAL;
		self->debugger_state = state;
		break;
	case IANJUTA_DEBUGGER_STARTED:
		self->stop_on_sharedlib = FALSE;
		signal = self->debugger_state < IANJUTA_DEBUGGER_STARTED ? DEBUGGER_STARTED_SIGNAL : PROGRAM_UNLOADED_SIGNAL;			
		self->debugger_state = state;
		break;
	case IANJUTA_DEBUGGER_PROGRAM_LOADED:
		self->stop_on_sharedlib = FALSE;
		signal = self->debugger_state < IANJUTA_DEBUGGER_PROGRAM_LOADED ? PROGRAM_LOADED_SIGNAL : PROGRAM_EXITED_SIGNAL;			
		self->debugger_state = state;
		break;
	case IANJUTA_DEBUGGER_PROGRAM_STOPPED:
		if (self->debugger_state < IANJUTA_DEBUGGER_PROGRAM_STOPPED)
		{
			signal = PROGRAM_STARTED_SIGNAL;
			/* Emit a debugger stopped after program started */
			self->debugger_state = IANJUTA_DEBUGGER_PROGRAM_RUNNING;
			break;
		}
		if (!self->stop_on_sharedlib)
		{
			signal = PROGRAM_STOPPED_SIGNAL;			
		}
		self->debugger_state = state;
		break;
	case IANJUTA_DEBUGGER_PROGRAM_RUNNING:
		self->stop_on_sharedlib = FALSE;
		if (self->debugger_state < IANJUTA_DEBUGGER_PROGRAM_STOPPED)
		{
			signal = PROGRAM_STARTED_SIGNAL;
			/* Emit a debugger stopped after program started */
			self->debugger_state = IANJUTA_DEBUGGER_PROGRAM_STOPPED;
			break;
		}
		signal = PROGRAM_RUNNING_SIGNAL;
		self->debugger_state = state;
		break;
	}

	self->insert_command = g_list_prepend (self->insert_command, g_queue_peek_head_link (self->queue));
	/* Emit signal */
	switch (signal)
	{
	case NO_SIGNAL:
		/* Do nothing */
		break;
	case DEBUGGER_STOPPED_SIGNAL:
		DEBUG_PRINT("%s", "** emit debugger-stopped **");
		g_signal_emit_by_name (self->plugin, "debugger-stopped", err);
		break;
	case DEBUGGER_STARTED_SIGNAL:
		DEBUG_PRINT("%s", "** emit debugger-started **");
		g_signal_emit_by_name (self->plugin, "debugger-started");
		break;
	case PROGRAM_LOADED_SIGNAL:
		DEBUG_PRINT("%s", "** emit program-loaded **");
		g_signal_emit_by_name (self->plugin, "program-loaded");
		break;
	case PROGRAM_UNLOADED_SIGNAL:
		DEBUG_PRINT("%s", "** emit program-unloaded **");
		g_signal_emit_by_name (self->plugin, "program-unloaded");
		break;
	case PROGRAM_STARTED_SIGNAL:
		DEBUG_PRINT("%s", "** emit program-started **");
		g_signal_emit_by_name (self->plugin, "program-started");
		break;
	case PROGRAM_EXITED_SIGNAL:
		DEBUG_PRINT("%s", "** emit program-exited **");
		g_signal_emit_by_name (self->plugin, "program-exited");
		break;
	case PROGRAM_STOPPED_SIGNAL:
		DEBUG_PRINT("%s", "** emit program-stopped **");
		g_signal_emit_by_name (self->plugin, "program-stopped");
		break;
	case PROGRAM_RUNNING_SIGNAL:
		DEBUG_PRINT("%s", "** emit program-running **");
		g_signal_emit_by_name (self->plugin, "program-running");
		break;
	}
	self->insert_command = g_list_delete_link (self->insert_command, self->insert_command);
}

static void
dma_queue_emit_debugger_state (DmaDebuggerQueue *self, IAnjutaDebuggerState state, GError* err)
{
	DEBUG_PRINT("update debugger state new %d old %d", state, self->debugger_state);

	/* Add missing states if useful */
	for(;self->debugger_state != state;)
	{
		IAnjutaDebuggerState next_state = state;
		
		switch (state)
		{
		case IANJUTA_DEBUGGER_STOPPED:
			if ((self->debugger_state == IANJUTA_DEBUGGER_PROGRAM_RUNNING) ||
				(self->debugger_state == IANJUTA_DEBUGGER_PROGRAM_STOPPED))
			{
				next_state = IANJUTA_DEBUGGER_PROGRAM_LOADED;
			}
			else if	(self->debugger_state == IANJUTA_DEBUGGER_PROGRAM_LOADED)
			{
				next_state = IANJUTA_DEBUGGER_STARTED;
			}
			break;
		case IANJUTA_DEBUGGER_STARTED:
			if ((self->debugger_state == IANJUTA_DEBUGGER_PROGRAM_RUNNING) ||
				(self->debugger_state == IANJUTA_DEBUGGER_PROGRAM_STOPPED))
			{
				next_state = IANJUTA_DEBUGGER_PROGRAM_LOADED;
			}
			break;
		case IANJUTA_DEBUGGER_PROGRAM_LOADED:
			if (self->debugger_state == IANJUTA_DEBUGGER_STOPPED)
			{
				next_state = IANJUTA_DEBUGGER_STARTED;
			}
			break;
		case IANJUTA_DEBUGGER_PROGRAM_STOPPED:
			if (self->debugger_state == IANJUTA_DEBUGGER_STOPPED)
			{
				next_state = IANJUTA_DEBUGGER_STARTED;
			}
			else if (self->debugger_state == IANJUTA_DEBUGGER_STARTED)
			{
				next_state = IANJUTA_DEBUGGER_PROGRAM_LOADED;
			}
			break;
		case IANJUTA_DEBUGGER_PROGRAM_RUNNING:
			if (self->debugger_state == IANJUTA_DEBUGGER_STOPPED)
			{
				next_state = IANJUTA_DEBUGGER_STARTED;
			}
			else if (self->debugger_state == IANJUTA_DEBUGGER_STARTED)
			{
				next_state = IANJUTA_DEBUGGER_PROGRAM_LOADED;
			}
			break;
		case IANJUTA_DEBUGGER_BUSY:
			return;
		}
		dma_queue_emit_debugger_state_change (self, next_state, NULL);
	}
}

static void
dma_queue_emit_debugger_ready (DmaDebuggerQueue *self)
{
	gboolean busy;
	
	if (g_queue_is_empty(self->queue) && (self->last == NULL))
	{
		busy = FALSE;
	}
	else
	{
		busy = TRUE;
	}

	if (busy != self->busy)
	{
		AnjutaStatus* status;
		
		status = anjuta_shell_get_status(ANJUTA_PLUGIN (self->plugin)->shell, NULL);
		self->busy = busy;
	}	
}

static void dma_debugger_queue_execute (DmaDebuggerQueue *self);

/* Call when debugger has completed the current command */

static void
dma_debugger_queue_complete (DmaDebuggerQueue *self, IAnjutaDebuggerState state)
{
	DEBUG_PRINT("debugger_queue_complete %d", state);

	if (state != IANJUTA_DEBUGGER_BUSY)
	{
		if (self->last != NULL)
		{
			if (dma_command_is_going_to_state (self->last) != state)
			{
				/* Command end in an unexpected state,
			 	* Remove invalid following command */
				dma_queue_cancel_unexpected (self, state);
			}

			/* Remove current command */
			DEBUG_PRINT("end command %x", dma_command_get_type (self->last));
			dma_command_free (self->last);
			self->last = NULL;
		}

	
		/* Emit new state if necessary */
		dma_queue_emit_debugger_state (self, state, NULL);
		
		/* Send next command */
		dma_debugger_queue_execute (self);
	}
}

/* Call to send next command */

static void
dma_debugger_queue_execute (DmaDebuggerQueue *self)
{
	DEBUG_PRINT("%s", "debugger_queue_execute");

	/* Check if debugger is connected to a debugger backend */
	g_return_if_fail (self->debugger != NULL);

	/* Check if there debugger is busy */
	if (self->last != NULL)
	{
		IAnjutaDebuggerState state;
		/* Recheck state in case of desynchronization */
		state = ianjuta_debugger_get_state (self->debugger, NULL);
		dma_debugger_queue_complete (self, state);
	}

	/* Check if there is something to execute */
	while (!g_queue_is_empty(self->queue) && (self->last == NULL))
	{
		DmaQueueCommand *cmd;
		GError *err = NULL;
		gboolean ok;
		
		cmd = (DmaQueueCommand *)g_queue_pop_head(self->queue);

		/* Start command */
		self->last = cmd;
		DEBUG_PRINT("run command %x", dma_command_get_type (cmd));
		ok = dma_command_run (cmd, self->debugger, self, &err);

		if (!ok || (err != NULL))
		{
			/* Something fail */
			if (dma_command_is_going_to_state (self->last) != IANJUTA_DEBUGGER_BUSY)
			{
				/* Command has been canceled in an unexpected state,
				 * Remove invalid following command */
				dma_queue_cancel_unexpected (self, self->debugger_state);
			}

			/* Remove current command */
			DEBUG_PRINT("cancel command %x", dma_command_get_type (self->last));
			dma_command_free (self->last);
			self->last = NULL;

			/* Display error message to user */
			if (err != NULL)
			{
				if (err->message != NULL)
				{
					anjuta_util_dialog_error (GTK_WINDOW (ANJUTA_PLUGIN (self->plugin)->shell), err->message);
				}
			
				g_error_free (err);
			}
		}
	}
	
	dma_queue_emit_debugger_ready (self);
}

static gboolean
dma_queue_check_state (DmaDebuggerQueue *self, DmaQueueCommand* cmd)
{
	gboolean recheck;

	for (recheck = FALSE; recheck != TRUE; recheck = TRUE)
	{
		IAnjutaDebuggerState state;
		
		if ((self->insert_command != NULL) || dma_command_has_flag (cmd, HIGH_PRIORITY))
		{
			/* Prepend command and high priority command use debugger state or current command state */
			if (self->last != NULL)
			{
				state = dma_command_is_going_to_state (self->last);
				if (state == IANJUTA_DEBUGGER_BUSY)
				{
					state = self->debugger_state;
				}
			}
			else
			{
				state = self->debugger_state;
			}
		}
		else
		{
			/* Append command use queue state */
			state = self->queue_state;
		}
	
		/* Only the debugger can be busy */
		g_return_val_if_fail (state != IANJUTA_DEBUGGER_BUSY, FALSE);
		
		if (dma_command_is_valid_in_state (cmd, state))
    	{
			/* State is right */
			return TRUE;
		}
		
		g_warning ("Cancel command %x, debugger in state %d", dma_command_get_type (cmd), state);
		
		/* Check if synchronization is still ok */
		state = ianjuta_debugger_get_state (self->debugger, NULL);
		dma_debugger_queue_complete (self, state);
		
		/* Check again */
	}
	
	return FALSE;
}

static gboolean
dma_debugger_activate_plugin (DmaDebuggerQueue* self, const gchar *mime_type)
{
	AnjutaPluginManager *plugin_manager;
	AnjutaPluginHandle *plugin;
	GList *plugins = NULL;
	gchar *value;

	/* Get list of debugger plugins */
	plugin_manager = anjuta_shell_get_plugin_manager (ANJUTA_PLUGIN(self->plugin)->shell, NULL);
	if (mime_type == NULL)
	{
		/* User has to select the right debugger */
		plugins = anjuta_plugin_manager_query (plugin_manager,
		                                       "Anjuta Plugin","Interfaces", "IAnjutaDebugger", NULL);
	}
	else
	{
		/* Propose only debugger supporting correct mime type */
		plugins = anjuta_plugin_manager_query (plugin_manager,
		                                       "Anjuta Plugin","Interfaces", "IAnjutaDebugger",
		                                       "File Loader", "SupportedMimeTypes", mime_type,
		                                       NULL);
	}

	if (plugins == NULL)
	{
		/* No plugin found */
		anjuta_util_dialog_error (GTK_WINDOW (ANJUTA_PLUGIN (self->plugin)->shell),
				_("Unable to find a debugger plugin supporting a target with %s MIME type"), mime_type);
			
		return FALSE;
	}
	else if (g_list_length (plugins) == 1)
	{
		/* Only one plugin found, use it */
		plugin = (AnjutaPluginHandle *)plugins->data;
	}
	else
	{
		/* Ask the user to select one plugin */
		plugin = anjuta_plugin_manager_select (plugin_manager,
		                                       _("Select a plugin"), 
		                                       _("Please select a plugin to activate"),
		                                       plugins);
	}
											   
	if (plugin != NULL)
	{
		/* Get debugger interface */
		self->debugger = (IAnjutaDebugger *)anjuta_plugin_manager_get_plugin_by_handle (plugin_manager, plugin);

		self->support = 0;
		/* Check if register interface is available */
		self->support |= IANJUTA_IS_DEBUGGER_REGISTER(self->debugger) ? HAS_REGISTER : 0;
		/* Check if memory interface is available */
		self->support |= IANJUTA_IS_DEBUGGER_MEMORY(self->debugger) ? HAS_MEMORY : 0;
		/* Check if instruction interface is available */
		self->support |= IANJUTA_IS_DEBUGGER_INSTRUCTION(self->debugger) ? HAS_INSTRUCTION : 0;
		/* Check if breakpoint interface is available */
		self->support |= IANJUTA_IS_DEBUGGER_BREAKPOINT(self->debugger) ? HAS_BREAKPOINT : 0;
		if (IANJUTA_IS_DEBUGGER_BREAKPOINT (self->debugger))
		{
			self->support |= ianjuta_debugger_breakpoint_implement_breakpoint (IANJUTA_DEBUGGER_BREAKPOINT (self->debugger), NULL) * HAS_BREAKPOINT * 2;
		}			
		/* Check if variable interface is available */
		self->support |= IANJUTA_IS_DEBUGGER_VARIABLE(self->debugger) ? HAS_VARIABLE : 0;

		return TRUE;
	}
	else
	{
		/* No plugin selected */
		
		return FALSE;
	}
}

/* IAnjutaDebugger callback
 *---------------------------------------------------------------------------*/

static void
on_dma_debugger_ready (DmaDebuggerQueue *self, IAnjutaDebuggerState state)
{
	DEBUG_PRINT ("From debugger: receive debugger ready %d", state);
	
	dma_debugger_queue_complete (self, state);
}

static void
on_dma_debugger_started (DmaDebuggerQueue *self)
{
	DEBUG_PRINT ("%s", "From debugger: receive debugger started");
	dma_queue_emit_debugger_state (self, IANJUTA_DEBUGGER_STARTED, NULL);
}

static void
on_dma_debugger_stopped (DmaDebuggerQueue *self, GError *err)
{
	IAnjutaDebuggerState state;

	DEBUG_PRINT ("From debugger: receive debugger stopped with error %p", err);
	dma_queue_emit_debugger_state (self, IANJUTA_DEBUGGER_STOPPED, err);

	/* Reread debugger state, could have changed while emitting signal */
	state = ianjuta_debugger_get_state (self->debugger, NULL);
	dma_debugger_queue_complete (self, state);
}

static void
on_dma_program_loaded (DmaDebuggerQueue *self)
{
	DEBUG_PRINT ("%s", "From debugger: receive program loaded");
	dma_queue_emit_debugger_state (self, IANJUTA_DEBUGGER_PROGRAM_LOADED, NULL);
}

static void
on_dma_program_running (DmaDebuggerQueue *self)
{
	DEBUG_PRINT ("%s", "From debugger: debugger_program_running");
	dma_queue_emit_debugger_state (self, IANJUTA_DEBUGGER_PROGRAM_RUNNING, NULL);
}

static void
on_dma_program_stopped (DmaDebuggerQueue *self)
{
	DEBUG_PRINT ("%s", "From debugger: receive program stopped");
	dma_queue_emit_debugger_state (self, IANJUTA_DEBUGGER_PROGRAM_STOPPED, NULL);
}

static void
on_dma_program_exited (DmaDebuggerQueue *self)
{
	DEBUG_PRINT ("%s", "From debugger: receive program exited");
	dma_queue_emit_debugger_state (self, IANJUTA_DEBUGGER_PROGRAM_LOADED, NULL);
}

static void
on_dma_program_moved (DmaDebuggerQueue *self, guint pid, gint tid, gulong address, const gchar* src_path, guint line)
{
	DEBUG_PRINT ("%s", "From debugger: program moved");
	self->insert_command = g_list_prepend (self->insert_command, g_queue_peek_head_link (self->queue));
	g_signal_emit_by_name (self->plugin, "program-moved", pid, tid, address, src_path, line);
	self->insert_command = g_list_delete_link (self->insert_command, self->insert_command);
}

static void
on_dma_frame_changed (DmaDebuggerQueue *self, guint frame, gint thread)
{
	DEBUG_PRINT ("%s", "From debugger: frame changed");
	self->insert_command = g_list_prepend (self->insert_command, g_queue_peek_head_link (self->queue));
	g_signal_emit_by_name (self->plugin, "frame-changed", frame, thread);
	self->insert_command = g_list_delete_link (self->insert_command, self->insert_command);
}

static void
on_dma_signal_received (DmaDebuggerQueue *self, const gchar* name, const gchar* description)
{
	DEBUG_PRINT ("%s", "From debugger: signal received");
	self->insert_command = g_list_prepend (self->insert_command, g_queue_peek_head_link (self->queue));
	g_signal_emit_by_name (self->plugin, "signal-received", name, description);
	self->insert_command = g_list_delete_link (self->insert_command, self->insert_command);
}

static void
on_dma_sharedlib_event (DmaDebuggerQueue *self)
{
	DEBUG_PRINT ("%s", "From debugger: shared lib event");
	self->stop_on_sharedlib = TRUE;
	dma_debugger_queue_complete (self, IANJUTA_DEBUGGER_PROGRAM_STOPPED);
	self->insert_command = g_list_prepend (self->insert_command, g_queue_peek_head_link (self->queue));
	g_signal_emit_by_name (self->plugin, "sharedlib-event");
	self->insert_command = g_list_delete_link (self->insert_command, self->insert_command);
	dma_queue_run (self);
}

/* Public function
 *---------------------------------------------------------------------------*/

/* Command callback will add their commands at the beginning of the queue */
void
dma_debugger_queue_command_callback (const gpointer data, gpointer user_data, GError* err)
{
	DmaDebuggerQueue *self = (DmaDebuggerQueue *)user_data;

	g_return_if_fail (self->last != NULL);
	
	self->insert_command = g_list_prepend (self->insert_command, g_queue_peek_head_link (self->queue));
	if (self->queue_state != IANJUTA_DEBUGGER_STOPPED)
	{
		dma_command_callback (self->last, data, err);
	}
	self->insert_command = g_list_delete_link (self->insert_command, self->insert_command);
}

gboolean
dma_debugger_queue_append (DmaDebuggerQueue *self, DmaQueueCommand *cmd)
{
	DEBUG_PRINT("append cmd %x prepend %p", dma_command_get_type (cmd), self->insert_command);
	DEBUG_PRINT("current %x", self->last == NULL ? 0 : dma_command_get_type (self->last));
	DEBUG_PRINT("queue %x", self->queue->head == NULL ? 0 : dma_command_get_type (self->queue->head->data));
	
	if ((self->debugger != NULL) && dma_queue_check_state(self, cmd))
	{
		/* If command is asynchronous stop current command */
		if (dma_command_has_flag (cmd, ASYNCHRONOUS))
		{
			IAnjutaDebuggerState state;
			
			state = dma_command_is_going_to_state (cmd);
			if (state != IANJUTA_DEBUGGER_BUSY)
			{
				/* Command is changing debugger state */
				dma_queue_cancel_unexpected (self, state);
			}
			
			/* Append command at the beginning */
			g_queue_push_head (self->queue, cmd);
			
			/* We must not interrupt command having callback, as the command
			 * will be removed, the callback when emitted will be redirected to
			 * the handler of the next command */
			if ((state == IANJUTA_DEBUGGER_STOPPED) || (self->debugger_state == IANJUTA_DEBUGGER_PROGRAM_RUNNING))
			{
				dma_debugger_queue_complete (self, self->debugger_state);
			}
		}
		else if (dma_command_has_flag (cmd, HIGH_PRIORITY))
		{
			IAnjutaDebuggerState state;
			
			state = dma_command_is_going_to_state (cmd);
			if (state != IANJUTA_DEBUGGER_BUSY)
			{
				/* Command is changing debugger state */
				dma_queue_cancel_unexpected (self, state);
			}
			
			/* Prepend command at the beginning */
			g_queue_push_head (self->queue, cmd);
		}
		else if ((self->insert_command != NULL) && (self->insert_command->data != NULL))
		{
			IAnjutaDebuggerState state;
			
			state = dma_command_is_going_to_state (cmd);
			if (state != IANJUTA_DEBUGGER_BUSY)
			{
				/* Command is changing debugger state */
				dma_queue_cancel_unexpected (self, state);
			}
			
			/* Insert command in the beginning */
			g_queue_insert_before (self->queue, (GList *)self->insert_command->data, cmd);
		}
		else
		{
			/* Append command at the end (in the queue) */
			IAnjutaDebuggerState state;

			g_queue_push_tail (self->queue, cmd);
			
			state = dma_command_is_going_to_state (cmd);
			if (state != IANJUTA_DEBUGGER_BUSY)
			{
				self->queue_state = state;
			}
		}
	
		dma_debugger_queue_execute(self);
		
		return TRUE;
	}
	else
	{
		dma_command_free (cmd);
		
		return FALSE;
	}
}

void
dma_debugger_queue_stop (DmaDebuggerQueue *self)
{
	/* Disconnect signal */
	if (self->debugger)
	{
		g_signal_handlers_disconnect_by_func (self->debugger, G_CALLBACK (on_dma_debugger_ready), self);
		g_signal_handlers_disconnect_by_func (self->debugger, G_CALLBACK (on_dma_debugger_started), self);
		g_signal_handlers_disconnect_by_func (self->debugger, G_CALLBACK (on_dma_debugger_stopped), self);
		g_signal_handlers_disconnect_by_func (self->debugger, G_CALLBACK (on_dma_program_loaded), self);
		g_signal_handlers_disconnect_by_func (self->debugger, G_CALLBACK (on_dma_program_running), self);
		g_signal_handlers_disconnect_by_func (self->debugger, G_CALLBACK (on_dma_program_stopped), self);
		g_signal_handlers_disconnect_by_func (self->debugger, G_CALLBACK (on_dma_program_exited), self);
		g_signal_handlers_disconnect_by_func (self->debugger, G_CALLBACK (on_dma_program_moved), self);
		g_signal_handlers_disconnect_by_func (self->debugger, G_CALLBACK (on_dma_signal_received), self);
		g_signal_handlers_disconnect_by_func (self->debugger, G_CALLBACK (on_dma_frame_changed), self);
		g_signal_handlers_disconnect_by_func (self->debugger, G_CALLBACK (on_dma_sharedlib_event), self);
		self->debugger = NULL;
		self->support = 0;
	}
}

gboolean
dma_debugger_queue_start (DmaDebuggerQueue *self, const gchar *mime_type)
{
	dma_debugger_queue_stop (self);
	
	/* Look for a debugger supporting mime_type */
	if (!dma_debugger_activate_plugin (self, mime_type))
	{
		return FALSE;
	}

	if (self->debugger)
	{
		/* Connect signal */
		g_signal_connect_swapped (self->debugger, "debugger-ready", G_CALLBACK (on_dma_debugger_ready), self);
		g_signal_connect_swapped (self->debugger, "debugger-started", G_CALLBACK (on_dma_debugger_started), self);
		g_signal_connect_swapped (self->debugger, "debugger-stopped", G_CALLBACK (on_dma_debugger_stopped), self);
		g_signal_connect_swapped (self->debugger, "program-loaded", G_CALLBACK (on_dma_program_loaded), self);
		g_signal_connect_swapped (self->debugger, "program-running", G_CALLBACK (on_dma_program_running), self);
		g_signal_connect_swapped (self->debugger, "program-stopped", G_CALLBACK (on_dma_program_stopped), self);
		g_signal_connect_swapped (self->debugger, "program-exited", G_CALLBACK (on_dma_program_exited), self);
		g_signal_connect_swapped (self->debugger, "program-moved", G_CALLBACK (on_dma_program_moved), self);
		g_signal_connect_swapped (self->debugger, "signal-received", G_CALLBACK (on_dma_signal_received), self);
		g_signal_connect_swapped (self->debugger, "frame-changed", G_CALLBACK (on_dma_frame_changed), self);
		g_signal_connect_swapped (self->debugger, "sharedlib-event", G_CALLBACK (on_dma_sharedlib_event), self);

		if (self->log == NULL)
		{
			dma_queue_disable_log (self);
		}
		else
		{
			dma_queue_enable_log (self, self->log);
		}
	}
	
	return self->debugger != NULL;
}

void
dma_queue_enable_log (DmaDebuggerQueue *self, IAnjutaMessageView *log)
{
	self->log = log;
	if (self->debugger != NULL)
	{
		ianjuta_debugger_enable_log (self->debugger, self->log, NULL);
	}
}

void
dma_queue_disable_log (DmaDebuggerQueue *self)
{
	self->log = NULL;
	if (self->debugger != NULL)
	{
		ianjuta_debugger_disable_log (self->debugger, NULL);
	}
}

IAnjutaDebuggerState
dma_debugger_queue_get_state (DmaDebuggerQueue *self)
{
	return self->queue_state;
}

gboolean
dma_debugger_queue_is_supported (DmaDebuggerQueue *self, DmaDebuggerCapability capability)
{
	return self->support & capability ? TRUE : FALSE;
}

/* GObject functions
 *---------------------------------------------------------------------------*/

/* Used in dispose and finalize */
static gpointer parent_class;

/* dispose is the first destruction step. It is used to unref object created
 * with instance_init in order to break reference counting cycles. This
 * function could be called several times. All function should still work
 * after this call. It has to called its parents.*/

static void
dma_debugger_queue_dispose (GObject *obj)
{
	DmaDebuggerQueue *self = DMA_DEBUGGER_QUEUE (obj);

	dma_debugger_queue_clear (self);

	G_OBJECT_CLASS (parent_class)->dispose (obj);
}

/* finalize is the last destruction step. It must free all memory allocated
 * with instance_init. It is called only one time just before releasing all
 * memory */

static void
dma_debugger_queue_finalize (GObject *obj)
{
	DmaDebuggerQueue *self = DMA_DEBUGGER_QUEUE (obj);

	g_queue_free (self->queue);

	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* instance_init is the constructor. All functions should work after this
 * call. */

static void
dma_debugger_queue_instance_init (DmaDebuggerQueue *self)
{
	self->plugin = NULL;
	self->debugger = NULL;
	self->support = 0;
	self->queue = g_queue_new ();
	self->last = NULL;
	self->busy = FALSE;
	self->insert_command = NULL;
	self->debugger_state = IANJUTA_DEBUGGER_STOPPED;
	self->queue_state = IANJUTA_DEBUGGER_STOPPED;
	self->log = NULL;
}

/* class_init intialize the class itself not the instance */

static void
dma_debugger_queue_class_init (DmaDebuggerQueueClass * klass)
{
	GObjectClass *object_class;

	g_return_if_fail (klass != NULL);
	object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_peek_parent (klass);
	
	object_class->dispose = dma_debugger_queue_dispose;
	object_class->finalize = dma_debugger_queue_finalize;
}

GType
dma_debugger_queue_get_type (void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo type_info = 
		{
			sizeof (DmaDebuggerQueueClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) dma_debugger_queue_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,           /* class_data */
			sizeof (DmaDebuggerQueue),
			0,              /* n_preallocs */
			(GInstanceInitFunc) dma_debugger_queue_instance_init,
			NULL            /* value_table */
		};

		type = g_type_register_static (G_TYPE_OBJECT,
		                            "DmaDebuggerQueue", &type_info, 0);
	}
	
	return type;
}

/* Creation and Destruction
 *---------------------------------------------------------------------------*/

DmaDebuggerQueue*
dma_debugger_queue_new (AnjutaPlugin *plugin)
{
	DmaDebuggerQueue *self;

	self = g_object_new (DMA_DEBUGGER_QUEUE_TYPE, NULL);
	self->plugin = plugin;
	
	return self;
}
	
void
dma_debugger_queue_free (DmaDebuggerQueue *self)
{
	dma_debugger_queue_stop (self);
	g_object_unref (self);
}

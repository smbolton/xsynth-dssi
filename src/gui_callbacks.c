/* Xsynth DSSI software synthesizer GUI
 *
 * Copyright (C) 2004 Sean Bolton and others.
 *
 * Portions of this file may have come from Steve Brookes'
 * Xsynth, copyright (C) 1999 S. J. Brookes.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define _BSD_SOURCE    1
#define _SVID_SOURCE   1
#define _ISOC99_SOURCE 1

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include <gtk/gtk.h>
#include <lo/lo.h>

#include "xsynth_types.h"
#include "xsynth.h"
#include "xsynth_ports.h"
#include "xsynth_voice.h"
#include "gui_main.h"
#include "gui_callbacks.h"
#include "gui_interface.h"
#include "gui_data.h"

extern GtkObject *voice_widget[XSYNTH_PORTS_COUNT];

extern xsynth_patch_t *patches;

extern lo_address osc_host_address;
extern char *     osc_configure_path;
extern char *     osc_control_path;
extern char *     osc_midi_path;
extern char *     osc_program_path;
extern char *     osc_update_path;

static int internal_gui_update_only = 0;

static unsigned char test_note_noteon_key = 60;
static unsigned char test_note_noteoff_key;

static gchar *file_selection_last_filename = NULL;

void
on_menu_open_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    gtk_widget_hide(save_file_selection);
    gtk_widget_hide(open_file_position_window);
    if (file_selection_last_filename)
        gtk_file_selection_set_filename(GTK_FILE_SELECTION(open_file_selection),
                                        file_selection_last_filename);
    gtk_widget_show(open_file_selection);
}


void
on_menu_save_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    gtk_widget_hide(open_file_selection);
    gtk_widget_hide(open_file_position_window);
    if (file_selection_last_filename)
        gtk_file_selection_set_filename(GTK_FILE_SELECTION(save_file_selection),
                                        file_selection_last_filename);
    gtk_widget_show(save_file_selection);
}


void
on_menu_quit_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    // -FIX- if any patches changed, ask "are you sure?" or "save changes?"
    gtk_main_quit();
}


void
on_menu_about_activate                 (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    gtk_widget_show(about_window);
}

gint
on_delete_event_wrapper( GtkWidget *widget, GdkEvent *event, gpointer data )
{
    void (*handler)(GtkWidget *, gpointer) = (void (*)(GtkWidget *, gpointer))data;

    /* call our 'dismiss' or 'cancel' callback (which must not need the user data) */
    (*handler)(widget, NULL);

    /* tell GTK+ to NOT emit 'destroy' */
    return TRUE;
}

void
on_open_file_ok( GtkWidget *widget, gpointer data )
{
    gtk_widget_hide(open_file_selection);
    file_selection_last_filename = gtk_file_selection_get_filename(
                                       GTK_FILE_SELECTION(open_file_selection));

    GDB_MESSAGE(GDB_GUI, " on_open_file_ok: file '%s' selected\n",
                    file_selection_last_filename);

    (GTK_ADJUSTMENT(open_file_position_spin_adj))->value = 0.0f;
    (GTK_ADJUSTMENT(open_file_position_spin_adj))->upper =
            (patch_count == 128 ? 127.0f : (float)patch_count);
    gtk_signal_emit_by_name (GTK_OBJECT (open_file_position_spin_adj), "value_changed");

    gtk_widget_show(open_file_position_window);
}

void
on_open_file_cancel( GtkWidget *widget, gpointer data )
{
    GDB_MESSAGE(GDB_GUI, ": on_open_file_cancel called\n");
    gtk_widget_hide(open_file_selection);
}

/*
 * on_position_change
 *
 * used by both the open file position and edit save position dialogs
 * data is a pointer to the dialog's patch name label
 */
void
on_position_change(GtkWidget *widget, gpointer data)
{
    int position = lrintf(GTK_ADJUSTMENT(widget)->value);
    GtkWidget *label = (GtkWidget *)data;

    if (position >= patch_count) {
        gtk_label_set_text (GTK_LABEL (label), "(empty)");
    } else {
        gtk_label_set_text (GTK_LABEL (label), patches[position].name);
    }
}

void
on_open_file_position_ok( GtkWidget *widget, gpointer data )
{
    int position = lrintf(GTK_ADJUSTMENT(open_file_position_spin_adj)->value);
    char *message;

    gtk_widget_hide(open_file_position_window);

    GDB_MESSAGE(GDB_GUI, " on_open_file_position_ok: position %d\n", position);

    if (gui_data_load(file_selection_last_filename, position, &message)) {

        /* successfully loaded at least one patch */
        rebuild_patches_clist();
        display_notice("Load Patch File succeeded:", message);

        if (patches_dirty) {

            /* our patch bank is dirty, so we need to save a temporary copy
             * for the plugin to load */
            if (gui_data_save_dirty_patches_to_tmp()) {
                lo_send(osc_host_address, osc_configure_path, "ss", "load",
                        patches_tmp_filename);
                last_configure_load_was_from_tmp = 1;
            } else {
                display_notice("Load Patch File error:", "couldn't save temporary bank to /tmp");
            }
            
        } else {

            /* patches is clean after the load, so tell the plugin to
             * load the same file */

            lo_send(osc_host_address, osc_configure_path, "ss", "load",
                    file_selection_last_filename);

            /* clean up old temporary file, if any */
            if (last_configure_load_was_from_tmp) {
                unlink(patches_tmp_filename);
            }
            last_configure_load_was_from_tmp = 0;
        }

    } else {  /* didn't load anything successfully */

        display_notice("Load Patch File failed:", message);

    }
    free(message);
}

void
on_open_file_position_cancel( GtkWidget *widget, gpointer data )
{
    GDB_MESSAGE(GDB_GUI, " on_open_file_position_cancel called\n");
    gtk_widget_hide(open_file_position_window);
}

void
on_save_file_ok( GtkWidget *widget, gpointer data )
{
    char *message;

    gtk_widget_hide(save_file_selection);
    file_selection_last_filename = gtk_file_selection_get_filename(
                                       GTK_FILE_SELECTION(save_file_selection));

    GDB_MESSAGE(GDB_GUI, " on_open_file_ok: file '%s' selected\n",
                    file_selection_last_filename);

    if (gui_data_save(file_selection_last_filename, &message)) {

        display_notice("Save Patch File succeeded:", message);

        patches_dirty = 0;
        lo_send(osc_host_address, osc_configure_path, "ss", "load",
                file_selection_last_filename);
        /* clean up old temporary file, if any */
        if (last_configure_load_was_from_tmp) {
            unlink(patches_tmp_filename);
        }
        last_configure_load_was_from_tmp = 0;

    } else {  /* problem with save */

        display_notice("Save Patch File failed:", message);

    }
    free(message);
}

void
on_save_file_cancel( GtkWidget *widget, gpointer data )
{
    GDB_MESSAGE(GDB_GUI, ": on_save_file_cancel called\n");
    gtk_widget_hide(save_file_selection);
}

void
on_about_dismiss( GtkWidget *widget, gpointer data )
{
    gtk_widget_hide(about_window);
}

void
on_notebook_switch_page(GtkNotebook     *notebook,
                        GtkNotebookPage *page,
                        guint            page_num)
{
    GDB_MESSAGE(GDB_GUI, " on_notebook_switch_page: page %d selected\n", page_num);
// gtk_notebook_get_current_page( GtkNotebook *notebook );

//     if (page_num == 1) {
//         gtk_widget_show(patch_edit_table);  // blinking
//     } else {
//         gtk_widget_hide(patch_edit_table);  // blinking: doesn't work because the whole tab disappears!
//     }
}

void
on_patches_selection(GtkWidget      *clist,
                     gint            row,
                     gint            column,
                     GdkEventButton *event,
                     gpointer        data )
{
    if (internal_gui_update_only) {
        /* GDB_MESSAGE(GDB_GUI, " on_patches_selection: skipping further action\n"); */
        return;
    }

    GDB_MESSAGE(GDB_GUI, " on_patches_selection: patch %d selected\n", row);

    /* set all the patch edit widgets to match */
    update_voice_widgets_from_patch(&patches[row]);

    lo_send(osc_host_address, osc_program_path, "ii", 0, row);
}

void
on_voice_slider_change( GtkWidget *widget, gpointer data )
{
    int index = (int)data;
    struct xsynth_port_descriptor *xpd = &xsynth_port_description[index];
    float cval = GTK_ADJUSTMENT(widget)->value / 10.0f;
    float value;

    if (internal_gui_update_only) {
        /* GDB_MESSAGE(GDB_GUI, " on_voice_slider_change: skipping further action\n"); */
        return;
    }

    if (xpd->type == XSYNTH_PORT_TYPE_LINEAR) {

        value = (xpd->a * cval + xpd->b) * cval + xpd->c;  /* linear or quadratic */

    } else { /* XSYNTH_PORT_TYPE_LOGARITHMIC */

        value = xpd->a * exp(xpd->c * cval * log(xpd->b));

    }

    GDB_MESSAGE(GDB_GUI, " on_voice_slider_change: slider %d changed to %10.6f => %10.6f\n",
            index, GTK_ADJUSTMENT(widget)->value, value);

    lo_send(osc_host_address, osc_control_path, "if", index, value);
}

void
on_voice_detent_change( GtkWidget *widget, gpointer data )
{
    int index = (int)data;
    int value = lrintf(GTK_ADJUSTMENT(widget)->value);

    update_detent_label(index, value);

    if (internal_gui_update_only) {
        /* GDB_MESSAGE(GDB_GUI, " on_voice_detent_change: skipping further action\n"); */
        return;
    }

    GDB_MESSAGE(GDB_GUI, " on_voice_detent_change: detent %d changed to %d\n",
            index, value);

    lo_send(osc_host_address, osc_control_path, "if", index, (float)value);
}

void
on_voice_onoff_toggled( GtkWidget *widget, gpointer data )
{
    int index = (int)data;
    int state = GTK_TOGGLE_BUTTON (widget)->active;

    if (internal_gui_update_only) {
        /* GDB_MESSAGE(GDB_GUI, " on_voice_onoff_toggled: skipping further action\n"); */
        return;
    }

    GDB_MESSAGE(GDB_GUI, " on_voice_onoff_toggled: button %d changed to %s\n",
                index, (state ? "on" : "off"));

    lo_send(osc_host_address, osc_control_path, "if", index, (state ? 1.0f : 0.0f));
}

void
on_test_note_slider_change(GtkWidget *widget, gpointer data)
{
    test_note_noteon_key = lrintf(GTK_ADJUSTMENT(widget)->value);

    GDB_MESSAGE(GDB_GUI, " on_test_note_slider_change: new test note key %d\n", test_note_noteon_key);
}

void
on_test_note_button_press(GtkWidget *widget, gpointer data)
{
    unsigned char midi[4] = { 0x00, 0x90, 0x3C, 0x40 };

    if ((int)data) {  /* button pressed */

        midi[1] = 0x90;
        midi[2] = test_note_noteon_key;
        lo_send(osc_host_address, osc_midi_path, "m", midi);
        test_note_noteoff_key = test_note_noteon_key;

    } else { /* button released */

        midi[1] = 0x80;
        midi[2] = test_note_noteoff_key;
        lo_send(osc_host_address, osc_midi_path, "m", midi);

    }
}

// !FIX! split this into two functions
void
on_edit_action_button_press(GtkWidget *widget, gpointer data)
{
    int save_changes = (int)data;

    if (save_changes) {
        
        GDB_MESSAGE(GDB_GUI, " on_edit_action_button_press: 'save changes' clicked\n");

        (GTK_ADJUSTMENT(edit_save_position_spin_adj))->value = 
                (patch_count == 128 ?   0.0f : (float)patch_count);
        (GTK_ADJUSTMENT(edit_save_position_spin_adj))->upper =
                (patch_count == 128 ? 127.0f : (float)patch_count);
        gtk_signal_emit_by_name (GTK_OBJECT (edit_save_position_spin_adj), "value_changed");

        gtk_widget_show(edit_save_position_window);

    } else {  /* discard changes */

        GDB_MESSAGE(GDB_GUI, " on_edit_action_button_press: 'discard changes' clicked\n");
        /* !FIX! implement */

    }
}

void
on_edit_save_position_ok( GtkWidget *widget, gpointer data )
{
    int position = lrintf(GTK_ADJUSTMENT(edit_save_position_spin_adj)->value);

    gtk_widget_hide(edit_save_position_window);

    GDB_MESSAGE(GDB_GUI, " on_edit_save_position_ok: position %d\n", position);

    /* set the patch to match all the edit widgets */
    update_patch_from_voice_widgets(&patches[position]);
    patches_dirty = 1;
    if (position == patch_count) patch_count++;
    rebuild_patches_clist();

    /* our patch bank is now dirty, so we need to save a temporary copy
     * for the plugin to load */
    if (gui_data_save_dirty_patches_to_tmp()) {
        lo_send(osc_host_address, osc_configure_path, "ss", "load",
                patches_tmp_filename);
        last_configure_load_was_from_tmp = 1;
    } else {
        display_notice("Patch Edit Save Changes error:", "couldn't save temporary bank to /tmp");
    }
}

void
on_edit_save_position_cancel( GtkWidget *widget, gpointer data )
{
    GDB_MESSAGE(GDB_GUI, " on_edit_save_position_cancel called\n");
    gtk_widget_hide(edit_save_position_window);
}

void
on_polyphony_change(GtkWidget *widget, gpointer data)
{
    int polyphony = lrintf(GTK_ADJUSTMENT(widget)->value);
    char buffer[4];
    
    GDB_MESSAGE(GDB_GUI, " on_polyphony_change: polyphony set to %d\n", polyphony);

    snprintf(buffer, 4, "%d", polyphony);
    lo_send(osc_host_address, osc_configure_path, "ss", "polyphony", buffer);
}

void
on_mono_mode_activate(GtkWidget *widget, gpointer data)
{
    char *mode = data;

    GDB_MESSAGE(GDB_GUI, " on_mono_mode_activate: monophonic mode '%s' selected\n", mode);

    lo_send(osc_host_address, osc_configure_path, "ss", "monophonic", mode);
}

void
display_notice(char *message1, char *message2)
{
    gtk_label_set_text (GTK_LABEL (notice_label_1), message1);
    gtk_label_set_text (GTK_LABEL (notice_label_2), message2);
    gtk_widget_show(notice_window);
}

void
on_notice_dismiss( GtkWidget *widget, gpointer data )
{
    gtk_widget_hide(notice_window);
}

void
update_detent_label(int index, int value)
{
    char *waveform;

    switch (value) {
      default:
      case 0:  waveform = "sine";          break;
      case 1:  waveform = "triangle";      break;
      case 2:  waveform = "sawtooth up";   break;
      case 3:  waveform = "sawtooth down"; break;
      case 4:  waveform = "square";        break;
      case 5:  waveform = "pulse";         break;
    }

    switch (index) {
      case XSYNTH_PORT_OSC1_WAVEFORM:
        gtk_label_set_text (GTK_LABEL (osc1_waveform_label), waveform);
        break;

      case XSYNTH_PORT_OSC2_WAVEFORM:
        gtk_label_set_text (GTK_LABEL (osc2_waveform_label), waveform);
        break;

      case XSYNTH_PORT_LFO_WAVEFORM:
        gtk_label_set_text (GTK_LABEL (lfo_waveform_label), waveform);
        break;

      default:
        break;
    }
}

void
update_voice_widget(int port, float value)
{
    struct xsynth_port_descriptor *xpd;
    GtkAdjustment *adj;
    GtkWidget *button;
    float cval;
    int dval;

    if (port < XSYNTH_PORT_OSC1_PITCH || port > XSYNTH_PORT_VOLUME) {
        return;
    }
    xpd = &xsynth_port_description[port];
    if (value < xpd->lower_bound)
        value = xpd->lower_bound;
    else if (value > xpd->upper_bound)
        value = xpd->upper_bound;
    
    /* while (gtk_main_iteration(FALSE)); ? */
    internal_gui_update_only = 1;

    switch (xpd->type) {

      case XSYNTH_PORT_TYPE_LINEAR:
        cval = (value - xpd->c) / xpd->b;  /* assume xpd->a == 0, which was always true for Xsynth */
        /* GDB_MESSAGE(GDB_GUI, " update_voice_widget: change of %s to %f => %f\n", xpd->name, value, cval); */
        adj = (GtkAdjustment *)voice_widget[port];
        adj->value = cval * 10.0f;
        /* gtk_signal_emit_by_name (GTK_OBJECT (adj), "changed");        does not cause call to on_voice_slider_change callback */
        gtk_signal_emit_by_name (GTK_OBJECT (adj), "value_changed");  /* causes call to on_voice_slider_change callback */
        break;

      case XSYNTH_PORT_TYPE_LOGARITHMIC:
        cval = log(value / xpd->a) / (xpd->c * log(xpd->b));
        if (cval < 1.0e-6f)
            cval = 0.0f;
        else if (cval > 1.0f - 1.0e-6f)
            cval = 1.0f;
        /* GDB_MESSAGE(GDB_GUI, " update_voice_widget: change of %s to %f => %f\n", xpd->name, value, cval); */
        adj = (GtkAdjustment *)voice_widget[port];
        adj->value = cval * 10.0f;
        gtk_signal_emit_by_name (GTK_OBJECT (adj), "value_changed");  /* causes call to on_voice_slider_change callback */
        break;

      case XSYNTH_PORT_TYPE_DETENT:
        dval = lrintf(value);
        /* GDB_MESSAGE(GDB_GUI, " update_voice_widget: change of %s to %f => %d\n", xpd->name, value, dval); */
        adj = (GtkAdjustment *)voice_widget[port];
        adj->value = (float)dval;
        gtk_signal_emit_by_name (GTK_OBJECT (adj), "value_changed");  /* causes call to on_voice_detent_change callback */
        break;

      case XSYNTH_PORT_TYPE_ONOFF:
        dval = (value > 0.0001f ? 1 : 0);
        /* GDB_MESSAGE(GDB_GUI, " update_voice_widget: change of %s to %f => %d\n", xpd->name, value, dval); */
        button = (GtkWidget *)voice_widget[port];
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), dval); /* causes call to on_voice_onoff_toggled callback */
        break;

      default:
        break;
    }

    /* gdk_flush(); ? */
    /* while (gtk_main_iteration(FALSE)); ? */
    internal_gui_update_only = 0;
}

void
update_voice_widgets_from_patch(xsynth_patch_t *patch)
{
    // gtk_widget_hide(patch_edit_table);  // blinking: no, makes it worse
    // gtk_clist_freeze(GTK_CLIST(patches_clist));  // blinking: no effect
    // gtk_widget_hide(notebook1);  // blinking: bad, but best so far
    update_voice_widget(XSYNTH_PORT_OSC1_PITCH,        patch->osc1_pitch);
    update_voice_widget(XSYNTH_PORT_OSC1_WAVEFORM,     (float)patch->osc1_waveform);
    update_voice_widget(XSYNTH_PORT_OSC1_PULSEWIDTH,   patch->osc1_pulsewidth);
    update_voice_widget(XSYNTH_PORT_OSC2_PITCH,        patch->osc2_pitch);
    update_voice_widget(XSYNTH_PORT_OSC2_WAVEFORM,     (float)patch->osc2_waveform);
    update_voice_widget(XSYNTH_PORT_OSC2_PULSEWIDTH,   patch->osc2_pulsewidth);
    update_voice_widget(XSYNTH_PORT_OSC_SYNC,          (float)patch->osc_sync);
    update_voice_widget(XSYNTH_PORT_OSC_BALANCE,       patch->osc_balance);
    update_voice_widget(XSYNTH_PORT_LFO_FREQUENCY,     patch->lfo_frequency);
    update_voice_widget(XSYNTH_PORT_LFO_WAVEFORM,      (float)patch->lfo_waveform);
    update_voice_widget(XSYNTH_PORT_LFO_AMOUNT_O,      patch->lfo_amount_o);
    update_voice_widget(XSYNTH_PORT_LFO_AMOUNT_F,      patch->lfo_amount_f);
    update_voice_widget(XSYNTH_PORT_EG1_ATTACK_TIME,   patch->eg1_attack_time);
    update_voice_widget(XSYNTH_PORT_EG1_DECAY_TIME,    patch->eg1_decay_time);
    update_voice_widget(XSYNTH_PORT_EG1_SUSTAIN_LEVEL, patch->eg1_sustain_level);
    update_voice_widget(XSYNTH_PORT_EG1_RELEASE_TIME,  patch->eg1_release_time);
    update_voice_widget(XSYNTH_PORT_EG1_AMOUNT_O,      patch->eg1_amount_o);
    update_voice_widget(XSYNTH_PORT_EG1_AMOUNT_F,      patch->eg1_amount_f);
    update_voice_widget(XSYNTH_PORT_EG2_ATTACK_TIME,   patch->eg2_attack_time);
    update_voice_widget(XSYNTH_PORT_EG2_DECAY_TIME,    patch->eg2_decay_time);
    update_voice_widget(XSYNTH_PORT_EG2_SUSTAIN_LEVEL, patch->eg2_sustain_level);
    update_voice_widget(XSYNTH_PORT_EG2_RELEASE_TIME,  patch->eg2_release_time);
    update_voice_widget(XSYNTH_PORT_EG2_AMOUNT_O,      patch->eg2_amount_o);
    update_voice_widget(XSYNTH_PORT_EG2_AMOUNT_F,      patch->eg2_amount_f);
    update_voice_widget(XSYNTH_PORT_VCF_CUTOFF,        patch->vcf_cutoff);
    update_voice_widget(XSYNTH_PORT_VCF_QRES,          patch->vcf_qres);
    update_voice_widget(XSYNTH_PORT_VCF_4POLE,         (float)patch->vcf_4pole);
    update_voice_widget(XSYNTH_PORT_GLIDE_TIME,        patch->glide_time);
    update_voice_widget(XSYNTH_PORT_VOLUME,            patch->volume);
    gtk_entry_set_text(GTK_ENTRY(name_entry), patch->name);
    // gtk_widget_show(notebook1);
    // gtk_clist_thaw(GTK_CLIST(patches_clist));
    // gtk_widget_show(patch_edit_table);
}

void
update_from_program_select(int bank, int program)
{
    internal_gui_update_only = 1;
    gtk_clist_select_row (GTK_CLIST(patches_clist), program, 0);
    internal_gui_update_only = 0;

    update_voice_widgets_from_patch(&patches[program]);
}

static float
get_value_from_slider(int index)
{
    struct xsynth_port_descriptor *xpd = &xsynth_port_description[index];
    float cval = GTK_ADJUSTMENT(voice_widget[index])->value / 10.0f;

    if (xpd->type == XSYNTH_PORT_TYPE_LINEAR) {

        return (xpd->a * cval + xpd->b) * cval + xpd->c;  /* linear or quadratic */

    } else { /* XSYNTH_PORT_TYPE_LOGARITHMIC */

        return xpd->a * exp(xpd->c * cval * log(xpd->b));

    }
}

static unsigned char
get_value_from_detent(int index)
{
    return lrintf(GTK_ADJUSTMENT(voice_widget[index])->value);
}

static unsigned char
get_value_from_onoff(int index)
{
    return (GTK_TOGGLE_BUTTON (voice_widget[index])->active ? 1 : 0);
}

void
update_patch_from_voice_widgets(xsynth_patch_t *patch)
{
    int i;

    patch->osc1_pitch           = get_value_from_slider(XSYNTH_PORT_OSC1_PITCH);        
    patch->osc1_waveform        = get_value_from_detent(XSYNTH_PORT_OSC1_WAVEFORM);     
    patch->osc1_pulsewidth      = get_value_from_slider(XSYNTH_PORT_OSC1_PULSEWIDTH);   
    patch->osc2_pitch           = get_value_from_slider(XSYNTH_PORT_OSC2_PITCH);        
    patch->osc2_waveform        = get_value_from_detent(XSYNTH_PORT_OSC2_WAVEFORM);     
    patch->osc2_pulsewidth      = get_value_from_slider(XSYNTH_PORT_OSC2_PULSEWIDTH);   
    patch->osc_sync             = get_value_from_onoff(XSYNTH_PORT_OSC_SYNC);          
    patch->osc_balance          = get_value_from_slider(XSYNTH_PORT_OSC_BALANCE);       
    patch->lfo_frequency        = get_value_from_slider(XSYNTH_PORT_LFO_FREQUENCY);     
    patch->lfo_waveform         = get_value_from_detent(XSYNTH_PORT_LFO_WAVEFORM);      
    patch->lfo_amount_o         = get_value_from_slider(XSYNTH_PORT_LFO_AMOUNT_O);      
    patch->lfo_amount_f         = get_value_from_slider(XSYNTH_PORT_LFO_AMOUNT_F);      
    patch->eg1_attack_time      = get_value_from_slider(XSYNTH_PORT_EG1_ATTACK_TIME);   
    patch->eg1_decay_time       = get_value_from_slider(XSYNTH_PORT_EG1_DECAY_TIME);    
    patch->eg1_sustain_level    = get_value_from_slider(XSYNTH_PORT_EG1_SUSTAIN_LEVEL); 
    patch->eg1_release_time     = get_value_from_slider(XSYNTH_PORT_EG1_RELEASE_TIME);  
    patch->eg1_amount_o         = get_value_from_slider(XSYNTH_PORT_EG1_AMOUNT_O);      
    patch->eg1_amount_f         = get_value_from_slider(XSYNTH_PORT_EG1_AMOUNT_F);      
    patch->eg2_attack_time      = get_value_from_slider(XSYNTH_PORT_EG2_ATTACK_TIME);   
    patch->eg2_decay_time       = get_value_from_slider(XSYNTH_PORT_EG2_DECAY_TIME);    
    patch->eg2_sustain_level    = get_value_from_slider(XSYNTH_PORT_EG2_SUSTAIN_LEVEL); 
    patch->eg2_release_time     = get_value_from_slider(XSYNTH_PORT_EG2_RELEASE_TIME);  
    patch->eg2_amount_o         = get_value_from_slider(XSYNTH_PORT_EG2_AMOUNT_O);      
    patch->eg2_amount_f         = get_value_from_slider(XSYNTH_PORT_EG2_AMOUNT_F);      
    patch->vcf_cutoff           = get_value_from_slider(XSYNTH_PORT_VCF_CUTOFF);        
    patch->vcf_qres             = get_value_from_slider(XSYNTH_PORT_VCF_QRES);          
    patch->vcf_4pole            = get_value_from_onoff(XSYNTH_PORT_VCF_4POLE);         
    patch->glide_time           = get_value_from_slider(XSYNTH_PORT_GLIDE_TIME);        
    patch->volume               = get_value_from_slider(XSYNTH_PORT_VOLUME);

    strncpy(patch->name, gtk_entry_get_text(GTK_ENTRY(name_entry)), 30);
    patch->name[30] = 0;
    /* trim trailing spaces */
    i = strlen(patch->name);
    while(i && patch->name[i - 1] == ' ') i--;
    patch->name[i] = 0;
}

void
rebuild_patches_clist(void)
{
    char number[4], name[31];
    char *data[2] = { number, name };
    int i;

    GDB_MESSAGE(GDB_GUI, ": rebuild_patches_clist called\n");

    gtk_clist_freeze(GTK_CLIST(patches_clist));
    gtk_clist_clear(GTK_CLIST(patches_clist));
    if (patch_count == 0) {
        strcpy(number, "0");
        strcpy(name, "default voice");
        gtk_clist_append(GTK_CLIST(patches_clist), data);
    } else {
        for (i = 0; i < patch_count; i++) {
            snprintf(number, 4, "%d", i);
            strncpy(name, patches[i].name, 31);
            gtk_clist_append(GTK_CLIST(patches_clist), data);
        }
    }
    gtk_clist_thaw(GTK_CLIST(patches_clist));
}


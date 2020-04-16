/**************************************************************************
 **
 ** sngrep - SIP Messages flow viewer
 **
 ** Copyright (C) 2013-2019 Ivan Alonso (Kaian)
 ** Copyright (C) 2013-2019 Irontec SL. All rights reserved.
 **
 ** This program is free software: you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, either version 3 of the License, or
 ** (at your option) any later version.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **
 ****************************************************************************/
/**
 * @file window.c
 * @author Ivan Alonso [aka Kaian] <kaian@irontec.com>
 *
 * @brief Source of functions defined in ui_panel.h
 */

#include "config.h"
#include <string.h>
#include <panel.h>
#include "glib-extra/glist.h"
#include "tui/theme.h"
#include "tui/widgets/box.h"
#include "tui/keybinding.h"
#include "tui/widgets/window.h"

typedef struct
{
    //! Curses panel pointer
    PANEL *panel;
    //! Panel Type @see PanelTypes enum
    WindowType type;
    //! Flag this panel as redraw required
    gboolean changed;
    //! Focusable widget chain
    GList *focus_chain;
    //! Default focus widget
    SngWidget *focus_default;
    //! Current focused widget
    SngWidget *focus;
} WindowPrivate;

// Window class definition
G_DEFINE_TYPE_WITH_PRIVATE(Window, window, TUI_TYPE_BOX)

Window *
window_new(gint height, gint width)
{
    Window *window = g_object_new(
        TUI_TYPE_WINDOW,
        "height", height,
        "width", width,
        "vexpand", TRUE,
        "hexpand", TRUE,
        NULL
    );

    return window;
}

PANEL *
window_get_ncurses_panel(Window *window)
{
    WindowPrivate *priv = window_get_instance_private(window);
    return priv->panel;
}

WINDOW *
window_get_ncurses_window(Window *window)
{
    return sng_widget_get_ncurses_window(SNG_WIDGET(window));
}

void
window_set_window_type(Window *window, WindowType type)
{
    WindowPrivate *priv = window_get_instance_private(window);
    priv->type = type;
}

guint
window_get_window_type(Window *window)
{
    WindowPrivate *priv = window_get_instance_private(window);
    return priv->type;
}

void
window_set_width(Window *window, gint width)
{
    sng_widget_set_width(SNG_WIDGET(window), width);
}

gint
window_get_width(Window *window)
{
    return sng_widget_get_width(SNG_WIDGET(window));
}

void
window_set_height(Window *window, gint height)
{
    sng_widget_set_height(SNG_WIDGET(window), height);
}

gint
window_get_height(Window *window)
{
    return sng_widget_get_height(SNG_WIDGET(window));
}

void
window_set_default_focus(Window *window, SngWidget *widget)
{
    WindowPrivate *priv = window_get_instance_private(window);
    priv->focus_default = widget;
    sng_widget_grab_focus(priv->focus_default);
}

SngWidget *
window_focused_widget(Window *window)
{
    WindowPrivate *priv = window_get_instance_private(window);
    return priv->focus;
}

void
window_set_focused_widget(Window *window, SngWidget *widget)
{
    WindowPrivate *priv = window_get_instance_private(window);

    // SngWidget already has the focus
    if (priv->focus == widget) {
        return;
    }

    // Remove focus from previous focused widget
    sng_widget_focus_lost(priv->focus);
    priv->focus = widget;
    sng_widget_focus_gain(widget);
}

static void
window_focus_default_widget(Window *window)
{
    WindowPrivate *priv = window_get_instance_private(window);
    sng_widget_grab_focus(priv->focus_default);
}

void
window_focus_next(Window *window)
{
    WindowPrivate *priv = window_get_instance_private(window);
    GList *current = g_list_find(priv->focus_chain, priv->focus);
    do {
        if (current->next == NULL) {
            current = g_list_first(priv->focus_chain);
        } else {
            current = (current->next)
                      ? current->next
                      : g_list_first(priv->focus_chain);
        }
    } while (!sng_widget_is_visible(current->data));

    window_set_focused_widget(window, current->data);
}

void
window_focus_prev(Window *window)
{
    WindowPrivate *priv = window_get_instance_private(window);
    GList *current = g_list_find(priv->focus_chain, priv->focus);
    do {
        if (current->prev == NULL) {
            current = g_list_last(priv->focus_chain);
        } else {
            current = (current->prev)
                      ? current->prev
                      : g_list_last(priv->focus_chain);
        }
    } while (!sng_widget_is_visible(current->data));

    window_set_focused_widget(window, current->data);
}

gboolean
window_redraw(Window *window)
{
    g_return_val_if_fail(TUI_IS_WINDOW(window), FALSE);

    WindowPrivate *priv = window_get_instance_private(window);
    // If ui has changed, force redraw. Don't even ask.
    if (priv->changed) {
        priv->changed = FALSE;
        return TRUE;
    }

    WindowClass *klass = TUI_WINDOW_GET_CLASS(window);
    if (klass->redraw != NULL) {
        return klass->redraw(window);
    }

    return TRUE;
}

static void
window_map_floating_child(SngWidget *widget, gpointer data)
{
    if (TUI_IS_CONTAINER(widget)) {
        GList *children = container_get_children(TUI_CONTAINER(widget));
        for (GList *l = children; l != NULL; l = l->next) {
            window_map_floating_child(l->data, data);
        }
    }

    if (sng_widget_is_floating(widget)) {
        sng_widget_map(widget);
    }
}

static void
window_map_floating(Window *window)
{
    container_foreach(TUI_CONTAINER(window), (GFunc) window_map_floating_child, NULL);
}

static void
window_realize(SngWidget *widget)
{
    if (!sng_widget_is_realized(widget)) {
        // Get current screen dimensions
        gint maxx, maxy, xpos = 0, ypos = 0;
        getmaxyx(stdscr, maxy, maxx);

        gint height = sng_widget_get_height(widget);
        gint width = sng_widget_get_width(widget);

        // If panel doesn't fill the screen center it
        if (height != maxy) {
            xpos = ABS((maxy - height) / 2);
        }
        if (width != maxx) {
            ypos = ABS((maxx - width) / 2);
        }
        sng_widget_set_position(widget, xpos, ypos);

        WINDOW *win = newwin(height, width, xpos, ypos);
        sng_widget_set_ncurses_window(widget, win);
        wtimeout(win, 0);
        keypad(win, TRUE);

        WindowPrivate *priv = window_get_instance_private(TUI_WINDOW(widget));
        priv->panel = new_panel(win);
    }

    SNG_WIDGET_CLASS(window_parent_class)->realize(widget);
}

static void
window_update_focus_chain(Window *window, SngWidget *widget)
{
    if (sng_widget_can_focus(widget)) {
        WindowPrivate *priv = window_get_instance_private(window);
        priv->focus_chain = g_list_append(priv->focus_chain, widget);
        g_signal_connect_swapped(widget, "lose-focus",
                                 G_CALLBACK(window_focus_default_widget), window);
        g_signal_connect_swapped(widget, "grab-focus",
                                 G_CALLBACK(window_set_focused_widget), window);
    }

    if (TUI_IS_CONTAINER(widget)) {
        GList *children = container_get_children(TUI_CONTAINER(widget));
        for (GList *l = children; l != NULL; l = l->next) {
            window_update_focus_chain(window, l->data);
        }
    }
}

static void
window_add_widget(Container *container, SngWidget *widget)
{
    window_update_focus_chain(TUI_WINDOW(container), widget);
    TUI_CONTAINER_CLASS(window_parent_class)->add(container, widget);
}

int
window_draw(Window *window)
{
    SngWidget *widget = SNG_WIDGET(window);
    // Draw all widgets of the window
    sng_widget_draw(widget);
    // Map all widgets to their screen positions
    sng_widget_map(widget);
    // Map all floating widgets
    window_map_floating(window);
    return 0;
}

int
window_resize(Window *window)
{
    g_return_val_if_fail(TUI_IS_WINDOW(window), FALSE);

    WindowClass *klass = TUI_WINDOW_GET_CLASS(window);
    if (klass->resize != NULL) {
        return klass->resize(window);
    }

    return 0;
}

void
window_help(Window *window)
{
    g_return_if_fail(TUI_IS_WINDOW(window));

    // Disable input timeout
    nocbreak();
    cbreak();

    WindowClass *klass = TUI_WINDOW_GET_CLASS(window);
    if (klass->help != NULL) {
        klass->help(window);
    }
}

gint
window_handle_mouse(Window *window, MEVENT mevent)
{
    WindowPrivate *priv = window_get_instance_private(window);
    priv->changed = TRUE;
    SngWidget *clicked_widget = container_find_by_position(TUI_CONTAINER(window), mevent.x, mevent.y);
    if (clicked_widget != NULL) {
        window_set_focused_widget(window, clicked_widget);
        return sng_widget_clicked(clicked_widget, mevent);
    }
    return KEY_HANDLED;
}

gint
window_handle_key(Window *window, gint key)
{
    WindowPrivate *priv = window_get_instance_private(window);
    priv->changed = TRUE;

    // Check actions for this key
    KeybindingAction action = key_find_action(key, ACTION_UNKNOWN);
    if (action == ACTION_NEXT_FIELD) {
        window_focus_next(window);
        return KEY_HANDLED;
    } else if (action == ACTION_PREV_FIELD) {
        window_focus_prev(window);
        return KEY_HANDLED;
    } else {
        return sng_widget_key_pressed(priv->focus, key);
    }
}

void
window_set_title(Window *window, const gchar *title)
{
    WINDOW *win = sng_widget_get_ncurses_window(SNG_WIDGET(window));

    // FIXME Reverse colors on monochrome terminals
    if (!has_colors()) {
        wattron(win, A_REVERSE);
    }

    // Center the title on the window
    wattron(win, A_BOLD | COLOR_PAIR(CP_DEF_ON_BLUE));
    window_clear_line(window, 0);
    mvwprintw(win, 0, (sng_widget_get_width(SNG_WIDGET(window)) - strlen(title)) / 2, "%s", title);
    wattroff(win, A_BOLD | A_REVERSE | COLOR_PAIR(CP_DEF_ON_BLUE));
}

void
window_clear_line(Window *window, gint line)
{
    // We could do this with wcleartoel but we want to
    // preserve previous window attributes. That way we
    // can set the background of the line.
    WINDOW *win = sng_widget_get_ncurses_window(SNG_WIDGET(window));
    mvwprintw(win, line, 0, "%*s", sng_widget_get_width(SNG_WIDGET(window)), "");
}

void
window_draw_bindings(Window *window, const char **keybindings, gint count)
{
    int key, xpos = 0;

    WINDOW *win = sng_widget_get_ncurses_window(SNG_WIDGET(window));

    // Reverse colors on monochrome terminals
    if (!has_colors()) {
        wattron(win, A_REVERSE);
    }

    // Write a line all the footer width
    wattron(win, COLOR_PAIR(CP_DEF_ON_CYAN));
    window_clear_line(window, sng_widget_get_height(SNG_WIDGET(window)) - 1);

    // Draw keys and their actions
    for (key = 0; key < count; key += 2) {
        wattron(win, A_BOLD | COLOR_PAIR(CP_WHITE_ON_CYAN));
        mvwprintw(win, sng_widget_get_height(SNG_WIDGET(window)) - 1, xpos, "%-*s",
                  strlen(keybindings[key]) + 1, keybindings[key]);
        xpos += strlen(keybindings[key]) + 1;
        wattroff(win, A_BOLD | COLOR_PAIR(CP_WHITE_ON_CYAN));
        wattron(win, COLOR_PAIR(CP_BLACK_ON_CYAN));
        mvwprintw(win, sng_widget_get_height(SNG_WIDGET(window)) - 1, xpos, "%-*s",
                  strlen(keybindings[key + 1]) + 1, keybindings[key + 1]);
        wattroff(win, COLOR_PAIR(CP_BLACK_ON_CYAN));
        xpos += strlen(keybindings[key + 1]) + 3;
    }

    // Disable reverse mode in all cases
    wattroff(win, A_REVERSE | A_BOLD);
}

static void
window_constructed(GObject *object)
{
    // Realize window as soon as its constructed
    sng_widget_realize(SNG_WIDGET(object));

    // Chain-up parent constructed
    G_OBJECT_CLASS(window_parent_class)->constructed(object);
}

static void
window_finalize(GObject *self)
{
    WindowPrivate *priv = window_get_instance_private(TUI_WINDOW(self));
    // Deallocate ncurses pointers
    hide_panel(priv->panel);
    del_panel(priv->panel);

    // Chain-up parent finalize function
    G_OBJECT_CLASS(window_parent_class)->finalize(self);
}

static void
window_class_init(WindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->constructed = window_constructed;
    object_class->finalize = window_finalize;

    SngWidgetClass *widget_class = SNG_WIDGET_CLASS(klass);
    widget_class->realize = window_realize;

    ContainerClass *container_class = TUI_CONTAINER_CLASS(klass);
    container_class->add = window_add_widget;
}

static void
window_init(Window *self)
{
    WindowPrivate *priv = window_get_instance_private(self);
    // Force draw on new created windows
    priv->changed = TRUE;
    // Set window as default focused SngWidget
    priv->focus = SNG_WIDGET(self);
    // Set window as visible by default
    sng_widget_show(SNG_WIDGET(self));
}

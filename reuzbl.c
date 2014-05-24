/* -*- c-basic-offset: 4; -*- */
// Original code taken from the example webkit-gtk+ application. see notice below.
// Modified code is licensed under the GPL 3.  See LICENSE file.


/*
 * Copyright (C) 2006, 2007 Apple Inc.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#define LENGTH(x) (sizeof x / sizeof x[0])
#define _POSIX_SOURCE

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <webkit/webkit.h>
#include <libsoup/soup.h>
#include <JavaScriptCore/JavaScript.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include <poll.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <assert.h>
#include "reuzbl.h"
#include "config.h"

reUzbl reuzbl;

/* commandline arguments (set initial values for the state variables) */
const
GOptionEntry entries[] =
{
    { "uri",      'u', 0, G_OPTION_ARG_STRING, &reuzbl.state.uri,
        "Uri to load at startup (equivalent to 'reuzbl <uri>' or 'set uri = URI' after reuzbl has launched)", "URI" },
    { "verbose",  'v', 0, G_OPTION_ARG_NONE,   &reuzbl.state.verbose,
        "Whether to print all messages or just errors.", NULL },
    { "name",     'n', 0, G_OPTION_ARG_STRING, &reuzbl.state.instance_name,
        "Name of the current instance (defaults to Xorg window id or pid when embed)", "NAME" },
    { "config",   'c', 0, G_OPTION_ARG_STRING, &reuzbl.state.config_file,
        "Path to config file or '-' for stdin", "FILE" },
    { "socket",   's', 0, G_OPTION_ARG_INT, &reuzbl.state.socket_id,
        "Socket ID", "SOCKET" },
    { "geometry", 'g', 0, G_OPTION_ARG_STRING, &reuzbl.gui.geometry,
        "Set window geometry (format: WIDTHxHEIGHT+-X+-Y)", "GEOMETRY" },
    { "version",  'V', 0, G_OPTION_ARG_NONE, &reuzbl.behave.print_version,
        "Print the version and exit", NULL },
    { NULL,      0, 0, 0, NULL, NULL, NULL }
};

enum ptr_type {TYPE_INT, TYPE_STR, TYPE_FLOAT};

/* associate command names to their properties */
typedef struct {
    enum ptr_type type;
    union {
        int *i;
        float *f;
        gchar **s;
    } ptr;
    int dump;
    int writeable;
    /*@null@*/ void (*func)(void);
} reuzbl_cmdprop;

/* abbreviations to help keep the table's width humane */
#define PTR_V_STR(var, d, fun) { .ptr.s = &(var), .type = TYPE_STR, .dump = d, .writeable = 1, .func = fun }
#define PTR_V_INT(var, d, fun) { .ptr.i = (int*)&(var), .type = TYPE_INT, .dump = d, .writeable = 1, .func = fun }
#define PTR_V_FLOAT(var, d, fun) { .ptr.f = &(var), .type = TYPE_FLOAT, .dump = d, .writeable = 1, .func = fun }
#define PTR_C_STR(var,    fun) { .ptr.s = &(var), .type = TYPE_STR, .dump = 0, .writeable = 0, .func = fun }
#define PTR_C_INT(var,    fun) { .ptr.i = (int*)&(var), .type = TYPE_INT, .dump = 0, .writeable = 0, .func = fun }
#define PTR_C_FLOAT(var,  fun) { .ptr.f = &(var), .type = TYPE_FLOAT, .dump = 0, .writeable = 0, .func = fun }

const struct var_name_to_ptr_t {
    const char *name;
    reuzbl_cmdprop cp;
} var_name_to_ptr[] = {
/*    variable name            pointer to variable in code                     dump callback function    */
/*  ---------------------------------------------------------------------------------------------- */
    { "uri",                    PTR_V_STR(reuzbl.state.uri,                       1,   cmd_load_uri)},
    { "verbose",                PTR_V_INT(reuzbl.state.verbose,                   1,   NULL)},
    { "inject_html",            PTR_V_STR(reuzbl.behave.inject_html,              0,   cmd_inject_html)},
    { "keycmd",                 PTR_V_STR(reuzbl.state.keycmd,                    1,   set_keycmd)},
    { "keycmd_pos",             PTR_V_INT(reuzbl.state.keycmd_pos,                1,   NULL)},
    { "keycmd_marked",          PTR_V_STR(reuzbl.state.keycmd_marked,             1,   NULL)},
    { "keycmd_screen_name",     PTR_V_STR(reuzbl.state.keycmd_screen_name,        1,   NULL)},
    { "last_event_name",        PTR_V_STR(reuzbl.behave.last_event_name,          1,   NULL)},
    { "last_event_details",     PTR_V_STR(reuzbl.behave.last_event_details,       1,   NULL)},
    { "status_message",         PTR_V_STR(reuzbl.gui.sbar.msg,                    1,   update_title)},
    { "show_status",            PTR_V_INT(reuzbl.behave.show_status,              1,   cmd_set_status)},
    { "status_top",             PTR_V_INT(reuzbl.behave.status_top,               1,   move_statusbar)},
    { "status_format",          PTR_V_STR(reuzbl.behave.status_format,            1,   update_title)},
    { "status_pbar_done",       PTR_V_STR(reuzbl.gui.sbar.progress_s,             1,   update_title)},
    { "status_pbar_pending",    PTR_V_STR(reuzbl.gui.sbar.progress_u,             1,   update_title)},
    { "status_pbar_width",      PTR_V_INT(reuzbl.gui.sbar.progress_w,             1,   update_title)},
    { "status_background",      PTR_V_STR(reuzbl.behave.status_background,        1,   update_title)},
    { "insert_indicator",       PTR_V_STR(reuzbl.behave.insert_indicator,         1,   update_indicator)},
    { "command_indicator",      PTR_V_STR(reuzbl.behave.cmd_indicator,            1,   update_indicator)},
    { "title_format_long",      PTR_V_STR(reuzbl.behave.title_format_long,        1,   update_title)},
    { "title_format_short",     PTR_V_STR(reuzbl.behave.title_format_short,       1,   update_title)},
    { "icon",                   PTR_V_STR(reuzbl.gui.icon,                        1,   set_icon)},
    { "insert_mode",            PTR_V_INT(reuzbl.behave.insert_mode,              1,   set_mode_indicator)},
    { "always_insert_mode",     PTR_V_INT(reuzbl.behave.always_insert_mode,       1,   cmd_always_insert_mode)},
    { "reset_command_mode",     PTR_V_INT(reuzbl.behave.reset_command_mode,       1,   NULL)},
    { "modkey",                 PTR_V_STR(reuzbl.behave.modkey,                   1,   cmd_modkey)},
    // { "load_finish_handler",    PTR_V_STR(reuzbl.behave.load_finish_handler,      1,   NULL)},
    // { "load_start_handler",     PTR_V_STR(reuzbl.behave.load_start_handler,       1,   NULL)},
    // { "load_commit_handler",    PTR_V_STR(reuzbl.behave.load_commit_handler,      1,   NULL)},
    // { "download_handler",       PTR_V_STR(reuzbl.behave.download_handler,         1,   NULL)},
    // { "cookie_handler",         PTR_V_STR(reuzbl.behave.cookie_handler,           1,   cmd_cookie_handler)},
    // { "new_window",             PTR_V_STR(reuzbl.behave.new_window,               1,   NULL)},
    // { "scheme_handler",         PTR_V_STR(reuzbl.behave.scheme_handler,           1,   cmd_scheme_handler)},
    { "fifo_dir",               PTR_V_STR(reuzbl.behave.fifo_dir,                 1,   cmd_fifo_dir)},
    { "socket_dir",             PTR_V_STR(reuzbl.behave.socket_dir,               1,   cmd_socket_dir)},
    { "http_debug",             PTR_V_INT(reuzbl.behave.http_debug,               1,   cmd_http_debug)},
    { "shell_cmd",              PTR_V_STR(reuzbl.behave.shell_cmd,                1,   NULL)},
    { "proxy_url",              PTR_V_STR(reuzbl.net.proxy_url,                   1,   set_proxy_url)},
    { "max_conns",              PTR_V_INT(reuzbl.net.max_conns,                   1,   cmd_max_conns)},
    { "max_conns_host",         PTR_V_INT(reuzbl.net.max_conns_host,              1,   cmd_max_conns_host)},
    { "useragent",              PTR_V_STR(reuzbl.net.useragent,                   1,   cmd_useragent)},

    /* exported WebKitWebSettings properties */
    { "zoom_level",             PTR_V_FLOAT(reuzbl.behave.zoom_level,             1,   cmd_zoom_level)},
    { "font_size",              PTR_V_INT(reuzbl.behave.font_size,                1,   cmd_font_size)},
    { "default_font_family",    PTR_V_STR(reuzbl.behave.default_font_family,      1,   cmd_default_font_family)},
    { "monospace_font_family",  PTR_V_STR(reuzbl.behave.monospace_font_family,    1,   cmd_monospace_font_family)},
    { "cursive_font_family",    PTR_V_STR(reuzbl.behave.cursive_font_family,      1,   cmd_cursive_font_family)},
    { "sans_serif_font_family", PTR_V_STR(reuzbl.behave.sans_serif_font_family,   1,   cmd_sans_serif_font_family)},
    { "serif_font_family",      PTR_V_STR(reuzbl.behave.serif_font_family,        1,   cmd_serif_font_family)},
    { "fantasy_font_family",    PTR_V_STR(reuzbl.behave.fantasy_font_family,      1,   cmd_fantasy_font_family)},
    { "monospace_size",         PTR_V_INT(reuzbl.behave.monospace_size,           1,   cmd_font_size)},
    { "minimum_font_size",      PTR_V_INT(reuzbl.behave.minimum_font_size,        1,   cmd_minimum_font_size)},
    { "disable_plugins",        PTR_V_INT(reuzbl.behave.disable_plugins,          1,   cmd_disable_plugins)},
    { "disable_scripts",        PTR_V_INT(reuzbl.behave.disable_scripts,          1,   cmd_disable_scripts)},
    { "autoload_images",        PTR_V_INT(reuzbl.behave.autoload_img,             1,   cmd_autoload_img)},
    { "autoshrink_images",      PTR_V_INT(reuzbl.behave.autoshrink_img,           1,   cmd_autoshrink_img)},
    { "enable_spellcheck",      PTR_V_INT(reuzbl.behave.enable_spellcheck,        1,   cmd_enable_spellcheck)},
    { "enable_private",         PTR_V_INT(reuzbl.behave.enable_private,           1,   cmd_enable_private)},
    { "print_backgrounds",      PTR_V_INT(reuzbl.behave.print_bg,                 1,   cmd_print_bg)},
    { "stylesheet_uri",         PTR_V_STR(reuzbl.behave.style_uri,                1,   cmd_style_uri)},
    { "resizable_text_areas",   PTR_V_INT(reuzbl.behave.resizable_txt,            1,   cmd_resizable_txt)},
    { "default_encoding",       PTR_V_STR(reuzbl.behave.default_encoding,         1,   cmd_default_encoding)},
    { "enforce_96_dpi",         PTR_V_INT(reuzbl.behave.enforce_96dpi,            1,   cmd_enforce_96dpi)},
    { "caret_browsing",         PTR_V_INT(reuzbl.behave.caret_browsing,           1,   cmd_caret_browsing)},

  /* constants (not dumpable or writeable) */
    { "WEBKIT_MAJOR",           PTR_C_INT(reuzbl.info.webkit_major,                    NULL)},
    { "WEBKIT_MINOR",           PTR_C_INT(reuzbl.info.webkit_minor,                    NULL)},
    { "WEBKIT_MICRO",           PTR_C_INT(reuzbl.info.webkit_micro,                    NULL)},
    { "ARCH_REUZBL",              PTR_C_STR(reuzbl.info.arch,                            NULL)},
    { "COMMIT",                 PTR_C_STR(reuzbl.info.commit,                          NULL)},
    { "LOAD_PROGRESS",          PTR_C_INT(reuzbl.gui.sbar.load_progress,               NULL)},
    { "LOAD_PROGRESSBAR",       PTR_C_STR(reuzbl.gui.sbar.progress_bar,                NULL)},
    { "TITLE",                  PTR_C_STR(reuzbl.gui.main_title,                       NULL)},
    { "SELECTED_URI",           PTR_C_STR(reuzbl.state.selected_url,                   NULL)},
    { "MODE",                   PTR_C_STR(reuzbl.gui.sbar.mode_indicator,              NULL)},
    { "NAME",                   PTR_C_STR(reuzbl.state.instance_name,                  NULL)},

    { NULL,                     {.ptr.s = NULL, .type = TYPE_INT, .dump = 0, .writeable = 0, .func = NULL}}
};

/* Event id to name mapping
 * Event names must be in the same 
 * order as in 'enum event_type'
 *
 * TODO: Add more useful events
*/
const char *event_table[LAST_EVENT] = {
     "LOAD_START"       , 
     "LOAD_COMMIT"      , 
     "LOAD_FINISH"      , 
     "LOAD_ERROR"       , 
     // "KEYPRESS"         , 
     "DOWNLOAD_REQUEST" , 
     "COMMAND_EXECUTED" ,
     "LINK_HOVER"       ,
     "TITLE_CHANGED"    ,
     "GEOMETRY_CHANGED" ,
     "WEBINSPECTOR"     ,
     "COOKIE"           ,
     "NEW_WINDOW"       ,
     "SELECTION_CHANGED",
     "SCHEME_REQUEST"   ,
     "BUTTONPRESS",
};


const struct {
    /*@null@*/ char *key;
    guint mask;
} modkeys[] = {
    { "SHIFT",   GDK_SHIFT_MASK   }, // shift
    { "LOCK",    GDK_LOCK_MASK    }, // capslock or shiftlock, depending on xserver's modmappings
    { "CONTROL", GDK_CONTROL_MASK }, // control
    { "MOD1",    GDK_MOD1_MASK    }, // 4th mod - normally alt but depends on modmappings
    { "MOD2",    GDK_MOD2_MASK    }, // 5th mod
    { "MOD3",    GDK_MOD3_MASK    }, // 6th mod
    { "MOD4",    GDK_MOD4_MASK    }, // 7th mod
    { "MOD5",    GDK_MOD5_MASK    }, // 8th mod
    { "BUTTON1", GDK_BUTTON1_MASK }, // 1st mouse button
    { "BUTTON2", GDK_BUTTON2_MASK }, // 2nd mouse button
    { "BUTTON3", GDK_BUTTON3_MASK }, // 3rd mouse button
    { "BUTTON4", GDK_BUTTON4_MASK }, // 4th mouse button
    { "BUTTON5", GDK_BUTTON5_MASK }, // 5th mouse button
    { "SUPER",   GDK_SUPER_MASK   }, // super (since 2.10)
    { "HYPER",   GDK_HYPER_MASK   }, // hyper (since 2.10)
    { "META",    GDK_META_MASK    }, // meta (since 2.10)
    { NULL,      0                }
};


/* construct a hash from the var_name_to_ptr array for quick access */
void
create_var_to_name_hash() {
    const struct var_name_to_ptr_t *n2v_p = var_name_to_ptr;
    reuzbl.comm.proto_var = g_hash_table_new(g_str_hash, g_str_equal);
    while(n2v_p->name) {
        g_hash_table_insert(reuzbl.comm.proto_var,
                (gpointer) n2v_p->name,
                (gpointer) &n2v_p->cp);
        n2v_p++;
    }
}


/* --- UTILITY FUNCTIONS --- */
enum exp_type {EXP_ERR, EXP_SIMPLE_VAR, EXP_BRACED_VAR, EXP_EXPR, EXP_JS, EXP_ESCAPE};
enum exp_type
get_exp_type(const gchar *s) {
    /* variables */
    if(*(s+1) == '(')
        return EXP_EXPR;
    else if(*(s+1) == '{')
        return EXP_BRACED_VAR;
    else if(*(s+1) == '<')
        return EXP_JS;
    else if(*(s+1) == '[')
        return EXP_ESCAPE;
    else
        return EXP_SIMPLE_VAR;

    /*@notreached@*/
    return EXP_ERR;
}

/*
 * recurse == 1: don't expand '@(command)@'
 * recurse == 2: don't expand '@<java script>@'
*/
gchar *
expand(const char *s, guint recurse) {
    reuzbl_cmdprop *c;
    enum exp_type etype;
    char *end_simple_var = "^°!\"§$%&/()=?'`'+~*'#-.:,;@<>| \\{}[]¹²³¼½";
    char *ret = NULL;
    char *vend = NULL;
    GError *err = NULL;
    gchar *cmd_stdout = NULL;
    gchar *mycmd = NULL;
    GString *buf = g_string_new("");
    GString *js_ret = g_string_new("");

    while(*s) {
        switch(*s) {
            case '\\':
                g_string_append_c(buf, *++s);
                s++;
                break;

            case '@':
                etype = get_exp_type(s);
                s++;

                switch(etype) {
                    case EXP_SIMPLE_VAR:
                        vend = strpbrk(s, end_simple_var);
                        if(!vend) vend = strchr(s, '\0');
                        break;
                    case EXP_BRACED_VAR:
                        s++;
                        vend = strchr(s, '}');
                        if(!vend) vend = strchr(s, '\0');
                        break;
                    case EXP_EXPR:
                        s++;
                        vend = strstr(s, ")@");
                        if(!vend) vend = strchr(s, '\0');
                        break;
                    case EXP_JS:
                        s++;
                        vend = strstr(s, ">@");
                        if(!vend) vend = strchr(s, '\0');
                        break;
                    case EXP_ESCAPE:
                        s++;
                        vend = strstr(s, "]@");
                        if(!vend) vend = strchr(s, '\0');
                        break;
                    /*@notreached@*/
                    case EXP_ERR:
                        break;
                }
                assert(vend);

                ret = g_strndup(s, vend-s);

                if(etype == EXP_SIMPLE_VAR ||
                   etype == EXP_BRACED_VAR) {
                    if( (c = g_hash_table_lookup(reuzbl.comm.proto_var, ret)) ) {
                        if(c->type == TYPE_STR && *c->ptr.s != NULL) {
                            g_string_append(buf, (gchar *)*c->ptr.s);
                        }
                        else if(c->type == TYPE_INT) {
                            g_string_append_printf(buf, "%d", *c->ptr.i);
                        }
                        else if(c->type == TYPE_FLOAT) {
                            g_string_append_printf(buf, "%f", *c->ptr.f);
                        }
                    }

                    if(etype == EXP_SIMPLE_VAR)
                        s = vend;
                    else
                        s = vend+1;
                }
                else if(recurse != 1 &&
                        etype == EXP_EXPR) {
                    mycmd = expand(ret, 1);
                    g_spawn_command_line_sync(mycmd, &cmd_stdout, NULL, NULL, &err);
                    g_free(mycmd);

                    if (err) {
                        g_printerr("error on running command: %s\n", err->message);
                        g_error_free (err);
                    }
                    else if (*cmd_stdout) {
                        size_t len = strlen(cmd_stdout);

                        if(len > 0 && cmd_stdout[len-1] == '\n')
                            cmd_stdout[--len] = '\0'; /* strip trailing newline */

                        g_string_append(buf, cmd_stdout);
                        g_free(cmd_stdout);
                    }
                    s = vend+2;
                }
                else if(recurse != 2 &&
                        etype == EXP_JS) {
                    mycmd = expand(ret, 2);
                    eval_js(reuzbl.gui.web_view, mycmd, js_ret);
                    g_free(mycmd);

                    if(js_ret->str) {
                        g_string_append(buf, js_ret->str);
                        g_string_free(js_ret, TRUE);
                        js_ret = g_string_new("");
                    }
                    s = vend+2;
                }
                else if(etype == EXP_ESCAPE) {
                    mycmd = expand(ret, 0);
                    char *escaped = g_markup_escape_text(mycmd, strlen(mycmd));

                    g_string_append(buf, escaped);

                    g_free(escaped);
                    g_free(mycmd);
                    s = vend+2;
                }

                g_free(ret);
                ret = NULL;
                break;

            default:
                g_string_append_c(buf, *s);
                s++;
                break;
        }
    }
    g_string_free(js_ret, TRUE);
    return g_string_free(buf, FALSE);
}

/* send events as strings over any of our interfaces 
 *
 * TODO: - also use an output fifo and the socket
 *       - probably we also want a variable/CL option
 *         that specifies wether to send events and through 
 *         which interface to send them
 *       - let the user select which event types
 *         to report
*/
void
send_event(int type, const gchar *details) {
    if(type < LAST_EVENT) {
        if (reuzbl.state.verbose) {
            printf("%s [%s] %s\n", event_table[type], reuzbl.state.instance_name, details);
            fflush(stdout);
        }

        g_free(reuzbl.behave.last_event_name);
        reuzbl.behave.last_event_name = g_strdup(event_table[type]);
        g_free(reuzbl.behave.last_event_details);
        reuzbl.behave.last_event_details = g_strdup(details);

        Action *act;
        if ((act = g_hash_table_lookup(reuzbl.handlers, event_table[type]))) {
            gchar* param = expand(act->param, 0);
            parse_command(act->name, param, NULL);
            g_free(param);
        }
    }
}

char *
itos(int val) {
    char tmp[20];

    snprintf(tmp, sizeof(tmp), "%i", val);
    return g_strdup(tmp);
}

gchar*
strfree(gchar *str) { g_free(str); return NULL; }  // for freeing & setting to null in one go

gchar*
argv_idx(const GArray *a, const guint idx) { return g_array_index(a, gchar*, idx); }

char *
str_replace (const char* search, const char* replace, const char* string) {
    gchar **buf;
    char *ret;

    buf = g_strsplit (string, search, -1);
    ret = g_strjoinv (replace, buf);
    g_strfreev(buf); // somebody said this segfaults

    return ret;
}

GArray*
read_file_by_line (const gchar *path) {
    GIOChannel *chan = NULL;
    gchar *readbuf = NULL;
    gsize len;
    GArray *lines = g_array_new(TRUE, FALSE, sizeof(gchar*));
    int i = 0;

    chan = g_io_channel_new_file(path, "r", NULL);

    if (chan) {
        while (g_io_channel_read_line(chan, &readbuf, &len, NULL, NULL) == G_IO_STATUS_NORMAL) {
            const gchar* val = g_strdup (readbuf);
            g_array_append_val (lines, val);
            g_free (readbuf);
            i ++;
        }

        g_io_channel_unref (chan);
    } else {
        fprintf(stderr, "File '%s' not be read.\n", path);
    }

    return lines;
}

gchar*
parseenv (char* string) {
    extern char** environ;
    gchar* tmpstr = NULL;
    unsigned int i = 0;

    // Expand variables from ENV
    while (environ[i] != NULL) {
        gchar** env = g_strsplit (environ[i], "=", 2);
        gchar* envname = g_strconcat ("$", env[0], NULL);

        if (g_strrstr (string, envname) != NULL) {
            tmpstr = g_strdup(string);
            g_free (string);
            string = str_replace(envname, env[1], tmpstr);
            g_free (tmpstr);
        }

        g_free (envname);
        g_strfreev (env); // somebody said this breaks reuzbl
        i++;
    }

    // XDG variables should be expanded to their default values if not in ENV
    for(i=0; i<LENGTH(XDG); i++) {
        gchar* envname = g_strconcat ("$", XDG[i].environmental, NULL);

        if (g_strrstr (string, envname) != NULL) {
            gchar* xdgv = get_xdg_var (XDG[i]);
            tmpstr = g_strdup(string);
            g_free (string);
            string = str_replace(envname, xdgv, tmpstr);
            g_free (tmpstr);
            g_free (xdgv);
        }
    }

    return string;
}

sigfunc*
setup_signal(int signr, sigfunc *shandler) {
    struct sigaction nh, oh;

    nh.sa_handler = shandler;
    sigemptyset(&nh.sa_mask);
    nh.sa_flags = 0;

    if(sigaction(signr, &nh, &oh) < 0)
        return SIG_ERR;

    return NULL;
}

void
clean_up(void) {
    if (reuzbl.behave.fifo_dir)
        unlink (reuzbl.comm.fifo_path);
    if (reuzbl.behave.socket_dir)
        unlink (reuzbl.comm.socket_path);

    g_free(reuzbl.state.executable_path);
    g_free(reuzbl.state.keycmd);
    reuzbl.state.keycmd_pos = 0;
    g_hash_table_destroy(reuzbl.bindings);
    g_hash_table_destroy(reuzbl.handlers);
    g_hash_table_destroy(reuzbl.buttons);
    g_hash_table_destroy(reuzbl.behave.commands);
}

/* --- SIGNAL HANDLER --- */

void
catch_sigterm(int s) {
    (void) s;
    clean_up();
}

void
catch_sigint(int s) {
    (void) s;
    clean_up();
    exit(EXIT_SUCCESS);
}

/* --- CALLBACKS --- */

gboolean
navigation_decision_cb (WebKitWebView *web_view, WebKitWebFrame *frame, WebKitNetworkRequest *request, WebKitWebNavigationAction *navigation_action, WebKitWebPolicyDecision *policy_decision, gpointer user_data) {
    (void) web_view;
    (void) frame;
    (void) navigation_action;
    (void) user_data;

    const gchar* uri = webkit_network_request_get_uri (request);
    gboolean decision_made = FALSE;

    if (reuzbl.state.verbose)
        printf("Navigation requested -> %s\n", uri);

    // if (reuzbl.behave.scheme_handler) {
    if (g_hash_table_lookup(reuzbl.handlers, "SCHEME_REQUEST")) {
        GString *s = g_string_new ("");
        g_string_printf(s, "'%s'", uri);

        //run_handler(reuzbl.behave.scheme_handler, s->str);
        send_event(SCHEME_REQUEST, s->str);

        if(reuzbl.comm.sync_stdout && strcmp (reuzbl.comm.sync_stdout, "") != 0) {
            char *p = strchr(reuzbl.comm.sync_stdout, '\n' );
            if ( p != NULL ) *p = '\0';
            if (!strcmp(reuzbl.comm.sync_stdout, "USED")) {
                webkit_web_policy_decision_ignore(policy_decision);
                decision_made = TRUE;
            }
        }
        if (reuzbl.comm.sync_stdout)
            reuzbl.comm.sync_stdout = strfree(reuzbl.comm.sync_stdout);

        g_string_free(s, TRUE);
    }
    if (!decision_made)
        webkit_web_policy_decision_use(policy_decision);

    return TRUE;
}

gboolean
new_window_cb (WebKitWebView *web_view, WebKitWebFrame *frame, WebKitNetworkRequest *request, WebKitWebNavigationAction *navigation_action, WebKitWebPolicyDecision *policy_decision, gpointer user_data) {
    (void) web_view;
    (void) frame;
    (void) navigation_action;
    (void) policy_decision;
    (void) user_data;
    const gchar* uri = webkit_network_request_get_uri (request);
    if (reuzbl.state.verbose)
        printf("New window requested -> %s \n", uri);
    webkit_web_policy_decision_use(policy_decision);
    send_event(NEW_WINDOW, uri);
    return TRUE;
}

gboolean
mime_policy_cb(WebKitWebView *web_view, WebKitWebFrame *frame, WebKitNetworkRequest *request, gchar *mime_type,  WebKitWebPolicyDecision *policy_decision, gpointer user_data) {
    (void) frame;
    (void) request;
    (void) user_data;

    /* If we can display it, let's display it... */
    if (webkit_web_view_can_show_mime_type (web_view, mime_type)) {
        webkit_web_policy_decision_use (policy_decision);
        return TRUE;
    }

    /* ...everything we can't displayed is downloaded */
    webkit_web_policy_decision_download (policy_decision);
    return TRUE;
}

/*@null@*/ WebKitWebView*
create_web_view_cb (WebKitWebView  *web_view, WebKitWebFrame *frame, gpointer user_data) {
    (void) web_view;
    (void) frame;
    (void) user_data;
    if (reuzbl.state.selected_url != NULL) {
        if (reuzbl.state.verbose)
            printf("\nNew web view -> %s\n",reuzbl.state.selected_url);
        new_window_load_uri(reuzbl.state.selected_url);
    } else {
        if (reuzbl.state.verbose)
            printf("New web view -> %s\n","Nothing to open, exiting");
    }
    return (NULL);
}

gboolean
download_cb (WebKitWebView *web_view, GObject *download, gpointer user_data) {
    (void) web_view;
    (void) user_data;
    // if (reuzbl.behave.download_handler) {
    //     const gchar* uri = webkit_download_get_uri ((WebKitDownload*)download);
    //     if (reuzbl.state.verbose)
    //         printf("Download -> %s\n",uri);
    //     /* if urls not escaped, we may have to escape and quote uri before this call */

    //     GString *args = g_string_new(uri);

    //     if (reuzbl.net.proxy_url) {
    //        g_string_append_c(args, ' ');
    //        g_string_append(args, reuzbl.net.proxy_url);
    //     }

    //     run_handler(reuzbl.behave.download_handler, args->str);

    //     g_string_free(args, TRUE);
    // }
    send_event(DOWNLOAD_REQ, webkit_download_get_uri ((WebKitDownload*)download));
    return (FALSE);
}

/* scroll a bar in a given direction */
void
scroll (GtkAdjustment* bar, GArray *argv) {
    gchar *end;
    gdouble max_value;

    gdouble page_size = gtk_adjustment_get_page_size(bar);
    gdouble value = gtk_adjustment_get_value(bar);
    gdouble amount = g_ascii_strtod(g_array_index(argv, gchar*, 0), &end);

    if (*end == '%')
        value += page_size * amount * 0.01;
    else
        value += amount;

    max_value = gtk_adjustment_get_upper(bar) - page_size;

    if (value > max_value)
        value = max_value; /* don't scroll past the end of the page */

    gtk_adjustment_set_value (bar, value);
}

void
scroll_begin(WebKitWebView* page, GArray *argv, GString *result) {
    (void) page; (void) argv; (void) result;
    gtk_adjustment_set_value (reuzbl.gui.bar_v, gtk_adjustment_get_lower(reuzbl.gui.bar_v));
}

void
scroll_end(WebKitWebView* page, GArray *argv, GString *result) {
    (void) page; (void) argv; (void) result;
    gtk_adjustment_set_value (reuzbl.gui.bar_v, gtk_adjustment_get_upper(reuzbl.gui.bar_v) -
                              gtk_adjustment_get_page_size(reuzbl.gui.bar_v));
}

void
scroll_vert(WebKitWebView* page, GArray *argv, GString *result) {
    (void) page; (void) result;
    scroll(reuzbl.gui.bar_v, argv);
}

void
scroll_horz(WebKitWebView* page, GArray *argv, GString *result) {
    (void) page; (void) result;
    scroll(reuzbl.gui.bar_h, argv);
}

void
cmd_set_geometry() {
    if(!gtk_window_parse_geometry(GTK_WINDOW(reuzbl.gui.main_window), reuzbl.gui.geometry)) {
        if(reuzbl.state.verbose)
            printf("Error in geometry string: %s\n", reuzbl.gui.geometry);
    }
    /* update geometry var with the actual geometry
       this is necessary as some WMs don't seem to honour
       the above setting and we don't want to end up with
       wrong geometry information
    */
    retrieve_geometry();
}

void
cmd_set_status() {
    if (!reuzbl.behave.show_status) {
        gtk_widget_hide(reuzbl.gui.mainbar);
    } else {
        gtk_widget_show(reuzbl.gui.mainbar);
    }
    update_title();
}

void
toggle_zoom_type (WebKitWebView* page, GArray *argv, GString *result) {
    (void)page;
    (void)argv;
    (void)result;

    webkit_web_view_set_full_content_zoom (page, !webkit_web_view_get_full_content_zoom (page));
}

void
toggle_status_cb (WebKitWebView* page, GArray *argv, GString *result) {
    (void)page;
    (void)argv;
    (void)result;

    if (reuzbl.behave.show_status) {
        gtk_widget_hide(reuzbl.gui.mainbar);
    } else {
        gtk_widget_show(reuzbl.gui.mainbar);
    }
    reuzbl.behave.show_status = !reuzbl.behave.show_status;
    update_title();
}

void
link_hover_cb (WebKitWebView* page, const gchar* title, const gchar* link, gpointer data) {
    (void) page;
    (void) title;
    (void) data;
    //Set selected_url state variable
    g_free(reuzbl.state.selected_url);
    reuzbl.state.selected_url = NULL;
    if (link) {
        reuzbl.state.selected_url = g_strdup(link);
        send_event(LINK_HOVER, reuzbl.state.selected_url);
    }
    update_title();
}

void
title_change_cb (WebKitWebView* web_view, GParamSpec param_spec) {
    (void) web_view;
    (void) param_spec;
    const gchar *title = webkit_web_view_get_title(web_view);
    if (reuzbl.gui.main_title)
        g_free (reuzbl.gui.main_title);
    reuzbl.gui.main_title = title ? g_strdup (title) : g_strdup ("(no title)");
    update_title();
    send_event(TITLE_CHANGED, reuzbl.gui.main_title);
}

void
progress_change_cb (WebKitWebView* page, gint progress, gpointer data) {
    (void) page;
    (void) data;
    reuzbl.gui.sbar.load_progress = progress;

    g_free(reuzbl.gui.sbar.progress_bar);
    reuzbl.gui.sbar.progress_bar = build_progressbar_ascii(reuzbl.gui.sbar.load_progress);

    update_title();
}

void
selection_changed_cb(WebKitWebView *webkitwebview, gpointer ud) {
    (void)ud;
    gchar *tmp;

    webkit_web_view_copy_clipboard(webkitwebview);
    tmp = gtk_clipboard_wait_for_text(gtk_clipboard_get (GDK_SELECTION_CLIPBOARD));
    send_event(SELECTION_CHANGED, tmp);
    g_free(tmp);
}

void
load_finish_cb (WebKitWebView* page, WebKitWebFrame* frame, gpointer data) {
    (void) page;
    (void) data;

    // if (reuzbl.behave.load_finish_handler)
    //     run_handler(reuzbl.behave.load_finish_handler, "");

    send_event(LOAD_FINISH, webkit_web_frame_get_uri(frame));
}

void clear_keycmd() {
  g_free(reuzbl.state.keycmd);
  reuzbl.state.keycmd = g_strdup("");
  reuzbl.state.keycmd_pos = 0;
}

void
load_start_cb (WebKitWebView* page, WebKitWebFrame* frame, gpointer data) {
    (void) page;
    (void) frame;
    (void) data;
    reuzbl.gui.sbar.load_progress = 0;
    // if (reuzbl.behave.load_start_handler)
    //     run_handler(reuzbl.behave.load_start_handler, "");

    send_event(LOAD_START, reuzbl.state.uri);
}

void
load_error_cb (WebKitWebView* page, WebKitWebFrame* frame, gchar *uri, gpointer web_err, gpointer ud) {
    (void) page;
    (void) frame;
    (void) ud;
    GError *err = web_err;
    gchar *details;

    details = g_strdup_printf("%s %d:%s", uri, err->code, err->message);
    send_event(LOAD_ERROR, details);
    g_free(details);
}

void
load_commit_cb (WebKitWebView* page, WebKitWebFrame* frame, gpointer data) {
    (void) page;
    (void) data;
    g_free (reuzbl.state.uri);
    GString* newuri = g_string_new (webkit_web_frame_get_uri (frame));
    reuzbl.state.uri = g_string_free (newuri, FALSE);
    if (reuzbl.behave.reset_command_mode && reuzbl.behave.insert_mode) {
        set_insert_mode(reuzbl.behave.always_insert_mode);
        update_title();
    }
    // if (reuzbl.behave.load_commit_handler)
    //     run_handler(reuzbl.behave.load_commit_handler, reuzbl.state.uri);

    /* event message */
    send_event(LOAD_COMMIT, webkit_web_frame_get_uri (frame));
}

void
destroy_cb (GtkWidget* widget, gpointer data) {
    (void) widget;
    (void) data;
    gtk_main_quit ();
}


/* VIEW funcs (little webkit wrappers) */
#define VIEWFUNC(name) void view_##name(WebKitWebView *page, GArray *argv, GString *result){(void)argv; (void)result; webkit_web_view_##name(page);}
VIEWFUNC(reload)
VIEWFUNC(reload_bypass_cache)
VIEWFUNC(stop_loading)
VIEWFUNC(zoom_in)
VIEWFUNC(zoom_out)
VIEWFUNC(go_back)
VIEWFUNC(go_forward)
#undef VIEWFUNC

/* -- command to callback/function map for things we cannot attach to any signals */
struct {const char *key; CommandInfo value;} cmdlist[] =
{   /* key                   function      no_split      */
    { "back",               { view_go_back,             0 }    },
    { "forward",            { view_go_forward,          0 }    },
    { "scroll_vert",        { scroll_vert,              0 }    },
    { "scroll_horz",        { scroll_horz,              0 }    },
    { "scroll_begin",       { scroll_begin,             0 }    },
    { "scroll_end",         { scroll_end,               0 }    },
    { "reload",             { view_reload,              0 }    },
    { "reload_ign_cache",   { view_reload_bypass_cache, 0 }    },
    { "stop",               { view_stop_loading,        0 }    },
    { "zoom_in",            { view_zoom_in,             0 }    }, // can crash (when max zoom reached?).
    { "zoom_out",           { view_zoom_out,            0 }    },
    { "toggle_zoom_type",   { toggle_zoom_type,         0 }    },
    { "uri",                { load_uri,                 TRUE } },
    { "js",                 { run_js,                   TRUE } },
    { "script",             { run_external_js,          0 }    },
    { "toggle_status",      { toggle_status_cb,         0 }    },
    { "spawn",              { spawn,                    0 }    },
    { "sync_spawn",         { spawn_sync,               0 }    }, // needed for cookie handler
    { "sh",                 { spawn_sh,                 0 }    },
    { "sync_sh",            { spawn_sh_sync,            0 }    }, // needed for cookie handler
    { "talk_to_socket",     { talk_to_socket,           0 }    },
    { "exit",               { close_reuzbl,               0 }    },
    { "search",             { search_forward_text,      TRUE } },
    { "search_reverse",     { search_reverse_text,      TRUE } },
    { "dehilight",          { dehilight,                0 }    },
    { "toggle_insert_mode", { toggle_insert_mode,       0 }    },
    { "set",                { set_var,                  TRUE } },
  //{ "get",                { get_var,                  TRUE } },
    { "bind",               { act_bind,                 TRUE } },
    { "handle",             { act_handle,              TRUE } },
    { "button",             { act_button,              TRUE } },
    { "dump_config",        { act_dump_config,          0 }    },
    { "keycmd",             { keycmd,                   TRUE } },
    { "keycmd_nl",          { keycmd_nl,                TRUE } },
    { "keycmd_bs",          { keycmd_bs,                0 }    },
    { "chain",              { chain,                    0 }    },
    { "print",              { print,                    TRUE } },
    { "update_gui",         { update_gui,               TRUE } }
}; 

void
commands_hash(void)
{
    unsigned int i;
    reuzbl.behave.commands = g_hash_table_new(g_str_hash, g_str_equal);

    for (i = 0; i < LENGTH(cmdlist); i++)
        g_hash_table_insert(reuzbl.behave.commands, (gpointer) cmdlist[i].key, &cmdlist[i].value);
}

/* -- CORE FUNCTIONS -- */

void
free_action(gpointer act) {
    Action *action = (Action*)act;
    g_free(action->name);
    if (action->param)
        g_free(action->param);
    g_free(action);
}

Action*
new_action(const gchar *name, const gchar *param, const gchar *screen_name) {
    Action *action = g_new(Action, 1);

    action->name = g_strdup(name);
    if (param)
        action->param = g_strdup(param);
    else
        action->param = NULL;

    if (screen_name)
        action->screen_name = g_strdup(screen_name);
    else
        action->screen_name = NULL;

    return action;
}

bool
file_exists (const char * filename) {
    return (access(filename, F_OK) == 0);
}

void
set_var(WebKitWebView *page, GArray *argv, GString *result) {
    (void) page; (void) result;
    gchar **split = g_strsplit(argv_idx(argv, 0), "=", 2);
    if (split[0] != NULL) {
        gchar *value = parseenv(g_strdup(split[1] ? g_strchug(split[1]) : " "));
        set_var_value(g_strstrip(split[0]), value);
        g_free(value);
    }
    g_strfreev(split);
}

void
update_gui(WebKitWebView *page, GArray *argv, GString *result) {
    (void) page; (void) argv; (void) result;

    update_title();
}

void
print(WebKitWebView *page, GArray *argv, GString *result) {
    (void) page; (void) result;
    gchar* buf;

    buf = expand(argv_idx(argv, 0), 0);
    g_string_assign(result, buf);
    g_free(buf);
}

void
act_bind(WebKitWebView *page, GArray *argv, GString *result) {
    (void) page; (void) result;
    gchar **split = g_strsplit(argv_idx(argv, 0), " = ", 2);
    gchar *value = parseenv(g_strdup(split[1] ? g_strchug(split[1]) : " "));
    add_binding(g_strstrip(split[0]), value);
    g_free(value);
    g_strfreev(split);
}

void
act_handle(WebKitWebView *page, GArray *argv, GString *result) {
    (void) page; (void) result;
    gchar **split = g_strsplit(argv_idx(argv, 0), " = ", 2);
    gchar *value = parseenv(g_strdup(split[1] ? g_strchug(split[1]) : " "));
    add_handler(g_strstrip(split[0]), value);
    g_free(value);
    g_strfreev(split);
}

void
act_button(WebKitWebView *page, GArray *argv, GString *result) {
    (void) page; (void) result;
    gchar **split = g_strsplit(argv_idx(argv, 0), " = ", 2);
    gchar *value = parseenv(g_strdup(split[1] ? g_strchug(split[1]) : " "));
    add_button(g_strstrip(split[0]), value);
    g_free(value);
    g_strfreev(split);
}


void
act_dump_config() {
    dump_config();
}

void
set_keycmd() {
    reuzbl.state.keycmd_pos = strlen(reuzbl.state.keycmd);
    run_keycmd(FALSE);
    update_title();
}

void
set_mode_indicator() {
    reuzbl.gui.sbar.mode_indicator = (reuzbl.behave.insert_mode ?
        reuzbl.behave.insert_indicator : reuzbl.behave.cmd_indicator);
}

void
update_indicator() {
  set_mode_indicator();
  update_title();
}

void
set_insert_mode(gboolean mode) {
    reuzbl.behave.insert_mode = mode;
    set_mode_indicator();
}

void
toggle_insert_mode(WebKitWebView *page, GArray *argv, GString *result) {
    (void) page; (void) result;

    if (argv_idx(argv, 0)) {
        if (strcmp (argv_idx(argv, 0), "0") == 0) {
            set_insert_mode(FALSE);
        } else {
            set_insert_mode(TRUE);
        }
    } else {
        set_insert_mode( !reuzbl.behave.insert_mode );
    }

    update_title();
}

void
load_uri (WebKitWebView *web_view, GArray *argv, GString *result) {
    (void) result;

    if (argv_idx(argv, 0)) {
        GString* newuri = g_string_new (argv_idx(argv, 0));
        if (g_strstr_len (argv_idx(argv, 0), 11, "javascript:") != NULL) {
            run_js(web_view, argv, NULL);
            return;
        }
        if (!soup_uri_new(argv_idx(argv, 0)))
            g_string_prepend (newuri, "http://");
        /* if we do handle cookies, ask our handler for them */
        webkit_web_view_load_uri (web_view, newuri->str);
        g_string_free (newuri, TRUE);
    }
}

/* Javascript*/

JSValueRef
js_run_command (JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject,
                size_t argumentCount, const JSValueRef arguments[],
                JSValueRef* exception) {
    (void) function;
    (void) thisObject;
    (void) exception;

    JSStringRef js_result_string;
    GString *result = g_string_new("");

    if (argumentCount >= 1) {
        JSStringRef arg = JSValueToStringCopy(ctx, arguments[0], NULL);
        size_t arg_size = JSStringGetMaximumUTF8CStringSize(arg);
        char ctl_line[arg_size];
        JSStringGetUTF8CString(arg, ctl_line, arg_size);

        parse_cmd_line(ctl_line, result);

        JSStringRelease(arg);
    }
    js_result_string = JSStringCreateWithUTF8CString(result->str);

    g_string_free(result, TRUE);

    return JSValueMakeString(ctx, js_result_string);
}

JSStaticFunction js_static_functions[] = {
    {"run", js_run_command, kJSPropertyAttributeNone},
};

void
js_init() {
    /* This function creates the class and its definition, only once */
    if (!reuzbl.js.initialized) {
        /* it would be pretty cool to make this dynamic */
        reuzbl.js.classdef = kJSClassDefinitionEmpty;
        reuzbl.js.classdef.staticFunctions = js_static_functions;

        reuzbl.js.classref = JSClassCreate(&reuzbl.js.classdef);
    }
}


void
eval_js(WebKitWebView * web_view, gchar *script, GString *result) {
    WebKitWebFrame *frame;
    JSGlobalContextRef context;
    JSObjectRef globalobject;
    JSStringRef var_name;

    JSStringRef js_script;
    JSValueRef js_result;
    JSStringRef js_result_string;
    size_t js_result_size;

    js_init();

    frame = webkit_web_view_get_main_frame(WEBKIT_WEB_VIEW(web_view));
    context = webkit_web_frame_get_global_context(frame);
    globalobject = JSContextGetGlobalObject(context);

    /* reuzbl javascript namespace */
    var_name = JSStringCreateWithUTF8CString("reUzbl");
    JSObjectSetProperty(context, globalobject, var_name,
                        JSObjectMake(context, reuzbl.js.classref, NULL),
                        kJSClassAttributeNone, NULL);

    /* evaluate the script and get return value*/
    js_script = JSStringCreateWithUTF8CString(script);
    js_result = JSEvaluateScript(context, js_script, globalobject, NULL, 0, NULL);
    if (js_result && !JSValueIsUndefined(context, js_result)) {
        js_result_string = JSValueToStringCopy(context, js_result, NULL);
        js_result_size = JSStringGetMaximumUTF8CStringSize(js_result_string);

        if (js_result_size) {
            char js_result_utf8[js_result_size];
            JSStringGetUTF8CString(js_result_string, js_result_utf8, js_result_size);
            g_string_assign(result, js_result_utf8);
        }

        JSStringRelease(js_result_string);
    }

    /* cleanup */
    JSObjectDeleteProperty(context, globalobject, var_name, NULL);

    JSStringRelease(var_name);
    JSStringRelease(js_script);
}

void
run_js (WebKitWebView * web_view, GArray *argv, GString *result) {
    if (argv_idx(argv, 0))
        eval_js(web_view, argv_idx(argv, 0), result);
}

void
run_external_js (WebKitWebView * web_view, GArray *argv, GString *result) {
    (void) result;
    if (argv_idx(argv, 0)) {
        GArray* lines = read_file_by_line (argv_idx (argv, 0));
        gchar*  js = NULL;
        int i = 0;
        gchar* line;

        while ((line = g_array_index(lines, gchar*, i))) {
            if (js == NULL) {
                js = g_strdup (line);
            } else {
                gchar* newjs = g_strconcat (js, line, NULL);
                js = newjs;
            }
            i ++;
            g_free (line);
        }

        if (reuzbl.state.verbose)
            printf ("External JavaScript file %s loaded\n", argv_idx(argv, 0));

        if (argv_idx (argv, 1)) {
            gchar* newjs = str_replace("%s", argv_idx (argv, 1), js);
            g_free (js);
            js = newjs;
        }
        eval_js (web_view, js, result);
        g_free (js);
        g_array_free (lines, TRUE);
    }
}

void
search_text (WebKitWebView *page, GArray *argv, const gboolean forward) {
    if (argv_idx(argv, 0) && (*argv_idx(argv, 0) != '\0')) {
        if (g_strcmp0 (reuzbl.state.searchtx, argv_idx(argv, 0)) != 0) {
            webkit_web_view_unmark_text_matches (page);
            webkit_web_view_mark_text_matches (page, argv_idx(argv, 0), FALSE, 0);
            reuzbl.state.searchtx = g_strdup(argv_idx(argv, 0));
        }
    }

    if (reuzbl.state.searchtx) {
        if (reuzbl.state.verbose)
            printf ("Searching: %s\n", reuzbl.state.searchtx);
        webkit_web_view_set_highlight_text_matches (page, TRUE);
        webkit_web_view_search_text (page, reuzbl.state.searchtx, FALSE, forward, TRUE);
    }
}

void
search_forward_text (WebKitWebView *page, GArray *argv, GString *result) {
    (void) result;
    search_text(page, argv, TRUE);
}

void
search_reverse_text (WebKitWebView *page, GArray *argv, GString *result) {
    (void) result;
    search_text(page, argv, FALSE);
}

void
dehilight (WebKitWebView *page, GArray *argv, GString *result) {
    (void) argv; (void) result;
    webkit_web_view_set_highlight_text_matches (page, FALSE);
}


void
new_window_load_uri (const gchar * uri) {
    // if (reuzbl.behave.new_window) {
    if (g_hash_table_lookup(reuzbl.handlers, "NEW_WINDOW")) {
        GString *s = g_string_new ("");
        g_string_printf(s, "'%s'", uri);
        // run_handler(reuzbl.behave.new_window, s->str);
        send_event(NEW_WINDOW, s->str);
        return;
    }
    GString* to_execute = g_string_new ("");
    g_string_append_printf (to_execute, "%s --uri '%s'", reuzbl.state.executable_path, uri);
    int i;
    for (i = 0; entries[i].long_name != NULL; i++) {
        if ((entries[i].arg == G_OPTION_ARG_STRING) && (strcmp(entries[i].long_name,"uri")!=0) && (strcmp(entries[i].long_name,"name")!=0)) {
            gchar** str = (gchar**)entries[i].arg_data;
            if (*str!=NULL) {
                g_string_append_printf (to_execute, " --%s '%s'", entries[i].long_name, *str);
            }
        }
    }
    if (reuzbl.state.verbose)
        printf("\n%s\n", to_execute->str);
    g_spawn_command_line_async (to_execute->str, NULL);
    /* TODO: should we just report the uri as event detail? */
    send_event(NEW_WINDOW, to_execute->str);
    g_string_free (to_execute, TRUE);
}

void
chain (WebKitWebView *page, GArray *argv, GString *result) {
    (void) page; (void) result;
    gchar *a = NULL;
    gchar **parts = NULL;
    guint i = 0;
    while ((a = argv_idx(argv, i++))) {
        parts = g_strsplit (a, " ", 2);
        if (parts[0])
          parse_command(parts[0], parts[1], result);
        g_strfreev (parts);
    }
}

void
keycmd (WebKitWebView *page, GArray *argv, GString *result) {
    (void)page;
    (void)argv;
    (void)result;
    reuzbl.state.keycmd = g_strdup(argv_idx(argv, 0));
    reuzbl.state.keycmd_pos = strlen(argv_idx(argv, 0));
    run_keycmd(FALSE);
    update_title();
}

void
keycmd_nl (WebKitWebView *page, GArray *argv, GString *result) {
    (void)page;
    (void)argv;
    (void)result;
    reuzbl.state.keycmd = g_strdup(argv_idx(argv, 0));
    reuzbl.state.keycmd_pos = strlen(argv_idx(argv, 0));
    run_keycmd(TRUE);
    update_title();
}

void
keycmd_bs (WebKitWebView *page, GArray *argv, GString *result) {
    gchar *prev;
    (void)page;
    (void)argv;
    (void)result;
    int len = strlen(reuzbl.state.keycmd);
    prev = g_utf8_find_prev_char(reuzbl.state.keycmd, reuzbl.state.keycmd + /*len*/ reuzbl.state.keycmd_pos);
    if (prev) {
        reuzbl.state.keycmd[prev - reuzbl.state.keycmd] = '\0';
        if (prev - reuzbl.state.keycmd < len - 1) {
            GString* keycmd = g_string_new(reuzbl.state.keycmd);
            g_string_append(keycmd, &reuzbl.state.keycmd[prev - reuzbl.state.keycmd + 1]);
            reuzbl.state.keycmd = g_string_free(keycmd, FALSE);
        }
        reuzbl.state.keycmd_pos--;
    }
    update_title();
}

void
close_reuzbl (WebKitWebView *page, GArray *argv, GString *result) {
    (void)page;
    (void)argv;
    (void)result;
    gtk_main_quit ();
}

/* --Statusbar functions-- */
char*
build_progressbar_ascii(int percent) {
   int width=reuzbl.gui.sbar.progress_w;
   int i;
   double l;
   GString *bar = g_string_new("");

   l = (double)percent*((double)width/100.);
   l = (int)(l+.5)>=(int)l ? l+.5 : l;

   for(i=0; i<(int)l; i++)
       g_string_append(bar, reuzbl.gui.sbar.progress_s);

   for(; i<width; i++)
       g_string_append(bar, reuzbl.gui.sbar.progress_u);

   return g_string_free(bar, FALSE);
}
/* --End Statusbar functions-- */

void
sharg_append(GArray *a, const gchar *str) {
    const gchar *s = (str ? str : "");
    g_array_append_val(a, s);
}

// make sure that the args string you pass can properly be interpreted (eg properly escaped against whitespace, quotes etc)
gboolean
run_command (const gchar *command, const guint npre, const gchar **args,
             const gboolean sync, char **output_stdout) {
   //command <reuzbl conf> <reuzbl pid> <reuzbl win id> <reuzbl fifo file> <reuzbl socket file> [args]
    GError *err = NULL;

    GArray *a = g_array_new (TRUE, FALSE, sizeof(gchar*));
    gchar *pid = itos(getpid());
    gchar *xwin = itos(reuzbl.xwin);
    guint i;
    sharg_append(a, command);
    for (i = 0; i < npre; i++) /* add n args before the default vars */
        sharg_append(a, args[i]);
    sharg_append(a, reuzbl.state.config_file);
    sharg_append(a, pid);
    sharg_append(a, xwin);
    sharg_append(a, reuzbl.comm.fifo_path);
    sharg_append(a, reuzbl.comm.socket_path);
    sharg_append(a, reuzbl.state.uri);
    sharg_append(a, reuzbl.gui.main_title);

    for (i = npre; i < g_strv_length((gchar**)args); i++)
        sharg_append(a, args[i]);

    gboolean result;
    if (sync) {
        if (*output_stdout) *output_stdout = strfree(*output_stdout);

        result = g_spawn_sync(NULL, (gchar **)a->data, NULL, G_SPAWN_SEARCH_PATH,
                              NULL, NULL, output_stdout, NULL, NULL, &err);
    } else result = g_spawn_async(NULL, (gchar **)a->data, NULL, G_SPAWN_SEARCH_PATH,
                                  NULL, NULL, NULL, &err);

    if (reuzbl.state.verbose) {
        GString *s = g_string_new("spawned:");
        for (i = 0; i < (a->len); i++) {
            gchar *qarg = g_shell_quote(g_array_index(a, gchar*, i));
            g_string_append_printf(s, " %s", qarg);
            g_free (qarg);
        }
        g_string_append_printf(s, " -- result: %s", (result ? "true" : "false"));
        printf("%s\n", s->str);
        g_string_free(s, TRUE);
        if(output_stdout) {
            printf("Stdout: %s\n", *output_stdout);
        }
    }
    if (err) {
        g_printerr("error on run_command: %s\n", err->message);
        g_error_free (err);
    }
    g_free (pid);
    g_free (xwin);
    g_array_free (a, TRUE);
    return result;
}

/*@null@*/ gchar**
split_quoted(const gchar* src, const gboolean unquote) {
    /* split on unquoted space, return array of strings;
       remove a layer of quotes and backslashes if unquote */
    if (!src) return NULL;

    gboolean dq = FALSE;
    gboolean sq = FALSE;
    GArray *a = g_array_new (TRUE, FALSE, sizeof(gchar*));
    GString *s = g_string_new ("");
    const gchar *p;
    gchar **ret;
    gchar *dup;
    for (p = src; *p != '\0'; p++) {
        if ((*p == '\\') && unquote) g_string_append_c(s, *++p);
        else if (*p == '\\') { g_string_append_c(s, *p++);
                               g_string_append_c(s, *p); }
        else if ((*p == '"') && unquote && !sq) dq = !dq;
        else if (*p == '"' && !sq) { g_string_append_c(s, *p);
                                     dq = !dq; }
        else if ((*p == '\'') && unquote && !dq) sq = !sq;
        else if (*p == '\'' && !dq) { g_string_append_c(s, *p);
                                      sq = ! sq; }
        else if ((*p == ' ') && !dq && !sq) {
            dup = g_strdup(s->str);
            g_array_append_val(a, dup);
            g_string_truncate(s, 0);
        } else g_string_append_c(s, *p);
    }
    dup = g_strdup(s->str);
    g_array_append_val(a, dup);
    ret = (gchar**)a->data;
    g_array_free (a, FALSE);
    g_string_free (s, TRUE);
    return ret;
}

void
spawn(WebKitWebView *web_view, GArray *argv, GString *result) {
    (void)web_view; (void)result;
    //TODO: allow more control over argument order so that users can have some arguments before the default ones from run_command, and some after
    if (argv_idx(argv, 0))
        run_command(argv_idx(argv, 0), 0, ((const gchar **) (argv->data + sizeof(gchar*))), FALSE, NULL);
}

void
spawn_sync(WebKitWebView *web_view, GArray *argv, GString *result) {
    (void)web_view; (void)result;

    if (argv_idx(argv, 0))
        run_command(argv_idx(argv, 0), 0, ((const gchar **) (argv->data + sizeof(gchar*))),
                    TRUE, &reuzbl.comm.sync_stdout);
}

void
spawn_sh(WebKitWebView *web_view, GArray *argv, GString *result) {
    (void)web_view; (void)result;
    if (!reuzbl.behave.shell_cmd) {
        g_printerr ("spawn_sh: shell_cmd is not set!\n");
        return;
    }

    guint i;
    gchar *spacer = g_strdup("");
    g_array_insert_val(argv, 1, spacer);
    gchar **cmd = split_quoted(reuzbl.behave.shell_cmd, TRUE);

    for (i = 1; i < g_strv_length(cmd); i++)
        g_array_prepend_val(argv, cmd[i]);

    if (cmd) run_command(cmd[0], g_strv_length(cmd) + 1, (const gchar **) argv->data, FALSE, NULL);
    g_free (spacer);
    g_strfreev (cmd);
}

void
spawn_sh_sync(WebKitWebView *web_view, GArray *argv, GString *result) {
    (void)web_view; (void)result;
    if (!reuzbl.behave.shell_cmd) {
        g_printerr ("spawn_sh_sync: shell_cmd is not set!\n");
        return;
    }

    guint i;
    gchar *spacer = g_strdup("");
    g_array_insert_val(argv, 1, spacer);
    gchar **cmd = split_quoted(reuzbl.behave.shell_cmd, TRUE);

    for (i = 1; i < g_strv_length(cmd); i++)
        g_array_prepend_val(argv, cmd[i]);

    if (cmd) run_command(cmd[0], g_strv_length(cmd) + 1, (const gchar **) argv->data,
                         TRUE, &reuzbl.comm.sync_stdout);
    g_free (spacer);
    g_strfreev (cmd);
}

void
talk_to_socket(WebKitWebView *web_view, GArray *argv, GString *result) {
    (void)web_view; (void)result;

    int fd, len;
    struct sockaddr_un sa;
    char* sockpath;
    ssize_t ret;
    struct pollfd pfd;
    struct iovec* iov;
    guint i;

    if(reuzbl.comm.sync_stdout) reuzbl.comm.sync_stdout = strfree(reuzbl.comm.sync_stdout);

    /* This function could be optimised by storing a hash table of socket paths
       and associated connected file descriptors rather than closing and
       re-opening for every call. Also we could launch a script if socket connect
       fails. */

    /* First element argv[0] is path to socket. Following elements are tokens to
       write to the socket. We write them as a single packet with each token
       separated by an ASCII nul (\0). */
    if(argv->len < 2) {
        g_printerr("talk_to_socket called with only %d args (need at least two).\n",
            (int)argv->len);
        return;
    }

    /* copy socket path, null terminate result */
    sockpath = g_array_index(argv, char*, 0);
    g_strlcpy(sa.sun_path, sockpath, sizeof(sa.sun_path));
    sa.sun_family = AF_UNIX;

    /* create socket file descriptor and connect it to path */
    fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if(fd == -1) {
        g_printerr("talk_to_socket: creating socket failed (%s)\n", strerror(errno));
        return;
    }
    if(connect(fd, (struct sockaddr*)&sa, sizeof(sa))) {
        g_printerr("talk_to_socket: connect failed (%s)\n", strerror(errno));
        close(fd);
        return;
    }

    /* build request vector */
    iov = g_malloc(sizeof(struct iovec) * (argv->len - 1));
    if(!iov) {
        g_printerr("talk_to_socket: unable to allocated memory for token vector\n");
        close(fd);
        return;
    }
    for(i = 1; i < argv->len; ++i) {
        iov[i - 1].iov_base = g_array_index(argv, char*, i);
        iov[i - 1].iov_len = strlen(iov[i - 1].iov_base) + 1; /* string plus \0 */
    }

    /* write request */
    ret = writev(fd, iov, argv->len - 1);
    g_free(iov);
    if(ret == -1) {
        g_printerr("talk_to_socket: write failed (%s)\n", strerror(errno));
        close(fd);
        return;
    }

    /* wait for a response, with a 500ms timeout */
    pfd.fd = fd;
    pfd.events = POLLIN;
    while(1) {
        ret = poll(&pfd, 1, 500);
        if(ret == 1) break;
        if(ret == 0) errno = ETIMEDOUT;
        if(errno == EINTR) continue;
        g_printerr("talk_to_socket: poll failed while waiting for input (%s)\n",
            strerror(errno));
        close(fd);
        return;
    }

    /* get length of response */
    if(ioctl(fd, FIONREAD, &len) == -1) {
        g_printerr("talk_to_socket: cannot find daemon response length, "
            "ioctl failed (%s)\n", strerror(errno));
        close(fd);
        return;
    }

    /* if there is a response, read it */
    if(len) {
        reuzbl.comm.sync_stdout = g_malloc(len + 1);
        if(!reuzbl.comm.sync_stdout) {
            g_printerr("talk_to_socket: failed to allocate %d bytes\n", len);
            close(fd);
            return;
        }
        reuzbl.comm.sync_stdout[len] = 0; /* ensure result is null terminated */

        ret = read(fd, reuzbl.comm.sync_stdout, len);
        if(ret == -1) {
            g_printerr("talk_to_socket: failed to read from socket (%s)\n",
                strerror(errno));
            close(fd);
            return;
        }
    }

    /* clean up */
    close(fd);
    return;
}

void
parse_command(const char *cmd, const char *param, GString *result) {
    CommandInfo *c;

    if ((c = g_hash_table_lookup(reuzbl.behave.commands, cmd))) {
            guint i;
            gchar **par = split_quoted(param, TRUE);
            GArray *a = g_array_new (TRUE, FALSE, sizeof(gchar*));

            if (c->no_split) { /* don't split */
                sharg_append(a, param);
            } else if (par) {
                for (i = 0; i < g_strv_length(par); i++)
                    sharg_append(a, par[i]);
            }

            if (result == NULL) {
                GString *result_print = g_string_new("");

                c->function(reuzbl.gui.web_view, a, result_print);
                if (result_print->len)
                    printf("%*s\n", (int)result_print->len, result_print->str);

                g_string_free(result_print, TRUE);
            } else {
                c->function(reuzbl.gui.web_view, a, result);
            }
            g_strfreev (par);
            g_array_free (a, TRUE);

    } else
        g_printerr ("command \"%s\" not understood. ignoring.\n", cmd);
}

void
set_proxy_url() {
    SoupURI *suri;

    if(reuzbl.net.proxy_url == NULL || *reuzbl.net.proxy_url == ' ') {
        soup_session_remove_feature_by_type(reuzbl.net.soup_session,
                (GType) SOUP_SESSION_PROXY_URI);
    }
    else {
        suri = soup_uri_new(reuzbl.net.proxy_url);
        g_object_set(G_OBJECT(reuzbl.net.soup_session),
                SOUP_SESSION_PROXY_URI,
                suri, NULL);
        soup_uri_free(suri);
    }
    return;
}

void
set_icon() {
    if(file_exists(reuzbl.gui.icon)) {
        if (reuzbl.gui.main_window)
            gtk_window_set_icon_from_file (GTK_WINDOW (reuzbl.gui.main_window), reuzbl.gui.icon, NULL);
    } else {
        g_printerr ("Icon \"%s\" not found. ignoring.\n", reuzbl.gui.icon);
    }
}

void
cmd_load_uri() {
    GArray *a = g_array_new (TRUE, FALSE, sizeof(gchar*));
    g_array_append_val (a, reuzbl.state.uri);
    load_uri(reuzbl.gui.web_view, a, NULL);
    g_array_free (a, TRUE);
}

void
cmd_always_insert_mode() {
    set_insert_mode(reuzbl.behave.always_insert_mode);
    update_title();
}

void
cmd_max_conns() {
    g_object_set(G_OBJECT(reuzbl.net.soup_session),
            SOUP_SESSION_MAX_CONNS, reuzbl.net.max_conns, NULL);
}

void
cmd_max_conns_host() {
    g_object_set(G_OBJECT(reuzbl.net.soup_session),
            SOUP_SESSION_MAX_CONNS_PER_HOST, reuzbl.net.max_conns_host, NULL);
}

void
cmd_http_debug() {
    soup_session_remove_feature
        (reuzbl.net.soup_session, SOUP_SESSION_FEATURE(reuzbl.net.soup_logger));
    /* do we leak if this doesn't get freed? why does it occasionally crash if freed? */
    /*g_free(reuzbl.net.soup_logger);*/

    reuzbl.net.soup_logger = soup_logger_new(reuzbl.behave.http_debug, -1);
    soup_session_add_feature(reuzbl.net.soup_session,
            SOUP_SESSION_FEATURE(reuzbl.net.soup_logger));
}

WebKitWebSettings*
view_settings() {
    return webkit_web_view_get_settings(reuzbl.gui.web_view);
}

void
cmd_font_size() {
    WebKitWebSettings *ws = view_settings();
    if (reuzbl.behave.font_size > 0) {
        g_object_set (G_OBJECT(ws), "default-font-size", reuzbl.behave.font_size, NULL);
    }

    if (reuzbl.behave.monospace_size > 0) {
        g_object_set (G_OBJECT(ws), "default-monospace-font-size",
                      reuzbl.behave.monospace_size, NULL);
    } else {
        g_object_set (G_OBJECT(ws), "default-monospace-font-size",
                      reuzbl.behave.font_size, NULL);
    }
}

void
cmd_default_font_family() {
    g_object_set (G_OBJECT(view_settings()), "default-font-family",
            reuzbl.behave.default_font_family, NULL);
}

void
cmd_monospace_font_family() {
    g_object_set (G_OBJECT(view_settings()), "monospace-font-family",
            reuzbl.behave.monospace_font_family, NULL);
}

void
cmd_sans_serif_font_family() {
    g_object_set (G_OBJECT(view_settings()), "sans_serif-font-family",
            reuzbl.behave.sans_serif_font_family, NULL);
}

void
cmd_serif_font_family() {
    g_object_set (G_OBJECT(view_settings()), "serif-font-family",
            reuzbl.behave.serif_font_family, NULL);
}

void
cmd_cursive_font_family() {
    g_object_set (G_OBJECT(view_settings()), "cursive-font-family",
            reuzbl.behave.cursive_font_family, NULL);
}

void
cmd_fantasy_font_family() {
    g_object_set (G_OBJECT(view_settings()), "fantasy-font-family",
            reuzbl.behave.fantasy_font_family, NULL);
}

void
cmd_zoom_level() {
    webkit_web_view_set_zoom_level (reuzbl.gui.web_view, reuzbl.behave.zoom_level);
}

void
cmd_disable_plugins() {
    g_object_set (G_OBJECT(view_settings()), "enable-plugins",
            !reuzbl.behave.disable_plugins, NULL);
}

void
cmd_disable_scripts() {
    g_object_set (G_OBJECT(view_settings()), "enable-scripts",
            !reuzbl.behave.disable_scripts, NULL);
}

void
cmd_minimum_font_size() {
    g_object_set (G_OBJECT(view_settings()), "minimum-font-size",
            reuzbl.behave.minimum_font_size, NULL);
}
void
cmd_autoload_img() {
    g_object_set (G_OBJECT(view_settings()), "auto-load-images",
            reuzbl.behave.autoload_img, NULL);
}


void
cmd_autoshrink_img() {
    g_object_set (G_OBJECT(view_settings()), "auto-shrink-images",
            reuzbl.behave.autoshrink_img, NULL);
}


void
cmd_enable_spellcheck() {
    g_object_set (G_OBJECT(view_settings()), "enable-spell-checking",
            reuzbl.behave.enable_spellcheck, NULL);
}

void
cmd_enable_private() {
    g_object_set (G_OBJECT(view_settings()), "enable-private-browsing",
            reuzbl.behave.enable_private, NULL);
}

void
cmd_print_bg() {
    g_object_set (G_OBJECT(view_settings()), "print-backgrounds",
            reuzbl.behave.print_bg, NULL);
}

void
cmd_style_uri() {
    g_object_set (G_OBJECT(view_settings()), "user-stylesheet-uri",
            reuzbl.behave.style_uri, NULL);
}

void
cmd_resizable_txt() {
    g_object_set (G_OBJECT(view_settings()), "resizable-text-areas",
            reuzbl.behave.resizable_txt, NULL);
}

void
cmd_default_encoding() {
    g_object_set (G_OBJECT(view_settings()), "default-encoding",
            reuzbl.behave.default_encoding, NULL);
}

void
cmd_enforce_96dpi() {
    g_object_set (G_OBJECT(view_settings()), "enforce-96-dpi",
            reuzbl.behave.enforce_96dpi, NULL);
}

void
cmd_caret_browsing() {
    g_object_set (G_OBJECT(view_settings()), "enable-caret-browsing",
            reuzbl.behave.caret_browsing, NULL);
}

// void
// cmd_cookie_handler() {
//     gchar **split = g_strsplit(reuzbl.behave.cookie_handler, " ", 2);
//     /* pitfall: doesn't handle chain actions; must the sync_ action manually */
//     if ((g_strcmp0(split[0], "sh") == 0) ||
//         (g_strcmp0(split[0], "spawn") == 0)) {
//         g_free (reuzbl.behave.cookie_handler);
//         reuzbl.behave.cookie_handler =
//             g_strdup_printf("sync_%s %s", split[0], split[1]);
//     }
//     g_strfreev (split);
// }

// void
// cmd_scheme_handler() {
//     gchar **split = g_strsplit(reuzbl.behave.scheme_handler, " ", 2);
//     /* pitfall: doesn't handle chain actions; must the sync_ action manually */
//     if ((g_strcmp0(split[0], "sh") == 0) ||
//         (g_strcmp0(split[0], "spawn") == 0)) {
//         g_free (reuzbl.behave.scheme_handler);
//         reuzbl.behave.scheme_handler =
//             g_strdup_printf("sync_%s %s", split[0], split[1]);
//     }
//     g_strfreev (split);
// }

void
cmd_fifo_dir() {
    reuzbl.behave.fifo_dir = init_fifo(reuzbl.behave.fifo_dir);
}

void
cmd_socket_dir() {
    reuzbl.behave.socket_dir = init_socket(reuzbl.behave.socket_dir);
}

void
cmd_inject_html() {
    if(reuzbl.behave.inject_html) {
        webkit_web_view_load_html_string (reuzbl.gui.web_view,
                reuzbl.behave.inject_html, NULL);
    }
}

void
cmd_modkey() {
    int i;
    char *buf;

    buf = g_utf8_strup(reuzbl.behave.modkey, -1);
    reuzbl.behave.modmask = 0;

    if(reuzbl.behave.modkey)
        g_free(reuzbl.behave.modkey);
    reuzbl.behave.modkey = buf;

    for (i = 0; modkeys[i].key != NULL; i++) {
        if (g_strrstr(buf, modkeys[i].key))
            reuzbl.behave.modmask |= modkeys[i].mask;
    }
}

void
cmd_useragent() {
    if (*reuzbl.net.useragent == ' ') {
        g_free (reuzbl.net.useragent);
        reuzbl.net.useragent = NULL;
    } else {
        g_object_set(G_OBJECT(reuzbl.net.soup_session), SOUP_SESSION_USER_AGENT,
            reuzbl.net.useragent, NULL);
    }
}

void
move_statusbar() {
    if (!reuzbl.gui.scrolled_win &&
            !reuzbl.gui.mainbar)
        return;

    gtk_widget_ref(reuzbl.gui.scrolled_win);
    gtk_widget_ref(reuzbl.gui.mainbar);
    gtk_container_remove(GTK_CONTAINER(reuzbl.gui.vbox), reuzbl.gui.scrolled_win);
    gtk_container_remove(GTK_CONTAINER(reuzbl.gui.vbox), reuzbl.gui.mainbar);

    if(reuzbl.behave.status_top) {
        gtk_box_pack_start (GTK_BOX (reuzbl.gui.vbox), reuzbl.gui.mainbar, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (reuzbl.gui.vbox), reuzbl.gui.scrolled_win, TRUE, TRUE, 0);
    }
    else {
        gtk_box_pack_start (GTK_BOX (reuzbl.gui.vbox), reuzbl.gui.scrolled_win, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (reuzbl.gui.vbox), reuzbl.gui.mainbar, FALSE, TRUE, 0);
    }
    gtk_widget_unref(reuzbl.gui.scrolled_win);
    gtk_widget_unref(reuzbl.gui.mainbar);
    gtk_widget_grab_focus (GTK_WIDGET (reuzbl.gui.web_view));
    return;
}

gboolean
set_var_value(const gchar *name, gchar *val) {
    reuzbl_cmdprop *c = NULL;
    char *endp = NULL;
    char *buf = NULL;
    char *invalid_chars = "^°!\"§$%&/()=?'`'+~*'#-.:,;@<>| \\{}[]¹²³¼½";

    if( (c = g_hash_table_lookup(reuzbl.comm.proto_var, name)) ) {
        if(!c->writeable) return FALSE;

        /* check for the variable type */
        if (c->type == TYPE_STR) {
            buf = expand(val, 0);
            g_free(*c->ptr.s);
            *c->ptr.s = buf;
        } else if(c->type == TYPE_INT) {
            buf = expand(val, 0);
            *c->ptr.i = (int)strtoul(buf, &endp, 10);
            g_free(buf);
        } else if (c->type == TYPE_FLOAT) {
            buf = expand(val, 0);
            *c->ptr.f = strtod(buf, &endp);
            g_free(buf);
        }

        /* invoke a command specific function */
        if(c->func) c->func();
    } else {
        /* check wether name violates our naming scheme */
        if(strpbrk(name, invalid_chars)) {
            if (reuzbl.state.verbose)
                printf("Invalid variable name\n");
            return FALSE;
        }

        /* custom vars */
        c = malloc(sizeof(reuzbl_cmdprop));
        c->type = TYPE_STR;
        c->dump = 0;
        c->func = NULL;
        c->writeable = 1;
        buf = expand(val, 0);
        c->ptr.s = malloc(sizeof(char *));
        *c->ptr.s = buf;
        g_hash_table_insert(reuzbl.comm.proto_var,
                g_strdup(name), (gpointer) c);
    }
    return TRUE;
}

enum {M_CMD, M_HTML};
void
parse_cmd_line(const char *ctl_line, GString *result) {
    size_t len=0;

    if((ctl_line[0] == '#') /* Comments */
            || (ctl_line[0] == ' ')
            || (ctl_line[0] == '\n'))
        ; /* ignore these lines */
    else { /* parse a command */
        gchar *ctlstrip;
        gchar **tokens = NULL;
        len = strlen(ctl_line);

        if (ctl_line[len - 1] == '\n') /* strip trailing newline */
            ctlstrip = g_strndup(ctl_line, len - 1);
        else ctlstrip = g_strdup(ctl_line);

        tokens = g_strsplit(ctlstrip, " ", 2);
        parse_command(tokens[0], tokens[1], result);
        g_free(ctlstrip);
        g_strfreev(tokens);
    }
}

/*@null@*/ gchar*
build_stream_name(int type, const gchar* dir) {
    State *s = &reuzbl.state;
    gchar *str = NULL;

    if (type == FIFO) {
        str = g_strdup_printf
            ("%s/reuzbl_fifo_%s", dir, s->instance_name);
    } else if (type == SOCKET) {
        str = g_strdup_printf
            ("%s/reuzbl_socket_%s", dir, s->instance_name);
    }
    return str;
}

gboolean
control_fifo(GIOChannel *gio, GIOCondition condition) {
    if (reuzbl.state.verbose)
        printf("triggered\n");
    gchar *ctl_line;
    GIOStatus ret;
    GError *err = NULL;

    if (condition & G_IO_HUP)
        g_error ("Fifo: Read end of pipe died!\n");

    if(!gio)
       g_error ("Fifo: GIOChannel broke\n");

    ret = g_io_channel_read_line(gio, &ctl_line, NULL, NULL, &err);
    if (ret == G_IO_STATUS_ERROR) {
        g_error ("Fifo: Error reading: %s\n", err->message);
        g_error_free (err);
    }

    parse_cmd_line(ctl_line, NULL);
    g_free(ctl_line);

    return TRUE;
}

/*@null@*/ gchar*
init_fifo(gchar *dir) { /* return dir or, on error, free dir and return NULL */
    if (reuzbl.comm.fifo_path) { /* get rid of the old fifo if one exists */
        if (unlink(reuzbl.comm.fifo_path) == -1)
            g_warning ("Fifo: Can't unlink old fifo at %s\n", reuzbl.comm.fifo_path);
        g_free(reuzbl.comm.fifo_path);
        reuzbl.comm.fifo_path = NULL;
    }

    GIOChannel *chan = NULL;
    GError *error = NULL;
    gchar *path = build_stream_name(FIFO, dir);

    if (!file_exists(path)) {
        if (mkfifo (path, 0666) == 0) {
            // we don't really need to write to the file, but if we open the file as 'r' we will block here, waiting for a writer to open the file.
            chan = g_io_channel_new_file(path, "r+", &error);
            if (chan) {
                if (g_io_add_watch(chan, G_IO_IN|G_IO_HUP, (GIOFunc) control_fifo, NULL)) {
                    if (reuzbl.state.verbose)
                        printf ("init_fifo: created successfully as %s\n", path);
                    reuzbl.comm.fifo_path = path;
                    return dir;
                } else g_warning ("init_fifo: could not add watch on %s\n", path);
            } else g_warning ("init_fifo: can't open: %s\n", error->message);
        } else g_warning ("init_fifo: can't create %s: %s\n", path, strerror(errno));
    } else g_warning ("init_fifo: can't create %s: file exists\n", path);

    /* if we got this far, there was an error; cleanup */
    if (error) g_error_free (error);
    g_free(dir);
    g_free(path);
    return NULL;
}

gboolean
control_stdin(GIOChannel *gio, GIOCondition condition) {
    (void) condition;
    gchar *ctl_line = NULL;
    GIOStatus ret;

    ret = g_io_channel_read_line(gio, &ctl_line, NULL, NULL, NULL);
    if ( (ret == G_IO_STATUS_ERROR) || (ret == G_IO_STATUS_EOF) )
        return FALSE;

    parse_cmd_line(ctl_line, NULL);
    g_free(ctl_line);

    return TRUE;
}

void
create_stdin () {
    GIOChannel *chan = NULL;
    GError *error = NULL;

    chan = g_io_channel_unix_new(fileno(stdin));
    if (chan) {
        if (!g_io_add_watch(chan, G_IO_IN|G_IO_HUP, (GIOFunc) control_stdin, NULL)) {
            g_error ("Stdin: could not add watch\n");
        } else {
            if (reuzbl.state.verbose)
                printf ("Stdin: watch added successfully\n");
        }
    } else {
        g_error ("Stdin: Error while opening: %s\n", error->message);
    }
    if (error) g_error_free (error);
}

gboolean
control_socket(GIOChannel *chan) {
    struct sockaddr_un remote;
    unsigned int t = sizeof(remote);
    int clientsock;
    GIOChannel *clientchan;

    clientsock = accept (g_io_channel_unix_get_fd(chan),
                         (struct sockaddr *) &remote, &t);

    if ((clientchan = g_io_channel_unix_new(clientsock))) {
        g_io_add_watch(clientchan, G_IO_IN|G_IO_HUP,
                       (GIOFunc) control_client_socket, clientchan);
    }

    return TRUE;
}

gboolean
control_client_socket(GIOChannel *clientchan) {
    char *ctl_line;
    GString *result = g_string_new("");
    GError *error = NULL;
    GIOStatus ret;
    gsize len;

    ret = g_io_channel_read_line(clientchan, &ctl_line, &len, NULL, &error);
    if (ret == G_IO_STATUS_ERROR) {
        g_warning ("Error reading: %s\n", error->message);
        g_io_channel_shutdown(clientchan, TRUE, &error);
        return FALSE;
    } else if (ret == G_IO_STATUS_EOF) {
        /* shutdown and remove channel watch from main loop */
        g_io_channel_shutdown(clientchan, TRUE, &error);
        return FALSE;
    }

    if (ctl_line) {
        parse_cmd_line (ctl_line, result);
        g_string_append_c(result, '\n');
        ret = g_io_channel_write_chars (clientchan, result->str, result->len,
                                        &len, &error);
        if (ret == G_IO_STATUS_ERROR) {
            g_warning ("Error writing: %s", error->message);
        }
        g_io_channel_flush(clientchan, &error);
    }

    if (error) g_error_free (error);
    g_string_free(result, TRUE);
    g_free(ctl_line);
    return TRUE;
}

/*@null@*/ gchar*
init_socket(gchar *dir) { /* return dir or, on error, free dir and return NULL */
    if (reuzbl.comm.socket_path) { /* remove an existing socket should one exist */
        if (unlink(reuzbl.comm.socket_path) == -1)
            g_warning ("init_socket: couldn't unlink socket at %s\n", reuzbl.comm.socket_path);
        g_free(reuzbl.comm.socket_path);
        reuzbl.comm.socket_path = NULL;
    }

    if (*dir == ' ') {
        g_free(dir);
        return NULL;
    }

    GIOChannel *chan = NULL;
    int sock, len;
    struct sockaddr_un local;
    gchar *path = build_stream_name(SOCKET, dir);

    sock = socket (AF_UNIX, SOCK_STREAM, 0);

    local.sun_family = AF_UNIX;
    strcpy (local.sun_path, path);
    unlink (local.sun_path);

    len = strlen (local.sun_path) + sizeof (local.sun_family);
    if (bind (sock, (struct sockaddr *) &local, len) != -1) {
        if (reuzbl.state.verbose)
            printf ("init_socket: opened in %s\n", path);
        listen (sock, 5);

        if( (chan = g_io_channel_unix_new(sock)) ) {
            g_io_add_watch(chan, G_IO_IN|G_IO_HUP, (GIOFunc) control_socket, chan);
            reuzbl.comm.socket_path = path;
            return dir;
        }
    } else g_warning ("init_socket: could not open in %s: %s\n", path, strerror(errno));

    /* if we got this far, there was an error; cleanup */
    g_free(path);
    g_free(dir);
    return NULL;
}

/*
 NOTE: we want to keep variables like b->title_format_long in their "unprocessed" state
 it will probably improve performance if we would "cache" the processed variant, but for now it works well enough...
*/
// this function may be called very early when the templates are not set (yet), hence the checks
void
update_title (void) {
    Behaviour *b = &reuzbl.behave;
    gchar *parsed;
    GString* keycmd;

    int len = strlen(reuzbl.state.keycmd);
    //gchar *prev = g_utf8_find_prev_char(reuzbl.state.keycmd, reuzbl.state.keycmd + reuzbl.state.keycmd_pos);
    if (reuzbl.state.keycmd_pos < len) {
            if (reuzbl.state.keycmd_pos>0) {
                keycmd = g_string_new_len(reuzbl.state.keycmd + reuzbl.state.keycmd_act_len, reuzbl.state.keycmd_pos - reuzbl.state.keycmd_act_len);
                g_string_append(keycmd, "<span underline=\"single\">");
            } else
                keycmd = g_string_new("<span underline=\"single\">");

            g_string_append_c(keycmd, reuzbl.state.keycmd[reuzbl.state.keycmd_pos]);
            g_string_append(keycmd, "</span>");
            if (reuzbl.state.keycmd_pos < len - 1)
                g_string_append(keycmd, &reuzbl.state.keycmd[reuzbl.state.keycmd_pos+1]);
            reuzbl.state.keycmd_marked = g_string_free(keycmd, FALSE);
    } else {
        if(reuzbl.state.keycmd_act_len>=strlen(reuzbl.state.keycmd))
            keycmd = g_string_new("");
        else
            keycmd = g_string_new(reuzbl.state.keycmd + reuzbl.state.keycmd_act_len);
        g_string_append(keycmd, "<span underline=\"single\"> </span>");
        reuzbl.state.keycmd_marked = g_string_free(keycmd, FALSE);
    }


    if (b->show_status) {
        if (b->title_format_short) {
            parsed = expand(b->title_format_short, 0);
            if (reuzbl.gui.main_window)
                gtk_window_set_title (GTK_WINDOW(reuzbl.gui.main_window), parsed);
            g_free(parsed);
        }
        if (b->status_format) {
            parsed = expand(b->status_format, 0);
            gtk_label_set_markup(GTK_LABEL(reuzbl.gui.mainbar_label), parsed);
            g_free(parsed);
        }
        if (b->status_background) {
            GdkColor color;
            gdk_color_parse (b->status_background, &color);
            //labels and hboxes do not draw their own background. applying this on the vbox/main_window is ok as the statusbar is the only affected widget. (if not, we could also use GtkEventBox)
            if (reuzbl.gui.main_window)
                gtk_widget_modify_bg (reuzbl.gui.main_window, GTK_STATE_NORMAL, &color);
            else if (reuzbl.gui.plug)
                gtk_widget_modify_bg (GTK_WIDGET(reuzbl.gui.plug), GTK_STATE_NORMAL, &color);
        }
    } else {
        if (b->title_format_long) {
            parsed = expand(b->title_format_long, 0);
            if (reuzbl.gui.main_window)
                gtk_window_set_title (GTK_WINDOW(reuzbl.gui.main_window), parsed);
            g_free(parsed);
        }
    }
}

gboolean
configure_event_cb(GtkWidget* window, GdkEventConfigure* event) {
    (void) window;
    (void) event;

    retrieve_geometry();
    send_event(GEOMETRY_CHANGED, reuzbl.gui.geometry);  
    return FALSE;
}

gboolean
button_press_cb (GtkWidget* window, GdkEventButton* event) {
    (void) window;

    if(event->type == GDK_BUTTON_PRESS) {
        Action *act;
        gchar* button = g_strdup_printf("%d", event->button);
        //printf("%d, %s\n", event->button, button);
        if ((act = g_hash_table_lookup(reuzbl.buttons, button))) {
            gchar* param = expand(act->param, 0);
            //printf("%s, %s, %s\n", act->name, act->param, param);

            parse_command(act->name, param, NULL);
            g_free(param);
            return TRUE;
        }
        g_free(button);
    }
    return FALSE;
}

// TODO: make it UTF8 compatible
gboolean
key_press_cb (GtkWidget* window, GdkEventKey* event) {
    //TRUE to stop other handlers from being invoked for the event. FALSE to propagate the event further.

    (void) window;

    // if(event->type == GDK_KEY_PRESS)
    //     send_event(KEYPRESS, gdk_keyval_name(event->keyval) );

    // TODO: at least try to make keys configurable heere!
    if (event->type   != GDK_KEY_PRESS ||
        event->keyval == GDK_Page_Up   ||
        event->keyval == GDK_Page_Down ||
        event->keyval == GDK_Home      ||
        event->keyval == GDK_End       ||
        event->keyval == GDK_Up        ||
        event->keyval == GDK_Down      ||
        // event->keyval == GDK_Left      ||
        // event->keyval == GDK_Right     ||
        event->keyval == GDK_Shift_L   ||
        event->keyval == GDK_Shift_R)
        return FALSE;

    /* turn off insert mode (if always_insert_mode is not used) */
    if (reuzbl.behave.insert_mode && (event->keyval == GDK_Escape)) {
        set_insert_mode(reuzbl.behave.always_insert_mode);
        update_title();
        reuzbl.state.keycmd_pos = 0;
        return TRUE;
    }

    if (reuzbl.behave.insert_mode &&
        ( ((event->state & reuzbl.behave.modmask) != reuzbl.behave.modmask) ||
          (!reuzbl.behave.modmask)
        )
       )
        return FALSE;


    // *KEY* ESCAPE
    if (event->keyval == GDK_Escape) {
        clear_keycmd();
        update_title();
        dehilight(reuzbl.gui.web_view, NULL, NULL);
        return TRUE;
    }
    // *KEY* INSERT
    if (event->keyval == GDK_Insert) {
        //Insert without shift - insert from clipboard; Insert with shift - insert from primary
        gchar * str;
        if ((event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK) {
            str = gtk_clipboard_wait_for_text (gtk_clipboard_get (GDK_SELECTION_PRIMARY));
        } else {
            str = gtk_clipboard_wait_for_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD));
        }
        if (str) {
            GString* keycmd = g_string_new(reuzbl.state.keycmd);
            g_string_insert(keycmd, reuzbl.state.keycmd_pos, str);
            reuzbl.state.keycmd = g_string_free(keycmd, FALSE);
            reuzbl.state.keycmd_pos += strlen(str);
            update_title ();
            g_free (str);
        }
        return TRUE;
    }
    // *KEY* BACKSPACE
    if (event->keyval == GDK_BackSpace)
        keycmd_bs(NULL, NULL, NULL);
    // TODO: KEY DELETE 
    gboolean key_ret = FALSE;
    gboolean key_no_add = FALSE;
    // TODO: history
    // *KEY* LEFT
    if (event->keyval == GDK_Left && reuzbl.state.keycmd_pos>0) {
    	key_no_add = TRUE;
    	reuzbl.state.keycmd_pos--;
    }
    // *KEY* RIGHT
    if (event->keyval == GDK_Right && reuzbl.state.keycmd_pos<(int)strlen(reuzbl.state.keycmd)) {
        key_no_add = TRUE;
        reuzbl.state.keycmd_pos++;
    }
    // *KEY* RETURN
    if ((event->keyval == GDK_Return) || (event->keyval == GDK_KP_Enter)) {
        key_ret = TRUE;
        if(reuzbl.state.keycmd_pos) reuzbl.state.keycmd_pos--;
    }
    if (!key_ret && !key_no_add) { // heere
        GString* keycmd = g_string_new(reuzbl.state.keycmd);
        g_string_insert(keycmd, reuzbl.state.keycmd_pos, event->string);
        reuzbl.state.keycmd = g_string_free(keycmd, FALSE);
        if(strlen(event->string)) reuzbl.state.keycmd_pos++;
    }
    if (reuzbl.state.verbose)
        printf("%d, %d, %d, '%s', '%s'\n", reuzbl.state.keycmd_pos, (int)strlen(reuzbl.state.keycmd), (int)strlen(event->string), event->string, reuzbl.state.keycmd);

    run_keycmd(key_ret);
    update_title();
    if (key_ret) return (!reuzbl.behave.insert_mode);
    return TRUE;
}

void
run_keycmd(const gboolean key_ret) {
    
    /* run the keycmd immediately if it isn't incremental and doesn't take args */
    Action *act;
    gchar *tmp;

    if ((act = g_hash_table_lookup(reuzbl.bindings, reuzbl.state.keycmd))) {
        clear_keycmd();
        parse_command(act->name, act->param, NULL);

        tmp = g_strdup_printf("%s %s", act->name, act->param?act->param:"");
        send_event(COMMAND_EXECUTED, tmp);
        g_free(tmp);
        return;
    }

    /* try if it's an incremental keycmd or one that takes args, and run it */
    GString* short_keys = g_string_new ("");
    GString* short_keys_inc = g_string_new ("");
    guint i;
    guint len = strlen(reuzbl.state.keycmd);
    if(reuzbl.state.keycmd_screen_name) g_free(reuzbl.state.keycmd_screen_name);
    reuzbl.state.keycmd_screen_name = NULL;
    reuzbl.state.keycmd_act_len = 0;
    for (i=0; i<len; i++) {
        g_string_append_c(short_keys, reuzbl.state.keycmd[i]);
        g_string_assign(short_keys_inc, short_keys->str);
        g_string_append_c(short_keys, '_');
        g_string_append_c(short_keys_inc, '*');

        if ((act = g_hash_table_lookup(reuzbl.bindings, short_keys->str))) {
            if (key_ret) {
                /* run normal cmds only if return was pressed */
                exec_paramcmd(act, i);
                clear_keycmd();
                tmp = g_strdup_printf("%s %s", act->name, act->param?act->param:"");
                send_event(COMMAND_EXECUTED, tmp);
                g_free(tmp);
                break;
            } else {
                reuzbl.state.keycmd_screen_name = g_strdup(act->screen_name);
                reuzbl.state.keycmd_act_len = strlen(short_keys->str) - 1;
            }
        } else if ((act = g_hash_table_lookup(reuzbl.bindings, short_keys_inc->str))) {
            if (key_ret)  /* just quit the incremental command on return */
                clear_keycmd();
            else {
                exec_paramcmd(act, i); /* otherwise execute the incremental */
                tmp = g_strdup_printf("%s %s", act->name, act->param?act->param:"");
                send_event(COMMAND_EXECUTED, tmp);
                g_free(tmp);
            }
            break;
        }

        g_string_truncate(short_keys, short_keys->len - 1);
    }
    g_string_free (short_keys, TRUE);
    g_string_free (short_keys_inc, TRUE);
}

void
exec_paramcmd(const Action *act, const guint i) {
    GString *parampart = g_string_new (reuzbl.state.keycmd);
    GString *actionname = g_string_new ("");
    GString *actionparam = g_string_new ("");
    g_string_erase (parampart, 0, i+1);
    if (act->name)
        g_string_printf (actionname, act->name, parampart->str);
    if (act->param)
        g_string_printf (actionparam, act->param, parampart->str);
    parse_command(actionname->str, actionparam->str, NULL);
    g_string_free(actionname, TRUE);
    g_string_free(actionparam, TRUE);
    g_string_free(parampart, TRUE);
}


void
create_browser () {
    GUI *g = &reuzbl.gui;

    g->web_view = WEBKIT_WEB_VIEW (webkit_web_view_new ());

    g_signal_connect (G_OBJECT (g->web_view), "notify::title",                        G_CALLBACK (title_change_cb),        NULL);
      // TODO: WHY ? "signal::title-changed",                        (GCallback)title_change_cb,         NULL,
    g_signal_connect (G_OBJECT (g->web_view), "selection-changed",                    G_CALLBACK (selection_changed_cb),   g->web_view);
    g_signal_connect (G_OBJECT (g->web_view), "load-progress-changed",                G_CALLBACK (progress_change_cb),     g->web_view);
    g_signal_connect (G_OBJECT (g->web_view), "load-committed",                       G_CALLBACK (load_commit_cb),         g->web_view);
    g_signal_connect (G_OBJECT (g->web_view), "load-started",                         G_CALLBACK (load_start_cb),          g->web_view);
    g_signal_connect (G_OBJECT (g->web_view), "load-finished",                        G_CALLBACK (load_finish_cb),         g->web_view);
    g_signal_connect (G_OBJECT (g->web_view), "load-error",                           G_CALLBACK (load_error_cb),          g->web_view);
    g_signal_connect (G_OBJECT (g->web_view), "hovering-over-link",                   G_CALLBACK (link_hover_cb),          g->web_view);
    g_signal_connect (G_OBJECT (g->web_view), "navigation-policy-decision-requested", G_CALLBACK (navigation_decision_cb), g->web_view);
    g_signal_connect (G_OBJECT (g->web_view), "new-window-policy-decision-requested", G_CALLBACK (new_window_cb),          g->web_view);
    g_signal_connect (G_OBJECT (g->web_view), "download-requested",                   G_CALLBACK (download_cb),            g->web_view);
    g_signal_connect (G_OBJECT (g->web_view), "create-web-view",                      G_CALLBACK (create_web_view_cb),     g->web_view);
    g_signal_connect (G_OBJECT (g->web_view), "mime-type-policy-decision-requested",  G_CALLBACK (mime_policy_cb),         g->web_view);
    g_signal_connect (G_OBJECT (g->web_view), "button-press-event",                   G_CALLBACK (button_press_cb),        g->web_view);
      // TODO: hmmmm ?
      // "signal::populate-popup",                       (GCallback)populate_popup_cb,       NULL,
      // "signal::focus-in-event",                       (GCallback)focus_cb,                NULL,
      // "signal::focus-out-event",                      (GCallback)focus_cb,                NULL,
}



GtkWidget*
create_mainbar () {
    GUI *g = &reuzbl.gui;

    g->mainbar = gtk_hbox_new (FALSE, 0);

    /* keep a reference to the bar so we can re-pack it at runtime*/
    //sbar_ref = g_object_ref(g->mainbar);

    g->mainbar_label = gtk_label_new ("");
    gtk_label_set_selectable((GtkLabel *)g->mainbar_label, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(g->mainbar_label), PANGO_ELLIPSIZE_END);
    gtk_misc_set_alignment (GTK_MISC(g->mainbar_label), 0, 0);
    gtk_misc_set_padding (GTK_MISC(g->mainbar_label), 2, 2);
    gtk_box_pack_start (GTK_BOX (g->mainbar), g->mainbar_label, TRUE, TRUE, 0);
    g_signal_connect (G_OBJECT (g->mainbar), "key-press-event", G_CALLBACK (key_press_cb), NULL);
    return g->mainbar;
}

GtkWidget*
create_window () {
    GtkWidget* window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
    gtk_widget_set_name (window, "reUzbl browser");
    g_signal_connect (G_OBJECT (window), "destroy", G_CALLBACK (destroy_cb), NULL);
    g_signal_connect (G_OBJECT (window), "key-press-event", G_CALLBACK (key_press_cb), NULL);
    g_signal_connect (G_OBJECT (window), "configure-event", G_CALLBACK (configure_event_cb), NULL);

    return window;
}

GtkPlug*
create_plug () {
    GtkPlug* plug = GTK_PLUG (gtk_plug_new (reuzbl.state.socket_id));
    g_signal_connect (G_OBJECT (plug), "destroy", G_CALLBACK (destroy_cb), NULL);
    g_signal_connect (G_OBJECT (plug), "key-press-event", G_CALLBACK (key_press_cb), NULL);

    return plug;
}


gchar**
inject_handler_args(const gchar *actname, const gchar *origargs, const gchar *newargs) {
    /*
      If actname is one that calls an external command, this function will inject
      newargs in front of the user-provided args in that command line.  They will
      come become after the body of the script (in sh) or after the name of
      the command to execute (in spawn).
      i.e. sh <body> <userargs> becomes sh <body> <ARGS> <userargs> and
      spawn <command> <userargs> becomes spawn <command> <ARGS> <userargs>.

      The return value consist of two strings: the action (sh, ...) and its args.

      If act is not one that calls an external command, then the given action merely
      gets duplicated.
    */
    GArray *rets = g_array_new(TRUE, FALSE, sizeof(gchar*));
    /* Arrr! Here be memory leaks */
    gchar *actdup = g_strdup(actname);
    g_array_append_val(rets, actdup);

    if ((g_strcmp0(actname, "spawn") == 0) ||
        (g_strcmp0(actname, "sh") == 0) ||
        (g_strcmp0(actname, "sync_spawn") == 0) ||
        (g_strcmp0(actname, "sync_sh") == 0) ||
        (g_strcmp0(actname, "talk_to_socket") == 0)) {
        guint i;
        GString *a = g_string_new("");
        gchar **spawnparts = split_quoted(origargs, FALSE);
        g_string_append_printf(a, "%s", spawnparts[0]); /* sh body or script name */
        if (newargs) g_string_append_printf(a, " %s", newargs); /* handler args */

        for (i = 1; i < g_strv_length(spawnparts); i++) /* user args */
            if (spawnparts[i]) g_string_append_printf(a, " %s", spawnparts[i]);

        g_array_append_val(rets, a->str);
        g_string_free(a, FALSE);
        g_strfreev(spawnparts);
    } else {
        gchar *origdup = g_strdup(origargs);
        g_array_append_val(rets, origdup);
    }
    return (gchar**)g_array_free(rets, FALSE);
}

void
run_handler (const gchar *act, const gchar *args) {
    /* Consider this code a temporary hack to make the handlers usable.
       In practice, all this splicing, injection, and reconstruction is
       inefficient, annoying and hard to manage.  Potential pitfalls arise
       when the handler specific args 1) are not quoted  (the handler
       callbacks should take care of this)  2) are quoted but interfere
       with the users' own quotation.  A more ideal solution is
       to refactor parse_command so that it doesn't just take a string
       and execute it; rather than that, we should have a function which
       returns the argument vector parsed from the string.  This vector
       could be modified (e.g. insert additional args into it) before
       passing it to the next function that actually executes it.  Though
       it still isn't perfect for chain actions..  will reconsider & re-
       factor when I have the time. -duc */

    char **parts = g_strsplit(act, " ", 2);
    if (!parts) return;
    if (g_strcmp0(parts[0], "chain") == 0) {
        GString *newargs = g_string_new("");
        gchar **chainparts = split_quoted(parts[1], FALSE);

        /* for every argument in the chain, inject the handler args
           and make sure the new parts are wrapped in quotes */
        gchar **cp = chainparts;
        gchar quot = '\'';
        gchar *quotless = NULL;
        gchar **spliced_quotless = NULL; // sigh -_-;
        gchar **inpart = NULL;

        while (*cp) {
            if ((**cp == '\'') || (**cp == '\"')) { /* strip old quotes */
                quot = **cp;
                quotless = g_strndup(&(*cp)[1], strlen(*cp) - 2);
            } else quotless = g_strdup(*cp);

            spliced_quotless = g_strsplit(quotless, " ", 2);
            inpart = inject_handler_args(spliced_quotless[0], spliced_quotless[1], args);
            g_strfreev(spliced_quotless);

            g_string_append_printf(newargs, " %c%s %s%c", quot, inpart[0], inpart[1], quot);
            g_free(quotless);
            g_strfreev(inpart);
            cp++;
        }

        parse_command(parts[0], &(newargs->str[1]), NULL);
        g_string_free(newargs, TRUE);
        g_strfreev(chainparts);

    } else {
        gchar **inparts = inject_handler_args(parts[0], parts[1], args);
        parse_command(inparts[0], inparts[1], NULL);
        g_free(inparts[0]);
        g_free(inparts[1]);
    }
    g_strfreev(parts);
}

void
add_binding (const gchar *key, const gchar *act) {
    gchar *screen_name = NULL; 
    gchar *new_key = g_strdup(key);
    char **parts = g_strsplit(act, " ", 2);
    Action *action;

    if (!parts)
        return;

    //Debug:
    if (reuzbl.state.verbose)
        printf ("Binding %-10s : %s\n", key, act);

    if(key) {
        gchar **left = g_strsplit(key, "<", 2);
        if(left[1]) {
            gchar **right = g_strsplit(left[1], ">", 2);
            if(right[1]) {
                screen_name = g_strdup(right[0]);
                new_key = g_strconcat(left[0], right[1], NULL);
                //printf("----- -%s- -%s- -%s-\n", key, new_key, screen_name);
            }
            g_strfreev(right);
        }
        g_strfreev(left);
    }


    action = new_action(parts[0], parts[1], screen_name);

    if (g_hash_table_remove (reuzbl.bindings, new_key))
        g_warning ("Overwriting existing binding for \"%s\"", new_key);
    g_hash_table_replace(reuzbl.bindings, g_strdup(new_key), action);
    g_strfreev(parts);
    g_free(new_key);
}

void
add_handler (const gchar *key, const gchar *act) {
    gchar *screen_name = NULL; 
    gchar *new_key = g_strdup(key);
    char **parts = g_strsplit(act, " ", 2);
    Action *action;

    if (!parts)
        return;

    //Debug:
    if (reuzbl.state.verbose)
        printf ("Handler %-10s : %s\n", key, act);

    if(key) {
        gchar **left = g_strsplit(key, "<", 2);
        if(left[1]) {
            gchar **right = g_strsplit(left[1], ">", 2);
            if(right[1]) {
                screen_name = g_strdup(right[0]);
                new_key = g_strconcat(left[0], right[1], NULL);
                //printf("----- -%s- -%s- -%s-\n", key, new_key, screen_name);
            }
            g_strfreev(right);
        }
        g_strfreev(left);
    }


    action = new_action(parts[0], parts[1], screen_name);

    if (g_hash_table_remove (reuzbl.handlers, new_key))
        g_warning ("Overwriting existing handler for \"%s\"", new_key);
    g_hash_table_replace(reuzbl.handlers, g_strdup(new_key), action);
    g_strfreev(parts);
    g_free(new_key);
}

void
add_button (const gchar *key, const gchar *act) {
    gchar *screen_name = NULL; 
    gchar *new_key = g_strdup(key);
    char **parts = g_strsplit(act, " ", 2);
    Action *action;

    if (!parts)
        return;

    //Debug:
    if (reuzbl.state.verbose)
        printf ("Button %-10s : %s\n", key, act);

    if(key) {
        gchar **left = g_strsplit(key, "<", 2);
        if(left[1]) {
            gchar **right = g_strsplit(left[1], ">", 2);
            if(right[1]) {
                screen_name = g_strdup(right[0]);
                new_key = g_strconcat(left[0], right[1], NULL);
                //printf("----- -%s- -%s- -%s-\n", key, new_key, screen_name);
            }
            g_strfreev(right);
        }
        g_strfreev(left);
    }


    action = new_action(parts[0], parts[1], screen_name);

    if (g_hash_table_remove (reuzbl.buttons, new_key))
        g_warning ("Overwriting existing button for \"%s\"", new_key);
    g_hash_table_replace(reuzbl.buttons, g_strdup(new_key), action);
    g_strfreev(parts);
    g_free(new_key);
}

/*@null@*/ gchar*
get_xdg_var (XDG_Var xdg) {
    const gchar* actual_value = getenv (xdg.environmental);
    const gchar* home         = getenv ("HOME");
    gchar* return_value;

    if (! actual_value || strcmp (actual_value, "") == 0) {
        if (xdg.default_value) {
            return_value = str_replace ("~", home, xdg.default_value);
        } else {
            return_value = NULL;
        }
    } else {
        return_value = str_replace("~", home, actual_value);
    }

    return return_value;
}

/*@null@*/ gchar*
find_xdg_file (int xdg_type, const char* filename) {
    /* xdg_type = 0 => config
       xdg_type = 1 => data
       xdg_type = 2 => cache*/

    gchar* xdgv = get_xdg_var (XDG[xdg_type]);
    gchar* temporary_file = g_strconcat (xdgv, filename, NULL);
    g_free (xdgv);

    gchar* temporary_string;
    char*  saveptr;
    char*  buf;

    if (! file_exists (temporary_file) && xdg_type != 2) {
        buf = get_xdg_var (XDG[3 + xdg_type]);
        temporary_string = (char *) strtok_r (buf, ":", &saveptr);
        g_free(buf);

        while ((temporary_string = (char * ) strtok_r (NULL, ":", &saveptr)) && ! file_exists (temporary_file)) {
            g_free (temporary_file);
            temporary_file = g_strconcat (temporary_string, filename, NULL);
        }
    }

    //g_free (temporary_string); - segfaults.

    if (file_exists (temporary_file)) {
        return temporary_file;
    } else {
        g_free(temporary_file);
        return NULL;
    }
}
void
settings_init () {
    State *s = &reuzbl.state;
    Network *n = &reuzbl.net;
    int i;
    for (i = 0; default_config[i].command != NULL; i++) {
        parse_cmd_line(default_config[i].command, NULL);
    }

    if (g_strcmp0(s->config_file, "-") == 0) {
        s->config_file = NULL;
        create_stdin();
    }

    else if (!s->config_file) {
        s->config_file = find_xdg_file (0, "/reuzbl/config");
    }

    if (s->config_file) {
        GArray* lines = read_file_by_line (s->config_file);
        int i = 0;
        gchar* line;

        while ((line = g_array_index(lines, gchar*, i))) {
            parse_cmd_line (line, NULL);
            i ++;
            g_free (line);
        }
        g_array_free (lines, TRUE);
    } else {
        if (reuzbl.state.verbose)
            printf ("No configuration file loaded.\n");
    }

    g_signal_connect_after(n->soup_session, "request-started", G_CALLBACK(handle_cookies), NULL);
}

void handle_cookies (SoupSession *session, SoupMessage *msg, gpointer user_data){
    (void) session;
    (void) user_data;
    //if (!reuzbl.behave.cookie_handler)
    //     return;

    soup_message_add_header_handler(msg, "got-headers", "Set-Cookie", G_CALLBACK(save_cookies), NULL);
    GString *s = g_string_new ("");
    SoupURI * soup_uri = soup_message_get_uri(msg);
    g_string_printf(s, "GET '%s' '%s' '%s'", soup_uri->scheme, soup_uri->host, soup_uri->path);
    // if(reuzbl.behave.cookie_handler)
    //     run_handler(reuzbl.behave.cookie_handler, s->str);
    send_event(COOKIE, s->str);

    // if(reuzbl.behave.cookie_handler &&
    if(g_hash_table_lookup(reuzbl.handlers, "COOKIE") &&
            reuzbl.comm.sync_stdout && strcmp (reuzbl.comm.sync_stdout, "") != 0) {
        char *p = strchr(reuzbl.comm.sync_stdout, '\n' );
        if ( p != NULL ) *p = '\0';
        soup_message_headers_replace (msg->request_headers, "Cookie", (const char *) reuzbl.comm.sync_stdout);

    }
    if (reuzbl.comm.sync_stdout)
        reuzbl.comm.sync_stdout = strfree(reuzbl.comm.sync_stdout);

    g_string_free(s, TRUE);
}

void
save_cookies (SoupMessage *msg, gpointer user_data){
    (void) user_data;
    GSList *ck;
    char *cookie;
    for (ck = soup_cookies_from_response(msg); ck; ck = ck->next){
        cookie = soup_cookie_to_set_cookie_header(ck->data);
        SoupURI * soup_uri = soup_message_get_uri(msg);
        GString *s = g_string_new ("");
        g_string_printf(s, "PUT '%s' '%s' '%s' '%s'", soup_uri->scheme, soup_uri->host, soup_uri->path, cookie);
        //run_handler(reuzbl.behave.cookie_handler, s->str);
        send_event(COOKIE, s->str);
        g_free (cookie);
        g_string_free(s, TRUE);
    }
    g_slist_free(ck);
}

/* --- WEBINSPECTOR --- */
void
hide_window_cb(GtkWidget *widget, gpointer data) {
    (void) data;

    gtk_widget_hide(widget);
}

WebKitWebView*
create_inspector_cb (WebKitWebInspector* web_inspector, WebKitWebView* page, gpointer data){
    (void) data;
    (void) page;
    (void) web_inspector;
    GtkWidget* scrolled_window;
    GtkWidget* new_web_view;
    GUI *g = &reuzbl.gui;

    g->inspector_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(G_OBJECT(g->inspector_window), "delete-event",
            G_CALLBACK(hide_window_cb), NULL);

    gtk_window_set_title(GTK_WINDOW(g->inspector_window), "reUzbl WebInspector");
    gtk_window_set_default_size(GTK_WINDOW(g->inspector_window), 400, 300);
    gtk_widget_show(g->inspector_window);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
            GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(g->inspector_window), scrolled_window);
    gtk_widget_show(scrolled_window);

    new_web_view = webkit_web_view_new();
    gtk_container_add(GTK_CONTAINER(scrolled_window), new_web_view);

    return WEBKIT_WEB_VIEW(new_web_view);
}

gboolean
inspector_show_window_cb (WebKitWebInspector* inspector){
    (void) inspector;
    gtk_widget_show(reuzbl.gui.inspector_window);

    send_event(WEBINSPECTOR, "open");
    return TRUE;
}

/* TODO: Add variables and code to make use of these functions */
gboolean
inspector_close_window_cb (WebKitWebInspector* inspector){
    (void) inspector;
    send_event(WEBINSPECTOR, "close");
    return TRUE;
}

gboolean
inspector_attach_window_cb (WebKitWebInspector* inspector){
    (void) inspector;
    return FALSE;
}

gboolean
inspector_detach_window_cb (WebKitWebInspector* inspector){
    (void) inspector;
    return FALSE;
}

gboolean
inspector_uri_changed_cb (WebKitWebInspector* inspector){
    (void) inspector;
    return FALSE;
}

gboolean
inspector_inspector_destroyed_cb (WebKitWebInspector* inspector){
    (void) inspector;
    return FALSE;
}

void
set_up_inspector() {
    GUI *g = &reuzbl.gui;
    WebKitWebSettings *settings = view_settings();
    g_object_set(G_OBJECT(settings), "enable-developer-extras", TRUE, NULL);

    reuzbl.gui.inspector = webkit_web_view_get_inspector(reuzbl.gui.web_view);
    g_signal_connect (G_OBJECT (g->inspector), "inspect-web-view", G_CALLBACK (create_inspector_cb), NULL);
    g_signal_connect (G_OBJECT (g->inspector), "show-window", G_CALLBACK (inspector_show_window_cb), NULL);
    g_signal_connect (G_OBJECT (g->inspector), "close-window", G_CALLBACK (inspector_close_window_cb), NULL);
    g_signal_connect (G_OBJECT (g->inspector), "attach-window", G_CALLBACK (inspector_attach_window_cb), NULL);
    g_signal_connect (G_OBJECT (g->inspector), "detach-window", G_CALLBACK (inspector_detach_window_cb), NULL);
    g_signal_connect (G_OBJECT (g->inspector), "finished", G_CALLBACK (inspector_inspector_destroyed_cb), NULL);

    g_signal_connect (G_OBJECT (g->inspector), "notify::inspected-uri", G_CALLBACK (inspector_uri_changed_cb), NULL);
}

void
dump_var_hash(gpointer k, gpointer v, gpointer ud) {
    (void) ud;
    reuzbl_cmdprop *c = v;

    if(!c->dump)
        return;

    if(c->type == TYPE_STR)
        printf("set %s = %s\n", (char *)k, *c->ptr.s ? *c->ptr.s : " ");
    else if(c->type == TYPE_INT)
        printf("set %s = %d\n", (char *)k, *c->ptr.i);
    else if(c->type == TYPE_FLOAT)
        printf("set %s = %f\n", (char *)k, *c->ptr.f);
}

void
dump_key_hash(gpointer k, gpointer v, gpointer ud) {
    (void) ud;
    Action *a = v;

    printf("bind %s = %s %s\n", (char *)k ,
            (char *)a->name, a->param?(char *)a->param:"");
}

void
dump_config() {
    g_hash_table_foreach(reuzbl.comm.proto_var, dump_var_hash, NULL);
    g_hash_table_foreach(reuzbl.bindings, dump_key_hash, NULL);
    g_hash_table_foreach(reuzbl.handlers, dump_key_hash, NULL);
    g_hash_table_foreach(reuzbl.buttons, dump_key_hash, NULL);
}

void
retrieve_geometry() {
    int w, h, x, y;
    GString *buf = g_string_new("");

    gtk_window_get_size(GTK_WINDOW(reuzbl.gui.main_window), &w, &h);
    gtk_window_get_position(GTK_WINDOW(reuzbl.gui.main_window), &x, &y);

    g_string_printf(buf, "%dx%d+%d+%d", w, h, x, y);

    if(reuzbl.gui.geometry)
        g_free(reuzbl.gui.geometry);
    reuzbl.gui.geometry = g_string_free(buf, FALSE);
}

/* set up gtk, gobject, variable defaults and other things that tests and other
 * external applications need to do anyhow */
void
initialize(int argc, char *argv[]) {
    // apparently this is deprected and can just let out
    // if (!g_thread_supported ())
    //     g_thread_init (NULL);
    reuzbl.state.executable_path = g_strdup(argv[0]);
    reuzbl.state.selected_url = NULL;
    reuzbl.state.searchtx = NULL;

    GOptionContext* context = g_option_context_new ("[ uri ] - load a uri by default");
    g_option_context_add_main_entries (context, entries, NULL);
    g_option_context_add_group (context, gtk_get_option_group (TRUE));
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free(context);

    if (reuzbl.behave.print_version) {
        printf("Commit: %s\n", COMMIT);
        exit(EXIT_SUCCESS);
    }

    /* initialize hash table */
    reuzbl.bindings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_action);
    reuzbl.handlers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_action);
    reuzbl.buttons = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_action);

    reuzbl.net.soup_session = webkit_get_default_session();
    reuzbl.state.keycmd = g_strdup("");
    reuzbl.state.keycmd_pos = 0;

    if(setup_signal(SIGTERM, catch_sigterm) == SIG_ERR)
        fprintf(stderr, "reuzbl: error hooking SIGTERM\n");
    if(setup_signal(SIGINT, catch_sigint) == SIG_ERR)
        fprintf(stderr, "reuzbl: error hooking SIGINT\n");

    reuzbl.gui.sbar.progress_s = g_strdup("="); //TODO: move these to config.h
    reuzbl.gui.sbar.progress_u = g_strdup("·");
    reuzbl.gui.sbar.progress_w = 10;

    /* default mode indicators */
    reuzbl.behave.insert_indicator = g_strdup("I");
    reuzbl.behave.cmd_indicator    = g_strdup("C");

    reuzbl.info.webkit_major = WEBKIT_MAJOR_VERSION;
    reuzbl.info.webkit_minor = WEBKIT_MINOR_VERSION;
    reuzbl.info.webkit_micro = WEBKIT_MICRO_VERSION;
    reuzbl.info.arch         = ARCH;
    reuzbl.info.commit       = COMMIT;

    commands_hash ();
    create_var_to_name_hash();

    create_browser();
}

#ifndef REUZBL_LIBRARY
/** -- MAIN -- **/
int
main (int argc, char* argv[]) {
    initialize(argc, argv);

    gtk_init (&argc, &argv);

    reuzbl.gui.scrolled_win = gtk_scrolled_window_new (NULL, NULL);
    //main_window_ref = g_object_ref(scrolled_window);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (reuzbl.gui.scrolled_win),
        GTK_POLICY_NEVER, GTK_POLICY_NEVER); //todo: some sort of display of position/total length. like what emacs does

    gtk_container_add (GTK_CONTAINER (reuzbl.gui.scrolled_win),
        GTK_WIDGET (reuzbl.gui.web_view));

    reuzbl.gui.vbox = gtk_vbox_new (FALSE, 0);

    create_mainbar();

    /* initial packing */
    gtk_box_pack_start (GTK_BOX (reuzbl.gui.vbox), reuzbl.gui.scrolled_win, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (reuzbl.gui.vbox), reuzbl.gui.mainbar, FALSE, TRUE, 0);

    if (reuzbl.state.socket_id) {
        reuzbl.gui.plug = create_plug ();
        gtk_container_add (GTK_CONTAINER (reuzbl.gui.plug), reuzbl.gui.vbox);
        gtk_widget_show_all (GTK_WIDGET (reuzbl.gui.plug));
    } else {
        reuzbl.gui.main_window = create_window ();
        gtk_container_add (GTK_CONTAINER (reuzbl.gui.main_window), reuzbl.gui.vbox);
        gtk_widget_show_all (reuzbl.gui.main_window);
        reuzbl.xwin = GDK_WINDOW_XID (GTK_WIDGET (reuzbl.gui.main_window)->window);
    }

    if(!reuzbl.state.instance_name) {
        if(reuzbl.xwin)
            reuzbl.state.instance_name = itos((int)reuzbl.xwin);
        else
            reuzbl.state.instance_name = itos(getpid());
    }

    gtk_widget_grab_focus (GTK_WIDGET (reuzbl.gui.web_view));

    if (reuzbl.state.verbose) {
        printf("reUzbl start location: %s\n", argv[0]);
        if (reuzbl.state.socket_id)
            printf("plug_id %i\n", gtk_plug_get_id(reuzbl.gui.plug));
        else
            printf("window_id %i\n",(int) reuzbl.xwin);
        printf("pid %i\n", getpid ());
        printf("name: %s\n", reuzbl.state.instance_name);
        printf("commit: %s\n", reuzbl.info.commit);
    }

    reuzbl.gui.scbar_v = (GtkScrollbar*) gtk_vscrollbar_new (NULL);
    reuzbl.gui.bar_v = gtk_range_get_adjustment((GtkRange*) reuzbl.gui.scbar_v);
    reuzbl.gui.scbar_h = (GtkScrollbar*) gtk_hscrollbar_new (NULL);
    reuzbl.gui.bar_h = gtk_range_get_adjustment((GtkRange*) reuzbl.gui.scbar_h);
    gtk_widget_set_scroll_adjustments ((GtkWidget*) reuzbl.gui.web_view, reuzbl.gui.bar_h, reuzbl.gui.bar_v);

    /* Check reuzbl is in window mode before getting/setting geometry */
    if (reuzbl.gui.main_window) {
        if(reuzbl.gui.geometry)
            cmd_set_geometry();
        else
            retrieve_geometry();
    }

    gchar *uri_override = (reuzbl.state.uri ? g_strdup(reuzbl.state.uri) : NULL);
    if (argc > 1 && !reuzbl.state.uri)
        uri_override = g_strdup(argv[1]);
    gboolean verbose_override = reuzbl.state.verbose;

    settings_init ();

    if (!reuzbl.behave.always_insert_mode)
      set_insert_mode(FALSE);

    if (!reuzbl.behave.show_status)
        gtk_widget_hide(reuzbl.gui.mainbar);
    else
        update_title();

    /* WebInspector */
    set_up_inspector();

    if (verbose_override > reuzbl.state.verbose)
        reuzbl.state.verbose = verbose_override;

    if (uri_override) {
        set_var_value("uri", uri_override);
        g_free(uri_override);
    } else if (reuzbl.state.uri)
        cmd_load_uri();

    gtk_main ();
    clean_up();

    return EXIT_SUCCESS;
}
#endif

/* vi: set et ts=4: */

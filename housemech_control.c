/* HouseMech - a web server for home automation
 *
 * Copyright 2025, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * housemech_control.c - Interface with the control servers.
 *
 * SYNOPSYS:
 *
 * This module handles detection of, and communication with, the control
 * servers:
 * - Run periodic discoveries to find which server handles each control.
 * - Handle the HTTP control requests (and redirects).
 *
 * Each control is independent of each other: see the zone and feed
 * modules for the application logic that applies to controls.
 *
 * This module remembers which controls are active, so that it does not
 * have to stop every known control on cancel.
 *
 * int housemech_control_ready (void);
 *
 *    Return 1 is at least one control point is known, 0 otherwise.
 *    The purpose is to delay rules execution until at least one control
 *    service has been detected.
 *
 * int housemech_control_start (const char *name,
 *                              int pulse, const char *reason);
 *
 *    Activate one control for the duration set by pulse. The reason
 *    typically indicates what triggered this control.
 *
 *    If the named control is not known on any server, the request is ignored.
 *
 * void housemech_control_cancel (const char *name, const char *reason);
 *
 *    Immediately stop a control, or all active controls if name is null.
 *
 * const char *housemech_control_state (const char *name);
 *
 *    Return the current state of the specified control.
 *
 * void housemech_control_background (time_t now);
 *
 *    The periodic function that detects the control servers.
 *
 * int housemech_control_status (char *buffer, int size);
 *
 *    Return the status of control points in JSON format.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housediscover.h"

#include "housemech_rule.h"
#include "housemech_control.h"

#define DEBUG if (echttp_isdebug()) printf

static char **Providers = 0;
static int   ProvidersCount = 0;
static int   ProvidersAllocated = 0;

typedef struct {
    const char *name;
    char *state;
    char status;
    time_t deadline;
    char url[256];
} HouseControl;

static HouseControl *Controls = 0;
static int           ControlsCount = 0;
static int           ControlsSize = 0;

static int ControlsActive = 0;

static HouseControl *housemech_control_search (const char *name) {

    int i;
    for (i = 0; i < ControlsCount; ++i) {
        if (!strcmp (name, Controls[i].name)) return Controls + i;
    }

    // This control was never seen before.

    if (ControlsCount >= ControlsSize) {
        ControlsSize += 32;
        Controls = realloc (Controls, ControlsSize*sizeof(HouseControl));
        if (!Controls) {
            houselog_trace (HOUSE_FAILURE, name, "no more memory");
            exit (1);
        }
    }
    i = ControlsCount++;
    Controls[i].name = strdup(name);
    Controls[i].state = 0;
    Controls[i].status = 'u';
    Controls[i].deadline = 0;
    Controls[i].url[0] = 0; // Need to (re)learn.

    return Controls + i;
}

static ParserToken *housemech_control_prepare (int count) {

    static ParserToken *EventTokens = 0;
    static int EventTokensAllocated = 0;

    if (count > EventTokensAllocated) {
        int need = EventTokensAllocated = count + 128;
        EventTokens = realloc (EventTokens, need*sizeof(ParserToken));
    }
    return EventTokens;
}

static void housemech_control_update (const char *provider,
                                      char *data, int length) {

   int  innerlist[256];
   char path[256];
   int  i;

   int count = echttp_json_estimate(data);
   ParserToken *tokens = housemech_control_prepare (count);

   const char *error = echttp_json_parse (data, tokens, &count);
   if (error) {
       houselog_trace
           (HOUSE_FAILURE, provider, "JSON syntax error, %s", error);
       return;
   }
   if (count <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no data");
       return;
   }

   int controls = echttp_json_search (tokens, ".control.status");
   if (controls <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no control data");
       return;
   }

   int n = tokens[controls].length;
   if (n <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "empty control data");
       return;
   }

   error = echttp_json_enumerate (tokens+controls, innerlist);
   if (error) {
       houselog_trace (HOUSE_FAILURE, path, "%s", error);
       return;
   }

   for (i = 0; i < n; ++i) {
       ParserToken *inner = tokens + controls + innerlist[i];
       HouseControl *control = housemech_control_search (inner->key);
       if (strcmp (control->url, provider)) {
           snprintf (control->url, sizeof(control->url), provider);
           control->status = 'i';
           houselog_event_local
               ("CONTROL", control->name, "ROUTE", "TO %s", control->url);
       }
       int stateidx = echttp_json_search (inner, ".state");
       if (stateidx > 0) {
           char *state = inner[stateidx].value.string;
           DEBUG ("Received point %s with state %s (previous: %s)\n", control->name, state, control->state?control->state:"unknown");
           if (control->state) {
               if (strcmp (state, control->state)) {
                   housemech_rule_trigger_control (control->name, state);
                   free (control->state);
                   control->state = strdup (state);
               }
           } else {
               control->state = strdup (state);
           }
       }
   }
}

static void housemech_control_result
               (void *origin, int status, char *data, int length) {

   HouseControl *control = (HouseControl *)origin;

   status = echttp_redirected("GET");
   if (!status) {
       echttp_submit (0, 0, housemech_control_result, origin);
       return;
   }

   if (status != 200) {
       if (control->status != 'e')
           houselog_trace (HOUSE_FAILURE, control->name, "HTTP code %d", status);
       control->status  = 'e';
       control->deadline  = 0;
   }
   housemech_control_update (control->url, data, length);
}

static const char *housemech_printable_period (int h, const char *hlabel,
                                           int l, const char *llabel) {
    static char Printable[128];
    if (l > 0) {
        snprintf (Printable, sizeof(Printable),
                  "%d %s%s, %d %s%s", h, hlabel, (h>1)?"S":"",
                                      l, llabel, (l>1)?"S":"");
    } else {
        snprintf (Printable, sizeof(Printable),
                  "%d %s%s", h, hlabel, (h>1)?"S":"");
    }
    return Printable;
}

static const char *housemech_printable_duration (int duration) {

    if (duration <= 0) return "NOW";
    if (duration > 86400) {
        return housemech_printable_period (duration / 86400, "DAY",
                                           (duration % 86400) / 3600, "HOUR");
    } else if (duration > 3600) {
        return housemech_printable_period (duration / 3600, "HOUR",
                                           (duration % 3600) / 60, "MINUTE");
    } else if (duration > 60) {
        return housemech_printable_period (duration / 60, "MINUTE",
                                           duration % 60, "SECOND");
    }
    return housemech_printable_period (duration, "SECOND", 0, "");
}

int housemech_control_ready (void) {
    return ControlsCount > 0;
}

static const char *housemech_control_cause (const char * reason) {
    static char Cause[256];
    if (reason) {
        snprintf (Cause, sizeof(Cause), "&cause=");
        int l = strlen(Cause);
        echttp_escape (reason, Cause+l, sizeof(Cause)-l);
    } else
        Cause[0] = 0;
    return Cause;
}

int housemech_control_start (const char *name, int pulse, const char *reason) {

    time_t now = time(0);
    DEBUG ("%ld: Start %s for %d seconds\n", now, name, pulse);

    HouseControl *control = housemech_control_search (name);
    if (! control->url[0]) {
        houselog_event ("CONTROL", name, "UNKNOWN", "");
        return 0;
    }

    if (!reason) reason = "";
    if (pulse) {
        houselog_event ("CONTROL", name, "ACTIVATED",
                        "FOR %s USING %s (%s)",
                        housemech_printable_duration (pulse),
                        control->url, reason);
    } else {
        houselog_event ("CONTROL", name, "ACTIVATED",
                        "USING %s (%s)", control->url, reason);
    }

    static char url[600];
    snprintf (url, sizeof(url),
              "%s/set?point=%s&state=on&pulse=%d%s",
              control->url, name, pulse, housemech_control_cause(reason));
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, name, "cannot create socket for %s, %s", url, error);
        return 0;
    }
    DEBUG ("GET %s\n", url);
    echttp_submit (0, 0, housemech_control_result, (void *)control);
    if (pulse > 0)
        control->deadline = now + pulse;
    control->status = 'a';
    ControlsActive = 1;
    return 1;
}

static void housemech_control_stop (HouseControl *control, const char *reason) {

    if (! control->url[0]) return;

    static char url[600];
    snprintf (url, sizeof(url),
              "%s/set?point=%s&state=off%s",
              control->url, control->name, housemech_control_cause(reason));
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, control->name, "cannot create socket for %s, %s", url, error);
        return;
    }
    DEBUG ("GET %s\n", url);
    echttp_submit (0, 0, housemech_control_result, (void *)control);
    control->status  = 'i';
}

void housemech_control_cancel (const char *name, const char *reason) {

    int i;
    time_t now = time(0);

    if (name) {
        DEBUG ("Trying to cancel point %s\n", name);
        HouseControl *control = housemech_control_search (name);
        if (control->url[0]) {
            DEBUG ("Canceling point %s\n", name);
            houselog_event ("CONTROL", name, "CANCEL",
                            "USING %s (%s)", control->url, reason?reason:"");
            housemech_control_stop (control, reason);
            control->deadline = 0;
        }
        return;
    }
    DEBUG ("%ld: Cancel all zones and feeds\n", now);
    for (i = 0; i < ControlsCount; ++i) {
        if (Controls[i].deadline) {
            housemech_control_stop ( Controls + i, reason);
            Controls[i].deadline = 0;
        }
    }
    ControlsActive = 0;
}

const char *housemech_control_state (const char *name) {
    HouseControl *control = housemech_control_search (name);
    if (! control->state) return "";
    return control->state;
}

static void housemech_control_discovered
               (void *origin, int status, char *data, int length) {

   const char *provider = (const char *) origin;

   status = echttp_redirected("GET");
   if (!status) {
       echttp_submit (0, 0, housemech_control_discovered, origin);
       return;
   }

   if (status != 200) {
       houselog_trace (HOUSE_FAILURE, provider, "HTTP error %d", status);
       return;
   }

   housemech_control_update (provider, data, length);
}

static void housemech_control_scan_server
                (const char *service, void *context, const char *provider) {

    char url[256];

    if (ProvidersCount >= ProvidersAllocated) {
        ProvidersAllocated += 64;
        Providers = realloc (Providers, ProvidersAllocated*(sizeof(char *)));
    }
    Providers[ProvidersCount++] = strdup(provider); // Keep the string.

    snprintf (url, sizeof(url), "%s/status", provider);

    DEBUG ("Attempting discovery at %s\n", url);
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, provider, "%s", error);
        return;
    }
    echttp_submit (0, 0, housemech_control_discovered, (void *)provider);
}

static void housemech_control_discover (time_t now) {

    static time_t latestdiscovery = 0;

    if (!now) { // This is a manual reset (force a discovery refresh)
        latestdiscovery = 0;
        return;
    }

    // If any new service was detected, force a scan now.
    //
    if ((latestdiscovery > 0) &&
        housediscover_changed ("control", latestdiscovery)) {
        latestdiscovery = 0;
    }

    // Even if nothing new was detected, still scan every few seconds, in case
    // the configuration of a service or the state of a control point changed.
    //
    if (now <= latestdiscovery + 2) return;
    latestdiscovery = now;

    // Rebuild the list of control servers, and then launch a discovery
    // refresh. This way we don't walk a stale cache while doing discovery.
    //
    DEBUG ("Reset providers cache\n");
    int i;
    for (i = 0; i < ProvidersCount; ++i) {
        if (Providers[i]) free(Providers[i]);
        Providers[i] = 0;
    }
    ProvidersCount = 0;
    DEBUG ("Proceeding with discovery\n");
    housediscovered ("control", 0, housemech_control_scan_server);
}

void housemech_control_background (time_t now) {

    if (ControlsActive) {
        ControlsActive = 0;
        int i;
        for (i = 0; i < ControlsCount; ++i) {
            if (Controls[i].deadline) {
                if (Controls[i].deadline < now) {
                    // No request: it automatically stops on end of pulse.
                    Controls[i].deadline = 0;
                    Controls[i].status  = 'i';
                } else {
                    ControlsActive = 1;
                }
            }
        }
    }
    housemech_control_discover (now);
}

int housemech_control_status (char *buffer, int size) {

    int i;
    int cursor = 0;
    const char *prefix = "";

    cursor = snprintf (buffer, size, "\"servers\":[");
    if (cursor >= size) goto overflow;

    for (i = 0; i < ProvidersCount; ++i) {
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s\"%s\"", prefix, Providers[i]);
        if (cursor >= size) goto overflow;
        prefix = ",";
    }
    cursor += snprintf (buffer+cursor, size-cursor, "]");
    if (cursor >= size) goto overflow;

    cursor += snprintf (buffer+cursor, size-cursor, ",\"controls\":[");
    if (cursor >= size) goto overflow;
    prefix = "";

    time_t now = time(0);

    for (i = 0; i < ControlsCount; ++i) {
        int remaining =
            (Controls[i].status == 'a')?(int)(Controls[i].deadline - now):0;
        cursor += snprintf (buffer+cursor, size-cursor, "%s[\"%s\",\"%c\",\"%s\",%d]",
                            prefix, Controls[i].name, Controls[i].status, Controls[i].url, remaining);
        if (cursor >= size) goto overflow;
        prefix = ",";
    }

    cursor += snprintf (buffer+cursor, size-cursor, "]");
    if (cursor >= size) goto overflow;

    return cursor;

overflow:
    houselog_trace (HOUSE_FAILURE, "STATUS",
                    "BUFFER TOO SMALL (NEED %d bytes)", cursor);
    buffer[0] = 0;
    return 0;
}

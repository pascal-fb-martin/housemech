/* HouseMech - a web server for home automation
 *
 * Copyright 2024, Pascal Martin
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
 * housemech_event.c - Fetch new events from HouseSaga.
 *
 * SYNOPSYS:
 *
 * void housemech_event_initialize (int argc, const char **argv);
 *
 *    Initialize this module.
 *
 * void housemech_event_background (time_t now);
 *
 *    The periodic function that manages the collect of metrics.
 *
 * int housemech_event_status (char *buffer, int size);
 *
 *    A function that populates the status of this module in JSON.
 *
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housediscover.h"
#include "housemech_rule.h"
#include "housemech_control.h"

#define DEBUG if (echttp_isdebug()) printf

#define HOUSE_EVENT_CYCLE 2

static long long HouseMechEventLatestTime = 0;

static long long HouseMechLatestId;
static char *    HouseMechCurrentServer = 0;


void housemech_event_initialize (int argc, const char **argv) {

    if (HouseMechEventLatestTime <= 0) {
        // Ignore old events, only look forward. Otherwise we would
        // refetch and reprocess all pre-existing events on restart.
        HouseMechEventLatestTime = (long long)time(0) * 1000;
    }
}

int housemech_event_status (char *buffer, int size) {

    return 0; // TBD
}

static ParserToken *housemech_event_prepare (int count) {

    static ParserToken *EventTokens = 0;
    static int EventTokensAllocated = 0;

    if (count > EventTokensAllocated) {
        int need = EventTokensAllocated = count + 128;
        EventTokens = realloc (EventTokens, need*sizeof(ParserToken));
    }
    return EventTokens;
}

static void housemech_event_response
                (void *origin, int status, char *data, int length) {

    const char *provider = (const char *)origin;

    if (HouseMechCurrentServer && strcmp (provider, HouseMechCurrentServer))
        return; // Not the server that this service is locked on.

    status = echttp_redirected("GET");
    if (!status) {
        echttp_submit (0, 0, housemech_event_response, origin);
        return;
    }

    if (status != 200) {
        houselog_trace (HOUSE_FAILURE, provider, "HTTP code %d", status);
        goto failure;
    }

    int count = echttp_json_estimate(data);
    ParserToken *tokens = housemech_event_prepare (count);

    const char *error = echttp_json_parse (data, tokens, &count);
    if (error) {
        houselog_trace (HOUSE_FAILURE, provider, "syntax error, %s", error);
        goto failure;
    }
    if (count <= 0) {
        houselog_trace (HOUSE_FAILURE, provider, "no data");
        goto failure;
    }

    int server = echttp_json_search (tokens, ".host");
    if (server < 0) {
        houselog_trace (HOUSE_FAILURE, provider, "No host name");
        goto failure;
    }
    const char *servername = tokens[server].value.string;

    int latest = echttp_json_search (tokens, ".saga.latest");
    if (latest < 0) {
        houselog_trace (HOUSE_FAILURE, provider, "No latest ID");
        goto failure;
    }

    if (!HouseMechCurrentServer) {
        // Lock on this new provider that seems to be working OK.
        HouseMechCurrentServer = strdup (provider);
        HouseMechLatestId = 0;
    }

    int events = echttp_json_search (tokens, ".saga.events");
    int n = tokens[events].length;

    if (n > 0) {
        long long latesttime = 0;

        int *list = calloc (n, sizeof(int));
        const char *error = echttp_json_enumerate (tokens+events, list);
        if (!error) {
            int i;
            for (i = n - 1; i >= 0; --i) {
                ParserToken *inner = tokens + events + list[i];
                if (inner->type != PARSER_ARRAY) continue;

                // Avoid processing the same event multiple times.
                // The event ID is always incrementing, even when the
                // event times are out of sequence (which should be rare).
                //
                int ididx = echttp_json_search (inner, "[7]");
                long long id = inner[ididx].value.integer;
                if (id <= HouseMechLatestId) continue;
                HouseMechLatestId = id;

                int timestampidx = echttp_json_search (inner, "[0]");
                long long timestamp = inner[timestampidx].value.integer;
                int categoryidx = echttp_json_search (inner, "[1]");
                const char *category = inner[categoryidx].value.string;
                int nameidx = echttp_json_search (inner, "[2]");
                const char *name = inner[nameidx].value.string;
                int actionidx = echttp_json_search (inner, "[3]");
                const char *action = inner[actionidx].value.string;

                housemech_rule_trigger_event (category, name, action);
                if (timestamp > latesttime) latesttime = timestamp;
            }
        }
        // Move the since parameter forward, but be lenient in the case
        // events are listed out of order. (Rare, but could happen.)
        if (latesttime - 5 > HouseMechEventLatestTime) {
            HouseMechEventLatestTime = latesttime - 5;
        }
        free (list);
    }
    DEBUG ("New latest processed event ID %lld from %s\n",
           HouseMechLatestId, provider);

    return;

failure:

    if (HouseMechCurrentServer) {
        free (HouseMechCurrentServer);
        HouseMechCurrentServer = 0; // Force locking on a new server.
    }
}

static void housemech_event_check_response
                (void *origin, int status, char *data, int length) {

    const char *provider = (const char *)origin;
    if (HouseMechCurrentServer && strcmp (provider, HouseMechCurrentServer))
        return; // Not the source that this service is locked on.

    status = echttp_redirected("GET");
    if (!status) {
        echttp_submit (0, 0, housemech_event_check_response, origin);
        return;
    }

    if (status != 200) {
        houselog_trace (HOUSE_FAILURE, provider, "HTTP code %d", status);
        goto failure;
    }

    int count = echttp_json_estimate(data);
    ParserToken *tokens = housemech_event_prepare (count);

    const char *error = echttp_json_parse (data, tokens, &count);
    if (error) {
        houselog_trace (HOUSE_FAILURE, provider, "syntax error, %s", error);
        goto failure;
    }
    if (count <= 0) {
        houselog_trace (HOUSE_FAILURE, provider, "no data");
        goto failure;
    }

    int server = echttp_json_search (tokens, ".host");
    if (server < 0) {
        houselog_trace (HOUSE_FAILURE, provider, "No host name");
        goto failure;
    }
    const char *servername = tokens[server].value.string;

    int latest = echttp_json_search (tokens, ".saga.latest");
    if (latest < 0) {
        houselog_trace (HOUSE_FAILURE, provider, "No latest ID");
        return;
    }
    long long latestvalue = tokens[latest].value.integer;

    // Got all the data needed to make decisions.
    //
    if (!HouseMechCurrentServer) {
        DEBUG ("Trying new event source %s\n", provider);
    } else {
        if (HouseMechLatestId == latestvalue) return; // No change.
        DEBUG ("Detected new events from %s\n", HouseMechCurrentServer);
    }
    if ((! housemech_rule_ready()) || (! housemech_control_ready())) {
        DEBUG ("Not ready for processing new events yet.\n");
        return;
    }

    char url[1024];
    snprintf (url, sizeof(url), "%s/log/events?since=%lld",
              provider, HouseMechEventLatestTime);

    error = echttp_client ("GET", url);
    if (error) return;

    echttp_submit (0, 0, housemech_event_response, (void *)provider);
    return;

failure:

    if (HouseMechCurrentServer) {
        free (HouseMechCurrentServer);
        HouseMechCurrentServer = 0; // Will force locking on a new server.
    }
}

static int HouseMechRequestCount = 0;

static void housemech_event_check
                (const char *service, void *context, const char *provider) {

    if (HouseMechCurrentServer && strcmp (provider, HouseMechCurrentServer))
        return;

    char url[1024];
    snprintf (url, sizeof(url), "%s/log/latest", provider);

    const char *error = echttp_client ("GET", url);
    if (error) {
        if (HouseMechCurrentServer) {
            free (HouseMechCurrentServer);
            HouseMechCurrentServer = 0;
        }
        return;
    }

    echttp_submit (0, 0, housemech_event_check_response, (void *)provider);
    HouseMechRequestCount += 1;
}

void housemech_event_background (time_t now) {

    static time_t NextEventCycle = 0;

    if (now < NextEventCycle) return;
    NextEventCycle = now + HOUSE_EVENT_CYCLE;

    HouseMechRequestCount = 0;
    housediscovered ("history", 0, housemech_event_check);

    if (HouseMechRequestCount == 0) {
        if (HouseMechCurrentServer) {
            // The server this is locked on is no longer operating.
            free (HouseMechCurrentServer);
            HouseMechCurrentServer = 0; // Will force locking on a new server.
        }
    }
}


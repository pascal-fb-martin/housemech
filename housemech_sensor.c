/* HouseMech - A simple home web service to automate actions.
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
 * housemech_sensor.c - Fetch new sensor data from HouseSaga.
 *
 * SYNOPSYS:
 *
 * void housemech_sensor_initialize (int argc, const char **argv);
 *
 *    Initialize this module.
 *
 * void housemech_sensor_background (time_t now);
 *
 *    The periodic function that manages the collect of metrics.
 *
 * int housemech_sensor_status (char *buffer, int size);
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

#include "housemech_sensor.h"

#define DEBUG if (echttp_isdebug()) printf

#define HOUSE_SENSOR_CYCLE 2

static long long HouseMechSensorLatestTime = 0;

static long long HouseMechLatestId;
static char *    HouseMechCurrentServer = 0;


void housemech_sensor_initialize (int argc, const char **argv) {

    if (HouseMechSensorLatestTime <= 0) {
        // Ignore old data, only look forward. Otherwise we would
        // refetch and reprocess all pre-existing data on restart.
        HouseMechSensorLatestTime = (long long)time(0) * 1000;
    }
}

int housemech_sensor_status (char *buffer, int size) {

    return 0; // TBD
}

static ParserToken *housemech_sensor_prepare (int count) {

    static ParserToken *SensorTokens = 0;
    static int SensorTokensAllocated = 0;

    if (count > SensorTokensAllocated) {
        int need = SensorTokensAllocated = count + 128;
        SensorTokens = realloc (SensorTokens, need*sizeof(ParserToken));
    }
    return SensorTokens;
}

static void housemech_sensor_response
                (void *origin, int status, char *data, int length) {

    const char *provider = (const char *)origin;

    if (HouseMechCurrentServer && strcmp (provider, HouseMechCurrentServer))
        return; // Not the server that this service is locked on.

    status = echttp_redirected("GET");
    if (!status) {
        echttp_submit (0, 0, housemech_sensor_response, origin);
        return;
    }

    if (status != 200) {
        houselog_trace (HOUSE_FAILURE, provider, "HTTP code %d", status);
        goto failure;
    }

    int count = echttp_json_estimate(data);
    ParserToken *tokens = housemech_sensor_prepare (count);

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

    int Sensors = echttp_json_search (tokens, ".saga.sensor");
    int n = tokens[Sensors].length;

    if (n > 0) {
        long long latesttime = 0;

        int *list = calloc (n, sizeof(int));
        const char *error = echttp_json_enumerate (tokens+Sensors, list);
        if (!error) {
            int i;
            for (i = n - 1; i >= 0; --i) {
                ParserToken *inner = tokens + Sensors + list[i];
                if (inner->type != PARSER_ARRAY) continue;

                // Avoid processing the same sensor data multiple times.
                // The ID is always incrementing, even when the
                // timestamps are out of sequence (which should be rare).
                //
                int ididx = echttp_json_search (inner, "[7]");
                long long id = inner[ididx].value.integer;
                if (id <= HouseMechLatestId) continue;
                HouseMechLatestId = id;

                int timestampidx = echttp_json_search (inner, "[0]");
                long long timestamp = inner[timestampidx].value.integer;
                int locationidx = echttp_json_search (inner, "[1]");
                const char *location = inner[locationidx].value.string;
                int nameidx = echttp_json_search (inner, "[2]");
                const char *name = inner[nameidx].value.string;
                int valueidx = echttp_json_search (inner, "[3]");
                const char *value = inner[valueidx].value.string;

                housemech_rule_trigger_sensor (location, name, value);
                if (timestamp > latesttime) latesttime = timestamp;
            }
        }
        // Move the since parameter forward, but be lenient in the case
        // Sensor data is listed out of order. (Rare, but could happen.)
        if (latesttime - 5 > HouseMechSensorLatestTime) {
            HouseMechSensorLatestTime = latesttime - 5;
        }
        free (list);
    }
    DEBUG ("New latest processed sensor data ID %lld from %s\n",
           HouseMechLatestId, provider);

    return;

failure:

    if (HouseMechCurrentServer) {
        free (HouseMechCurrentServer);
        HouseMechCurrentServer = 0; // Force locking on a new server.
    }
}

static void housemech_sensor_check_response
                (void *origin, int status, char *data, int length) {

    const char *provider = (const char *)origin;
    if (HouseMechCurrentServer && strcmp (provider, HouseMechCurrentServer))
        return; // Not the source that this service is locked on.

    status = echttp_redirected("GET");
    if (!status) {
        echttp_submit (0, 0, housemech_sensor_check_response, origin);
        return;
    }

    if (status != 200) {
        houselog_trace (HOUSE_FAILURE, provider, "HTTP code %d", status);
        goto failure;
    }

    int count = echttp_json_estimate(data);
    ParserToken *tokens = housemech_sensor_prepare (count);

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

    int latest = echttp_json_search (tokens, ".saga.latest");
    if (latest < 0) {
        houselog_trace (HOUSE_FAILURE, provider, "No latest ID");
        return;
    }
    long long latestvalue = tokens[latest].value.integer;

    // Got all the data needed to make decisions.
    //
    if (!HouseMechCurrentServer) {
        DEBUG ("Trying new sensor data source %s\n", provider);
    } else {
        if (HouseMechLatestId == latestvalue) return; // No change.
        if (HouseMechLatestId > latestvalue) {
            // This should never happen, except if the server restarted.
            // In that case, look at everything: this is all new.
            HouseMechLatestId = 0;
        }
        DEBUG ("Detected new sensor data from %s\n", HouseMechCurrentServer);
    }
    if ((! housemech_rule_ready()) || (! housemech_control_ready())) {
        DEBUG ("Not ready for processing new sensor data yet.\n");
        return;
    }

    char url[1024];
    snprintf (url, sizeof(url), "%s/log/sensor/data?since=%lld",
              provider, HouseMechSensorLatestTime);

    error = echttp_client ("GET", url);
    if (error) return;

    echttp_submit (0, 0, housemech_sensor_response, (void *)provider);
    return;

failure:

    if (HouseMechCurrentServer) {
        free (HouseMechCurrentServer);
        HouseMechCurrentServer = 0; // Will force locking on a new server.
    }
}

static int HouseMechRequestCount = 0;

static void housemech_sensor_check
                (const char *service, void *context, const char *provider) {

    if (HouseMechCurrentServer && strcmp (provider, HouseMechCurrentServer))
        return;

    char url[1024];
    snprintf (url, sizeof(url), "%s/log/sensor/latest", provider);

    const char *error = echttp_client ("GET", url);
    if (error) {
        if (HouseMechCurrentServer) {
            free (HouseMechCurrentServer);
            HouseMechCurrentServer = 0;
        }
        return;
    }

    echttp_submit (0, 0, housemech_sensor_check_response, (void *)provider);
    HouseMechRequestCount += 1;
}

void housemech_sensor_background (time_t now) {

    static time_t NextSensorCycle = 0;

    if (now < NextSensorCycle) return;
    NextSensorCycle = now + HOUSE_SENSOR_CYCLE;

    HouseMechRequestCount = 0;
    housediscovered ("history", 0, housemech_sensor_check);

    if (HouseMechRequestCount == 0) {
        if (HouseMechCurrentServer) {
            // The server this is locked on is no longer operating.
            free (HouseMechCurrentServer);
            HouseMechCurrentServer = 0; // Will force locking on a new server.
        }
    }
}


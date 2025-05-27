/* HouseMech - A simple home web service to automate actions.
 *
 * Copyright 2020, Pascal Martin
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
 * housemech.c - the main module for the home automation service.
 */

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include "echttp.h"
#include "echttp_static.h"
#include "houseportalclient.h"

#include "housediscover.h"
#include "housedepositor.h"
#include "houselog.h"
#include "houselog_sensor.h"
#include "housealmanac.h"

#include "housemech_event.h"
#include "housemech_rule.h"
#include "housemech_control.h"

static int Debug = 0;

#define DEBUG if (Debug) printf


static const char *housemech_status (const char *method, const char *uri,
                                      const char *data, int length) {
    static char buffer[65537];
    static char host[256];

    int cursor;

    if (host[0] == 0) gethostname (host, sizeof(host));

    cursor = snprintf (buffer, sizeof(buffer),
                       "{\"host\":\"%s\",\"proxy\":\"%s\",\"timestamp\":%lld",
                       host, houseportal_server(), (long long)time(0));

    cursor += housemech_event_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += housemech_rule_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += housealmanac_status (buffer+cursor, sizeof(buffer)-cursor);

    snprintf (buffer+cursor, sizeof(buffer)-cursor, "}");

    echttp_content_type_json ();
    return buffer;
}

static const char *housemech_set (const char *method, const char *uri,
                                   const char *data, int length) {
    // TBD
    return housemech_status (method, uri, data, length);
}

static void housemech_background (int fd, int mode) {

    static time_t LastCall = 0;
    time_t now = time(0);

    if (now == LastCall) return;
    LastCall = now;

    if (echttp_dynamic_port()) {
        static time_t Renewed = 0;
        if (Renewed) {
            if (now > Renewed + 60) {
                houseportal_renew();
                Renewed = now;
            }
        } else if (now % 5 == 0) {
            static const char *path[] = {"mech:/mech"};
            houseportal_register (echttp_port(4), path, 1);
            Renewed = now;
        }
    }

    housediscover (now);
    houselog_background (now);
    housedepositor_periodic (now);

    housemech_event_background (now);
    housemech_control_background (now);
    housealmanac_background (now);
    housemech_rule_background (now);
}

int main (int argc, const char **argv) {

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    int i;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_present ("-d", argv[i])) {
            Debug = 1;
            continue;
        }
    }
    echttp_default ("-http-service=dynamic");
    echttp_static_default ("-http-root=/usr/local/share/house/public");

    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port())
        houseportal_initialize (argc, argv);
    echttp_static_initialize (argc, argv);

    housediscover_initialize (argc, argv);
    houselog_initialize ("mech", argc, argv);
    housedepositor_initialize (argc, argv);

    housealmanac_tonight_ready (); // Tell we want to fetch the "tonight" set.

    housemech_rule_initialize (argc, argv);
    housemech_event_initialize (argc, argv);

    echttp_route_uri ("/mech/set", housemech_set);
    echttp_route_uri ("/mech/status", housemech_status);
    echttp_background (&housemech_background);
    echttp_loop();
}


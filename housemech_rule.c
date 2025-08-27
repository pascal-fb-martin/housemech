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
 * housemech_rule.c - Manage the environment for automation scripts.
 *
 * This implementation uses Tcl scripts.
 *
 * SYNOPSYS:
 *
 * void housemech_rule_initialize (int argc, const char **argv);
 *
 *    Initialize this module.
 *
 * void housemech_rule_background (time_t now);
 *
 *    The periodic function that manages the collect of metrics.
 *
 * int housemech_rule_status (char *buffer, int size);
 *
 *    A function that populates the status of this module in JSON.
 *
 * int housemech_rule_ready (void);
 *
 *    Return 1 if ready to apply rule, 0 otherwise.
 *
 * int housemech_rule_trigger_event
 *        (const char *category, const char *name, const char *action);
 *
 * int housemech_rule_trigger_sensor
 *        (const char *location, const char *name, const char *value);
 *
 * int housemech_rule_trigger_control (const char *name, const char *state);
 *
 *    Process all the rule matching the specified change.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <tcl.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housedepositor.h"
#include "housealmanac.h"

#include "housemech_control.h"
#include "housemech_rule.h"

#define DEBUG if (echttp_isdebug()) printf

#define HOUSE_TCL_CYCLE      1

static int HouseMechReady = 0;

static Tcl_Interp *HouseMechInterpreter = 0;

static const char *HouseMechBoot = "/usr/local/share/house/mech/bootstrap.tcl";
static const char *HouseMechScript = "mechrules.tcl";

static int housemech_rule_event_cmd (ClientData clientData,
                                     Tcl_Interp *interp,
                                     int objc,
                                     Tcl_Obj *const objv[]) {

    if (objc < 4) {
        Tcl_SetResult (interp, "missing parameters", TCL_STATIC);
        return TCL_ERROR;
    }
    const char *category = Tcl_GetString (objv[1]);
    const char *name = Tcl_GetString (objv[2]);
    const char *action = Tcl_GetString (objv[3]);
    const char *text = (objc > 4) ? Tcl_GetString (objv[4]) : "";

    houselog_event (category, name, action, text);
    return TCL_OK;
}

static int housemech_rule_control_cmd (ClientData clientData,
                                       Tcl_Interp *interp,
                                       int objc,
                                       Tcl_Obj *const objv[]) {

    const char *reason = "HOUSEMECH TRIGGER";

    if (objc < 3) {
        Tcl_SetResult (interp, "missing parameters", TCL_STATIC);
        return TCL_ERROR;
    }
    const char *cmd = Tcl_GetString (objv[1]);
    const char *name = Tcl_GetString (objv[2]);

    if ((!cmd) || (!name)) {
        Tcl_SetResult (interp, "invalid parameters", TCL_STATIC);
        return TCL_ERROR;
    }

    if (!strcmp ("start", cmd)) {
        int pulse = 0;
        if (objc >= 4) {
            if (Tcl_GetIntFromObj (interp, objv[3], &pulse) != TCL_OK) {
                DEBUG ("Invalid pulse %s: %s\n", Tcl_GetString (objv[3]), Tcl_GetStringResult(interp));
                Tcl_SetResult (interp, "invalid pulse", TCL_STATIC);
                return TCL_ERROR;
            }
            if (pulse < 0) {
                Tcl_SetResult (interp, "invalid pulse range", TCL_STATIC);
                return TCL_ERROR;
            }
            if (objc >= 5) {
                const char *userreason = Tcl_GetString (objv[4]);
                if (userreason) reason = userreason;
            }
        }
        if (!housemech_control_start (name, pulse, reason)) {
            Tcl_SetResult (interp, "control failure", TCL_STATIC);
            return TCL_ERROR;
        }
    } else if (!strcmp ("cancel", cmd)) {
        if (objc >= 4) {
            const char *userreason = Tcl_GetString (objv[3]);
            if (userreason) reason = userreason;
        }
        housemech_control_cancel (name, reason);
    } else if (!strcmp ("state", cmd)) {
        Tcl_SetResult (interp, (char *)housemech_control_state (name), TCL_VOLATILE);
    } else {
        Tcl_SetResult (interp, "invalid subcommand", TCL_STATIC);
        return TCL_ERROR;
    }
    return TCL_OK;
}

static int housemech_rule_sunset_cmd (ClientData clientData,
                                      Tcl_Interp *interp,
                                      int objc,
                                      Tcl_Obj *const objv[]) {

    Tcl_SetObjResult (interp, Tcl_NewWideIntObj (housealmanac_tonight_sunset()));
    return TCL_OK;
}

static int housemech_rule_sunrise_cmd (ClientData clientData,
                                       Tcl_Interp *interp,
                                       int objc,
                                       Tcl_Obj *const objv[]) {

    Tcl_SetObjResult (interp, Tcl_NewWideIntObj (housealmanac_tonight_sunrise()));
    return TCL_OK;
}

static void housemech_rule_listener (const char *name, time_t timestamp,
                                      const char *data, int length) {

    houselog_event ("SCRIPT", HouseMechScript, "LOAD", "FROM DEPOT %s", name);
    Tcl_Eval (HouseMechInterpreter, data);
    HouseMechReady = 1;
}

void housemech_rule_initialize (int argc, const char **argv) {

    Tcl_FindExecutable (argv[0]);
    HouseMechInterpreter = Tcl_CreateInterp();
    if (Tcl_Init(HouseMechInterpreter) != TCL_OK) {
        DEBUG ("Cannot create the Tcl interpeter.\n");
        exit(1);
    }
    if (Tcl_EvalFile (HouseMechInterpreter, HouseMechBoot) != TCL_OK) {
        DEBUG ("Cannot load %s: %s\n",
                HouseMechBoot, Tcl_GetStringResult(HouseMechInterpreter));
        exit(1);
    }

    Tcl_CreateObjCommand (HouseMechInterpreter,
                 "House::control", housemech_rule_control_cmd, 0, 0);

    Tcl_CreateObjCommand (HouseMechInterpreter,
                 "House::nativeevent", housemech_rule_event_cmd, 0, 0);

    Tcl_CreateObjCommand (HouseMechInterpreter,
                 "House::sunset", housemech_rule_sunset_cmd, 0, 0);

    Tcl_CreateObjCommand (HouseMechInterpreter,
                 "House::sunrise", housemech_rule_sunrise_cmd, 0, 0);

    housedepositor_subscribe
        ("scripts", HouseMechScript, housemech_rule_listener);
}

int housemech_rule_status (char *buffer, int size) {

    return 0; // TBD
}

int housemech_rule_ready (void) {
    return HouseMechReady && housealmanac_tonight_ready();
}

int housemech_rule_trigger_event
        (const char *category, const char *name, const char *action) {

    char buffer[256];

    // Record the latest action for this specific event.
    if (action) {
        snprintf (buffer, sizeof(buffer),
                  "House::event state {%s} {%s} {%s}", category, name, action);
        Tcl_Eval (HouseMechInterpreter, buffer);
    } else {
        action = "";
    }

    // Try to process the rules for this event in the following order
    // until one is successful:
    // <category>.<name>.<action> (no parameter)
    // <category>.<name> <action> (where action is a parameter)
    // <category> <name> <action> (where name and action are parameters)
    //
    snprintf (buffer, sizeof(buffer),
              "{EVENT.%s.%s.%s}", category, name, action);
    DEBUG ("Applying rules %s\n", buffer);
    fflush (stdout);
    if (Tcl_Eval (HouseMechInterpreter, buffer) == TCL_OK) return 1;

    DEBUG ("Rule %s failed: %s\n",
           buffer, Tcl_GetStringResult (HouseMechInterpreter));

    snprintf (buffer, sizeof(buffer),
              "{EVENT.%s.%s} {%s}", category, name, action);
    DEBUG ("Applying rules %s\n", buffer);
    fflush (stdout);
    if (Tcl_Eval (HouseMechInterpreter, buffer) == TCL_OK) return 1;

    DEBUG ("Rule for %s failed: %s\n",
           buffer, Tcl_GetStringResult (HouseMechInterpreter));

    snprintf (buffer, sizeof(buffer),
              "{EVENT.%s} {%s} {%s}", category, name, action);
    DEBUG ("Applying rules %s\n", buffer);
    fflush (stdout);
    if (Tcl_Eval (HouseMechInterpreter, buffer) == TCL_OK) return 1;

    DEBUG ("Rule for %s failed: %s\n",
           buffer, Tcl_GetStringResult (HouseMechInterpreter));
    return 0;
}

int housemech_rule_trigger_sensor
       (const char *location, const char *name, const char *value) {

    char buffer[256];

    // Try to process the rules for this sensor data in the following order
    // until one is successful:
    // <location>.<name> <value> (where value is a parameter)
    // <location> <name> <value> (where name and value are parameters)
    //
    snprintf (buffer, sizeof(buffer),
              "{SENSOR.%s.%s} {%s}", location, name, value);
    DEBUG ("Applying rules %s\n", buffer);
    fflush (stdout);
    if (Tcl_Eval (HouseMechInterpreter, buffer) == TCL_OK) return 1;

    DEBUG ("Rule for %s failed: %s\n",
           buffer, Tcl_GetStringResult (HouseMechInterpreter));

    snprintf (buffer, sizeof(buffer),
              "{SENSOR.%s} {%s} {%s}", location, name, value);
    DEBUG ("Applying rules %s\n", buffer);
    fflush (stdout);
    if (Tcl_Eval (HouseMechInterpreter, buffer) == TCL_OK) return 1;

    DEBUG ("Rule for %s failed: %s\n",
           buffer, Tcl_GetStringResult (HouseMechInterpreter));
    return 0;
}

int housemech_rule_trigger_control (const char *name, const char *state) {

    char buffer[256];
    snprintf (buffer, sizeof(buffer), "{POINT.%s} {%s}", name, state);
    DEBUG ("Applying rules %s\n", buffer);
    fflush (stdout);
    if (Tcl_Eval (HouseMechInterpreter, buffer) == TCL_OK) return 1;

    DEBUG ("Rule %s failed: %s\n",
           buffer, Tcl_GetStringResult (HouseMechInterpreter));
    return 0;
}

void housemech_rule_background (time_t now) {

    static time_t NextTclCycle = 0;

    if (now < NextTclCycle) return;
    NextTclCycle = now + HOUSE_TCL_CYCLE;

    // TBD
}


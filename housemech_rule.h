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
 * housemech_rule.h - Manage the automation rules.
 */
void housemech_rule_initialize (int argc, const char **argv);
int  housemech_rule_ready (void);

int  housemech_rule_trigger_event
        (const char *category, const char *name, const char *action);

int housemech_rule_trigger_control (const char *name, const char *state);

int  housemech_rule_status (char *buffer, int size);
void housemech_rule_background (time_t now);


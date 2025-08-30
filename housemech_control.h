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
 * housemech_control.h - Interface with the control servers.
 */
int housemech_control_ready (void);

int housemech_control_start   (const char *name, int pulse,
                               const char *reason, int verbose);
void housemech_control_cancel (const char *name, const char *reason);

const char *housemech_control_state  (const char *name);

int housemech_control_status (char *buffer, int size);
void housemech_control_background (time_t now);

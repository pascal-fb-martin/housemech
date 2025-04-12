/* HouseMech - A simple home web service to automate actions.
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
 * housemech_almanac.h - Interface with the almanac services.
 */
int    housemech_almanac_ready (void);
time_t housemech_almanac_sunset (void);
time_t housemech_almanac_sunrise (void);
void   housemech_almanac_background (time_t now);
int    housemech_almanac_status (char *buffer, int size);


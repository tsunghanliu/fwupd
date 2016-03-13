/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <gusb.h>

#include "fu-plugin.h"

#define STEELSERIES_REPLUG_TIMEOUT		5000

/**
 * fu_plugin_get_name:
 */
const gchar *
fu_plugin_get_name (void)
{
	return "steelseries";
}

/**
 * fu_plugin_device_probe:
 **/
gboolean
fu_plugin_device_probe (FuPlugin *plugin, FuDevice *device, GError **error)
{
	fu_device_add_flag (device, FU_DEVICE_FLAG_ALLOW_ONLINE);
	return TRUE;
}

/**
 * fu_plugin_device_update:
 **/
gboolean
fu_plugin_device_update (FuPlugin *plugin,
			 FuDevice *device,
			 GBytes *data,
			 GError **error)
{
	const gchar *platform_id;
	const gchar *vendor_driver;
	g_autoptr(GUsbContext) usb_ctx = NULL;
	g_autoptr(GUsbDevice) usb_device = NULL;
	g_autoptr(GUsbDevice) usb_devnew = NULL;

	/* get GUsbDevice */
	platform_id = fu_device_get_id (device);
	usb_ctx = g_usb_context_new (NULL);
	usb_device = g_usb_context_find_by_platform_id (usb_ctx,
							platform_id,
							error);
	if (usb_device == NULL)
		return FALSE;

	// if not bootloader
	//     issue vendor specific command and wait for replug
	usb_devnew = g_usb_context_wait_for_replug (usb_ctx,
						    usb_device,
						    STEELSERIES_REPLUG_TIMEOUT,
						    error);
	if (usb_devnew == NULL)
		return FALSE;

	/* open device */
	if (!g_usb_device_open (usb_devnew, error))
		return FALSE;

	// squirt in firmware

	/* close device */
	if (!g_usb_device_close (usb_devnew, error))
		return FALSE;
	return TRUE;
}

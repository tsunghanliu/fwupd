/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

#include <dfu.h>
#include <fwupd.h>
#include <libintl.h>
#include <locale.h>
#include <stdlib.h>

/**
 * dfu_util_get_default_device:
 **/
static DfuTarget *
dfu_util_get_default_device (const gchar *device_vid_pid, guint8 alt_setting, GError **error)
{
	guint i;
	g_autoptr(GUsbContext) usb_ctx = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* get USB context */
	usb_ctx = g_usb_context_new (NULL);
	g_usb_context_enumerate (usb_ctx);

	/* we specified it manually */
	if (device_vid_pid != NULL) {
		g_auto(GStrv) vid_pid = NULL;
		g_autoptr(GUsbDevice) usb_device = NULL;
		g_autoptr(DfuDevice) dfu_device = NULL;

		/* split up */
		vid_pid = g_strsplit (device_vid_pid, ":", -1);
		if (g_strv_length (vid_pid) != 2) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Invalid format of VID:PID");
			return NULL;
		}

		/* find device */
		usb_device = g_usb_context_find_by_vid_pid (usb_ctx,
							    atoi (vid_pid[0]),
							    atoi (vid_pid[1]),
							    error);
		if (usb_device == NULL)
			return NULL;

		/* get DFU device */
		dfu_device = dfu_device_new (usb_device);
		if (dfu_device == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Not a DFU device");
			return NULL;
		}
		return dfu_device_get_target_by_alt_setting (dfu_device, alt_setting, error);
	}

	/* auto-detect first device */
	devices = g_usb_context_get_devices (usb_ctx);
	for (i = 0; i < devices->len; i++) {
		GUsbDevice *usb_device;
		g_autoptr(DfuDevice) dfu_device = NULL;

		usb_device = g_ptr_array_index (devices, i);
		dfu_device = dfu_device_new (usb_device);
		if (dfu_device != NULL)
			return dfu_device_get_target_by_alt_setting (dfu_device,
								     alt_setting,
								     error);
	}

	/* boo-hoo*/
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "No DFU devices detected");
	return NULL;
}

/**
 * dfu_tool_cmd_convert:
 **/
static gboolean
dfu_tool_cmd_convert (gchar **argv, GError **error)
{
	guint64 tmp;
	guint argc = g_strv_length (argv);
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file_in = NULL;
	g_autoptr(GFile) file_out = NULL;

	/* check args */
	if (argc < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FILE-IN FILE-OUT [VID] [PID] [PRODUCT] [SIZE]"
				     " -- e.g. firmware.hex firmware.dfu 273f 1004 ffff 8000");
		return FALSE;
	}

	/* create new firmware helper */
	firmware = dfu_firmware_new ();

	/* set target size */
	if (argc > 5) {
		tmp = g_ascii_strtoull (argv[5], NULL, 16);
		if (tmp == 0 || tmp > 0xffff) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to parse target size '%s'", argv[4]);
			return FALSE;
		}
		dfu_firmware_set_target_size (firmware, tmp);
	}

	/* parse file */
	file_in = g_file_new_for_path (argv[0]);
	file_out = g_file_new_for_path (argv[1]);
	if (!dfu_firmware_parse_file (firmware, file_in,
				      DFU_FIRMWARE_PARSE_FLAG_NONE,
				      NULL, error)) {
		return FALSE;
	}

	/* set VID */
	if (argc > 2) {
		tmp = g_ascii_strtoull (argv[2], NULL, 16);
		if (tmp == 0 || tmp > 0xffff) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to parse VID '%s'", argv[2]);
			return FALSE;
		}
		dfu_firmware_set_vid (firmware, tmp);
	}

	/* set PID */
	if (argc > 3) {
		tmp = g_ascii_strtoull (argv[3], NULL, 16);
		if (tmp == 0 || tmp > 0xffff) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to parse PID '%s'", argv[3]);
			return FALSE;
		}
		dfu_firmware_set_pid (firmware, tmp);
	}

	/* set release */
	if (argc > 4) {
		tmp = g_ascii_strtoull (argv[4], NULL, 16);
		if (tmp == 0 || tmp > 0xffff) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to parse release '%s'", argv[4]);
			return FALSE;
		}
		dfu_firmware_set_release (firmware, tmp);
	}

	/* print the new object */
	str_debug = dfu_firmware_to_string (firmware);
	g_debug ("DFU: %s", str_debug);

	/* write out new file */
	return dfu_firmware_write_file (firmware, file_out, NULL, error);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean ret;
	gboolean verbose = FALSE;
	gboolean version = FALSE;
	gboolean list = FALSE;
	gboolean reset = FALSE;
	gboolean detach = FALSE;
	guint8 iface_alt_setting = 0;
	guint16 transfer_size = 0;
	g_autofree gchar *device_vid_pid = NULL;
	g_autofree gchar *filename_download = NULL;
	g_autofree gchar *filename_upload = NULL;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = NULL;
	const GOptionEntry options[] = {
		{ "version", 'V', 0, G_OPTION_ARG_NONE, &version,
			"Print the version number", NULL },
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Print verbose debug statements", NULL },
		{ "list", 'l', 0, G_OPTION_ARG_NONE, &list,
			"List currently attached DFU capable devices", NULL },
		{ "detach", 'e', 0, G_OPTION_ARG_NONE, &detach,
			"Detach currently attached DFU capable devices", NULL },
		{ "device", 'd', 0, G_OPTION_ARG_STRING, &device_vid_pid,
			"Specify Vendor/Product ID(s) of DFU device", "VID:PID" },
		{ "alt", 'a', 0, G_OPTION_ARG_INT, &iface_alt_setting,
			"Specify the alternate setting of the DFU interface", "NUMBER" },
		{ "transfer-size", 't', 0, G_OPTION_ARG_STRING, &transfer_size,
			"Specify the number of bytes per USB transfer", "BYTES" },
		{ "upload", 'U', 0, G_OPTION_ARG_FILENAME, &filename_upload,
			"Read firmware from device into file", "FILENAME" },
		{ "download", 'D', 0, G_OPTION_ARG_FILENAME, &filename_download,
			"Write firmware from file into device", "FILENAME" },
		{ "reset", 'R', 0, G_OPTION_ARG_NONE, &reset,
			"Issue USB host reset once finished", NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* get a list of the commands */
	context = g_option_context_new (NULL);
	g_set_application_name ("DFU Utility");
	g_option_context_add_main_entries (context, options, NULL);
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_print ("%s: %s\n", "Failed to parse arguments", error->message);
		return EXIT_FAILURE;
	}

	/* set verbose? */
	if (verbose)
		g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);

	/* version */
	if (version) {
		g_print ("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
		return EXIT_SUCCESS;
	}

	/* list */
	if (list) {
		GUsbDevice *usb_device;
		guint i;
		g_autoptr(GPtrArray) devices = NULL;
		g_autoptr(GUsbContext) usb_ctx = NULL;

		/* get all the connected USB devices */
		usb_ctx = g_usb_context_new (NULL);
		g_usb_context_enumerate (usb_ctx);
		devices = g_usb_context_get_devices (usb_ctx);
		for (i = 0; i < devices->len; i++) {
			g_autoptr(DfuDevice) dfu_device = NULL;
			GPtrArray *dfu_targets;
			DfuTarget *dfu_target;
			guint j;

			usb_device = g_ptr_array_index (devices, i);
			dfu_device = dfu_device_new (usb_device);
			if (dfu_device == NULL)
				continue;
			dfu_targets = dfu_device_get_targets (dfu_device);
			for (j = 0; j < dfu_targets->len; j++) {
				g_autoptr(GError) error_local = NULL;
				dfu_target = g_ptr_array_index (dfu_targets, j);

				if (transfer_size > 0)
					dfu_target_set_transfer_size (dfu_target, transfer_size);
				ret = dfu_target_open (dfu_target,
						       DFU_TARGET_OPEN_FLAG_NONE,
						       NULL, &error_local);
				g_print ("Found %s: [%04x:%04x] ver=%04x, devnum=%i, cfg=%i, intf=%i, ts=%i, alt=%i, name=%s",
					 dfu_mode_to_string (dfu_target_get_mode (dfu_target)),
					 g_usb_device_get_vid (usb_device),
					 g_usb_device_get_pid (usb_device),
					 g_usb_device_get_release (usb_device),
					 g_usb_device_get_address (usb_device),
					 g_usb_device_get_configuration (usb_device, NULL),
					 dfu_target_get_interface_number (dfu_target),
					 dfu_target_get_transfer_size (dfu_target),
					 dfu_target_get_interface_alt_setting (dfu_target),
					 dfu_target_get_interface_alt_name (dfu_target));
				if (ret) {
					g_print (", status=%s, state=%s\n",
						 dfu_status_to_string (dfu_target_get_status (dfu_target)),
						 dfu_state_to_string (dfu_target_get_state (dfu_target)));
				} else {
					g_print (": %s\n", error_local->message);
				}
				dfu_target_close (dfu_target, NULL);
			}
		}
		return EXIT_SUCCESS;
	}

	/* detach */
	if (detach) {
		g_autoptr(DfuTarget) dfu_target = NULL;

		/* open correct device */
		dfu_target = dfu_util_get_default_device (device_vid_pid,
							  iface_alt_setting,
							  &error);
		if (dfu_target == NULL) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}
		if (transfer_size > 0)
			dfu_target_set_transfer_size (dfu_target, transfer_size);
		if (!dfu_target_open (dfu_target, DFU_TARGET_OPEN_FLAG_NONE, NULL, &error)) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}

		/* detatch */
		if (!dfu_target_detach (dfu_target, NULL, &error)) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}

	/* upload */
	if (filename_upload != NULL) {
		g_autoptr(DfuTarget) dfu_target = NULL;
		g_autoptr(DfuFirmware) dfu_firmware = NULL;
		g_autoptr(GBytes) bytes = NULL;
		g_autoptr(GFile) file = NULL;

		/* open correct device */
		dfu_target = dfu_util_get_default_device (device_vid_pid,
							  iface_alt_setting,
							  &error);
		if (dfu_target == NULL) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}
		if (transfer_size > 0)
			dfu_target_set_transfer_size (dfu_target, transfer_size);
		if (!dfu_target_open (dfu_target, DFU_TARGET_OPEN_FLAG_NONE, NULL, &error)) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}

		/* APP -> DFU */
		if (dfu_target_get_mode (dfu_target) == DFU_MODE_RUNTIME) {
			//FIXME: on the device?
			if (!dfu_target_wait_for_reset (dfu_target, 5000, NULL, &error)) {
				g_print ("%s\n", error->message);
				return EXIT_FAILURE;
			}
		}

		/* transfer */
		bytes = dfu_target_upload (dfu_target, NULL, &error);
		if (bytes == NULL) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}

		/* save file */
		dfu_firmware = dfu_firmware_new ();
		dfu_firmware_set_vid (dfu_firmware, dfu_target_get_runtime_vid (dfu_target));
		dfu_firmware_set_pid (dfu_firmware, dfu_target_get_runtime_pid (dfu_target));
		dfu_firmware_set_contents (dfu_firmware, bytes);
		file = g_file_new_for_path (filename_upload);
		if (!dfu_firmware_write_file (dfu_firmware, file, NULL, &error)) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}

		/* print the new object */
		str_debug = dfu_firmware_to_string (dfu_firmware);
		g_debug ("DFU: %s", str_debug);

		/* success */
		g_print ("%li bytes successfully uploaded from device\n",
			 g_bytes_get_size (bytes));

		return EXIT_SUCCESS;
	}

	/* download */
	if (filename_download != NULL) {
		DfuTargetDownloadFlags flags = DFU_TARGET_DOWNLOAD_FLAG_VERIFY;
		GBytes *contents = NULL;
		g_autoptr(DfuTarget) dfu_target = NULL;
		g_autoptr(DfuFirmware) dfu_firmware = NULL;
		g_autoptr(GFile) file = NULL;

		/* open correct device */
		dfu_target = dfu_util_get_default_device (device_vid_pid,
							  iface_alt_setting,
							  &error);
		if (dfu_target == NULL) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}
		if (transfer_size > 0)
			dfu_target_set_transfer_size (dfu_target, transfer_size);
		if (!dfu_target_open (dfu_target, DFU_TARGET_OPEN_FLAG_NONE, NULL, &error)) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}

		/* APP -> DFU */
		if (dfu_target_get_mode (dfu_target) == DFU_MODE_RUNTIME) {
			if (!dfu_target_wait_for_reset (dfu_target, 5000, NULL, &error)) {
				g_print ("%s\n", error->message);
				return EXIT_FAILURE;
			}
		}

		/* open file */
		dfu_firmware = dfu_firmware_new ();
		file = g_file_new_for_path (filename_download);
		if (!dfu_firmware_parse_file (dfu_firmware, file,
					      DFU_FIRMWARE_PARSE_FLAG_NONE,
					      NULL, &error)) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}

		/* print the new object */
		str_debug = dfu_firmware_to_string (dfu_firmware);
		g_debug ("DFU: %s", str_debug);

		/* check vendor matches */
		if (dfu_firmware_get_vid (dfu_firmware) != 0xffff &&
		    dfu_target_get_runtime_pid (dfu_target) != 0xffff &&
		    dfu_firmware_get_vid (dfu_firmware) != dfu_target_get_runtime_pid (dfu_target)) {
			g_print ("Vendor ID incorrect, expected 0x%04x got 0x%04x\n",
				 dfu_firmware_get_vid (dfu_firmware),
				 dfu_target_get_runtime_pid (dfu_target));
			return EXIT_FAILURE;
		}

		/* check product matches */
		if (dfu_firmware_get_pid (dfu_firmware) != 0xffff &&
		    dfu_target_get_runtime_pid (dfu_target) != 0xffff &&
		    dfu_firmware_get_pid (dfu_firmware) != dfu_target_get_runtime_pid (dfu_target)) {
			g_print ("Product ID incorrect, expected 0x%04x got 0x%04x\n",
				 dfu_firmware_get_pid (dfu_firmware),
				 dfu_target_get_runtime_pid (dfu_target));
			return EXIT_FAILURE;
		}

		/* optional reset */
		if (reset)
			flags |= DFU_TARGET_DOWNLOAD_FLAG_HOST_RESET;

		/* transfer */
		contents = dfu_firmware_get_contents (dfu_firmware);
		if (!dfu_target_download (dfu_target, contents, flags, NULL, &error)) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}

		/* success */
		g_print ("%li bytes successfully downloaded to device\n",
			 g_bytes_get_size (contents));

		return EXIT_SUCCESS;
	}

	/* just do this on its own */
	if (reset) {
		g_autoptr(DfuTarget) dfu_target = NULL;
		dfu_target = dfu_util_get_default_device (device_vid_pid,
							  iface_alt_setting,
							  &error);
		if (dfu_target == NULL) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}
		if (!dfu_target_open (dfu_target,
				      DFU_TARGET_OPEN_FLAG_NO_AUTO_REFRESH,
				      NULL, &error)) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}
		if (!dfu_target_reset (dfu_target, &error)) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}

	/* hidden features */
	if (g_strcmp0 (argv[1], "convert") == 0) {
		if (!dfu_tool_cmd_convert (argv + 2, &error)) {
			g_print ("%s\n", error->message);
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}

	/* success */
	return EXIT_SUCCESS;
}

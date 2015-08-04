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

#include <archive_entry.h>
#include <archive.h>
#include <fwupd.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <string.h>

#include "fu-cleanup.h"
#include "fu-device.h"
#include "fu-provider-rpi.h"

static void     fu_provider_rpi_finalize	(GObject	*object);

#define FU_PROVIDER_RPI_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_PROVIDER_RPI, FuProviderRpiPrivate))

/**
 * FuProviderRpiPrivate:
 **/
struct _FuProviderRpiPrivate
{
	GHashTable		*devices;
};

G_DEFINE_TYPE (FuProviderRpi, fu_provider_rpi, FU_TYPE_PROVIDER)

/**
 * fu_provider_rpi_get_name:
 **/
static const gchar *
fu_provider_rpi_get_name (FuProvider *provider)
{
	return "RaspberryPi";
}

/**
 * fu_provider_rpi_explode_file:
 **/
static gboolean
fu_provider_rpi_explode_file (struct archive_entry *entry, const gchar *dir)
{
	const gchar *tmp;
	_cleanup_free_ gchar *buf = NULL;

	/* no output file */
	if (archive_entry_pathname (entry) == NULL)
		return FALSE;

	/* update output path */
	tmp = archive_entry_pathname (entry);
	buf = g_build_filename (dir, tmp, NULL);
	archive_entry_update_pathname_utf8 (entry, buf);
	return TRUE;
}

/**
 * fu_provider_rpi_update:
 **/
static gboolean
fu_provider_rpi_update (FuProvider *provider,
			 FuDevice *device,
			 gint fd,
			 FuProviderFlags flags,
			 GError **error)
{
	gboolean ret = TRUE;
	gboolean valid;
	int r;
	struct archive *arch = NULL;
	struct archive_entry *entry;
	const gchar *dir = "/tmp/boot";

	/* decompress anything matching either glob */
	arch = archive_read_new ();
	archive_read_support_format_all (arch);
	archive_read_support_filter_all (arch);
	r = archive_read_open_fd (arch, fd, 1024 * 32);
	if (r) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Cannot open: %s",
			     archive_error_string (arch));
		goto out;
	}
	for (;;) {
		_cleanup_free_ gchar *path = NULL;
		r = archive_read_next_header (arch, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			ret = FALSE;
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Cannot read header: %s",
				     archive_error_string (arch));
			goto out;
		}

		/* only extract if valid */
		valid = fu_provider_rpi_explode_file (entry, dir);
		if (!valid)
			continue;
		r = archive_read_extract (arch, entry, 0);
		if (r != ARCHIVE_OK) {
			ret = FALSE;
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Cannot extract: %s",
				     archive_error_string (arch));
			goto out;
		}
	}
out:
	if (arch != NULL) {
		archive_read_close (arch);
		archive_read_free (arch);
	}
	return ret;
}

/**
 * fu_provider_rpi_strstr:
 **/
static gchar *
fu_provider_rpi_strstr (const guint8 *haystack, gsize haystack_len, const gchar *needle, guint *offset)
{
	guint i;
	guint needle_len;

	if (needle == NULL || needle[0] == '\0')
		return NULL;
	if (haystack == NULL || haystack_len == 0)
		return NULL;
	needle_len = strlen (needle);
	if (needle_len > haystack_len)
		return NULL;
	for (i = 0; i < haystack_len - needle_len; i++) {
		if (memcmp (haystack + i, needle, needle_len) == 0) {
			if (offset != NULL)
				*offset = i + needle_len;
			return g_strdup ((const gchar *) &haystack[i + needle_len]);
		}
	}
	return NULL;
}

/**
 * fu_provider_rpi_find_version:
 **/
static gchar *
fu_provider_rpi_find_version (const gchar *fn, GError **error)
{
	GDate *date;
	gboolean ret;
	gchar *fwver = NULL;
	gsize len = 0;
	guint offset;
	_cleanup_free_ gchar *vc_date = NULL;
	_cleanup_free_ gchar *vc_time = NULL;
	_cleanup_free_ guint8 *data = NULL;
	_cleanup_strv_free_ gchar **split = NULL;

	/* read file */
	ret = g_file_get_contents (fn, (gchar **) &data, &len, error);
	if (!ret)
		return FALSE;

	/* find the VC_BUILD info which paradoxically is split into two
	 * string segments */
	vc_time = fu_provider_rpi_strstr (data, len,
					  "VC_BUILD_ID_TIME: ", &offset);
	if (vc_time == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to get 1st VC_BUILD_ID_TIME");
		return FALSE;
	}
	vc_date = fu_provider_rpi_strstr (data + offset, len - offset,
					  "VC_BUILD_ID_TIME: ", NULL);
	if (vc_date == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to get 2nd VC_BUILD_ID_TIME");
		return FALSE;
	}

	/* parse the date */
	date = g_date_new ();
	g_date_set_parse (date, vc_date);
	if (!g_date_valid (date)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to parse date '%s'",
			     vc_date);
		return FALSE;
	}

	/* parse the time */
	split = g_strsplit (vc_time, ":", -1);
	if (g_strv_length (split) != 3) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to parse time '%s'",
			     vc_time);
		return FALSE;
	}

	/* create a version number from the date and time */
	fwver = g_strdup_printf ("%04i%02i%02i%s%s%s",
				 g_date_get_year (date),
				 g_date_get_month (date),
				 g_date_get_day (date),
				 split[0], split[1], split[2]);
	g_date_free (date);
	return fwver;
}

/**
 * fu_provider_rpi_coldplug:
 **/
static gboolean
fu_provider_rpi_coldplug (FuProvider *provider, GError **error)
{
	_cleanup_free_ gchar *fwver = NULL;
	_cleanup_object_unref_ FuDevice *device = NULL;

	/* anything interesting */
	if (!g_file_test ("/boot/overlays", G_FILE_TEST_EXISTS))
		return TRUE;

	device = fu_device_new ();
	fu_device_set_id (device, "raspberry-pi");
	fu_device_set_guid (device, "c7c77d64-7a7d-49b0-aecd-759930aa4ae2");
	fu_device_set_display_name (device, "Raspberry Pi");
	fu_device_add_flag (device, FU_DEVICE_FLAG_INTERNAL);

	/* can we update */
	if (g_file_test ("/opt/vc/bin/vcgencmd version", G_FILE_TEST_EXISTS)) {
		fu_device_add_flag (device, FU_DEVICE_FLAG_ALLOW_OFFLINE);
		fu_device_add_flag (device, FU_DEVICE_FLAG_ALLOW_ONLINE);
	}

	/* get the VC build info -- things we can find are:
	 *
	 * VC_BUILD_ID_USER: dc4
	 * VC_BUILD_ID_TIME: 14:58:37
	 * VC_BUILD_ID_BRANCH: master
	 * VC_BUILD_ID_TIME: Aug  3 2015
	 * VC_BUILD_ID_HOSTNAME: dc4-XPS13-9333
	 * VC_BUILD_ID_PLATFORM: raspberrypi_linux
	 * VC_BUILD_ID_VERSION: 4b51d81eb0068a875b336f4cc2c468cbdd06d0c5 (clean)
	 */
	fwver = fu_provider_rpi_find_version ("/boot/start.elf", error);
	if (fwver == NULL)
		return FALSE;
	fu_device_set_metadata (device, FU_DEVICE_KEY_VERSION, fwver);

	fu_provider_device_add (provider, device);
	return TRUE;
}

/**
 * fu_provider_rpi_class_init:
 **/
static void
fu_provider_rpi_class_init (FuProviderRpiClass *klass)
{
	FuProviderClass *provider_class = FU_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->get_name = fu_provider_rpi_get_name;
	provider_class->coldplug = fu_provider_rpi_coldplug;
	provider_class->update_online = fu_provider_rpi_update;
	object_class->finalize = fu_provider_rpi_finalize;

	g_type_class_add_private (klass, sizeof (FuProviderRpiPrivate));
}

/**
 * fu_provider_rpi_init:
 **/
static void
fu_provider_rpi_init (FuProviderRpi *provider_rpi)
{
	provider_rpi->priv = FU_PROVIDER_RPI_GET_PRIVATE (provider_rpi);
}

/**
 * fu_provider_rpi_finalize:
 **/
static void
fu_provider_rpi_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_provider_rpi_parent_class)->finalize (object);
}

/**
 * fu_provider_rpi_new:
 **/
FuProvider *
fu_provider_rpi_new (void)
{
	FuProviderRpi *provider;
	provider = g_object_new (FU_TYPE_PROVIDER_RPI, NULL);
	return FU_PROVIDER (provider);
}

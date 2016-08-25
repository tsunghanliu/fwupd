/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <string.h>
#include <libelf.h>
#include <gelf.h>

//#include "dfu-element.h"
#include "dfu-format-elf.h"
//#include "dfu-image.h"
#include "dfu-error.h"

/**
 * dfu_firmware_detect_elf: (skip)
 * @bytes: data to parse
 *
 * Attempts to sniff the data and work out the firmware format
 *
 * Returns: a #DfuFirmwareFormat, e.g. %DFU_FIRMWARE_FORMAT_ELF
 **/
DfuFirmwareFormat
dfu_firmware_detect_elf (GBytes *bytes)
{
	guint8 *data;
	gsize len;

	/* check data size */
	data = (guint8 *) g_bytes_get_data (bytes, &len);
	if (len < 16)
		return DFU_FIRMWARE_FORMAT_UNKNOWN;

	/* sniff the signature bytes */
	if (memcmp (data + 1, "ELF", 3) != 0)
		return DFU_FIRMWARE_FORMAT_UNKNOWN;

	/* success */
	return DFU_FIRMWARE_FORMAT_ELF;
}

static DfuElement *
_get_image_by_section_name (Elf *e, const gchar *desired_name)
{
	DfuElement *element = NULL;
	Elf_Scn *scn = NULL;
	GElf_Shdr shdr;
	const gchar *name;
	size_t shstrndx;

	if (elf_getshdrstrndx (e , &shstrndx) != 0) {
		g_warning ("failed elf_getshdrstrndx");
		return NULL;
	}
	while ((scn = elf_nextscn (e, scn)) != NULL ) {
		if (gelf_getshdr (scn, &shdr ) != & shdr) {
			g_warning ("failed gelf_getshdr");
			continue;
		}
		if ((name = elf_strptr (e, shstrndx, shdr.sh_name)) == NULL) {
			g_warning ("failed elf_strptr");
			continue;
		}
		if (g_strcmp0 (name, desired_name) == 0) {
			Elf_Data *data = elf_getdata (scn, NULL);
			if (data != NULL && data->d_buf != NULL) {
				g_autoptr(GBytes) bytes = NULL;
				bytes = g_bytes_new (data->d_buf, data->d_size);
				element = dfu_element_new ();
				dfu_element_set_contents (element, bytes);
				dfu_element_set_address (element, shdr.sh_addr);
			}
			break;
		}
	}
	return element;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Elf, elf_end);

/**
 * dfu_firmware_from_elf: (skip)
 * @firmware: a #DfuFirmware
 * @bytes: data to parse
 * @flags: some #DfuFirmwareParseFlags
 * @error: a #GError, or %NULL
 *
 * Unpacks into a firmware object from ELF data.
 *
 * Returns: %TRUE for success
 **/
gboolean
dfu_firmware_from_elf (DfuFirmware *firmware,
		       GBytes *bytes,
		       DfuFirmwareParseFlags flags,
		       GError **error)
{
	guint i;
	guint sections_cnt = 0;
	g_autoptr(Elf) e = NULL;
	const gchar *section_names[] = {
		".text",
		NULL };

	/* load library */
	if (elf_version (EV_CURRENT) == EV_NONE) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "ELF library init failed: %s",
			     elf_errmsg (-1));
		return FALSE;
	}

	/* parse data */
	e = elf_memory ((gchar *) g_bytes_get_data (bytes, NULL),
			g_bytes_get_size (bytes));
	if (e == NULL) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "failed to load data as ELF: %s",
			     elf_errmsg (-1));
		return FALSE;
	}
	if (elf_kind (e) != ELF_K_ELF) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "not a supported ELF format: %s",
			     elf_errmsg (-1));
		return FALSE;
	}
	g_debug ("loading %ib ELF object" ,
		 gelf_getclass (e) == ELFCLASS32 ? 32 : 64);

	/* add interesting sections as the image */
	for (i = 0; section_names[i] != NULL; i++) {
		g_autoptr(DfuElement) element = NULL;
		g_autoptr(DfuImage) image = NULL;
		element = _get_image_by_section_name (e, section_names[i]);
		if (element == NULL)
			continue;
		image = dfu_image_new ();
		dfu_image_add_element (image, element);
		dfu_image_set_name (image, section_names[i]);
		dfu_firmware_add_image (firmware, image);
		sections_cnt++;
	}

	/* nothing found */
	if (sections_cnt == 0) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "no firmware found in ELF file");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * dfu_firmware_to_elf: (skip)
 * @firmware: a #DfuFirmware
 * @error: a #GError, or %NULL
 *
 * Packs elf firmware
 *
 * Returns: (transfer full): the packed data
 **/
GBytes *
dfu_firmware_to_elf (DfuFirmware *firmware, GError **error)
{
	g_set_error_literal (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_FOUND,
			     "no ELF write support");
	return NULL;
}

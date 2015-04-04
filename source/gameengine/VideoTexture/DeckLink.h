/*
 * ***** BEGIN GPL LICENSE BLOCK *****
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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Copyright (c) 2006 The Zdeno Ash Miklas
 *
 * This source file is part of VideoTexture library
 *
 * Contributor(s):
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file VideoTexture/DeckLink.h
 *  \ingroup bgevideotex
 */

#ifndef __DECKLINK_H__
#define __DECKLINK_H__

#ifdef WITH_DECKLINK

#include "PyObjectPlus.h"
#include <structmember.h>

#include "DNA_image_types.h"

#include "DeckLinkAPI.h"

#include "ImageBase.h"
#include "BlendType.h"
#include "Exception.h"


// type DeckLink declaration
struct DeckLink
{
	PyObject_HEAD

	// last refresh
	double m_lastClock;

	// image source
	PyImage * m_source;
};


// DeckLink type description
extern PyTypeObject DeckLinkType;

#endif	/* WITH_DECKLINK */

#endif	/* __DECKLINK_H__ */
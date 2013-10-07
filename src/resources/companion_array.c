/*
 * Copyright (c) 2011-2012 Luc Verhaegen <libv@skynet.be>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <stdlib.h>

#include "companion.h"

float *
companion_vertices_array(void)
{
	float *vertices = calloc(COMPANION_ARRAY_COUNT, 3 * sizeof(float));
	short *triangles = (short *)companion_triangles;
	int i, index;

	for (i = 0; i < COMPANION_ARRAY_COUNT; i++) {
		index = triangles[i];
		vertices[3 * i + 0] = companion_vertices[index][0];
		vertices[3 * i + 1] = companion_vertices[index][1];
		vertices[3 * i + 2] = companion_vertices[index][2];
	}

	return vertices;
}

float *
companion_texture_coordinates_array(void)
{
	float *texture_coordinates = calloc(COMPANION_ARRAY_COUNT,
					    2 * sizeof(float));
	short *triangles = (short *)companion_triangles;
	int i, index;

	for (i = 0; i < COMPANION_ARRAY_COUNT; i++) {
		index = triangles[i];
		texture_coordinates[2 * i + 0] =
			companion_texture_coordinates[index][0];
		texture_coordinates[2 * i + 1] =
			companion_texture_coordinates[index][1];
	}

	return texture_coordinates;
}

float *
companion_normals_array(void)
{
	float *normals = calloc(COMPANION_ARRAY_COUNT, 3 * sizeof(float));
	short *triangles = (short *)companion_triangles;
	int i, index;

	for (i = 0; i < COMPANION_ARRAY_COUNT; i++) {
		index = triangles[i];
		normals[3 * i + 0] = companion_normals[index][0];
		normals[3 * i + 1] = companion_normals[index][1];
		normals[3 * i + 2] = companion_normals[index][2];
	}

	return normals;
}

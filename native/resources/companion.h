/*
 * Copyright (c) 2012 Luc Verhaegen <libv@skynet.be>
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

#ifndef COMPANION_H
#define COMPANION_H 1

/*
 * mesh, indexed
 */
#define COMPANION_VERTEX_COUNT 4761
extern float companion_vertices[COMPANION_VERTEX_COUNT][3];
extern float companion_texture_coordinates[COMPANION_VERTEX_COUNT][2];
extern float companion_normals[COMPANION_VERTEX_COUNT][3];

#define COMPANION_TRIANGLE_COUNT 4052
extern short companion_triangles[COMPANION_TRIANGLE_COUNT][3];

/*
 * convert mesh from indexed to arrays - alloced arrays.
 */
#define COMPANION_ARRAY_COUNT (COMPANION_TRIANGLE_COUNT * 3)

float *companion_vertices_array(void);
float *companion_texture_coordinates_array(void);
float *companion_normals_array(void);

/*
 * textures
 */
#define COMPANION_TEXTURE_WIDTH 512
#define COMPANION_TEXTURE_HEIGHT 512
#define COMPANION_TEXTURE_FORMAT LIMA_TEXEL_FORMAT_RGB_888

#define COMPANION_TEXTURE_SIZE \
	(COMPANION_TEXTURE_WIDTH * COMPANION_TEXTURE_HEIGHT * 3 / 4)

/* texture for the mesh */
extern unsigned int companion_texture[COMPANION_TEXTURE_SIZE];

/* flattened texture for more simple uses */
extern unsigned int companion_texture_flat[COMPANION_TEXTURE_SIZE];

#endif /* COMPANION_H */

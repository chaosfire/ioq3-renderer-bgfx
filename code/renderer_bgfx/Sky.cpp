/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#include "Precompiled.h"
#pragma hdrstop

#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2
#define SKY_SUBDIVISIONS		8
#define HALF_SKY_SUBDIVISIONS	(SKY_SUBDIVISIONS/2)
#define SQR(a) ((a)*(a))
#define	ON_EPSILON		0.1f			// point on plane side epsilon
#define	MAX_CLIP_VERTS	64

namespace renderer {

static float s_cloudTexCoords[6][SKY_SUBDIVISIONS+1][SKY_SUBDIVISIONS+1][2];
static float s_cloudTexP[6][SKY_SUBDIVISIONS+1][SKY_SUBDIVISIONS+1];

/*
===================================================================================

POLYGON TO BOX SIDE PROJECTION

===================================================================================
*/

static vec3_t sky_clip[6] = 
{
	{1,1,0},
	{1,-1,0},
	{0,-1,1},
	{0,1,1},
	{1,0,1},
	{-1,0,1} 
};

static float	sky_mins[2][6], sky_maxs[2][6];
static float	sky_min, sky_max;

static void AddSkyPolygon(int nump, vec3_t vecs) 
{
	int		i,j;
	vec3_t	v, av;
	float	s, t, dv;
	int		axis;
	float	*vp;
	// s = [0]/[2], t = [1]/[2]
	static int	vec_to_st[6][3] =
	{
		{-2,3,1},
		{2,3,-1},

		{1,3,2},
		{-1,3,-2},

		{-2,-1,3},
		{-2,1,-3}

	//	{-1,2,3},
	//	{1,2,-3}
	};

	// decide which face it maps to
	VectorCopy (vec3_origin, v);
	for (i=0, vp=vecs ; i<nump ; i++, vp+=3)
	{
		VectorAdd (vp, v, v);
	}
	av[0] = fabs(v[0]);
	av[1] = fabs(v[1]);
	av[2] = fabs(v[2]);
	if (av[0] > av[1] && av[0] > av[2])
	{
		if (v[0] < 0)
			axis = 1;
		else
			axis = 0;
	}
	else if (av[1] > av[2] && av[1] > av[0])
	{
		if (v[1] < 0)
			axis = 3;
		else
			axis = 2;
	}
	else
	{
		if (v[2] < 0)
			axis = 5;
		else
			axis = 4;
	}

	// project new texture coords
	for (i=0 ; i<nump ; i++, vecs+=3)
	{
		j = vec_to_st[axis][2];
		if (j > 0)
			dv = vecs[j - 1];
		else
			dv = -vecs[-j - 1];
		if (dv < 0.001)
			continue;	// don't divide by zero
		j = vec_to_st[axis][0];
		if (j < 0)
			s = -vecs[-j -1] / dv;
		else
			s = vecs[j-1] / dv;
		j = vec_to_st[axis][1];
		if (j < 0)
			t = -vecs[-j -1] / dv;
		else
			t = vecs[j-1] / dv;

		if (s < sky_mins[0][axis])
			sky_mins[0][axis] = s;
		if (t < sky_mins[1][axis])
			sky_mins[1][axis] = t;
		if (s > sky_maxs[0][axis])
			sky_maxs[0][axis] = s;
		if (t > sky_maxs[1][axis])
			sky_maxs[1][axis] = t;
	}
}

static void ClipSkyPolygon(int nump, vec3_t vecs, int stage) 
{
	float	*norm;
	float	*v;
	qboolean	front, back;
	float	d, e;
	float	dists[MAX_CLIP_VERTS];
	int		sides[MAX_CLIP_VERTS];
	vec3_t	newv[2][MAX_CLIP_VERTS];
	int		newc[2];
	int		i, j;

	if (nump > MAX_CLIP_VERTS-2)
		ri.Error (ERR_DROP, "ClipSkyPolygon: MAX_CLIP_VERTS");
	if (stage == 6)
	{	// fully clipped, so draw it
		AddSkyPolygon (nump, vecs);
		return;
	}

	front = back = qfalse;
	norm = sky_clip[stage];
	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		d = DotProduct (v, norm);
		if (d > ON_EPSILON)
		{
			front = qtrue;
			sides[i] = SIDE_FRONT;
		}
		else if (d < -ON_EPSILON)
		{
			back = qtrue;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
		dists[i] = d;
	}

	if (!front || !back)
	{	// not clipped
		ClipSkyPolygon (nump, vecs, stage+1);
		return;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy (vecs, (vecs+(i*3)));
	newc[0] = newc[1] = 0;

	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i])
			continue;

		d = dists[i] / (dists[i] - dists[i+1]);
		for (j=0 ; j<3 ; j++)
		{
			e = v[j] + d*(v[j+3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	ClipSkyPolygon(newc[0], newv[0][0], stage+1);
	ClipSkyPolygon(newc[1], newv[1][0], stage+1);
}

/*
===================================================================================

CLOUD VERTEX GENERATION

===================================================================================
*/

/*
** MakeSkyVec
**
** Parms: s, t range from -1 to 1
*/
static void MakeSkyVec(float zMax, float s, float t, int axis, float outSt[2], vec3_t outXYZ)
{
	// 1 = s, 2 = t, 3 = 2048
	static int	st_to_vec[6][3] =
	{
		{3,-1,2},
		{-3,1,2},

		{1,3,2},
		{-1,-3,2},

		{-2,-1,3},		// 0 degrees yaw, look straight up
		{2,-1,-3}		// look straight down
	};

	vec3_t		b;
	int			j, k;
	float	boxSize;

	boxSize = zMax / 1.75f;		// div sqrt(3)
	b[0] = s*boxSize;
	b[1] = t*boxSize;
	b[2] = boxSize;

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
		{
			outXYZ[j] = -b[-k - 1];
		}
		else
		{
			outXYZ[j] = b[k - 1];
		}
	}

	// avoid bilerp seam
	s = (s+1)*0.5;
	t = (t+1)*0.5;
	if (s < sky_min)
	{
		s = sky_min;
	}
	else if (s > sky_max)
	{
		s = sky_max;
	}

	if (t < sky_min)
	{
		t = sky_min;
	}
	else if (t > sky_max)
	{
		t = sky_max;
	}

	t = 1.0 - t;


	if (outSt)
	{
		outSt[0] = s;
		outSt[1] = t;
	}
}

static int	sky_texorder[6] = {0,2,1,3,4,5};
static vec3_t	s_skyPoints[SKY_SUBDIVISIONS+1][SKY_SUBDIVISIONS+1];
static float	s_skyTexCoords[SKY_SUBDIVISIONS+1][SKY_SUBDIVISIONS+1][2];

void Sky_InitializeTexCoords(float heightCloud)
{
	int i, s, t;
	float radiusWorld = 4096;
	float p;
	float sRad, tRad;
	vec3_t skyVec;
	vec3_t v;

	for (i = 0; i < 6; i++)
	{
		for (t = 0; t <= SKY_SUBDIVISIONS; t++)
		{
			for (s = 0; s <= SKY_SUBDIVISIONS; s++)
			{
				// compute vector from view origin to sky side integral point
				MakeSkyVec(1024, (s - HALF_SKY_SUBDIVISIONS) / (float) HALF_SKY_SUBDIVISIONS, 
							(t - HALF_SKY_SUBDIVISIONS) / (float) HALF_SKY_SUBDIVISIONS, 
							i, 
							NULL,
							skyVec);

				// compute parametric value 'p' that intersects with cloud layer
				p = (1.0f / (2 * DotProduct(skyVec, skyVec))) *
					(-2 * skyVec[2] * radiusWorld + 
					   2 * sqrt(SQR(skyVec[2]) * SQR(radiusWorld) + 
					             2 * SQR(skyVec[0]) * radiusWorld * heightCloud +
								 SQR(skyVec[0]) * SQR(heightCloud) + 
								 2 * SQR(skyVec[1]) * radiusWorld * heightCloud +
								 SQR(skyVec[1]) * SQR(heightCloud) + 
								 2 * SQR(skyVec[2]) * radiusWorld * heightCloud +
								 SQR(skyVec[2]) * SQR(heightCloud)));

				s_cloudTexP[i][t][s] = p;

				// compute intersection point based on p
				VectorScale(skyVec, p, v);
				v[2] += radiusWorld;

				// compute vector from world origin to intersection point 'v'
				VectorNormalize(v);

				sRad = Q_acos(v[0]);
				tRad = Q_acos(v[1]);

				s_cloudTexCoords[i][t][s][0] = sRad;
				s_cloudTexCoords[i][t][s][1] = tRad;
			}
		}
	}
}

/// Either tessellate, or do a dry run to calculate the total number of vertices and indices to use.
static void TessellateCloudBox(Vertex *vertices, uint16_t *indices, uint32_t *nVertices, uint32_t *nIndices, float zMax)
{
	assert((vertices && indices) || (nVertices && nIndices));

	sky_min = 1.0 / 256.0f;		// FIXME: not correct?
	sky_max = 255.0 / 256.0f;
	uint32_t currentVertex = 0, currentIndex = 0;

	for (int sideIndex = 0; sideIndex < 6; sideIndex++)
	{
		int sky_mins_subd[2], sky_maxs_subd[2];
		float MIN_T;

		if (1) // FIXME? shader->sky.fullClouds)
		{
			MIN_T = -HALF_SKY_SUBDIVISIONS;

			// still don't want to draw the bottom, even if fullClouds
			if (sideIndex == 5)
				continue;
		}
		else
		{
			switch(sideIndex)
			{
			case 0:
			case 1:
			case 2:
			case 3:
				MIN_T = -1;
				break;
			case 5:
				// don't draw clouds beneath you
				continue;
			case 4:		// top
			default:
				MIN_T = -HALF_SKY_SUBDIVISIONS;
				break;
			}
		}

		sky_mins[0][sideIndex] = floor(sky_mins[0][sideIndex] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;
		sky_mins[1][sideIndex] = floor(sky_mins[1][sideIndex] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;
		sky_maxs[0][sideIndex] = ceil(sky_maxs[0][sideIndex] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;
		sky_maxs[1][sideIndex] = ceil(sky_maxs[1][sideIndex] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;

		if ((sky_mins[0][sideIndex] >= sky_maxs[0][sideIndex]) || (sky_mins[1][sideIndex] >= sky_maxs[1][sideIndex]))
			continue;

		sky_mins_subd[0] = ri.ftol(sky_mins[0][sideIndex] * HALF_SKY_SUBDIVISIONS);
		sky_mins_subd[1] = ri.ftol(sky_mins[1][sideIndex] * HALF_SKY_SUBDIVISIONS);
		sky_maxs_subd[0] = ri.ftol(sky_maxs[0][sideIndex] * HALF_SKY_SUBDIVISIONS);
		sky_maxs_subd[1] = ri.ftol(sky_maxs[1][sideIndex] * HALF_SKY_SUBDIVISIONS);

		if (sky_mins_subd[0] < -HALF_SKY_SUBDIVISIONS) 
			sky_mins_subd[0] = -HALF_SKY_SUBDIVISIONS;
		else if (sky_mins_subd[0] > HALF_SKY_SUBDIVISIONS) 
			sky_mins_subd[0] = HALF_SKY_SUBDIVISIONS;
		if (sky_mins_subd[1] < MIN_T)
			sky_mins_subd[1] = MIN_T;
		else if (sky_mins_subd[1] > HALF_SKY_SUBDIVISIONS) 
			sky_mins_subd[1] = HALF_SKY_SUBDIVISIONS;

		if (sky_maxs_subd[0] < -HALF_SKY_SUBDIVISIONS) 
			sky_maxs_subd[0] = -HALF_SKY_SUBDIVISIONS;
		else if (sky_maxs_subd[0] > HALF_SKY_SUBDIVISIONS) 
			sky_maxs_subd[0] = HALF_SKY_SUBDIVISIONS;
		if (sky_maxs_subd[1] < MIN_T)
			sky_maxs_subd[1] = MIN_T;
		else if (sky_maxs_subd[1] > HALF_SKY_SUBDIVISIONS) 
			sky_maxs_subd[1] = HALF_SKY_SUBDIVISIONS;

		// iterate through the subdivisions
		for (int t = sky_mins_subd[1]+HALF_SKY_SUBDIVISIONS; t <= sky_maxs_subd[1]+HALF_SKY_SUBDIVISIONS; t++)
		{
			for (int s = sky_mins_subd[0]+HALF_SKY_SUBDIVISIONS; s <= sky_maxs_subd[0]+HALF_SKY_SUBDIVISIONS; s++)
			{
				MakeSkyVec(zMax, (s - HALF_SKY_SUBDIVISIONS) / (float) HALF_SKY_SUBDIVISIONS, 
							(t - HALF_SKY_SUBDIVISIONS) / (float) HALF_SKY_SUBDIVISIONS, 
							sideIndex, 
							NULL,
							s_skyPoints[t][s]);

				s_skyTexCoords[t][s][0] = s_cloudTexCoords[sideIndex][t][s][0];
				s_skyTexCoords[t][s][1] = s_cloudTexCoords[sideIndex][t][s][1];
			}
		}

		int tHeight = sky_maxs_subd[1] - sky_mins_subd[1] + 1;
		int sWidth = sky_maxs_subd[0] - sky_mins_subd[0] + 1;
		const uint32_t startVertex = currentVertex;

		for (int t = sky_mins_subd[1]+HALF_SKY_SUBDIVISIONS; t <= sky_maxs_subd[1]+HALF_SKY_SUBDIVISIONS; t++)
		{
			for (int s = sky_mins_subd[0]+HALF_SKY_SUBDIVISIONS; s <= sky_maxs_subd[0]+HALF_SKY_SUBDIVISIONS; s++)
			{
				if (vertices)
				{
					vertices[currentVertex].pos = vec3(s_skyPoints[t][s]) + g_main->cameraPosition;
					vertices[currentVertex].texCoord[0] = s_skyTexCoords[t][s][0];
					vertices[currentVertex].texCoord[1] = s_skyTexCoords[t][s][1];
				}

				currentVertex++;
			}
		}

		for (int t = 0; t < tHeight-1; t++)
		{	
			for (int s = 0; s < sWidth-1; s++)
			{
				if (indices)
				{
					indices[currentIndex + 0] = startVertex + s + t * (sWidth);
					indices[currentIndex + 1] = startVertex + s + (t + 1) * (sWidth);
					indices[currentIndex + 2] = startVertex + s + 1 + t * (sWidth);
					indices[currentIndex + 3] = startVertex + s + (t + 1) * (sWidth);
					indices[currentIndex + 4] = startVertex + s + 1 + (t + 1) * (sWidth);
					indices[currentIndex + 5] = startVertex + s + 1 + t * (sWidth);
				}
				
				currentIndex += 6;
			}
		}
	}

	if (nVertices && nIndices)
	{
		*nVertices = currentVertex;
		*nIndices = currentIndex;
	}
}

void Sky_Render(DrawCallList *drawCallList, vec3 viewPosition, uint8_t visCacheId, float zMax)
{
	assert(drawCallList);

	if (!g_main->world.get())
		return;

	auto mat = g_main->world->getSkyMaterial(visCacheId);

	if (mat == nullptr)
		return;

	auto &vertices = g_main->world->getSkyVertices(visCacheId);
	assert(!vertices.empty());

	// Clear sky box.
	for (size_t i = 0; i < 6; i++)
	{
		sky_mins[0][i] = sky_mins[1][i] = 9999;
		sky_maxs[0][i] = sky_maxs[1][i] = -9999;
	}

	// Clip sky polygons.
	for (size_t i = 0; i < vertices.size(); i += 3)
	{
		vec3_t p[5]; // need one extra point for clipping

		for (size_t j = 0 ; j < 3 ; j++) 
		{
			VectorSubtract(vertices[i + j].pos, viewPosition, p[j]);
		}

		ClipSkyPolygon(3, p[0], 0);
	}

	// Draw the clouds.
	if (mat->sky.cloudHeight > 0)
	{
		uint32_t nVertices, nIndices;
		TessellateCloudBox(nullptr, nullptr, &nVertices, &nIndices, zMax);
		DrawCall drawCall;

		if (!bgfx::allocTransientBuffers(&drawCall.vb.transientHandle, Vertex::decl, nVertices, &drawCall.ib.transientHandle, nIndices)) 
			return;

		TessellateCloudBox((Vertex *)drawCall.vb.transientHandle.data, (uint16_t *)drawCall.ib.transientHandle.data, nullptr, nullptr, zMax);
		drawCall.vb.type = drawCall.ib.type = DrawCall::BufferType::Transient;
		drawCall.material = mat;

		// Write depth as 1.
		drawCall.zOffset = 1.0f;
		drawCall.zScale = 0.0f;

		drawCallList->push_back(drawCall);
	}
}

} // namespace renderer
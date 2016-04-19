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
/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of the Return to Castle Wolfenstein single player GPL Source Code (RTCW SP Source Code).

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the RTCW SP Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the RTCW SP Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#include "Precompiled.h"
#pragma hdrstop

#if defined(ENGINE_IORTCW)

namespace renderer {

/*
==============================================================================

MDS file format (Wolfenstein Skeletal Format)

==============================================================================
*/

#define MDS_IDENT           ( ( 'W' << 24 ) + ( 'S' << 16 ) + ( 'D' << 8 ) + 'M' )
#define MDS_VERSION         4
#define MDS_MAX_VERTS       6000
#define MDS_MAX_TRIANGLES   8192
#define MDS_MAX_BONES       128
#define MDS_MAX_SURFACES    32
#define MDS_MAX_TAGS        128

#define MDS_TRANSLATION_SCALE   ( 1.0 / 64 )

typedef struct {
	int boneIndex;              // these are indexes into the boneReferences,
	float boneWeight;           // not the global per-frame bone list
	vec3_t offset;
} mdsWeight_t;

typedef struct {
	vec3_t normal;
	vec2_t texCoords;
	int numWeights;
	int fixedParent;            // stay equi-distant from this parent
	float fixedDist;
	mdsWeight_t weights[1];     // variable sized
} mdsVertex_t;

typedef struct {
	int indexes[3];
} mdsTriangle_t;

typedef struct {
	int ident;

	char name[MAX_QPATH];           // polyset name
	char shader[MAX_QPATH];
	int shaderIndex;                // for in-game use

	int minLod;

	int ofsHeader;                  // this will be a negative number

	int numVerts;
	int ofsVerts;

	int numTriangles;
	int ofsTriangles;

	int ofsCollapseMap;           // numVerts * int

									// Bone references are a set of ints representing all the bones
									// present in any vertex weights for this surface.  This is
									// needed because a model may have surfaces that need to be
									// drawn at different sort times, and we don't want to have
									// to re-interpolate all the bones for each surface.
	int numBoneReferences;
	int ofsBoneReferences;

	int ofsEnd;                     // next surface follows
} mdsSurface_t;

typedef struct {
	//float		angles[3];
	//float		ofsAngles[2];
	short angles[4];            // to be converted to axis at run-time (this is also better for lerping)
	short ofsAngles[2];         // PITCH/YAW, head in this direction from parent to go to the offset position
} mdsBoneFrameCompressed_t;

// NOTE: this only used at run-time
typedef struct {
	float matrix[3][3];             // 3x3 rotation
	vec3_t translation;             // translation vector
} mdsBoneFrame_t;

typedef struct {
	vec3_t bounds[2];               // bounds of all surfaces of all LOD's for this frame
	vec3_t localOrigin;             // midpoint of bounds, used for sphere cull
	float radius;                   // dist from localOrigin to corner
	vec3_t parentOffset;            // one bone is an ascendant of all other bones, it starts the hierachy at this position
	mdsBoneFrameCompressed_t bones[1];              // [numBones]
} mdsFrame_t;

typedef struct {
	int numSurfaces;
	int ofsSurfaces;                // first surface, others follow
	int ofsEnd;                     // next lod follows
} mdsLOD_t;

typedef struct {
	char name[MAX_QPATH];           // name of tag
	float torsoWeight;
	int boneIndex;                  // our index in the bones
} mdsTag_t;

#define BONEFLAG_TAG        1       // this bone is actually a tag

typedef struct {
	char name[MAX_QPATH];           // name of bone
	int parent;                     // not sure if this is required, no harm throwing it in
	float torsoWeight;              // scale torso rotation about torsoParent by this
	float parentDist;
	int flags;
} mdsBoneInfo_t;

typedef struct {
	int ident;
	int version;

	char name[MAX_QPATH];           // model name

	float lodScale;
	float lodBias;

	// frames and bones are shared by all levels of detail
	int numFrames;
	int numBones;
	int ofsFrames;                  // mdsFrame_t[numFrames]
	int ofsBones;                   // mdsBoneInfo_t[numBones]
	int torsoParent;                // index of bone that is the parent of the torso

	int numSurfaces;
	int ofsSurfaces;

	// tag data
	int numTags;
	int ofsTags;                    // mdsTag_t[numTags]

	int ofsEnd;                     // end of file
} mdsHeader_t;

struct mat4wrapper
{
	const vec4 &operator[](size_t column) const
	{
		return *((vec4 *)&m[column * 4]);
	}

	vec4 &operator[](size_t column)
	{
		return *((vec4 *)&m[column * 4]);
	}

	mat4 m;
};

class Model_mds : public Model
{
public:
	Model_mds(const char *name);
	bool load(const ReadOnlyFile &file) override;
	Bounds getBounds() const override;
	Material *getMaterial(size_t surfaceNo) const override { return nullptr; }
	bool isCulled(Entity *entity, const Frustum &cameraFrustum) const override;
	int lerpTag(const char *name, const Entity &entity, int startIndex, Transform *transform) const override;
	void render(const mat3 &sceneRotation, DrawCallList *drawCallList, Entity *entity) override;

private:
	struct Bone
	{
		mat3 rotation;
		vec3 translation;
	};

	struct BoneInfo
	{
		int flags;
		char name[MAX_QPATH];
		int parent;
		float parentDist;
		float torsoWeight;
	};

	struct BoneFrameCompressed
	{
		short angles[4];
		short ofsAngles[2];
	};

	struct Frame
	{
		std::vector<BoneFrameCompressed> boneFrames;
		Bounds bounds;
		vec3 parentOffset;
		vec3 position;
		float radius;
	};

	struct Surface
	{
		Material *material;
		uint32_t nIndices;
		uint32_t nVertices;
	};

	struct Tag
	{
		int boneIndex;
		char name[MAX_QPATH];
		float torsoWeight;
	};

	struct Skeleton
	{
		Bone bones[MDS_MAX_BONES];
		bool boneCalculated[MDS_MAX_BONES] = { false };
		const Frame *frame, *oldFrame;
		const Frame *torsoFrame, *oldTorsoFrame;
		float frontLerp, backLerp;
		float torsoFrontLerp, torsoBackLerp;
	};

	void recursiveBoneListAdd(int boneIndex, int *boneList, int *nBones) const;
	Bone calculateBoneRaw(const Entity &entity, int boneIndex, const Skeleton &skeleton) const;
	Bone calculateBoneLerp(const Entity &entity, int boneIndex, const Skeleton &skeleton) const;
	Bone calculateBone(const Entity &entity, int boneIndex, const Skeleton &skeleton, bool lerp) const;
	Skeleton calculateSkeleton(const Entity &entity, int *boneList, int nBones) const;

	std::vector<uint8_t> data_;
	std::vector<BoneInfo> boneInfo_;
	std::vector<Frame> frames_;
	std::vector<Surface> surfaces_;
	std::vector<Material *> surfaceMaterials_;
	std::vector<Tag> tags_;
	int torsoParent_; // index of bone that is the parent of the torso
};

std::unique_ptr<Model> Model::createMDS(const char *name)
{
	return std::make_unique<Model_mds>(name);
}

Model_mds::Model_mds(const char *name)
{
	util::Strncpyz(name_, name, sizeof(name_));
}

bool Model_mds::load(const ReadOnlyFile &file)
{
	const uint8_t *data = file.getData();
	data_.resize(file.getLength());
	memcpy(data_.data(), file.getData(), file.getLength());

	// Header
	auto fileHeader = (mdsHeader_t *)data;

	if (fileHeader->ident != MDS_IDENT)
	{
		interface::PrintWarningf("Model %s: wrong ident (%i should be %i)\n", name_, fileHeader->ident, MDS_IDENT);
		return false;
	}

	if (fileHeader->version != MDS_VERSION)
	{
		interface::PrintWarningf("Model %s: wrong version (%i should be %i)\n", name_, fileHeader->version, MDS_VERSION);
		return false;
	}

	if (fileHeader->numFrames < 1)
	{
		interface::PrintWarningf("Model %s: no frames\n", name_);
		return false;
	}

	torsoParent_ = fileHeader->torsoParent;

	// Bone info
	boneInfo_.resize(fileHeader->numBones);

	for (size_t i = 0; i < boneInfo_.size(); i++)
	{
		const auto &fbi = ((mdsBoneInfo_t *)(data + fileHeader->ofsBones))[i];
		BoneInfo &bi = boneInfo_[i];
		bi.flags = fbi.flags;
		util::Strncpyz(bi.name, fbi.name, sizeof(bi.name));
		bi.parent = fbi.parent;
		bi.parentDist = fbi.parentDist;
		bi.torsoWeight = fbi.torsoWeight;
	}

	// Frames
	frames_.resize(fileHeader->numFrames);

	for (size_t i = 0; i < frames_.size(); i++)
	{
		const size_t frameSize = sizeof(mdsFrame_t) - sizeof(mdsBoneFrameCompressed_t) + fileHeader->numBones * sizeof(mdsBoneFrameCompressed_t);
		const mdsFrame_t &ff = *((mdsFrame_t *)(data + fileHeader->ofsFrames + i * frameSize));
		Frame &f = frames_[i];
		f.boneFrames.resize(fileHeader->numBones);
		f.bounds = Bounds(ff.bounds[0], ff.bounds[1]);
		f.parentOffset = ff.parentOffset;
		f.position = ff.localOrigin;
		f.radius = ff.radius;

		for (size_t j = 0; j < f.boneFrames.size(); j++)
		{
			//const mdsBoneFrameCompressed_t &fbfc = ((mdsBoneFrameCompressed_t *)((uint8_t *)&ff + sizeof(mdsFrame_t) - sizeof(mdsBoneFrameCompressed_t)))[j];
			const mdsBoneFrameCompressed_t &fbfc = ff.bones[j];
			BoneFrameCompressed &bfc = f.boneFrames[j];
			memcpy(bfc.angles, fbfc.angles, sizeof(bfc.angles));
			memcpy(bfc.ofsAngles, fbfc.ofsAngles, sizeof(bfc.ofsAngles));
		}
	}

	// Surfaces
	surfaces_.resize(fileHeader->numSurfaces);
	surfaceMaterials_.resize(fileHeader->numSurfaces);
	auto fileSurface = (mdsSurface_t *)(data + fileHeader->ofsSurfaces);

	for (size_t i = 0; i < surfaces_.size(); i++)
	{
		const auto &fs = *fileSurface;
		Surface &s = surfaces_[i];

		if (fs.shader[0])
		{
			surfaceMaterials_[i] = s.material = g_materialCache->findMaterial(fs.shader, MaterialLightmapId::None);
		}
		else
		{
			s.material = nullptr;
		}

		s.nIndices = int32_t(fs.numTriangles * 3);
		s.nVertices = int32_t(fs.numVerts);

		// Move to the next surface.
		fileSurface = (mdsSurface_t *)((uint8_t *)fileSurface + fileSurface->ofsEnd);
	}

	// Tags
	tags_.resize(fileHeader->numTags);

	for (size_t i = 0; i < tags_.size(); i++)
	{
		const auto &ft = ((mdsTag_t *)(data + fileHeader->ofsTags))[i];
		Tag &t = tags_[i];
		t.boneIndex = ft.boneIndex;
		util::Strncpyz(t.name, ft.name, sizeof(t.name));
		t.torsoWeight = ft.torsoWeight;
	}

	return true;
}

Bounds Model_mds::getBounds() const
{
	return Bounds();
}

bool Model_mds::isCulled(Entity *entity, const Frustum &cameraFrustum) const
{
	assert(entity);
	return false;
}

int Model_mds::lerpTag(const char *name, const Entity &entity, int startIndex, Transform *transform) const
{
	assert(transform);

	for (int i = 0; i < (int)tags_.size(); i++)
	{
		const Tag &tag = tags_[i];

		if (i >= startIndex && !strcmp(tags_[i].name, name))
		{
			// Now build the list of bones we need to calc to get this tag's bone information.
			int boneList[MDS_MAX_BONES];
			int nBones = 0;
			recursiveBoneListAdd(tags_[i].boneIndex, boneList, &nBones);

			// Calculate the skeleton.
			Skeleton skeleton = calculateSkeleton(entity, boneList, nBones);

			// Now extract the transform for the bone that represents our tag.
			transform->position = skeleton.bones[tag.boneIndex].translation;
			transform->rotation = skeleton.bones[tag.boneIndex].rotation;
			return i;
		}
	}

	return -1;
}

static void LocalAddScaledMatrixTransformVectorTranslate(vec3 in, float s, const mat3 &mat, vec3 tr, vec3 &out)
{
	out[0] += s * (in[0] * mat[0][0] + in[1] * mat[0][1] + in[2] * mat[0][2] + tr[0]);
	out[1] += s * (in[0] * mat[1][0] + in[1] * mat[1][1] + in[2] * mat[1][2] + tr[1]);
	out[2] += s * (in[0] * mat[2][0] + in[1] * mat[2][1] + in[2] * mat[2][2] + tr[2]);
}

void Model_mds::render(const mat3 &sceneRotation, DrawCallList *drawCallList, Entity *entity)
{
	assert(drawCallList);
	assert(entity);

	// It is possible to have a bad frame while changing models.
	const int frameIndex = Clamped(entity->frame, 0, (int)frames_.size() - 1);
	const int oldFrameIndex = Clamped(entity->oldFrame, 0, (int)frames_.size() - 1);
	const mat4 modelMatrix = mat4::transform(entity->rotation, entity->position);

	auto header = (mdsHeader_t *)data_.data();
	auto surface = (mdsSurface_t *)(data_.data() + header->ofsSurfaces);

	for (int i = 0; i < header->numSurfaces; i++)
	{
		Material *mat = surfaceMaterials_[i];

		if (entity->customMaterial > 0)
		{
			mat = g_materialCache->getMaterial(entity->customMaterial);
		}
		else if (entity->customSkin > 0)
		{
			Skin *skin = g_materialCache->getSkin(entity->customSkin);
			Material *customMat = skin ? skin->findMaterial(surface->name) : nullptr;

			if (customMat)
				mat = customMat;
		}

		bgfx::TransientIndexBuffer tib;
		bgfx::TransientVertexBuffer tvb;
		assert(surface->numVerts > 0);
		assert(surface->numTriangles > 0);

		if (!bgfx::allocTransientBuffers(&tvb, Vertex::decl, surface->numVerts, &tib, surface->numTriangles * 3))
		{
			WarnOnce(WarnOnceId::TransientBuffer);
			return;
		}

		auto indices = (uint16_t *)tib.data;
		auto vertices = (Vertex *)tvb.data;
		auto mdsIndices = (const int *)((uint8_t *)surface + surface->ofsTriangles);

		for (int i = 0; i < surface->numTriangles * 3; i++)
		{
			indices[i] = mdsIndices[i];
		}

		Skeleton skeleton = calculateSkeleton(*entity, (int *)((uint8_t *)surface + surface->ofsBoneReferences), surface->numBoneReferences);
		auto mdsVertex = (const mdsVertex_t *)((uint8_t *)surface + surface->ofsVerts);

		for (int i = 0; i < surface->numVerts; i++)
		{
			Vertex &v = vertices[i];
			v.pos = vec3::empty;

			for (int j = 0; j < mdsVertex->numWeights; j++)
			{
				const mdsWeight_t &weight = mdsVertex->weights[j];
				const Bone &bone = skeleton.bones[weight.boneIndex];
				LocalAddScaledMatrixTransformVectorTranslate(weight.offset, weight.boneWeight, bone.rotation, bone.translation, v.pos);
			}
			
			v.normal = mdsVertex->normal;
			v.texCoord = mdsVertex->texCoords;
			v.color = vec4::white;

			// Move to the next vertex.
			mdsVertex = (mdsVertex_t *)&mdsVertex->weights[mdsVertex->numWeights];
		}

		DrawCall dc;
		dc.entity = entity;
		dc.fogIndex = -1;
		dc.material = mat;
		dc.modelMatrix = modelMatrix;
		dc.vb.type = DrawCall::BufferType::Transient;
		dc.vb.transientHandle = tvb;
		dc.vb.nVertices = surface->numVerts;
		dc.ib.type = DrawCall::BufferType::Transient;
		dc.ib.transientHandle = tib;
		dc.ib.nIndices = surface->numTriangles * 3;
		drawCallList->push_back(dc);

		// Move to the next surface.
		surface = (mdsSurface_t *)((uint8_t *)surface + surface->ofsEnd);
	}
}

void Model_mds::recursiveBoneListAdd(int boneIndex, int *boneList, int *nBones) const
{
	assert(boneList);
	assert(nBones);

	if (boneInfo_[boneIndex].parent >= 0)
	{
		recursiveBoneListAdd(boneInfo_[boneIndex].parent, boneList, nBones);
	}

	boneList[(*nBones)++] = boneIndex;
}

// TTimo: const vec_t ** would require explicit casts for ANSI C conformance
// see unix/const-arg.c in Wolf MP source
static void Matrix4MultiplyInto3x3AndTranslation(const mat4wrapper &a, const mat4wrapper &b, mat3 &dst, vec3 &t)
{
	dst[0][0] = a[0][0] * b[0][0] + a[0][1] * b[1][0] + a[0][2] * b[2][0] + a[0][3] * b[3][0];
	dst[0][1] = a[0][0] * b[0][1] + a[0][1] * b[1][1] + a[0][2] * b[2][1] + a[0][3] * b[3][1];
	dst[0][2] = a[0][0] * b[0][2] + a[0][1] * b[1][2] + a[0][2] * b[2][2] + a[0][3] * b[3][2];
	t[0] = a[0][0] * b[0][3] + a[0][1] * b[1][3] + a[0][2] * b[2][3] + a[0][3] * b[3][3];

	dst[1][0] = a[1][0] * b[0][0] + a[1][1] * b[1][0] + a[1][2] * b[2][0] + a[1][3] * b[3][0];
	dst[1][1] = a[1][0] * b[0][1] + a[1][1] * b[1][1] + a[1][2] * b[2][1] + a[1][3] * b[3][1];
	dst[1][2] = a[1][0] * b[0][2] + a[1][1] * b[1][2] + a[1][2] * b[2][2] + a[1][3] * b[3][2];
	t[1] = a[1][0] * b[0][3] + a[1][1] * b[1][3] + a[1][2] * b[2][3] + a[1][3] * b[3][3];

	dst[2][0] = a[2][0] * b[0][0] + a[2][1] * b[1][0] + a[2][2] * b[2][0] + a[2][3] * b[3][0];
	dst[2][1] = a[2][0] * b[0][1] + a[2][1] * b[1][1] + a[2][2] * b[2][1] + a[2][3] * b[3][1];
	dst[2][2] = a[2][0] * b[0][2] + a[2][1] * b[1][2] + a[2][2] * b[2][2] + a[2][3] * b[3][2];
	t[2] = a[2][0] * b[0][3] + a[2][1] * b[1][3] + a[2][2] * b[2][3] + a[2][3] * b[3][3];
}

// can put an axis rotation followed by a translation directly into one matrix
// TTimo: const vec_t ** would require explicit casts for ANSI C conformance
// see unix/const-arg.c in Wolf MP source
static void Matrix4FromAxisPlusTranslation(const mat3 &axis, const vec3 t, mat4wrapper &dst)
{
	int i, j;
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			dst[i][j] = axis[i][j];
		}
		dst[3][i] = 0;
		dst[i][3] = t[i];
	}
	dst[3][3] = 1;
}

// can put a scaled axis rotation followed by a translation directly into one matrix
// TTimo: const vec_t ** would require explicit casts for ANSI C conformance
// see unix/const-arg.c in Wolf MP source
static void Matrix4FromScaledAxisPlusTranslation(const mat3 &axis, const float scale, const vec3 t, mat4wrapper &dst)
{
	int i, j;

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			dst[i][j] = scale * axis[i][j];
			if (i == j) {
				dst[i][j] += 1.0f - scale;
			}
		}
		dst[3][i] = 0;
		dst[i][3] = t[i];
	}
	dst[3][3] = 1;
}

static vec3 LocalScaledMatrixTransformVector(vec3 in, float s, const mat3 &mat)
{
	vec3 out;
	out[0] = (1.0f - s) * in[0] + s * (in[0] * mat[0][0] + in[1] * mat[0][1] + in[2] * mat[0][2]);
	out[1] = (1.0f - s) * in[1] + s * (in[0] * mat[1][0] + in[1] * mat[1][1] + in[2] * mat[1][2]);
	out[2] = (1.0f - s) * in[2] + s * (in[0] * mat[2][0] + in[1] * mat[2][1] + in[2] * mat[2][2]);
	return out;
}

static vec3 LocalAngleVector(const vec3 angles)
{
	float LAVangle = angles[YAW] * ((float)M_PI * 2 / 360);
	float sy = sin(LAVangle);
	float cy = cos(LAVangle);
	LAVangle = angles[PITCH] * ((float)M_PI * 2 / 360);
	float sp = sin(LAVangle);
	float cp = cos(LAVangle);

	vec3 forward;
	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = -sp;
	return forward;
}

#define SHORT2ANGLE( x )  ( ( x ) * ( 360.0f / 65536 ) )

static vec3 SLerp_Normal(const vec3 from, const vec3 to, float tt)
{
	vec3 out;
	const float ft = 1.0f - tt;
	out[0] = from[0] * ft + to[0] * tt;
	out[1] = from[1] * ft + to[1] * tt;
	out[2] = from[2] * ft + to[2] * tt;
	return out.normal();
}

/*
=================
AngleNormalize360

returns angle normalized to the range [0 <= angle < 360]
=================
*/
static float AngleNormalize360(float angle) {
	return (360.0f / 65536) * ((int)(angle * (65536 / 360.0f)) & 65535);
}

/*
=================
AngleNormalize180

returns angle normalized to the range [-180 < angle <= 180]
=================
*/
static float AngleNormalize180(float angle) {
	angle = AngleNormalize360(angle);
	if (angle > 180.0) {
		angle -= 360.0;
	}
	return angle;
}

Model_mds::Bone Model_mds::calculateBoneRaw(const Entity &entity, int boneIndex, const Skeleton &skeleton) const
{
	const BoneInfo &bi = boneInfo_[boneIndex];
	bool isTorso = false, fullTorso = false;
	const BoneFrameCompressed *compressedTorsoBone = nullptr;

	if (bi.torsoWeight)
	{
		compressedTorsoBone = &skeleton.torsoFrame->boneFrames[boneIndex];
		isTorso = true;

		if (bi.torsoWeight == 1.0f)
		{
			fullTorso = true;
		}
	}

	const BoneFrameCompressed &compressedBone = skeleton.frame->boneFrames[boneIndex];
	Bone bone;

	// we can assume the parent has already been uncompressed for this frame + lerp
	const Bone *parentBone = nullptr;

	if (bi.parent >= 0)
	{
		parentBone = &skeleton.bones[bi.parent];
	}

	// rotation
	vec3 angles;

	if (fullTorso)
	{
		for (int i = 0; i < 3; i++)
			angles[i] = SHORT2ANGLE(compressedTorsoBone->angles[i]);
	}
	else
	{
		for (int i = 0; i < 3; i++)
			angles[i] = SHORT2ANGLE(compressedBone.angles[i]);

		if (isTorso)
		{
			vec3 torsoAngles;

			for (int i = 0; i < 3; i++)
				torsoAngles[i] = SHORT2ANGLE(compressedTorsoBone->angles[i]);

			// blend the angles together
			for (int i = 0; i < 3; i++)
			{
				float diff = torsoAngles[i] - angles[i];

				if (fabs(diff) > 180)
					diff = AngleNormalize180(diff);

				angles[i] = angles[i] + bi.torsoWeight * diff;
			}
		}
	}

	bone.rotation = mat3(angles);

	// translation
	if (parentBone)
	{
		vec3 vec;

		if (fullTorso)
		{
			angles[0] = SHORT2ANGLE(compressedTorsoBone->ofsAngles[0]);
			angles[1] = SHORT2ANGLE(compressedTorsoBone->ofsAngles[1]);
			angles[2] = 0;
			vec = LocalAngleVector(angles);
		}
		else
		{
			angles[0] = SHORT2ANGLE(compressedBone.ofsAngles[0]);
			angles[1] = SHORT2ANGLE(compressedBone.ofsAngles[1]);
			angles[2] = 0;
			vec = LocalAngleVector(angles);

			if (isTorso)
			{
				vec3 torsoAngles;
				torsoAngles[0] = SHORT2ANGLE(compressedTorsoBone->ofsAngles[0]);
				torsoAngles[1] = SHORT2ANGLE(compressedTorsoBone->ofsAngles[1]);
				torsoAngles[2] = 0;
				vec3 v2 = LocalAngleVector(torsoAngles);

				// blend the angles together
				vec = SLerp_Normal(vec, v2, bi.torsoWeight);
			}
		}

		bone.translation = parentBone->translation + vec * bi.parentDist;
	}
	else // just use the frame position
	{
		bone.translation = frames_[entity.frame].parentOffset;
	}

	return bone;
}

Model_mds::Bone Model_mds::calculateBoneLerp(const Entity &entity, int boneIndex, const Skeleton &skeleton) const
{
	const BoneInfo &bi = boneInfo_[boneIndex];
	const Bone *parentBone = nullptr;

	if (bi.parent >= 0)
	{
		parentBone = &skeleton.bones[bi.parent];
	}

	bool isTorso = false, fullTorso = false;
	const BoneFrameCompressed *compressedTorsoBone = nullptr, *oldCompressedTorsoBone = nullptr;

	if (bi.torsoWeight)
	{
		compressedTorsoBone = &skeleton.torsoFrame->boneFrames[boneIndex];
		oldCompressedTorsoBone = &skeleton.oldTorsoFrame->boneFrames[boneIndex];
		isTorso = true;

		if (bi.torsoWeight == 1.0f)
			fullTorso = true;
	}

	const BoneFrameCompressed &compressedBone = skeleton.frame->boneFrames[boneIndex];
	const BoneFrameCompressed &oldCompressedBone = skeleton.oldFrame->boneFrames[boneIndex];
	Bone bone;

	// rotation (take into account 170 to -170 lerps, which need to take the shortest route)
	vec3 angles;

	if (fullTorso)
	{
		for (int i = 0; i < 3; i++)
		{
			const float a1 = SHORT2ANGLE(compressedTorsoBone->angles[i]);
			const float a2 = SHORT2ANGLE(oldCompressedTorsoBone->angles[i]);
			const float diff = AngleNormalize180(a1 - a2);
			angles[i] = a1 - skeleton.torsoBackLerp * diff;
		}
	}
	else
	{
		for (int i = 0; i < 3; i++)
		{
			const float a1 = SHORT2ANGLE(compressedBone.angles[i]);
			const float a2 = SHORT2ANGLE(oldCompressedBone.angles[i]);
			const float diff = AngleNormalize180(a1 - a2);
			angles[i] = a1 - skeleton.backLerp * diff;
		}

		if (isTorso)
		{
			vec3 torsoAngles;

			for (int i = 0; i < 3; i++)
			{
				const float a1 = SHORT2ANGLE(compressedTorsoBone->angles[i]);
				const float a2 = SHORT2ANGLE(oldCompressedTorsoBone->angles[i]);
				const float diff = AngleNormalize180(a1 - a2);
				torsoAngles[i] = a1 - skeleton.torsoBackLerp * diff;
			}

			// blend the angles together
			for (int j = 0; j < 3; j++)
			{
				float diff = torsoAngles[j] - angles[j];

				if (fabs(diff) > 180)
					diff = AngleNormalize180(diff);

				angles[j] = angles[j] + bi.torsoWeight * diff;
			}
		}
	}

	bone.rotation = mat3(angles);

	if (parentBone)
	{
		const short *sh1, *sh2;

		if (fullTorso)
		{
			sh1 = compressedTorsoBone->ofsAngles;
			sh2 = oldCompressedTorsoBone->ofsAngles;
		}
		else
		{
			sh1 = compressedBone.ofsAngles;
			sh2 = oldCompressedBone.ofsAngles;
		}

		angles[0] = SHORT2ANGLE(sh1[0]);
		angles[1] = SHORT2ANGLE(sh1[1]);
		angles[2] = 0;
		vec3 v2 = LocalAngleVector(angles); // new

		angles[0] = SHORT2ANGLE(sh2[0]);
		angles[1] = SHORT2ANGLE(sh2[1]);
		angles[2] = 0;
		vec3 vec = LocalAngleVector(angles); // old

		// blend the angles together
		vec3 dir;

		if (fullTorso)
		{
			dir = SLerp_Normal(vec, v2, skeleton.torsoFrontLerp);
		}
		else
		{
			dir = SLerp_Normal(vec, v2, skeleton.frontLerp);
		}

		// translation
		if (!fullTorso && isTorso)
		{
			// partial legs/torso, need to lerp according to torsoWeight
			// calc the torso frame
			angles[0] = SHORT2ANGLE(compressedTorsoBone->ofsAngles[0]);
			angles[1] = SHORT2ANGLE(compressedTorsoBone->ofsAngles[1]);
			angles[2] = 0;
			vec3 v2 = LocalAngleVector(angles); // new

			angles[0] = SHORT2ANGLE(oldCompressedTorsoBone->ofsAngles[0]);
			angles[1] = SHORT2ANGLE(oldCompressedTorsoBone->ofsAngles[1]);
			angles[2] = 0;
			vec3 vec = LocalAngleVector(angles); // old

			// blend the angles together
			v2 = SLerp_Normal(vec, v2, skeleton.torsoFrontLerp);

			// blend the torso/legs together
			dir = SLerp_Normal(dir, v2, bi.torsoWeight);
		}

		bone.translation = parentBone->translation + dir * bi.parentDist;
	}
	else
	{
		// just interpolate the frame positions
		const Frame &frame = frames_[entity.frame], &oldFrame = frames_[entity.oldFrame];
		bone.translation[0] = skeleton.frontLerp * frame.parentOffset[0] + skeleton.backLerp * oldFrame.parentOffset[0];
		bone.translation[1] = skeleton.frontLerp * frame.parentOffset[1] + skeleton.backLerp * oldFrame.parentOffset[1];
		bone.translation[2] = skeleton.frontLerp * frame.parentOffset[2] + skeleton.backLerp * oldFrame.parentOffset[2];
	}

	return bone;
}

Model_mds::Bone Model_mds::calculateBone(const Entity &entity, int boneIndex, const Skeleton &skeleton, bool lerp) const
{
	return lerp ? calculateBoneLerp(entity, boneIndex, skeleton) : calculateBoneRaw(entity, boneIndex, skeleton);
}

Model_mds::Skeleton Model_mds::calculateSkeleton(const Entity &entity, int *boneList, int nBones) const
{
	assert(boneList);
	Skeleton skeleton;

	if (entity.oldFrame == entity.frame)
	{
		skeleton.backLerp = 0;
		skeleton.frontLerp = 1;
	}
	else
	{
		skeleton.backLerp = 1.0f - entity.lerp;
		skeleton.frontLerp = entity.lerp;
	}

	if (entity.oldTorsoFrame == entity.torsoFrame)
	{
		skeleton.torsoBackLerp = 0;
		skeleton.torsoFrontLerp = 1;
	}
	else
	{
		skeleton.torsoBackLerp = 1.0f - entity.torsoLerp;
		skeleton.torsoFrontLerp = entity.torsoLerp;
	}

	
	skeleton.frame = &frames_[entity.frame];
	skeleton.oldFrame = &frames_[entity.oldFrame];
	skeleton.torsoFrame = entity.torsoFrame >= 0 && entity.torsoFrame < (int)frames_.size() ? &frames_[entity.torsoFrame] : nullptr;
	skeleton.oldTorsoFrame = entity.oldTorsoFrame >= 0 && entity.oldTorsoFrame < (int)frames_.size() ? &frames_[entity.oldTorsoFrame] : nullptr;

	// Lerp all the needed bones (torsoParent is always the first bone in the list).
	int *boneRefs = boneList;
	mat3 torsoRotation(entity.torsoRotation);
	torsoRotation.transpose();
	const bool lerp = skeleton.backLerp || skeleton.torsoBackLerp;

	for (int i = 0; i < nBones; i++, boneRefs++)
	{
		if (skeleton.boneCalculated[*boneRefs])
			continue;

		// find our parent, and make sure it has been calculated
		const int parentBoneIndex = boneInfo_[*boneRefs].parent;

		if (parentBoneIndex >= 0 && !skeleton.boneCalculated[parentBoneIndex])
		{
			skeleton.bones[parentBoneIndex] = calculateBone(entity, parentBoneIndex, skeleton, lerp);
			skeleton.boneCalculated[parentBoneIndex] = true;
		}

		skeleton.bones[*boneRefs] = calculateBone(entity, *boneRefs, skeleton, lerp);
		skeleton.boneCalculated[*boneRefs] = true;
	}

	// Get the torso parent.
	vec3 torsoParentOffset;
	boneRefs = boneList;

	for (int i = 0; i < nBones; i++, boneRefs++)
	{
		if (*boneRefs == torsoParent_)
		{
			torsoParentOffset = skeleton.bones[*boneRefs].translation;
		}
	}

	// Adjust for torso rotations.
	float torsoWeight = 0;
	boneRefs = boneList;
	mat4wrapper m2;

	for (int i = 0; i < nBones; i++, boneRefs++)
	{
		const BoneInfo &bi = boneInfo_[*boneRefs];
		Bone *bone = &skeleton.bones[*boneRefs];

		// add torso rotation
		if (bi.torsoWeight > 0)
		{
			if (!(bi.flags & BONEFLAG_TAG))
			{
				// 1st multiply with the bone->matrix
				// 2nd translation for rotation relative to bone around torso parent offset
				const vec3 t = bone->translation - torsoParentOffset;
				mat4wrapper m1;
				Matrix4FromAxisPlusTranslation(bone->rotation, t, m1);
				// 3rd scaled rotation
				// 4th translate back to torso parent offset
				// use previously created matrix if available for the same weight
				if (torsoWeight != bi.torsoWeight)
				{
					Matrix4FromScaledAxisPlusTranslation(torsoRotation, bi.torsoWeight, torsoParentOffset, m2);
					torsoWeight = bi.torsoWeight;
				}

				// multiply matrices to create one matrix to do all calculations
				Matrix4MultiplyInto3x3AndTranslation(m2, m1, bone->rotation, bone->translation);
			}
			else // tags require special handling
			{
				// rotate each of the axis by the torsoAngles
				mat3 tempRotation;
				tempRotation[0] = LocalScaledMatrixTransformVector(bone->rotation[0], bi.torsoWeight, torsoRotation);
				tempRotation[1] = LocalScaledMatrixTransformVector(bone->rotation[1], bi.torsoWeight, torsoRotation);
				tempRotation[2] = LocalScaledMatrixTransformVector(bone->rotation[2], bi.torsoWeight, torsoRotation);
				bone->rotation = tempRotation;

				// rotate the translation around the torsoParent
				const vec3 t = bone->translation - torsoParentOffset;
				bone->translation = LocalScaledMatrixTransformVector(t, bi.torsoWeight, torsoRotation);
				bone->translation = bone->translation + torsoParentOffset;
			}
		}
	}

	return skeleton;
}

} // namespace renderer

#endif // ENGINE_IORTCW
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

extern "C"
{
	float R_NoiseGet4f(float x, float y, float z, float t);
	void R_NoiseInit();
}

namespace renderer {

void Material::precalculate()
{
	time_ = g_main->getFloatTime() - timeOffset;

	if (g_main->currentEntity)
	{
		time_ -= g_main->currentEntity->e.shaderTime;
	}

	calculateDeformValues();
}

void Material::setStageShaderUniforms(size_t stageIndex) const
{
	auto &stage = stages[stageIndex];
	assert(stage.active);

	g_main->uniforms->lightType.set(vec4((float)stage.light, 0, 0, 0));

	vec4 generators;
	generators[Uniforms::Generators::TexCoord] = (float)stage.bundles[0].tcGen;

	if (stage.rgbGen == MaterialColorGen::LightingDiffuse)
	{
		generators[Uniforms::Generators::Color] = (float)stage.rgbGen;
	}
	else
	{
		// Not done in shaders.
		generators[Uniforms::Generators::Color] = (float)MaterialColorGen::Identity;
	}

	if (stage.alphaGen == MaterialAlphaGen::LightingSpecular || stage.alphaGen == MaterialAlphaGen::Portal)
	{
		generators[Uniforms::Generators::Alpha] = (float)stage.alphaGen;
	}
	else
	{
		// Not done in shaders.
		generators[Uniforms::Generators::Alpha] = (float)MaterialAlphaGen::Identity;
	}

	generators[Uniforms::Generators::Deform] = (float)deformGen_;
	g_main->uniforms->generators.set(generators);

	if (deformGen_ != MaterialDeformGen::None)
	{
		g_main->uniforms->deformParameters1.set(deformParameters1_);
		g_main->uniforms->deformParameters2.set(deformParameters2_);
	}

	/*if ( input->fogNum ) {
		GLSL_SetUniformVec4(sp, UNIFORM_FOGDISTANCE, fogDistanceVector);
		GLSL_SetUniformVec4(sp, UNIFORM_FOGDEPTH, fogDepthVector);
		GLSL_SetUniformFloat(sp, UNIFORM_FOGEYET, eyeT);
	}*/

	// rgbGen and alphaGen
	vec4 baseColor, vertexColor;
	calculateColors(stage, &baseColor, &vertexColor);
	g_main->uniforms->baseColor.set(baseColor);
	g_main->uniforms->vertexColor.set(vertexColor);

	if (stage.rgbGen == MaterialColorGen::LightingDiffuse)
	{
		assert(g_main->currentEntity);

		g_main->uniforms->ambientLight.set(vec4(g_main->currentEntity->ambientLight / 255.0f, 0));
		g_main->uniforms->directedLight.set(vec4(g_main->currentEntity->directedLight / 255.0f, 0));
		g_main->uniforms->lightDirection.set(vec4(g_main->currentEntity->lightDir, 0));
		g_main->uniforms->modelLightDir.set(g_main->currentEntity->modelLightDir);
		g_main->uniforms->lightRadius.set(vec4(0.0f));
	}

	if (stage.alphaGen == MaterialAlphaGen::Portal)
	{
		g_main->uniforms->portalRange.set(portalRange);
	}

	// tcGen and tcMod
	vec4 texMatrix, texOffTurb;
	calculateTexMods(stage, &texMatrix, &texOffTurb);
	g_main->uniforms->diffuseTextureMatrix.set(texMatrix);
	g_main->uniforms->diffuseTextureOffsetTurbulent.set(texOffTurb);

	if (stage.bundles[0].tcGen == MaterialTexCoordGen::Vector)
	{
		g_main->uniforms->tcGenVector0.set(stage.bundles[0].tcGenVectors[0]);
		g_main->uniforms->tcGenVector1.set(stage.bundles[0].tcGenVectors[1]);
	}

	g_main->uniforms->normalScale.set(stage.normalScale);
	g_main->uniforms->specularScale.set(stage.specularScale);

	// Alpha test
	g_main->uniforms->alphaTest.set((float)stage.alphaTest);
}

void Material::setFogShaderUniforms() const
{
	vec4 generators;
	generators[Uniforms::Generators::Deform] = (float)deformGen_;
	g_main->uniforms->generators.set(generators);

	if (deformGen_ != MaterialDeformGen::None)
	{
		g_main->uniforms->deformParameters1.set(deformParameters1_);
		g_main->uniforms->deformParameters2.set(deformParameters2_);
	}
}

void Material::setStageTextureSamplers(size_t stageIndex) const
{
	auto &stage = stages[stageIndex];
	assert(stage.active);

	setStageTextureSampler(stageIndex, MaterialTextureBundleIndex::DiffuseMap);
	setStageTextureSampler(stageIndex, MaterialTextureBundleIndex::Lightmap);

	if (stage.light != MaterialLight::None)
	{
		// bind textures that are sampled and used in the glsl shader, and
		// bind whiteImage to textures that are sampled but zeroed in the glsl shader
		//
		// alternatives:
		//  - use the last bound texture
		//     -> costs more to sample a higher res texture then throw out the result
		//  - disable texture sampling in glsl shader with #ifdefs, as before
		//     -> increases the number of shaders that must be compiled
		const bool phong = g_main->cvars.normalMapping->integer || g_main->cvars.specularMapping->integer;
		vec4 enableTextures;

		if (stage.light != MaterialLight::None && phong)
		{
			if (stage.bundles[MaterialTextureBundleIndex::NormalMap].textures[0])
			{
				setStageTextureSampler(stageIndex, MaterialTextureBundleIndex::NormalMap);
				enableTextures[0] = 1.0f;
			}
			else if (g_main->cvars.normalMapping->integer)
			{
				g_main->textureCache->getWhiteTexture()->setSampler(MaterialTextureBundleIndex::NormalMap);
			}

			if (stage.bundles[MaterialTextureBundleIndex::Deluxemap].textures[0])
			{
				setStageTextureSampler(stageIndex, MaterialTextureBundleIndex::Deluxemap);
				enableTextures[1] = 1.0f;
			}
			else if (g_main->cvars.deluxeMapping->integer)
			{
				g_main->textureCache->getWhiteTexture()->setSampler(MaterialTextureBundleIndex::Deluxemap);
			}

			if (stage.bundles[MaterialTextureBundleIndex::Specularmap].textures[0])
			{
				setStageTextureSampler(stageIndex, MaterialTextureBundleIndex::Specularmap);
				enableTextures[2] = 1.0f;
			}
			else if (g_main->cvars.specularMapping->integer)
			{
				g_main->textureCache->getWhiteTexture()->setSampler(MaterialTextureBundleIndex::Specularmap);
			}
		}

		g_main->uniforms->enableTextures.set(enableTextures);
	}
}

uint64_t Material::calculateStageState(size_t stageIndex, uint64_t state) const
{
	auto &stage = stages[stageIndex];
	assert(stage.active);

	state |= BGFX_STATE_BLEND_FUNC(stage.blendSrc, stage.blendDst);
	state |= stage.depthTestBits;

	if (stage.depthWrite)
	{
		state |= BGFX_STATE_DEPTH_WRITE;
	}

	if (cullType != MaterialCullType::TwoSided)
	{
		bool cullFront = (cullType == MaterialCullType::FrontSided);

		if (g_main->isMirrorCamera)
			cullFront = !cullFront;

		state |= cullFront ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
	}

	return state;
}

bgfx::ProgramHandle Material::calculateStageShaderProgramHandle(size_t stageIndex) const
{
	auto &stage = stages[stageIndex];
	assert(stage.active);

	int index = 0;

	if (stage.alphaTest != MaterialAlphaTest::None)
	{
		index |= ShaderCache::GenericPermutations::AlphaTest;
	}

	return g_main->shaderCache->getHandle(ShaderProgramId::Generic, index);
}

void Material::setStageTextureSampler(size_t stageIndex, int sampler) const
{
	auto &stage = stages[stageIndex];
	assert(stage.active);
	auto &bundle = stage.bundles[sampler];

	if (!bundle.textures[0])
		return;

	if (bundle.numImageAnimations <= 1)
	{
		bundle.textures[0]->setSampler(sampler);
	}
	else
	{
		// It is necessary to do this messy calc to make sure animations line up exactly with waveforms of the same frequency.
		int index = ri.ftol(time_ * bundle.imageAnimationSpeed * Main::funcTableSize);
		index >>= Main::funcTableSize2;
		index = std::max(0, index); // May happen with shader time offsets.
		index %= bundle.numImageAnimations;
		bundle.textures[index]->setSampler(sampler);
	}
}

#define	WAVEVALUE(table, base, amplitude, phase, freq) ((base) + table[ri.ftol((((phase) + time_ * (freq)) * Main::funcTableSize)) & Main::funcTableMask] * (amplitude))

float *Material::tableForFunc(MaterialWaveformGenFunc func) const
{
	switch(func)
	{
	case MaterialWaveformGenFunc::Sin:
		return g_main->sinTable;
	case MaterialWaveformGenFunc::Triangle:
		return g_main->triangleTable;
	case MaterialWaveformGenFunc::Square:
		return g_main->squareTable;
	case MaterialWaveformGenFunc::Sawtooth:
		return g_main->sawToothTable;
	case MaterialWaveformGenFunc::InverseSawtooth:
		return g_main->inverseSawToothTable;
	case MaterialWaveformGenFunc::None:
	default:
		break;
	}

	ri.Error(ERR_DROP, "TableForFunc called with invalid function '%d' in shader '%s'", func, name);
	return NULL;
}

float Material::evaluateWaveForm(const MaterialWaveForm &wf) const
{
	return WAVEVALUE(tableForFunc(wf.func), wf.base, wf.amplitude, wf.phase, wf.frequency);
}

float Material::evaluateWaveFormClamped(const MaterialWaveForm &wf) const
{
	return math::Clamped(evaluateWaveForm(wf), 0.0f, 1.0f);
}

void Material::calculateTexMods(const MaterialStage &stage, vec4 *outMatrix, vec4 *outOffTurb) const
{
	assert(outMatrix);
	assert(outOffTurb);

	float matrix[6] = { 1, 0, 0, 1, 0, 0 };
	float currentMatrix[6] = { 1, 0, 0, 1, 0, 0 };
	(*outMatrix) = { 1, 0, 0, 1 };
	(*outOffTurb) = { 0, 0, 0, 0 };
	const auto &bundle = stage.bundles[0];

	for (int tm = 0; tm < bundle.numTexMods; tm++)
	{
		switch (bundle.texMods[tm].type)
		{
		case MaterialTexMod::None:
			tm = MaterialTextureBundle::maxTexMods; // break out of for loop
			break;

		case MaterialTexMod::Turbulent:
			calculateTurbulentFactors(bundle.texMods[tm].wave, &(*outOffTurb)[2], &(*outOffTurb)[3]);
			break;

		case MaterialTexMod::EntityTranslate:
			calculateScrollTexMatrix(g_main->currentEntity->e.shaderTexCoord, matrix);
			break;

		case MaterialTexMod::Scroll:
			calculateScrollTexMatrix(bundle.texMods[tm].scroll, matrix);
			break;

		case MaterialTexMod::Scale:
			calculateScaleTexMatrix(bundle.texMods[tm].scale, matrix);
			break;
		
		case MaterialTexMod::Stretch:
			calculateStretchTexMatrix(bundle.texMods[tm].wave,  matrix);
			break;

		case MaterialTexMod::Transform:
			calculateTransformTexMatrix(bundle.texMods[tm], matrix);
			break;

		case MaterialTexMod::Rotate:
			calculateRotateTexMatrix(bundle.texMods[tm].rotateSpeed, matrix);
			break;

		default:
			ri.Error(ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'", bundle.texMods[tm].type, name);
			break;
		}

		switch (bundle.texMods[tm].type)
		{	
		case MaterialTexMod::None:
		case MaterialTexMod::Turbulent:
		default:
			break;

		case MaterialTexMod::EntityTranslate:
		case MaterialTexMod::Scroll:
		case MaterialTexMod::Scale:
		case MaterialTexMod::Stretch:
		case MaterialTexMod::Transform:
		case MaterialTexMod::Rotate:
			(*outMatrix)[0] = matrix[0] * currentMatrix[0] + matrix[2] * currentMatrix[1];
			(*outMatrix)[1] = matrix[1] * currentMatrix[0] + matrix[3] * currentMatrix[1];
			(*outMatrix)[2] = matrix[0] * currentMatrix[2] + matrix[2] * currentMatrix[3];
			(*outMatrix)[3] = matrix[1] * currentMatrix[2] + matrix[3] * currentMatrix[3];

			(*outOffTurb)[0] = matrix[0] * currentMatrix[4] + matrix[2] * currentMatrix[5] + matrix[4];
			(*outOffTurb)[1] = matrix[1] * currentMatrix[4] + matrix[3] * currentMatrix[5] + matrix[5];

			currentMatrix[0] = (*outMatrix)[0];
			currentMatrix[1] = (*outMatrix)[1];
			currentMatrix[2] = (*outMatrix)[2];
			currentMatrix[3] = (*outMatrix)[3];
			currentMatrix[4] = (*outOffTurb)[0];
			currentMatrix[5] = (*outOffTurb)[1];
			break;
		}
	}
}

void Material::calculateTurbulentFactors(const MaterialWaveForm &wf, float *amplitude, float *now) const
{
	*now = wf.phase + time_ * wf.frequency;
	*amplitude = wf.amplitude;
}

void Material::calculateScaleTexMatrix(vec2 scale, float *matrix) const
{
	matrix[0] = scale[0]; matrix[2] = 0.0f;     matrix[4] = 0.0f;
	matrix[1] = 0.0f;     matrix[3] = scale[1]; matrix[5] = 0.0f;
}

void Material::calculateScrollTexMatrix(vec2 scrollSpeed, float *matrix) const
{
	float adjustedScrollS = scrollSpeed[0] * time_;
	float adjustedScrollT = scrollSpeed[1] * time_;

	// clamp so coordinates don't continuously get larger, causing problems with hardware limits
	adjustedScrollS = adjustedScrollS - floor(adjustedScrollS);
	adjustedScrollT = adjustedScrollT - floor(adjustedScrollT);

	matrix[0] = 1.0f; matrix[2] = 0.0f; matrix[4] = adjustedScrollS;
	matrix[1] = 0.0f; matrix[3] = 1.0f; matrix[5] = adjustedScrollT;
}

void Material::calculateStretchTexMatrix(const MaterialWaveForm &wf, float *matrix) const
{
	const float p = 1.0f / evaluateWaveForm(wf);
	matrix[0] = p; matrix[2] = 0; matrix[4] = 0.5f - 0.5f * p;
	matrix[1] = 0; matrix[3] = p; matrix[5] = 0.5f - 0.5f * p;
}

void Material::calculateTransformTexMatrix(const MaterialTexModInfo &tmi, float *matrix) const
{
	matrix[0] = tmi.matrix[0][0]; matrix[2] = tmi.matrix[1][0]; matrix[4] = tmi.translate[0];
	matrix[1] = tmi.matrix[0][1]; matrix[3] = tmi.matrix[1][1]; matrix[5] = tmi.translate[1];
}

void Material::calculateRotateTexMatrix(float degsPerSecond, float *matrix) const
{
	float degs = -degsPerSecond * time_;
	int index = degs * (Main::funcTableSize / 360.0f);
	float sinValue = g_main->sinTable[index & Main::funcTableMask];
	float cosValue = g_main->sinTable[(index + Main::funcTableSize / 4) & Main::funcTableMask];
	matrix[0] = cosValue; matrix[2] = -sinValue; matrix[4] = 0.5 - 0.5 * cosValue + 0.5 * sinValue;
	matrix[1] = sinValue; matrix[3] = cosValue;  matrix[5] = 0.5 - 0.5 * sinValue - 0.5 * cosValue;
}

float Material::calculateWaveColorSingle(const MaterialWaveForm &wf) const
{
	float glow;

	if (wf.func == MaterialWaveformGenFunc::Noise)
	{
		glow = wf.base + R_NoiseGet4f(0, 0, 0, (time_ + wf.phase ) * wf.frequency ) * wf.amplitude;
	}
	else
	{
		glow = evaluateWaveForm(wf) * g_main->identityLight;
	}
	
	return math::Clamped(glow, 0.0f, 1.0f);
}

float Material::calculateWaveAlphaSingle(const MaterialWaveForm &wf) const
{
	return evaluateWaveFormClamped(wf);
}

void Material::calculateColors(const MaterialStage &stage, vec4 *baseColor, vec4 *vertColor) const
{
	assert(baseColor);
	assert(vertColor);

	*baseColor = vec4::white;
	*vertColor = vec4(0, 0, 0, 0);

	// rgbGen
	switch (stage.rgbGen)
	{
		case MaterialColorGen::IdentityLighting:
			(*baseColor).r = (*baseColor).g = (*baseColor).b = g_main->identityLight;
			break;
		case MaterialColorGen::ExactVertex:
		case MaterialColorGen::ExactVertexLit:
			*baseColor = vec4::black;
			*vertColor = vec4::white;
			break;
		case MaterialColorGen::Const:
			(*baseColor).r = stage.constantColor[0] / 255.0f;
			(*baseColor).g = stage.constantColor[1] / 255.0f;
			(*baseColor).b = stage.constantColor[2] / 255.0f;
			(*baseColor).a = stage.constantColor[3] / 255.0f;
			break;
		case MaterialColorGen::Vertex:
			*baseColor = vec4::black;
			*vertColor = vec4(g_main->identityLight, g_main->identityLight, g_main->identityLight, 1);
			break;
		case MaterialColorGen::VertexLit:
			*baseColor = vec4::black;
			*vertColor = vec4(g_main->identityLight);
			break;
		case MaterialColorGen::OneMinusVertex:
			(*baseColor).r = (*baseColor).g = (*baseColor).b = g_main->identityLight;
			(*vertColor).r = (*vertColor).g = (*vertColor).b = -g_main->identityLight;
			break;
		case MaterialColorGen::Fog:
			/*{
				fog_t		*fog;

				fog = tr.world->fogs + tess.fogNum;

				(*baseColor).r = ((unsigned char *)(&fog->colorInt)).r / 255.0f;
				(*baseColor).g = ((unsigned char *)(&fog->colorInt)).g / 255.0f;
				(*baseColor).b = ((unsigned char *)(&fog->colorInt)).b / 255.0f;
				(*baseColor).a = ((unsigned char *)(&fog->colorInt)).a / 255.0f;
			}*/
			break;
		case MaterialColorGen::Waveform:
			(*baseColor).r = (*baseColor).g = (*baseColor).b = calculateWaveColorSingle(stage.rgbWave);
			break;
		case MaterialColorGen::Entity:
			if (g_main->currentEntity)
			{
				(*baseColor).r = g_main->currentEntity->e.shaderRGBA[0] / 255.0f;
				(*baseColor).g = g_main->currentEntity->e.shaderRGBA[1] / 255.0f;
				(*baseColor).b = g_main->currentEntity->e.shaderRGBA[2] / 255.0f;
				(*baseColor).a = g_main->currentEntity->e.shaderRGBA[3] / 255.0f;
			}
			break;
		case MaterialColorGen::OneMinusEntity:
			if (g_main->currentEntity)
			{
				(*baseColor).r = 1.0f - g_main->currentEntity->e.shaderRGBA[0] / 255.0f;
				(*baseColor).g = 1.0f - g_main->currentEntity->e.shaderRGBA[1] / 255.0f;
				(*baseColor).b = 1.0f - g_main->currentEntity->e.shaderRGBA[2] / 255.0f;
				(*baseColor).a = 1.0f - g_main->currentEntity->e.shaderRGBA[3] / 255.0f;
			}
			break;
		case MaterialColorGen::Identity:
		case MaterialColorGen::LightingDiffuse:
		case MaterialColorGen::Bad:
			break;
	}

	// alphaGen
	switch (stage.alphaGen)
	{
		case MaterialAlphaGen::Skip:
			break;
		case MaterialAlphaGen::Const:
			(*baseColor).a = stage.constantColor[3] / 255.0f;
			(*vertColor).a = 0.0f;
			break;
		case MaterialAlphaGen::Waveform:
			(*baseColor).a = calculateWaveAlphaSingle(stage.alphaWave);
			(*vertColor).a = 0.0f;
			break;
		case MaterialAlphaGen::Entity:
			if (g_main->currentEntity)
			{
				(*baseColor).a = g_main->currentEntity->e.shaderRGBA[3] / 255.0f;
			}
			(*vertColor).a = 0.0f;
			break;
		case MaterialAlphaGen::OneMinusEntity:
			if (g_main->currentEntity)
			{
				(*baseColor).a = 1.0f - g_main->currentEntity->e.shaderRGBA[3] / 255.0f;
			}
			(*vertColor).a = 0.0f;
			break;
		case MaterialAlphaGen::Vertex:
			(*baseColor).a = 0.0f;
			(*vertColor).a = 1.0f;
			break;
		case MaterialAlphaGen::OneMinusVertex:
			(*baseColor).a = 1.0f;
			(*vertColor).a = -1.0f;
			break;
		case MaterialAlphaGen::Identity:
		case MaterialAlphaGen::LightingSpecular:
		case MaterialAlphaGen::Portal:
			// Done entirely in vertex program
			(*baseColor).a = 1.0f;
			(*vertColor).a = 0.0f;
			break;
	}

	// Multiply color by overbrightbits if this isn't a blend.
	if (g_main->overbrightBits 
		&& stage.blendSrc != BGFX_STATE_BLEND_DST_COLOR
		&& stage.blendSrc != BGFX_STATE_BLEND_INV_DST_COLOR
		&& stage.blendDst != BGFX_STATE_BLEND_SRC_COLOR
		&& stage.blendDst != BGFX_STATE_BLEND_INV_SRC_COLOR)
	{
		const float scale = 1 << g_main->overbrightBits;
		(*baseColor)[0] *= scale;
		(*baseColor)[1] *= scale;
		(*baseColor)[2] *= scale;
		(*vertColor)[0] *= scale;
		(*vertColor)[1] *= scale;
		(*vertColor)[2] *= scale;
	}
}

bool Material::requiresCpuDeforms() const
{
	if (numDeforms)
	{
		if (numDeforms > 1)
			return true;

		switch (deforms[0].deformation)
		{
		case MaterialDeform::Wave:
		case MaterialDeform::Bulge:
			return false;

		default:
			return true;
		}
	}

	return false;
}

void Material::calculateDeformValues()
{
	deformGen_ = MaterialDeformGen::None;

	if (numDeforms > 0 && !requiresCpuDeforms())
	{
		// Only support the first one.
		auto &ds = deforms[0];

		switch (ds.deformation)
		{
		case MaterialDeform::Wave:
			deformGen_ = (MaterialDeformGen)ds.deformationWave.func;
			deformParameters1_[Uniforms::DeformParameters1::Base] = ds.deformationWave.base;
			deformParameters1_[Uniforms::DeformParameters1::Amplitude] = ds.deformationWave.amplitude;
			deformParameters1_[Uniforms::DeformParameters1::Phase] = ds.deformationWave.phase;
			deformParameters1_[Uniforms::DeformParameters1::Frequency] = ds.deformationWave.frequency;
			deformParameters2_[Uniforms::DeformParameters2::Spread] = ds.deformationSpread;
			break;

		case MaterialDeform::Bulge:
			deformGen_ = MaterialDeformGen::Bulge;
			deformParameters1_[Uniforms::DeformParameters1::Base] = 0;
			deformParameters1_[Uniforms::DeformParameters1::Amplitude] = ds.bulgeHeight;
			deformParameters1_[Uniforms::DeformParameters1::Phase] = ds.bulgeWidth;
			deformParameters1_[Uniforms::DeformParameters1::Frequency] = ds.bulgeSpeed;
			deformParameters2_[Uniforms::DeformParameters2::Spread] = 0;
			break;

		default:
			break;
		}

		deformParameters2_[Uniforms::DeformParameters2::Time] = time_;
	}
}

} // namespace renderer
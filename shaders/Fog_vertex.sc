$input a_position, a_normal, a_texcoord0
$output v_position, v_texcoord0

#include <bgfx_shader.sh>
#include "Common.sh"
#include "Generators.sh"

#define v_scale v_texcoord0.x
uniform vec4 u_Color;
uniform vec4 u_FogDistance;
uniform vec4 u_FogDepth;
uniform vec4 u_FogEyeT;
uniform vec4 u_DepthRange;

float CalcFog(vec3 position)
{
	float s = dot(vec4(position, 1.0), u_FogDistance) * 8.0;
	float t = dot(vec4(position, 1.0), u_FogDepth);

	float eyeOutside = float(u_FogEyeT.x < 0.0);
	float fogged = float(t >= eyeOutside);

	t += 1e-6;
	t *= fogged / (t - u_FogEyeT.x * eyeOutside);

	return s * t;
}

void main()
{
	v_position = mul(u_model[0], vec4(a_position, 1.0)).xyz;

	if (u_DeformGen != DGEN_NONE)
	{
		v_position = DeformPosition(v_position, a_normal, a_texcoord0);
	}

	gl_Position = ApplyDepthRange(mul(u_viewProj, vec4(v_position, 1.0)), u_DepthRange.x, u_DepthRange.y);
	v_scale = CalcFog(v_position) * u_Color.a * u_Color.a;
}
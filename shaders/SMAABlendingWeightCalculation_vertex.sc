$input a_position, a_texcoord0
$output v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4

#include <bgfx_shader.sh>

#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 0
#include "SMAA.sh"

void main()
{
	vec2 pixcoord;
	vec4 offset[3];
	SMAABlendingWeightCalculationVS(a_texcoord0, pixcoord, offset);
	v_texcoord0 = a_texcoord0;
	v_texcoord1 = pixcoord;
	v_texcoord2 = offset[0];
	v_texcoord3 = offset[1];
	v_texcoord4 = offset[2];
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}

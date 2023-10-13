/** 
 * @file class1\deferred\terrainF.glsl
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#define TERRAIN_PBR_DETAIL_EMISSIVE 0

out vec4 frag_data[4];

uniform sampler2D alpha_ramp;

// *TODO: Configurable quality level which disables PBR features on machines
// with limited texture availability
// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#additional-textures
uniform sampler2D detail_0_base_color;
uniform sampler2D detail_1_base_color;
uniform sampler2D detail_2_base_color;
uniform sampler2D detail_3_base_color;
uniform sampler2D detail_0_normal;
uniform sampler2D detail_1_normal;
uniform sampler2D detail_2_normal;
uniform sampler2D detail_3_normal;
uniform sampler2D detail_0_metallic_roughness;
uniform sampler2D detail_1_metallic_roughness;
uniform sampler2D detail_2_metallic_roughness;
uniform sampler2D detail_3_metallic_roughness;
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
uniform sampler2D detail_0_emissive;
uniform sampler2D detail_1_emissive;
uniform sampler2D detail_2_emissive;
uniform sampler2D detail_3_emissive;
#endif

// *TODO: More efficient packing?
uniform vec4[4] baseColorFactors; // See also vertex_color in pbropaqueV.glsl
uniform vec4 metallicFactors;
uniform vec4 roughnessFactors;
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
uniform vec3[4] emissiveColors;
#endif
uniform vec4 minimum_alphas; // PBR alphaMode: MASK, See: mAlphaCutoff, setAlphaCutoff()

in vec3 vary_normal;
in vec3 vary_tangent;
flat in float vary_sign;
in vec4 vary_texcoord0;
in vec4 vary_texcoord1;

vec2 encode_normal(vec3 n);
vec3 linear_to_srgb(vec3 c);
vec3 srgb_to_linear(vec3 c);

// *TODO: This mixing function feels like it can be optimized. The terrain code's use of texcoord1 is dubious. It feels like the same thing can be accomplished with less memory bandwidth by calculating the offsets on-the-fly
float terrain_mix(vec4 samples, float alpha1, float alpha2, float alphaFinal)
{
    return mix( mix(samples.w, samples.z, alpha2), mix(samples.y, samples.x, alpha1), alphaFinal );
}

vec3 terrain_mix(vec3[4] samples, float alpha1, float alpha2, float alphaFinal)
{
    return mix( mix(samples[3], samples[2], alpha2), mix(samples[1], samples[0], alpha1), alphaFinal );
}

vec4 terrain_mix(vec4[4] samples, float alpha1, float alpha2, float alphaFinal)
{
    return mix( mix(samples[3], samples[2], alpha2), mix(samples[1], samples[0], alpha1), alphaFinal );
}

vec3 sample_and_mix_color3(float alpha1, float alpha2, float alphaFinal, vec2 texcoord, vec3[4] factors, sampler2D tex0, sampler2D tex1, sampler2D tex2, sampler2D tex3)
{
    vec3[4] samples;
    samples[0] = texture(tex0, texcoord).xyz;
    samples[1] = texture(tex1, texcoord).xyz;
    samples[2] = texture(tex2, texcoord).xyz;
    samples[3] = texture(tex3, texcoord).xyz;
    samples[0] = srgb_to_linear(samples[0]);
    samples[1] = srgb_to_linear(samples[1]);
    samples[2] = srgb_to_linear(samples[2]);
    samples[3] = srgb_to_linear(samples[3]);
    samples[0] *= factors[0];
    samples[1] *= factors[1];
    samples[2] *= factors[2];
    samples[3] *= factors[3];
    return terrain_mix(samples, alpha1, alpha2, alphaFinal);
}

vec4 sample_and_mix_color4(float alpha1, float alpha2, float alphaFinal, vec2 texcoord, vec4[4] factors, sampler2D tex0, sampler2D tex1, sampler2D tex2, sampler2D tex3)
{
    vec4[4] samples;
    samples[0] = texture(tex0, texcoord);
    samples[1] = texture(tex1, texcoord);
    samples[2] = texture(tex2, texcoord);
    samples[3] = texture(tex3, texcoord);
    samples[0].xyz = srgb_to_linear(samples[0].xyz);
    samples[1].xyz = srgb_to_linear(samples[1].xyz);
    samples[2].xyz = srgb_to_linear(samples[2].xyz);
    samples[3].xyz = srgb_to_linear(samples[3].xyz);
    samples[0] *= factors[0];
    samples[1] *= factors[1];
    samples[2] *= factors[2];
    samples[3] *= factors[3];
    return terrain_mix(samples, alpha1, alpha2, alphaFinal);
}

vec3 sample_and_mix_vector3(float alpha1, float alpha2, float alphaFinal, vec2 texcoord, vec3[4] factors, sampler2D tex0, sampler2D tex1, sampler2D tex2, sampler2D tex3)
{
    vec3[4] samples;
    samples[0] = texture(tex0, texcoord).xyz;
    samples[1] = texture(tex1, texcoord).xyz;
    samples[2] = texture(tex2, texcoord).xyz;
    samples[3] = texture(tex3, texcoord).xyz;
    samples[0] *= factors[0];
    samples[1] *= factors[1];
    samples[2] *= factors[2];
    samples[3] *= factors[3];
    return terrain_mix(samples, alpha1, alpha2, alphaFinal);
}

vec3 sample_and_mix_vector3_no_scale(float alpha1, float alpha2, float alphaFinal, vec2 texcoord, sampler2D tex0, sampler2D tex1, sampler2D tex2, sampler2D tex3)
{
    vec3[4] samples;
    samples[0] = texture(tex0, texcoord).xyz;
    samples[1] = texture(tex1, texcoord).xyz;
    samples[2] = texture(tex2, texcoord).xyz;
    samples[3] = texture(tex3, texcoord).xyz;
    return terrain_mix(samples, alpha1, alpha2, alphaFinal);
}

void main()
{
    vec2 terrain_texcoord = vary_texcoord0.xy;
    float alpha1 = texture(alpha_ramp, vary_texcoord0.zw).a;
    float alpha2 = texture(alpha_ramp,vary_texcoord1.xy).a;
    float alphaFinal = texture(alpha_ramp, vary_texcoord1.zw).a;

    vec4 col = sample_and_mix_color4(alpha1, alpha2, alphaFinal, terrain_texcoord, baseColorFactors, detail_0_base_color, detail_1_base_color, detail_2_base_color, detail_3_base_color);
    float minimum_alpha = terrain_mix(minimum_alphas, alpha1, alpha2, alphaFinal);
    if (col.a < minimum_alpha)
    {
        discard;
    }

    vec3 normal_texture = sample_and_mix_vector3_no_scale(alpha1, alpha2, alphaFinal, terrain_texcoord, detail_0_normal, detail_1_normal, detail_2_normal, detail_3_normal);

    vec3[4] orm_factors;
    orm_factors[0] = vec3(1.0, roughnessFactors.x, metallicFactors.x);
    orm_factors[1] = vec3(1.0, roughnessFactors.y, metallicFactors.y);
    orm_factors[2] = vec3(1.0, roughnessFactors.z, metallicFactors.z);
    orm_factors[3] = vec3(1.0, roughnessFactors.w, metallicFactors.w);
    // RGB = Occlusion, Roughness, Metal
    // default values, see LLViewerTexture::sDefaultPBRORMImagep
    //   occlusion 1.0
    //   roughness 0.0
    //   metal     0.0
    vec3 spec = sample_and_mix_vector3(alpha1, alpha2, alphaFinal, terrain_texcoord, orm_factors, detail_0_metallic_roughness, detail_1_metallic_roughness, detail_2_metallic_roughness, detail_3_metallic_roughness);

#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    vec3 emissive = sample_and_mix_color3(alpha1, alpha2, alphaFinal, terrain_texcoord, emissiveColors, detail_0_emissive, detail_1_emissive, detail_2_emissive, detail_3_emissive);
#else
    vec3 emissive = vec3(0);
#endif

    float base_color_factor_alpha = terrain_mix(vec4(baseColorFactors[0].z, baseColorFactors[1].z, baseColorFactors[2].z, baseColorFactors[3].z), alpha1, alpha2, alphaFinal);

    // from mikktspace.com
    vec3 vNt = normal_texture.xyz*2.0-1.0;
    float sign = vary_sign;
    vec3 vN = vary_normal;
    vec3 vT = vary_tangent.xyz;
    
    vec3 vB = sign * cross(vN, vT);
    vec3 tnorm = normalize( vNt.x * vT + vNt.y * vB + vNt.z * vN );

    tnorm *= gl_FrontFacing ? 1.0 : -1.0;

   
    frag_data[0] = max(vec4(col.xyz, 0.0), vec4(0));                                                   // Diffuse
    frag_data[1] = max(vec4(spec.rgb, base_color_factor_alpha), vec4(0));                                    // PBR linear packed Occlusion, Roughness, Metal.
    frag_data[2] = max(vec4(encode_normal(tnorm), base_color_factor_alpha, GBUFFER_FLAG_HAS_PBR), vec4(0)); // normal, environment intensity, flags
    frag_data[3] = max(vec4(emissive,0), vec4(0));                                                // PBR sRGB Emissive
}

/** 
 * @file debugV.glsl
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

in vec3 position;
in vec3 normal;
#if HAS_ATTRIBUTE_TANGENT == 1
in vec4 tangent;
out vec3 tangent_color;
#endif

uniform float debug_normal_draw_length;

#ifdef HAS_SKIN
mat4 getObjectSkinnedTransform();
uniform mat4 projection_matrix;
uniform mat4 modelview_matrix;
#else
uniform mat3 normal_matrix;
uniform mat4 modelview_projection_matrix;
#endif

void main()
{
    vec3 scaled_normal = debug_normal_draw_length * normal.xyz;
#ifdef HAS_SKIN
    mat4 mat = getObjectSkinnedTransform();
    mat = modelview_matrix * mat;
    vec4 world_pos = mat * vec4(position.xyz,1.0);
    vec4 screen_pos = projection_matrix * world_pos;
	vec4 screen_normal = mat*vec4(scaled_normal.xyz+position.xyz,1.0);
#if HAS_ATTRIBUTE_TANGENT == 1
	vec3 screen_tangent = normalize((mat*vec4(tangent.xyz+position.xyz,1.0)).xyz-world_pos.xyz);
#endif
#else
	vec4 screen_pos = modelview_projection_matrix * vec4(position.xyz, 1.0);
	vec4 screen_normal = modelview_projection_matrix*vec4(scaled_normal.xyz+position.xyz,1.0);
#if HAS_ATTRIBUTE_TANGENT == 1
	vec3 screen_tangent = normalize(normal_matrix * tangent.xyz);
#endif
#endif
#if HAS_ATTRIBUTE_TANGENT == 1 // TODO: Remove
    tangent_color = (screen_tangent.xyz + 2.0) * 0.5;
#endif
    gl_Position = screen_normal;
}


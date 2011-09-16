/** 
 * @file waterF.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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
 
#ifndef gl_FragColor
out vec4 gl_FragColor;
#endif

vec3 scaleSoftClip(vec3 inColor);
vec3 atmosTransport(vec3 inColor);
vec3 applyWaterFog(vec4 inColor);

uniform sampler2D diffuseMap;
uniform sampler2D bumpMap;   
uniform sampler2D screenTex;
uniform sampler2D refTex;

uniform float sunAngle;
uniform float sunAngle2;
uniform float scaledAngle;
uniform vec3 lightDir;
uniform vec3 specular;
uniform float lightExp;
uniform float refScale;
uniform float kd;
uniform vec2 screenRes;
uniform vec3 normScale;
uniform float fresnelScale;
uniform float fresnelOffset;
uniform float blurMultiplier;
uniform vec4 fogCol;

//bigWave is (refCoord.w, view.w);
VARYING vec4 refCoord;
VARYING vec4 littleWave;
VARYING vec4 view;

void main() 
{
	vec3 viewVec = view.xyz;
	vec4 color;
	
	float dist = length(viewVec.xy);
	
	//normalize view vector
	viewVec = normalize(viewVec);
	
	//get wave normals
	vec3 wavef = texture2D(bumpMap, vec2(refCoord.w, view.w)).xyz*2.0;

	//get detail normals
	vec3 dcol = texture2D(bumpMap, littleWave.xy).rgb*0.75;
	dcol += texture2D(bumpMap, littleWave.zw).rgb*1.25;

	//interpolate between big waves and little waves (big waves in deep water)
	wavef = (wavef + dcol) * 0.5;
	
	//crunch normal to range [-1,1]
	wavef -= vec3(1,1,1);
	wavef = normalize(wavef);
   
	//get base fresnel components	
	
	float df = dot(viewVec,wavef) * fresnelScale + fresnelOffset;
		    
	vec2 distort = (refCoord.xy/refCoord.z) * 0.5 + 0.5;
	
	float dist2 = dist;
	dist = max(dist, 5.0);
	
	//get reflected color
	vec2 refdistort = wavef.xy*dot(normScale, vec3(0.333));
	vec2 refvec = distort+refdistort/dist;
	vec4 refcol = texture2D(refTex, refvec);

	//get specular component
	float spec = clamp(dot(lightDir, (reflect(viewVec,wavef))),0.0,1.0);
	
	//harden specular
	spec = pow(spec, lightExp);

	//figure out distortion vector (ripply)   
	vec2 distort2 = distort+wavef.xy*refScale/max(dist*df, 1.0);
		
	vec4 fb = texture2D(screenTex, distort2);
	
	//mix with reflection
	color.rgb = mix(mix(fogCol.rgb, fb.rgb, fogCol.a), refcol.rgb, df);
	color.rgb += spec * specular;
	
	//color.rgb = applyWaterFog(color);//atmosTransport(color.rgb);
	color.rgb = scaleSoftClip(color.rgb);
	color.a = spec * sunAngle2;

	gl_FragColor = color;
}

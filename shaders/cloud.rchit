#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "ptStructures.glsl"
#include "ptConstants.glsl"
#include "layoutPTAccel.glsl"
#include "layoutPTPushConstants.glsl"
#include "color.glsl"

layout(location = 1) rayPayloadInEXT RayPayload rayPayload;
layout(binding = 12) buffer Lights{Light l[]; } lights;
layout (binding = 30) uniform sampler3D gridImage[];

layout(binding = 26) uniform Infos{
    uint lightCount;
    uint minRecursionDepth;
    uint maxRecursionDepth;

    // Cloud properties
    vec3 extinction;
    vec3 scatteringAlbedo;

    // Sky properties
    vec3 sun_direction;
    vec3 sun_intensity;
} parameters;

layout(constant_id=0) const int BUNDLE_SZ = 1;
layout(constant_id=1) const float phaseG = 0.857;
layout(constant_id=2) const float extinction = 1024;
layout(constant_id=3) const float scatteringAlbedo = 1;

uint pcg32(inout uint state)
{
    // based on PCG reference C++ implementation.
    uint x = (state = state * 747796405u + 2891336453u);
    x ^= x >> ((x >> 28) + 4);
    x *= 277803737u;
    x ^= x >> 22;
    return x;
}

float rand01(inout uint state)
{
    return uintBitsToFloat(0x3f800000u | (pcg32(state) & 0x007fffffu)) - 1.0;
}

/*
Taken from https://github.com/lleonart1984/vulkansimplecloudrendering

Copyright (c) 2021 Ludwig Leonard

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// --- Constants
const float pi = 3.14159265359;
const float two_pi = 2*3.14159265359;

// --- Randomization

void CreateOrthonormalBasis(vec3 D, out vec3 B, out vec3 T) {
	vec3 other = abs(D.z) >= 0.9999 ? vec3(1, 0, 0) : vec3(0, 0, 1);
	B = normalize(cross(other, D));
	T = normalize(cross(D, B));
}

vec3 randomDirection(vec3 D, inout uint rngState) {
	float r1 = rand01(rngState);
	float r2 = rand01(rngState) * 2 - 1;
	float sqrR2 = r2 * r2;
	float two_pi_by_r1 = two_pi * r1;
	float sqrt_of_one_minus_sqrR2 = sqrt(1.0 - sqrR2);
	float x = cos(two_pi_by_r1) * sqrt_of_one_minus_sqrR2;
	float y = sin(two_pi_by_r1) * sqrt_of_one_minus_sqrR2;
	float z = r2;

	vec3 t0, t1;
	CreateOrthonormalBasis(D, t0, t1);

	return t0 * x + t1 * y + D * z;
}

#define one_minus_g2 (1.0 - (GFactor) * (GFactor))
#define one_plus_g2 (1.0 + (GFactor) * (GFactor))
#define one_over_2g (0.5 / (GFactor))

float invertcdf(float GFactor, float xi) {
	float t = (one_minus_g2) / (1.0f - GFactor + 2.0f * GFactor * xi);
	return one_over_2g * (one_plus_g2 - t * t);
}

vec3 ImportanceSamplePhase(float GFactor, vec3 D, out float pdf, inout uint rngState) {
	if (abs(GFactor) < 0.001) {
        pdf = 1.0 / (4 * pi);
		return randomDirection(-D, rngState);
	}

	float phi = rand01(rngState) * 2 * pi;
	float cosTheta = invertcdf(GFactor, rand01(rngState));
	float sinTheta = sqrt(max(0, 1.0f - cosTheta * cosTheta));

	vec3 t0, t1;
	CreateOrthonormalBasis(D, t0, t1);

    pdf = 0.25 / pi * (one_minus_g2) / pow(one_plus_g2 - 2 * GFactor * cosTheta, 1.5);

    return sinTheta * sin(phi) * t0 + sinTheta * cos(phi) * t1 +
		cosTheta * D;
}

//--- Tools

vec3 sampleLight(in vec3 dir){
    return vec3(0);
	int N = 10;
	float phongNorm = (N + 2) / (2 * 3.14159);
	return parameters.sun_intensity * pow(max(0, dot(dir, parameters.sun_direction)), N) * phongNorm;
}

ivec3 imgDims = textureSize(gridImage[gl_InstanceCustomIndexEXT], 0);

float sampleCloud(in vec3 pos, inout uint rngState)
{
    vec3 coord = pos;
    //coord += vec3(rand01(rngState) - 0.5, rand01(rngState) - 0.5, rand01(rngState) - 0.5) / imgDims;
    return textureLod(gridImage[gl_InstanceCustomIndexEXT], coord, 0).x;
}

bool rayBoxIntersect(vec3 bMin, vec3 bMax, vec3 P, vec3 D, out float tMin, out float tMax)
{
    // un-parallelize D
    D.x = abs(D).x <= 0.000001 ? 0.000001 : D.x;
    D.y = abs(D).y <= 0.000001 ? 0.000001 : D.y;
    D.z = abs(D).z <= 0.000001 ? 0.000001 : D.z;
    vec3 C_Min = (bMin - P)/D;
    vec3 C_Max = (bMax - P)/D;
	tMin = max(max(min(C_Min[0], C_Max[0]), min(C_Min[1], C_Max[1])), min(C_Min[2], C_Max[2]));
	tMin = max(0.0, tMin);
	tMax = min(min(max(C_Min[0], C_Max[0]), max(C_Min[1], C_Max[1])), max(C_Min[2], C_Max[2]));
	if (tMax <= tMin || tMax <= 0) {
		return false;
	}
	return true;
}

float maxComponent(vec3 v)
{
    return max(v.x, max(v.y, v.z));
}


// --- Volume Pathtracer

struct ScatterEvent
{
    bool hasValue;
    vec3 x; float pdf_x;
    vec3 w; float pdf_w;
};

// TODO: this should be covered by a miss shader/recursive RT
vec3 sampleSkybox(vec3 direction)
{
    direction = normalize(gl_ObjectToWorldEXT * vec4(direction, 0));
	vec3 upper_color = SRGBtoLINEAR(vec3(0.3, 0.5, 0.92));
	upper_color = mix(vec3(1), upper_color, max(direction.z, 0));
	vec3 lower_color = vec3(0.2, 0.2, 0.2);
	float weight = smoothstep(-0.02, 0.02, direction.z);
	return mix(lower_color, upper_color, weight);
}

vec3 PathtraceBundle(vec3 x_in, vec3 w_in, inout uint rngState)
{
    float majorant = extinction;
    float absorptionAlbedo = 1 - scatteringAlbedo;
    float PA = absorptionAlbedo * extinction;
    float PS = scatteringAlbedo * extinction;

    float tMin[BUNDLE_SZ], tMax[BUNDLE_SZ];
    vec3 x[BUNDLE_SZ], w[BUNDLE_SZ];
    for (uint i = 0; i < BUNDLE_SZ; i++) { x[i] = x_in; w[i] = w_in; }
    bool running[BUNDLE_SZ];
    uint runCount = 0;
    vec3 acc = vec3(0);

    if (rayBoxIntersect(vec3(0), vec3(1), x_in, w_in, tMin[0], tMax[0]))
    {
        runCount = BUNDLE_SZ;
        for (uint i = 0; i < BUNDLE_SZ; i++)
        {
            running[i] = true;
            tMin[i] = tMin[0];
            tMax[i] = tMax[0];
        }

        for (uint i = 0; i < BUNDLE_SZ; i++) x[i] += w[i] * tMin[i];
        float d[BUNDLE_SZ];
        for (uint i = 0; i < BUNDLE_SZ; i++) d[i] = tMax[i] - tMin[i];

        uint max_steps = 2000;
        while (runCount > 0 && max_steps-- > 0) {
            float t[BUNDLE_SZ];
            for (uint i = 0; i < BUNDLE_SZ; i++) t[i] = -log(max(0.0000000001, 1 - rand01(rngState))) / majorant;

            for (uint i = 0; i < BUNDLE_SZ; i++)
                if (running[i] && t[i] > d[i])
                {
                    acc += sampleSkybox(w[i]) + sampleLight(w[i]);
                    running[i] = false;
                    runCount--;
                }

            for (uint i = 0; i < BUNDLE_SZ; i++) x[i] += w[i] * t[i];

            float16_t density[BUNDLE_SZ];
            for (uint i = 0; i < BUNDLE_SZ; i++) density[i] = float16_t(sampleCloud(x[i], rngState));
            // force loading all of these NOW by having a dependency chain. THIS HAS NO FUNCTION!
            for (uint i = 0; i < BUNDLE_SZ; i++) if (density[i] < float16_t(0)) density[i] = density[(i+1) % BUNDLE_SZ];

            float16_t sigma_a[BUNDLE_SZ];
            float16_t sigma_s[BUNDLE_SZ];
            float16_t sigma_n[BUNDLE_SZ];
            for (uint i = 0; i < BUNDLE_SZ; i++) sigma_a[i] = float16_t(PA) * density[i];
            for (uint i = 0; i < BUNDLE_SZ; i++) sigma_s[i] = float16_t(PS) * density[i];
            for (uint i = 0; i < BUNDLE_SZ; i++) sigma_n[i] = float16_t(extinction) * (float16_t(1) - density[i]);

            float Pa[BUNDLE_SZ];
            float Ps[BUNDLE_SZ];
            float Pn[BUNDLE_SZ];
            for (uint i = 0; i < BUNDLE_SZ; i++) Pa[i] = sigma_a[i] / float16_t(majorant);
            for (uint i = 0; i < BUNDLE_SZ; i++) Ps[i] = sigma_s[i] / float16_t(majorant);
            for (uint i = 0; i < BUNDLE_SZ; i++) Pn[i] = sigma_n[i] / float16_t(majorant);

            float16_t xi[BUNDLE_SZ];
            for (uint i = 0; i < BUNDLE_SZ; i++) xi[i] = float16_t(rand01(rngState));

            for (uint i = 0; i < BUNDLE_SZ; i++)
            {
                if (xi[i] < Pa[i])
                {
                    running[i] = false;
                    runCount--;
                }

                if (xi[i] < 1 - Pn[i]) // scattering event
                {
                    float pdf_w;
                    w[i] = ImportanceSamplePhase(phaseG, w[i], pdf_w, rngState);

                    if (rayBoxIntersect(vec3(0), vec3(1), x[i], w[i], tMin[i], tMax[i]))
                    {
                        x[i] += w[i] * tMin[i];
                        d[i] = tMax[i] - tMin[i];
                    }
                }
                else
                {
                    d[i] -= t[i];
                }
            }
        }
    }
    else
    {
        return sampleSkybox(w_in) + sampleLight(w_in);
    }

    if (runCount > 0)
        for (uint i = 0; i < BUNDLE_SZ; i++)
            if (running[i])
                acc += sampleSkybox(w[i]) + sampleLight(w[i]);

    return acc / BUNDLE_SZ;
}

vec3 GetPrimaryStats(vec3 x, vec3 w)
{
    // Ray march to get transmittance-weighted depth
    const uint STEPS = 256;
    float tMin, tMax;
    rayBoxIntersect(vec3(0, 0, 0), vec3(1, 1, 1), x, w, tMin, tMax);
    // tMin is always zero (intersection shader reports accurate hit location)
    vec3 delta = w * tMax / STEPS;
    x += 0.5 * delta;
    float16_t mean = float16_t(0), sum_w = float16_t(0);
    float16_t trans = float16_t(1);
    float16_t init_dist = float16_t(distance(gl_ObjectToWorldEXT * vec4(x, 1), gl_WorldRayOriginEXT));
    float16_t delta_dist = float16_t(length(gl_ObjectToWorldEXT * vec4(delta, 0)));
    float16_t dist1 = float16_t(1.0/0.0), dist0 = float16_t(0);
    // Iterative weighted mean and variance, after West 1979, https://doi.org/10.1145/359146.359153
    for (uint i = 0; i < STEPS; i++)
    {
        vec3 loc = x + i * delta;
        float16_t d = init_dist + float16_t(i) * delta_dist;
        float16_t density = float16_t(textureLod(gridImage[gl_InstanceCustomIndexEXT], loc, 0).x);

        // Weight = Amount of energy flow to camera = Transmittance times Scattering = transmittance * const * density.
        // Constant factor is normalized away through the weight sum, so ignore it.
        float16_t w = trans * density;
        trans *= exp(density * -float16_t(extinction / STEPS));
        if (w > float16_t(0.01))
        {
            dist1 = min(dist1, d);
            dist0 = max(dist0, d);
        }

        sum_w += w;
        mean += w * d;
    }
    return vec3(float(mean)/float(sum_w), float(dist1), float(dist0));
}

// http://www.jcgt.org/published/0009/03/02/
uvec3 pcg3d(uvec3 v) {
    v = v * 1664525u + 1013904223u;

    v.x += v.y*v.z;
    v.y += v.z*v.x;
    v.z += v.x*v.y;

    v ^= v >> 16u;

    v.x += v.y*v.z;
    v.y += v.z*v.x;
    v.z += v.x*v.y;

    return v;
}

void main()
{
    uint rngState = pcg3d(uvec3(gl_LaunchIDEXT.xy, camParams.frameNumber)).x;

    vec3 w = normalize(gl_ObjectRayDirectionEXT);
    vec3 x = gl_ObjectRayOriginEXT + gl_ObjectRayDirectionEXT * gl_HitTEXT;

    vec3 stats = GetPrimaryStats(x, w);
    // Perform a single path and get radiance
    ScatterEvent first_event;

    vec3 result;
    if (!any(isnan(stats)) && !any(isinf(stats)))
        result = PathtraceBundle(x, w, rngState);
    else
        result = sampleSkybox(w) + sampleLight(w);

    rayPayload.position = first_event.hasValue ? (gl_ObjectToWorldEXT * vec4(first_event.x, 1)) : vec3(1/0);
    rayPayload.si.emissiveColor = result;

    rayPayload.si.perceptualRoughness = 0;
    rayPayload.si.metalness = 0;
    rayPayload.si.alphaRoughness = 0;
    rayPayload.si.reflectance0 = stats; // re-using reflectance
    rayPayload.si.reflectance90 = vec3(0);
    rayPayload.si.diffuseColor = vec3(0);
    rayPayload.si.specularColor = vec3(0);
    rayPayload.si.normal = vec3(-1);
    rayPayload.si.basis = mat3(1, 0, 0, 0, 1, 0, 0, 0, 1);
}

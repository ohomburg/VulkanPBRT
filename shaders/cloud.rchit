#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "ptStructures.glsl"
#include "ptConstants.glsl"
#include "layoutPTAccel.glsl"
#include "random.glsl"
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

const float phaseG = 0.857;

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

vec3 randomDirection(vec3 D, inout RandomEngine re) {
	float r1 = randomFloat(re);
	float r2 = randomFloat(re) * 2 - 1;
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

vec3 ImportanceSamplePhase(float GFactor, vec3 D, out float pdf, inout RandomEngine re) {
	if (abs(GFactor) < 0.001) {
        pdf = 1.0 / (4 * pi);
		return randomDirection(-D, re);
	}

	float phi = randomFloat(re) * 2 * pi;
	float cosTheta = invertcdf(GFactor, randomFloat(re));
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

float sampleCloud(in vec3 pos, inout RandomEngine re)
{
    vec3 coord = pos;
    coord += vec3(randomFloat(re) - 0.5, randomFloat(re) - 0.5, randomFloat(re) - 0.5) / imgDims;
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

// Pathtracing with Delta tracking and Spectral tracking
vec3 PathtraceSpectral(vec3 x, vec3 w, inout RandomEngine re)
{
    float majorant = maxComponent(parameters.extinction);

    vec3 weights = vec3(1,1,1);

    vec3 absorptionAlbedo = vec3(1,1,1) - parameters.scatteringAlbedo;
    vec3 scatteringAlbedo = parameters.scatteringAlbedo;
    float PA = maxComponent (absorptionAlbedo * parameters.extinction);
    float PS = maxComponent (scatteringAlbedo * parameters.extinction);

    float tMin, tMax;
    if (rayBoxIntersect(vec3(0, 0, 0), vec3(1, 1, 1), x, w, tMin, tMax))
    {
        x += w * tMin;
        float d = tMax - tMin;
	    while (true) {
            float t = -log(max(0.0000000001, 1 - randomFloat(re)))/majorant;

            if (t > d)
                break;

            x += w * t;

            float density = sampleCloud(x, re);

            vec3 sigma_a = absorptionAlbedo * parameters.extinction * density;
            vec3 sigma_s = scatteringAlbedo * parameters.extinction * density;
            vec3 sigma_n = vec3(majorant) - parameters.extinction * density;

            float Pa = maxComponent(sigma_a);
            float Ps = maxComponent(sigma_s);
            float Pn = maxComponent(sigma_n);
            float C = Pa + Ps + Pn;
            Pa /= C;
            Ps /= C;
            Pn /= C;

            float xi = randomFloat(re);

            if (xi < Pa)
                return vec3(0); // weights * sigma_a / (majorant * Pa) * L_e; // 0 - No emission

            if (xi < 1 - Pn) // scattering event
            {
                float pdf_w;
                w = ImportanceSamplePhase(phaseG, w, pdf_w, re);
                if (rayBoxIntersect(vec3(0, 0, 0), vec3(1, 1, 1), x, w, tMin, tMax))
                {
                    x += w*tMin;
                    d = tMax - tMin;
                }
                weights *= sigma_s / (majorant * Ps);
            }
            else {
                d -= t;
                weights *= sigma_n / (majorant * Pn);
            }
	    }
    }

    return min(weights, vec3(100000,100000,100000)) * ( sampleSkybox(w) + sampleLight(w) );
}

vec3 Pathtrace(vec3 x, vec3 w, out ScatterEvent first_event, inout RandomEngine re)
{
    first_event = ScatterEvent( false, x, 0.0f, w, 0.0f );

    float majorant = parameters.extinction.x;
    float absorptionAlbedo = 1 - parameters.scatteringAlbedo.x;
    float scatteringAlbedo = parameters.scatteringAlbedo.x;
    float PA = absorptionAlbedo * parameters.extinction.x;
    float PS = scatteringAlbedo * parameters.extinction.x;

    float tMin, tMax;
    if (rayBoxIntersect(vec3(0, 0, 0), vec3(1, 1, 1), x, w, tMin, tMax))
    {
        x += w * tMin;
        float d = tMax - tMin;

        float pdf_x = 1;

	    while (true) {
            float t = -log(max(0.0000000001, 1 - randomFloat(re)))/majorant;

            if (t > d)
                break;

            x += w * t;

            float density = sampleCloud(x, re);

            float sigma_a = PA * density;
            float sigma_s = PS * density;
            float sigma_n = majorant - parameters.extinction.x * density;

            float Pa = sigma_a/majorant;
            float Ps = sigma_s/majorant;
            float Pn = sigma_n/majorant;

            float xi = randomFloat(re);

            if (xi < Pa)
                return vec3(0); // weights * sigma_a / (majorant * Pa) * L_e; // 0 - No emission

            if (xi < 1 - Pn) // scattering event
            {
                float pdf_w;
                w = ImportanceSamplePhase(phaseG, w, pdf_w, re);

                if (!first_event.hasValue) {
                    first_event.x = x;
                    first_event.pdf_x = sigma_s * pdf_x;
                    first_event.w = w;
                    first_event.pdf_w = pdf_w;
                    first_event.hasValue = true;
                }

                if (rayBoxIntersect(vec3(0, 0, 0), vec3(1, 1, 1), x, w, tMin, tMax))
                {
                    x += w*tMin;
                    d = tMax - tMin;
                }
            }
            else {
                pdf_x *= exp(-parameters.extinction.x * density);
                d -= t;
            }
	    }
    }

    return ( sampleSkybox(w) + sampleLight(w) );
}

void main()
{
    RandomEngine re = rEInit(gl_LaunchIDEXT.xy, camParams.frameNumber);

    vec3 w = normalize(gl_ObjectRayDirectionEXT);
    vec3 x = gl_ObjectRayOriginEXT + gl_ObjectRayDirectionEXT * gl_HitTEXT;

    // Perform a single path and get radiance
    ScatterEvent first_event;
    vec3 result = Pathtrace(x, w, first_event, re);

    rayPayload.position = first_event.hasValue ? (gl_ObjectToWorldEXT * vec4(first_event.x, 1)) : vec3(1/0);
    rayPayload.si.emissiveColor = result;

    rayPayload.si.perceptualRoughness = 0;
    rayPayload.si.metalness = 0;
    rayPayload.si.alphaRoughness = 0;
    rayPayload.si.reflectance0 = vec3(0);
    rayPayload.si.reflectance90 = vec3(0);
    rayPayload.si.diffuseColor = vec3(0);
    rayPayload.si.specularColor = vec3(0);
    rayPayload.si.normal = vec3(-1);
    rayPayload.si.basis = mat3(1, 0, 0, 0, 1, 0, 0, 0, 1);
}

/*
Unprocessed old stuff. Kept around for reference.

layout (binding = 2) uniform Parameters {
    // Transform from Projection space to world space
    mat4 proj2world;

    // Cloud properties
    vec3 box_minim;
    vec3 box_maxim;

    vec3 extinction;
    vec3 scatteringAlbedo;
    float phaseG;

    // Sky properties
    vec3 sun_direction;
    vec3 sun_intensity;

} parameters;

layout (binding = 3) uniform FrameInfo
{
    uint frameCount;
    uvec3 other;
} frameInfo;

layout (binding = 4, rgba32f) uniform image2D accImage;

layout (binding = 5, rgba32f) uniform image2D firstX;

layout (binding = 6, rgba32f) uniform image2D firstW;

//--- Tools

// NOTE: hopefully covered by raygen shader
void createCameraRay(in vec2 coord, out vec3 x, out vec3 w)
{
    vec4 ndcP = vec4(coord, 0, 1);
	ndcP.y *= -1;
	vec4 ndcT = ndcP + vec4(0, 0, 1, 0);

	vec4 viewP = parameters.proj2world * ndcP;
	viewP.xyz /= viewP.w;
	vec4 viewT = parameters.proj2world * ndcT;
	viewT.xyz /= viewT.w;

	x = viewP.xyz;
	w = normalize(viewT.xyz - viewP.xyz);
}
*/

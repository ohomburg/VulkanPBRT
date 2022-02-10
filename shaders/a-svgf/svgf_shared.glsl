/*
Modifications Copyright 2022 Oskar Homburg

Copyright (c) 2018, Christoph Schied
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Karlsruhe Institute of Technology nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

vec3 uncompress_normal(vec2 comp)
{
    vec3 n;
    n.x = cos(comp.y) * sin(comp.x);
    n.y = sin(comp.y) * sin(comp.x);
    n.z = cos(comp.x);
    return n;
}

bool test_reprojected_normal(vec3 n1, vec3 n2)
{
	return dot(n1, n2) > 0.95;
}

bool test_inside_screen(ivec2 p, ivec2 res)
{
	return all(greaterThanEqual(p, ivec2(0)))
		&& all(lessThan(p, res));
}

bool test_reprojected_depth(float z1, float z2, float dz)
{
	float z_diff = abs(z1 - z2);
	return z_diff < 2.0 * (dz + 1e-3);
}

#define TILE_OFFSET_SHIFT 3u
#define TILE_OFFSET_MASK ((1 << TILE_OFFSET_SHIFT) - 1)

ivec2 get_gradient_tile_pos(uint idx, int gradientDownsample)
{
	/* didn't store a gradient sample in the previous frame, this creates
	   a new sample in the center of the tile */
	if(idx < (1u<<31))
		return ivec2(gradientDownsample / 2);

	return ivec2((idx & TILE_OFFSET_MASK), (idx >> TILE_OFFSET_SHIFT) & TILE_OFFSET_MASK);
}

uint get_gradient_idx_from_tile_pos(ivec2 pos)
{
	return (1 << 31) | (pos.x) | (pos.y << TILE_OFFSET_SHIFT);
}

bool is_gradient_sample(uimage2D tex_gradient, ivec2 ipos, int gradientDownsample)
{
	ivec2 ipos_grad = ipos / gradientDownsample;
	uint u = imageLoad(tex_gradient, ipos_grad).r;

	ivec2 tile_pos = ivec2((u & TILE_OFFSET_MASK), (u >> TILE_OFFSET_SHIFT) & TILE_OFFSET_MASK);
	return (u >= (1u << 31) && all(equal(ipos_grad * gradientDownsample + tile_pos, ipos)));
}

#ifndef NO_BINDINGS

// GBuffer inputs
layout(set=0, binding=0, rgba32f)  uniform   image2D tex_color_unfiltered;
layout(set=0, binding=1)           uniform sampler2D tex_color_unfiltered_prev;
layout(set=0, binding=2, r32ui)    uniform  uimage2D tex_gradient_samples;
layout(set=0, binding=3, rgba32f)  uniform   image2D tex_albedo;
layout(set=0, binding=4, rgba8)    uniform   image2D tex_color;
layout(set=0, binding=5, rgba16f)  uniform   image2D tex_color_prev;
layout(set=0, binding=6, rgba32f)  uniform   image2D tex_motion;
layout(set=0, binding=9, rgba32f)  uniform   image2D tex_moments_prev;
layout(set=0, binding=10, rgba32f) uniform   image2D tex_history_length;
layout(set=0, binding=11, rgba32f) uniform   image2D tex_normal_curr;
layout(set=0, binding=12)          uniform sampler2D tex_normal_prev;
layout(set=0, binding=13, rgba32f) uniform   image2D tex_vbuf_curr;
layout(set=0, binding=14)          uniform sampler2D tex_vbuf_prev;

// outputs from CreateGradientSamples
layout(set=1, binding=0, rgba32f) uniform image2D img_diffA1;
layout(set=1, binding=1, rgba32f) uniform image2D img_diffA2;

// outputs from AtrousGradient
layout(set=1, binding=2, rgba32f) uniform image2D img_diffB1;
layout(set=1, binding=3, rgba32f) uniform image2D img_diffB2;

// outputs from TemporalAccumulation
layout(set=1, binding=4, rgba16f) uniform image2D img_accumulated;
layout(set=1, binding=5, rg32f) uniform image2D img_moments;
layout(set=1, binding=6, r16f) uniform image2D img_histlen;

// output from EstimateVariance
layout(set=1, binding=7, rgba16f) uniform image2D img_varianceA;

// output from Atrous
layout(set=1, binding=8, rgba16f) uniform image2D img_varianceB;

// for Atrous
layout(constant_id=0) const int FILTER_KERNEL = 0;

layout(push_constant) uniform PerImageCB {
	int iteration;
	int step_size;
	int gradientDownsample;
    float temporal_alpha;
    int modulate_albedo;
};

layout (local_size_x=8, local_size_y=8, local_size_z=1) in;

#endif

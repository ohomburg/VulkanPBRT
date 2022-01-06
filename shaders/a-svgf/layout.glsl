// GBuffer inputs
layout(set=0, binding=0) uniform sampler2D tex_color_unfiltered;
layout(set=0, binding=1) uniform sampler2D tex_color_unfiltered_prev;
layout(set=0, binding=2) uniform usampler2D tex_gradient_samples;
layout(set=0, binding=3) uniform sampler2D tex_albedo;
layout(set=0, binding=4) uniform sampler2D tex_color;
layout(set=0, binding=5) uniform sampler2D tex_color_prev;
layout(set=0, binding=6) uniform sampler2D tex_motion;
layout(set=0, binding=7) uniform sampler2D tex_z_curr;
layout(set=0, binding=8) uniform sampler2D tex_z_prev;
layout(set=0, binding=9) uniform sampler2D tex_moments_prev;
layout(set=0, binding=10) uniform sampler2D tex_history_length;
layout(set=0, binding=11) uniform sampler2D tex_normal_curr;
layout(set=0, binding=12) uniform sampler2D tex_normal_prev;
layout(set=0, binding=13) uniform sampler2D tex_vbuf_curr;
layout(set=0, binding=14) uniform sampler2D tex_vbuf_prev;

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
    vec2 jitter_offset;
	int iteration;
	int step_size;
	int gradientDownsample;
    float temporal_alpha;
    int modulate_albedo;
};

layout (local_size_x=8, local_size_y=8, local_size_z=1) in;

#ifndef LAYOUTPTIMAGES_H
#define LAYOUTPTIMAGES_H

#ifdef FINAL_IMAGE
layout(binding = 1, rgba32f) uniform image2D outputImage;
#endif

#ifdef GBUFFER
layout(binding = 15, r32f) uniform image2D depthImage;
layout(binding = 16, rg32f) uniform image2D normalImage;
layout(binding = 17, rgba8) uniform image2D materialImage;
layout(binding = 18, rgba8) uniform image2D albedoImage;
layout(binding = 31, rgba32f) uniform image2D volumeImage;
#endif

#ifdef DEMOD_ILLUMINATION_FLOAT
layout(binding = 25, rgba32f) uniform image2D illumination;
#endif

#ifdef TEMP_GRADIENT
layout(binding = 29, rgba32f) uniform image2D merged_vbuf;
#endif

#endif //LAYOUTPTIMAGES_H
#pragma once

/*
Copyright 2021 Oskar Homburg

Adapted in part from a_svgf, Copyright (c) 2018, Christoph Schied
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

#include <vsg/all.h>
#include "../AccumulationBuffer.hpp"
#include "../GBuffer.hpp"
#include "../IlluminationBuffer.hpp"

struct ASvgfPushConst {
    vsg::vec2 jitter_offset;
    int iteration;
    int step_size;
    int gradientDownsample;
    float temporal_alpha;
    int modulate_albedo;
};

class A_SVGF : public vsg::Inherit<vsg::Object, A_SVGF> {
public:
    A_SVGF(uint32_t width, uint32_t height, vsg::ref_ptr<GBuffer> gBuffer, vsg::ref_ptr<IlluminationBuffer> illuBuffer, vsg::ref_ptr<AccumulationBuffer> accBuffer);

    void compile(vsg::Context&);
    void addDispatchToCommandGraph(vsg::ref_ptr<vsg::Commands> commandGraph);
    vsg::ref_ptr<vsg::DescriptorImage> getFinalDescriptorImage() const;

    int   GradientDownsample = 3;
    bool  ModulateAlbedo = true;
    int   NumIterations = 5;
    int   HistoryTap = 0;
    int   FilterKernel = 1;
    float TemporalAlpha = 0.1f;
    int   DiffAtrousIterations = 5;
    int   GradientFilterRadius = 2;
    bool  NormalizeGradient = true;
    bool  ShowAntilagAlpha  = false;

private:
    uint32_t width, height;

    template<typename T>
    struct PerPass {
        T createGradSamples, atrousGrad, tempAccum, estVariance, atrous;

        template<typename U, typename F>
        PerPass<U> map(F f) {
            PerPass<U> result = {};
            result.createGradSamples = f(createGradSamples);
            result.atrousGrad = f(atrousGrad);
            result.tempAccum = f(tempAccum);
            result.estVariance = f(estVariance);
            result.atrous = f(atrous);
            return result;
        }
    };

    PerPass<vsg::ref_ptr<vsg::ComputePipeline>> pipelines;
    PerPass<vsg::ref_ptr<vsg::BindComputePipeline>> bindPipelines;
    vsg::ref_ptr<vsg::BindDescriptorSet> bindDescriptorSet0, bindDescriptorSet1A, bindDescriptorSet1B;

    // Resources
    vsg::ImageInfo diffA1, diffA2, diffB1, diffB2, accum_color, accum_moments, accum_histlen, accum_color_prev, accum_moments_prev, accum_histlen_prev, varA, varB;
};

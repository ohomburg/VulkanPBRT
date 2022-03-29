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
#include <buffers/AccumulationBuffer.hpp>
#include <buffers/GBuffer.hpp>
#include <buffers/VBuffer.hpp>
#include <buffers/IlluminationBuffer.hpp>

struct ASvgfPushConst {
    int iteration;
    int step_size;
    int gradientDownsample;
    float temporal_alpha;
    int modulate_albedo;
};

struct GradientProjectPushConst {
    vsg::mat4 reprojectionMatrix;
    unsigned frameNum;
};

class GradientProjector : public vsg::Inherit<vsg::Object, GradientProjector> {
    friend class A_SVGF;
public:
    explicit GradientProjector(vsg::ref_ptr<VBuffer> vBuffer);

    void compile(vsg::Context&);
    void addDispatchToCommandGraph(vsg::ref_ptr<vsg::Commands> commandGraph);
    void updateImageLayouts(vsg::Context& context) const;
    void updateDescriptor(vsg::BindDescriptorSet* descSet, const vsg::BindingMap& bindingMap) const;
    void updatePushConstants(vsg::dmat4 projMatrix, vsg::dmat4 viewMatrix, unsigned frameNum);

    uint32_t width, height;
    int GradientDownsample = 3;
    vsg::ref_ptr<vsg::DescriptorImage> mergedVisBuffer; // RGBA32: frameId (bit 31) + depth (30..0), meshId, samplePos X/Y
    vsg::ref_ptr<vsg::DescriptorImage> prevVisBuffer; // same from previous frame.
    vsg::ref_ptr<vsg::DescriptorImage> gradientSamples; // tracking image for subsampled blocks

private:
    vsg::ref_ptr<vsg::BindComputePipeline> bindCreateImg, bindProject;
    vsg::ref_ptr<vsg::BindDescriptorSet> bindDescriptorSetCI, bindDescriptorSetP;
    vsg::ref_ptr<vsg::Value<GradientProjectPushConst>> pushConstValue;
    vsg::mat4 prevProjMatrix, prevViewMatrix;
};

class A_SVGF : public vsg::Inherit<vsg::Object, A_SVGF> {
public:
    A_SVGF(uint32_t width, uint32_t height, vsg::ref_ptr<GBuffer> gBuffer,
           vsg::ref_ptr<IlluminationBuffer> illuBuffer, vsg::ref_ptr<AccumulationBuffer> accBuffer,
           vsg::ref_ptr<GradientProjector> gradProjector, vsg::CommandLine&);

    void compile(vsg::Context&);
    void addDispatchToCommandGraph(vsg::ref_ptr<vsg::Commands> commandGraph, vsg::ref_ptr<vsg::QueryPool> queryPool, std::vector<std::string> &queryNames);
    vsg::ref_ptr<vsg::DescriptorImage> getFinalDescriptorImage() const;
    void updateImageLayouts(vsg::Context& context);
    void updatePushConstants(vsg::dmat4 projMatrix, vsg::dmat4 viewMatrix);

    int   GradientDownsample = 3;
    bool  ModulateAlbedo = false;
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
    vsg::ref_ptr<vsg::ImageInfo> diffA1, diffA2, diffB1, diffB2, accum_color, accum_moments, accum_histlen,
        accum_moments_prev, accum_histlen_prev, accum_volume_prev, varA, varB, color_hist, debug_img;

    vsg::ref_ptr<vsg::mat4Value> projConstantValue;
    vsg::dmat4 prevProjMatrix, prevViewMatrix;
};

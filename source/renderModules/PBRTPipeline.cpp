#include <renderModules/PBRTPipeline.hpp>

#include <cassert>

namespace
{
    struct ConstantInfos
    {
        uint32_t lightCount;
        float lightStrengthSum;
        uint32_t minRecursionDepth;
        uint32_t maxRecursionDepth;
        vsg::vec4 extinction, scattering;
        vsg::vec4 sunDirection, sunColor;
    };

    class ConstantInfosValue : public vsg::Inherit<vsg::Value<ConstantInfos>, ConstantInfosValue>
    {
    public:
        ConstantInfosValue()
        {
        }
    };
}

PBRTPipeline::PBRTPipeline(vsg::ref_ptr<vsg::Node> scene, vsg::ref_ptr<GBuffer> gBuffer,
                 vsg::ref_ptr<IlluminationBuffer> illuminationBuffer, vsg::ref_ptr<GradientProjector> gradProjector, bool writeGBuffer, RayTracingRayOrigin rayTracingRayOrigin) :
    width(illuminationBuffer->illuminationImages[0]->imageInfoList[0]->imageView->image->extent.width),
    height(illuminationBuffer->illuminationImages[0]->imageInfoList[0]->imageView->image->extent.height),
    maxRecursionDepth(2),
    illuminationBuffer(illuminationBuffer),
    gBuffer(gBuffer),
    gradientProjector(gradProjector)
{
    if (writeGBuffer) assert(gBuffer);
    bool useExternalGBuffer = rayTracingRayOrigin == RayTracingRayOrigin::GBUFFER;
    setupPipeline(scene, useExternalGBuffer);
}
void PBRTPipeline::setTlas(vsg::ref_ptr<vsg::AccelerationStructure> as)
{
    auto tlas = as.cast<vsg::TopLevelAccelerationStructure>();
    assert(tlas);
    uint32_t triangleBlasCount = 0, aabbBlasCount = 0;
    for (int i = 0; i < tlas->geometryInstances.size(); ++i)
    {
        // remap indices for volumetric instances separately
        bool isTriangles = geometryTypes[i] < 2;
        uint32_t& count = isTriangles ? triangleBlasCount : aabbBlasCount;
        tlas->geometryInstances[i]->id = count++;
        tlas->geometryInstances[i]->shaderOffset = geometryTypes[i];
        tlas->geometryInstances[i]->flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
    }
    auto accelDescriptor = vsg::DescriptorAccelerationStructure::create(vsg::AccelerationStructures{as}, 0, 0);
    bindRayTracingDescriptorSet->descriptorSet->descriptors.push_back(accelDescriptor);
}
void PBRTPipeline::compile(vsg::Context &context)
{
    illuminationBuffer->compile(context);
}
void PBRTPipeline::updateImageLayouts(vsg::Context &context)
{
    illuminationBuffer->updateImageLayouts(context);
}
void PBRTPipeline::addTraceRaysToCommandGraph(vsg::ref_ptr<vsg::Commands> commandGraph, vsg::ref_ptr<vsg::PushConstants> pushConstants)
{
    auto pipelineBarrier = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_DEPENDENCY_DEVICE_GROUP_BIT);
    commandGraph->addChild(bindRayTracingPipeline);
    commandGraph->addChild(bindRayTracingDescriptorSet);
    commandGraph->addChild(pushConstants);
    auto traceRays = vsg::TraceRays::create();
    traceRays->bindingTable = shaderBindingTable;
    traceRays->width = width;
    traceRays->height = height;
    traceRays->depth = 1;
    commandGraph->addChild(traceRays);
    commandGraph->addChild(pipelineBarrier);
}
vsg::ref_ptr<IlluminationBuffer> PBRTPipeline::getIlluminationBuffer() const
{
    return illuminationBuffer;
}
void PBRTPipeline::setupPipeline(vsg::Node *scene, bool useExternalGbuffer)
{
    // parsing data from scene
    RayTracingSceneDescriptorCreationVisitor buildDescriptorBinding;
    scene->accept(buildDescriptorBinding);
    geometryTypes = buildDescriptorBinding.geometryType;

    const int maxLights = 800;
    if(buildDescriptorBinding.packedLights.size() > maxLights) lightSamplingMethod = LightSamplingMethod::SampleUniform;

    //creating the shader stages and shader binding table
    std::string raygenPath = "shaders/ptRaygen.rgen"; //raygen shader not yet precompiled
    std::string raymissPath = "shaders/ptMiss.rmiss.spv";
    std::string shadowMissPath = "shaders/shadow.rmiss.spv";
    std::string closesthitPath = "shaders/ptClosesthit.rchit.spv";
    std::string anyHitPath = "shaders/ptAlphaHit.rahit.spv";
    std::string cloudHitPath = "shaders/cloud.rchit.spv";
    std::string cloudIntPath = "shaders/cloud.rint.spv";

    auto raygenShader = setupRaygenShader(raygenPath, useExternalGbuffer);
    auto raymissShader = vsg::ShaderStage::read(VK_SHADER_STAGE_MISS_BIT_KHR, "main", raymissPath);
    auto shadowMissShader = vsg::ShaderStage::read(VK_SHADER_STAGE_MISS_BIT_KHR, "main", shadowMissPath);
    auto closesthitShader = vsg::ShaderStage::read(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, "main", closesthitPath);
    auto anyHitShader = vsg::ShaderStage::read(VK_SHADER_STAGE_ANY_HIT_BIT_KHR, "main", anyHitPath);
    auto cloudHitShader = vsg::ShaderStage::read(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, "main", cloudHitPath);
    auto cloudIntShader = vsg::ShaderStage::read(VK_SHADER_STAGE_INTERSECTION_BIT_KHR, "main", cloudIntPath);
    if (!raygenShader || !raymissShader || !closesthitShader || !shadowMissShader || !anyHitShader || !cloudHitShader || !cloudIntShader)
    {
        throw vsg::Exception{"Error: PBRTPipeline::PBRTPipeline(...) failed to create shader stages."};
    }
    bindingMap = vsg::ShaderStage::mergeBindingMaps(
        {raygenShader->getDescriptorSetLayoutBindingsMap(),
         raymissShader->getDescriptorSetLayoutBindingsMap(),
         shadowMissShader->getDescriptorSetLayoutBindingsMap(),
         closesthitShader->getDescriptorSetLayoutBindingsMap(),
         anyHitShader->getDescriptorSetLayoutBindingsMap(),
         cloudHitShader->getDescriptorSetLayoutBindingsMap(),
         cloudIntShader->getDescriptorSetLayoutBindingsMap()});

    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(bindingMap.begin()->second.bindings);
    // auto rayTracingPipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, vsg::PushConstantRanges{{VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(RayTracingPushConstants)}});
    auto pushConstRanges = raygenShader->getPushConstantRanges();
    for (auto &range : pushConstRanges) range.stageFlags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    auto rayTracingPipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout},
                                                                pushConstRanges);
    auto shaderStage = vsg::ShaderStages{raygenShader, raymissShader, shadowMissShader, closesthitShader, anyHitShader, cloudHitShader, cloudIntShader};
    auto raygenShaderGroup = vsg::RayTracingShaderGroup::create();
    raygenShaderGroup->type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    raygenShaderGroup->generalShader = 0;
    auto raymissShaderGroup = vsg::RayTracingShaderGroup::create();
    raymissShaderGroup->type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    raymissShaderGroup->generalShader = 1;
    auto shadowMissShaderGroup = vsg::RayTracingShaderGroup::create();
    shadowMissShaderGroup->type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shadowMissShaderGroup->generalShader = 2;
    auto closesthitShaderGroup = vsg::RayTracingShaderGroup::create();
    closesthitShaderGroup->type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    closesthitShaderGroup->closestHitShader = 3;
    auto transparenthitShaderGroup = vsg::RayTracingShaderGroup::create();
    transparenthitShaderGroup->type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    transparenthitShaderGroup->closestHitShader = 3;
    transparenthitShaderGroup->anyHitShader = 4;
    auto cloudShaderGroup = vsg::RayTracingShaderGroup::create();
    cloudShaderGroup->type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
    cloudShaderGroup->intersectionShader = 6;
    cloudShaderGroup->closestHitShader = 5;

    auto shaderGroups = vsg::RayTracingShaderGroups{
        raygenShaderGroup, raymissShaderGroup, shadowMissShaderGroup, closesthitShaderGroup, transparenthitShaderGroup, cloudShaderGroup};
    shaderBindingTable = vsg::RayTracingShaderBindingTable::create();
    shaderBindingTable->bindingTableEntries.raygenGroups = {raygenShaderGroup};
    shaderBindingTable->bindingTableEntries.raymissGroups = {raymissShaderGroup, shadowMissShaderGroup};
    shaderBindingTable->bindingTableEntries.hitGroups = {closesthitShaderGroup, transparenthitShaderGroup, cloudShaderGroup};
    auto pipeline = vsg::RayTracingPipeline::create(rayTracingPipelineLayout, shaderStage, shaderGroups, shaderBindingTable, 1);
    bindRayTracingPipeline = vsg::BindRayTracingPipeline::create(pipeline);
    auto descriptorSet = vsg::DescriptorSet::create(descriptorSetLayout, vsg::Descriptors{});
    bindRayTracingDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipelineLayout, descriptorSet);

    buildDescriptorBinding.updateDescriptor(bindRayTracingDescriptorSet, bindingMap);
    // creating the constant infos uniform buffer object
    auto constantInfos = ConstantInfosValue::create();
    constantInfos->value().lightCount = buildDescriptorBinding.packedLights.size();
    constantInfos->value().lightStrengthSum = buildDescriptorBinding.packedLights.back().inclusiveStrength;
    constantInfos->value().maxRecursionDepth = maxRecursionDepth;
    constantInfos->value().extinction = vsg::vec4(1024, 1024, 1024, 0);
    constantInfos->value().scattering = vsg::vec4(1, 1, 1, 0);
    constantInfos->value().sunDirection = vsg::normalize(vsg::vec4(0.5826, 0.7660, 0.2717, 0));
    constantInfos->value().sunColor = vsg::vec4(2.6, 2.5, 2.3, 0);
    uint32_t uniformBufferBinding = vsg::ShaderStage::getSetBindingIndex(bindingMap, "Infos").second;
    auto constantInfosDescriptor = vsg::DescriptorBuffer::create(constantInfos, uniformBufferBinding, 0);
    bindRayTracingDescriptorSet->descriptorSet->descriptors.push_back(constantInfosDescriptor);

    // update the descriptor sets
    illuminationBuffer->updateDescriptor(bindRayTracingDescriptorSet, bindingMap);
    if (gBuffer)
        gBuffer->updateDescriptor(bindRayTracingDescriptorSet, bindingMap);
    if (gradientProjector)
        gradientProjector->updateDescriptor(bindRayTracingDescriptorSet, bindingMap);
}
vsg::ref_ptr<vsg::ShaderStage> PBRTPipeline::setupRaygenShader(std::string raygenPath, bool useExternalGBuffer)
{
    std::vector<std::string> defines; // needed defines for the correct illumination buffer
    
    // set different raygen shaders according to state of external gbuffer and illumination buffer type
    if (useExternalGBuffer)
    {
        // TODO: implement things for external gBuffer
        // raygenPath = "shaders/raygen.rgen.spv";
    }
    else
    {
        if (illuminationBuffer.cast<IlluminationBufferFinalFloat>())
        {
            defines.push_back("FINAL_IMAGE");
        }
        else if(illuminationBuffer.cast<IlluminationBufferDemodulatedFloat>()){
            defines.push_back("DEMOD_ILLUMINATION_FLOAT");
        }
        else if (illuminationBuffer.cast<IlluminationBufferFinalDirIndir>())
        {
            // TODO:
        }
        else
        {
            throw vsg::Exception{"Error: PBRTPipeline::setupRaygenShader(...) Illumination buffer not supported."};
        }
    }
    if (gBuffer)
        defines.push_back("GBUFFER");
    if (gradientProjector)
        defines.push_back("TEMP_GRADIENT");

    switch(lightSamplingMethod){
        case LightSamplingMethod::SampleSurfaceStrength:
            defines.push_back("LIGHT_SAMPLE_SURFACE_STRENGTH");
            break;
        case LightSamplingMethod::SampleLightStrength:
            defines.push_back("LIGHT_SAMPLE_LIGHT_STRENGTH");
            break;
        default:
            break;
    }

    auto options = vsg::Options::create(vsgXchange::glsl::create());
    auto raygenShader = vsg::ShaderStage::read(VK_SHADER_STAGE_RAYGEN_BIT_KHR, "main", raygenPath, options);
    if(!raygenShader)
        throw vsg::Exception{"Error: PBRTPipeline::setupRaygenShader() Could not load ray generation shader."};
    auto compileHints = vsg::ShaderCompileSettings::create();
    compileHints->vulkanVersion = VK_API_VERSION_1_2;
    compileHints->target = vsg::ShaderCompileSettings::SPIRV_1_4;
    compileHints->defines = defines;
    raygenShader->module->hints = compileHints;

    return raygenShader;
}
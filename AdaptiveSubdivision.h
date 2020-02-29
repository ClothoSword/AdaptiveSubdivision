#pragma once
#include "Falcor.h"

using namespace Falcor;

enum class ShadingMode {
    Lod,
    Diffuse,
    Normal,
};

enum class TessellationMode {
    Phong,
    None
};

uint32_t ShadingModeID = 1;
uint32_t TessellationModeID = 0;

Gui::DropdownList ShadingModeList = {
    {(uint32_t)ShadingMode::Lod,"Lod"},
    {(uint32_t)ShadingMode::Diffuse,"Diffuse"},
    {(uint32_t)ShadingMode::Normal,"Normal" }
};
Gui::DropdownList TessellationModeList = {
    {(uint32_t)TessellationMode::Phong,"Phong"},
    {(uint32_t)TessellationMode::None,"None"}
};

struct AppConfig {
    bool FreezeSubd = false;
    bool RenderSuzanne = false;
    float TargetPixelSize = 5.0f;
    bool OnlyRender = false;
    bool EnableCulling = true;
    bool Wireframe = false;
    float DisplacementFactor = 0.3f;
    bool Displace = true;
    ShadingMode SM = ShadingMode::Diffuse;
    TessellationMode TM = TessellationMode::Phong;
};

struct LodKernelConfig {
    float FovX;
    float TargetPixelSize;
    uint ScreenResolutionWidth;
    float DisplacementFactor;
};

struct RenderKernelConfig {
    float DisplacementFactor;
};

struct ModelRendererElements {
    std::string ModelFilePath = "";
    std::string ShaderFileName = "";
    std::string VertexShaderName = "";
    std::string PixelShaderName = "";
    GraphicsProgram::SharedPtr Program = nullptr;
    GraphicsVars::SharedPtr ProgramVars = nullptr;
    GraphicsState::SharedPtr GraphicsState = nullptr;
};

class AdaptiveSubdivision : public IRenderer
{
public:
    void onLoad(RenderContext* pRenderContext) override;
    void onFrameRender(RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo) override;
    void onShutdown() override;
    void onResizeSwapChain(uint32_t width, uint32_t height) override;
    bool onKeyEvent(const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(const MouseEvent& mouseEvent) override;
    void onDataReload() override;
    void onGuiRender(Gui* pGui) override;

private:
    struct ComputeShaderUtils {
        ComputeProgram::SharedPtr mpComputeProgram = nullptr;
        ComputeVars::SharedPtr mpComputeVars = nullptr;
        ComputeState::SharedPtr mpComputeState = nullptr;
    };

    Scene::SharedPtr mpScene = nullptr;

    uvec2 InitSubdBuffer[4] = { uvec2(0,2),uvec2(1,2),uvec2(0,3),uvec2(1,3) };
    float4 VertexData[4] = { float4(-1.0f,-1.0f,0.0f,1.0f), float4(1.0f,-1.0f,0.0f,1.0f) ,float4(1.0f,1.0f,0.0f,1.0f) ,float4(-1.0f,1.0f,0.0f,1.0f) };
    uint32 IndexData[6] = { 0,1,3,2,3,1 };

    void LoadRenderState();
    void LoadModelRenderer(ModelRendererElements &inModelRendererElements, const std::string &inRasterizerStateGroupName, const std::string &inDepthStencilStateGroupName);

    void LoadTexture();

    void LoadLodKernel();
    void LoadRenderKernel();
    void LoadIndirectBatcherKernel();

    void LoadBuffer();

    Scene::SharedPtr GetRenderScene(ModelRendererElements &inModelRendererElements);
    void RenderModel(RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo, ModelRendererElements &inModelRendererElements);
    ModelRendererElements mSuzanneModelRenderer;

    StructuredBuffer::SharedPtr mpSubdBuffer_0 = nullptr;
    StructuredBuffer::SharedPtr mpSubdBuffer_1 = nullptr;
    StructuredBuffer::SharedPtr mpSubdCulledBuffer = nullptr;
    TypedBuffer<float4>::SharedPtr mpVertexBuffer = nullptr;
    TypedBuffer<uint32>::SharedPtr mpIndexBuffer = nullptr;
    TypedBuffer<uint32>::SharedPtr mpInstanceIndexBuffer = nullptr;
    Buffer::SharedPtr mpIndirectDrawBuffer = nullptr;
    Buffer::SharedPtr mpIndirectDispatchBuffer = nullptr;
    Buffer::SharedPtr mpBufferCounter = nullptr;
    ConstantBuffer::SharedPtr mpLodKernelCB = nullptr;

    ComputeProgram::SharedPtr mpLodKernelProgram = nullptr;
    ComputeVars::SharedPtr mpLodKernelVars = nullptr;
    ComputeState::SharedPtr mpLodKernelState = nullptr;

    GraphicsProgram::SharedPtr mpRenderKernelProgram = nullptr;
    GraphicsVars::SharedPtr mpRenderKernelVars = nullptr;
    GraphicsState::SharedPtr mpRenderKernelState = nullptr;
    RasterizerState::SharedPtr mpRenderKernelRastState = nullptr;
    DepthStencilState::SharedPtr mpRenderKernelDepthTest = nullptr;
    StructuredBuffer::SharedPtr mpSubdUV = nullptr;
    Buffer::SharedPtr mpPerInstancedIndex = nullptr;
    Texture::SharedPtr mpHeightMap = nullptr;
    Texture::SharedPtr mpSlopeMap = nullptr;
    ConstantBuffer::SharedPtr mpRenderKernelCB = nullptr;

    ComputeProgram::SharedPtr mpIndirectBatcherKernelProgram = nullptr;
    ComputeVars::SharedPtr mpIndirectBatcherKernelVars = nullptr;
    ComputeState::SharedPtr mpIndirectBatcherKernelState = nullptr;

    bool Pingping = true;

    AppConfig mAppConfig;
    LodKernelConfig mLodKernelCB;
    RenderKernelConfig mRenderKernelCB;
    float test = 1.0f;

    std::map<std::string, RasterizerState::SharedPtr> RasterizerStateGroup;
    std::map<std::string, DepthStencilState::SharedPtr> DepthStencilStateGroup;
    std::map<std::string, Sampler::SharedPtr> SamplerGroup;
};

const vec2 SubdUVData[] = {
    { 0.25f*0.5f, 0.75f*0.5f + 0.5f },
    { 0.0f*0.5f, 1.0f*0.5f + 0.5f },
    { 0.0f*0.5f, 0.75f*0.5f + 0.5f },
    { 0.0f*0.5f , 0.5f*0.5f + 0.5f },
    { 0.25f*0.5f, 0.5f*0.5f + 0.5f },
    { 0.5f*0.5f, 0.5f*0.5f + 0.5f },
    { 0.25f*0.5f, 0.25f*0.5f + 0.5f },
    { 0.0f*0.5f, 0.25f*0.5f + 0.5f },
    { 0.0f*0.5f, 0.0f*0.5f + 0.5f },
    { 0.25f*0.5f, 0.0f*0.5f + 0.5f },
    { 0.5f*0.5f, 0.0f*0.5f + 0.5f },
    { 0.5f*0.5f, 0.25f*0.5f + 0.5f },
    { 0.75f*0.5f, 0.25f*0.5f + 0.5f },
    { 0.75f*0.5f, 0.0f*0.5f + 0.5f },
    { 1.0f*0.5f, 0.0f*0.5f + 0.5f },        //14

    { 0.375f, 0.375f },
    { 0.25f, 0.375f },
    { 0.25f, 0.25f },
    { 0.375f, 0.25f },
    { 0.5f, 0.25f },
    { 0.5f, 0.375f },    //20

    { 0.125f, 0.375f },
    { 0.0f, 0.375f },
    { 0.0f, 0.25f },
    { 0.125f, 0.25f },    //24

    { 0.125f, 0.125f },
    { 0.0f, 0.125f },
    { 0.0f, 0.0f },
    { 0.125f, 0.0f },
    { 0.25f, 0.0f },
    { 0.25f, 0.125f },    //30

    { 0.375f, 0.125f },
    { 0.375f, 0.0f },
    { 0.5f, 0.0f },
    { 0.5f, 0.125f },    //34

    { 0.625f, 0.375f },
    { 0.625f, 0.25f },
    { 0.75f, 0.25f },    //37

    { 0.625f, 0.125f },
    { 0.625f, 0.0f },
    { 0.75f, 0.0f },
    { 0.75f, 0.125f },    //41

    { 0.875f, 0.125f },
    { 0.875f, 0.0f },
    { 1.0f, 0.0f }    //44
};

const uint16_t Indexes[] = {
    0u, 1u, 2u,
    0u, 2u, 3u,
    0u, 3u, 4u,
    0u, 4u, 5u,

    6u, 5u, 4u,
    6u, 4u, 3u,
    6u, 3u, 7u,
    6u, 7u, 8u,

    6u, 8u, 9u,
    6u, 9u, 10u,
    6u, 10u, 11u,
    6u, 11u, 5u,

    12u, 5u, 11u,
    12u, 11u, 10u,
    12u, 10u, 13u,
    12u, 13u, 14u,

    15u, 14u, 13u,
    15u, 13u, 10u,
    15u, 10u, 16u,
    15u, 16u, 17u,
    15u, 17u, 18u,
    15u, 18u, 19u,
    15u, 19u, 20u,
    15u, 20u, 14u,

    21u, 10u, 9u,
    21u, 9u, 8u,
    21u, 8u, 22u,
    21u, 22u, 23u,
    21u, 23u, 24u,
    21u, 24u, 17u,
    21u, 17u, 16u,
    21u, 16u, 10u,

    25u, 17u, 24u,
    25u, 24u, 23u,
    25u, 23u, 26u,
    25u, 26u, 27u,
    25u, 27u, 28u,
    25u, 28u, 29u,
    25u, 29u, 30u,
    25u, 30u, 17u,

    31u, 19u, 18u,
    31u, 18u, 17u,
    31u, 17u, 30u,
    31u, 30u, 29u,
    31u, 29u, 32u,
    31u, 32u, 33u,
    31u, 33u, 34u,
    31u, 34u, 19u,

    35u, 14u, 20u,
    35u, 20u, 19u,
    35u, 19u, 36u,
    35u, 36u, 37u,

    38u, 37u, 36u,
    38u, 36u, 19u,
    38u, 19u, 34u,
    38u, 34u, 33u,
    38u, 33u, 39u,
    38u, 39u, 40u,
    38u, 40u, 41u,
    38u, 41u, 37u,

    42u, 37u, 41u,
    42u, 41u, 40u,
    42u, 40u, 43u,
    42u, 43u, 44u
};

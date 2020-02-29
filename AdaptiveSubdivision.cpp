#include "AdaptiveSubdivision.h"

std::string ProjectName = "Adaptive Subdivision";
std::string ModelFileName = "Suzanne.obj";
std::string HeightMapName = "HeightMap.png";

const size_t SubdBufferSize = 1 << 20;

void AdaptiveSubdivision::onGuiRender(Gui* pGui)
{
    Gui::Window w(pGui, "Falcor", { 600, 350 }, { 50,50 });
    gpFramework->renderGlobalUI(pGui);
    w.text("Adaptive Subdivision Configuration");

    auto controlsGroup = Gui::Group(pGui, "Controls");
    if (controlsGroup.open()) {
        w.checkbox("Only Render", mAppConfig.OnlyRender);
        w.checkbox("Enable Culling", mAppConfig.EnableCulling);
        w.checkbox("Wireframe", mAppConfig.Wireframe);
        w.checkbox("Displace", mAppConfig.Displace);

        w.checkbox("Freeze Subdivision", mAppConfig.FreezeSubd);
        w.slider("Target Pixel Size", mAppConfig.TargetPixelSize, 0.3f, 20.0f);
        w.slider("Displacement Factor", mAppConfig.DisplacementFactor, 0.0f, 0.5f);

        if (w.dropdown("Shading Mode", ShadingModeList, ShadingModeID)) {
            mAppConfig.SM = (ShadingMode)ShadingModeID;
        }
        if (w.dropdown("Tessellation Mode", TessellationModeList, TessellationModeID)) {
            mAppConfig.TM = (TessellationMode)TessellationModeID;
        }
    }

    auto TestGroup = Gui::Group(pGui, "Tests");
    if (TestGroup.open()) {
        w.checkbox("Render Suzanne", mAppConfig.RenderSuzanne);
    }

    gpFramework->getWindow()->setWindowTitle(ProjectName + " " +gpFramework->getFrameRate().getMsg());
}

void AdaptiveSubdivision::onLoad(RenderContext* pRenderContext)
{
    LoadRenderState();

    //Load Test Model
    {
        mSuzanneModelRenderer.ModelFilePath = ModelFileName;
        mSuzanneModelRenderer.ShaderFileName = "Test.ps.hlsl";
        mSuzanneModelRenderer.PixelShaderName = "main";
        LoadModelRenderer(mSuzanneModelRenderer, "SolidBackCull", "LessEnableDepth");
        mpScene = GetRenderScene(mSuzanneModelRenderer);
    }

    mpScene->setCameraController(Scene::CameraControllerType::Orbiter);
    mpScene->getCamera()->setDepthRange(0.0001f, 95.0f);
    mpScene->getCamera()->setPosition(vec3(1, 1, 1));

    //Load Subd Pipeline Assets
    {
        LoadTexture();
        LoadLodKernel();
        LoadRenderKernel();
        LoadIndirectBatcherKernel();
        LoadBuffer();
    }
}

void AdaptiveSubdivision::LoadTexture() {
    Texture::SharedPtr TempTexture = Texture::createFromFile(HeightMapName, false, true);
    int w = TempTexture->getWidth();
    int h = TempTexture->getHeight();
    Bitmap::UniqueConstPtr pBitmap = Bitmap::createFromFile(HeightMapName, true);
    uint16_t *texels = (uint16_t *)pBitmap->getData();

    //Create Height Map
    {
        std::vector<uint16_t> dmap(w * h);
        for (int j = 0; j < h; ++j) {
            for (int i = 0; i < w; ++i) {
                dmap[j*w + i] = texels[j*w + i];
            }
        }
        mpHeightMap = Texture::create2D(w, h, ResourceFormat::R16Unorm, 1u, 4294967295u, &dmap[0]);
    }

    //Create Slope Map
    {
        std::vector<float> smap(w * h * 2);
        for (int j = 0; j < h; ++j) {
            for (int i = 0; i < w; ++i) {
                int i1 = std::max(0, i - 1);
                int i2 = std::min(w - 1, i + 1);
                int j1 = std::max(0, j - 1);
                int j2 = std::min(h - 1, j + 1);
                uint16_t px_l = texels[i1 + w * j];
                uint16_t px_r = texels[i2 + w * j];
                uint16_t px_b = texels[i + w * j1];
                uint16_t px_t = texels[i + w * j2];
                float z_l = (float)px_l / 65535.0f;
                float z_r = (float)px_r / 65535.0f;
                float z_b = (float)px_b / 65535.0f;
                float z_t = (float)px_t / 65535.0f;
                float slope_x = (float)w * 0.5f * (z_r - z_l);
                float slope_y = (float)h * 0.5f * (z_t - z_b);

                smap[2 * (i + w * j)] = slope_x;
                smap[1 + 2 * (i + w * j)] = slope_y;
            }
        }
        mpSlopeMap = Texture::create2D(w, h, ResourceFormat::RG32Float, 1u, 4294967295u, &smap[0]);
    }
}

void AdaptiveSubdivision::LoadBuffer() {
    {
        mpSubdBuffer_0 = StructuredBuffer::create(mpLodKernelProgram.get(), "SubdIn", SubdBufferSize);
        mpSubdBuffer_0->setBlob(InitSubdBuffer, 0, sizeof(InitSubdBuffer));
        mpSubdBuffer_1 = StructuredBuffer::create(mpLodKernelProgram.get(), "SubdOut", SubdBufferSize);
        mpSubdCulledBuffer = StructuredBuffer::create(mpLodKernelProgram.get(), "SubdCulledOut", SubdBufferSize);
        mpSubdUV = StructuredBuffer::create(mpRenderKernelProgram.get(), "SubdInstanced", sizeof(SubdUVData) / sizeof(SubdUVData[0]));
        mpSubdUV->setBlob(SubdUVData, 0, sizeof(SubdUVData));
    }

    {
        mpVertexBuffer = TypedBuffer<float4>::create(4);
        mpVertexBuffer->setBlob(VertexData, 0, sizeof(VertexData));
        mpIndexBuffer = TypedBuffer<uint32>::create(6);
        mpIndexBuffer->setBlob(IndexData, 0, sizeof(IndexData));
    }

    {
        D3D12_DRAW_INDEXED_ARGUMENTS mdraw = { 192,0,0,0,0 };
        mpIndirectDrawBuffer = Buffer::create(sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), Buffer::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg, Buffer::CpuAccess::Read, &mdraw);
        D3D12_DISPATCH_ARGUMENTS mdispatch = { 1,1,1 };
        mpIndirectDispatchBuffer = Buffer::create(sizeof(D3D12_DISPATCH_ARGUMENTS), Buffer::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg, Buffer::CpuAccess::Read, &mdispatch);
        mpBufferCounter = Buffer::create(sizeof(uvec3), Buffer::BindFlags::UnorderedAccess, Buffer::CpuAccess::Read, nullptr);
        mpBufferCounter->setBlob(&uvec3(0, 0, sizeof(InitSubdBuffer) / sizeof(InitSubdBuffer[0])), 0, sizeof(uvec3));
    }
}

void AdaptiveSubdivision::LoadLodKernel() {
#ifdef DEBUG
    mpLodKernelProgram = ComputeProgram::createFromFile("AdaptiveSubdivision.hlsl", "LodKernel", Program::DefineList(), Shader::CompilerFlags::GenerateDebugInfo);
#else
    mpLodKernelProgram = ComputeProgram::createFromFile("AdaptiveSubdivision.hlsl", "LodKernel");
#endif 
    mpLodKernelCB = ConstantBuffer::create(mpLodKernelProgram.get(), "LodKernelCB", sizeof(LodKernelConfig));

    mpLodKernelVars = ComputeVars::create(mpLodKernelProgram->getReflector());
    mpLodKernelVars->setConstantBuffer("LodKernelCB", mpLodKernelCB);

    mpLodKernelState = ComputeState::create();
    mpLodKernelState->setProgram(mpLodKernelProgram);

    mLodKernelCB.FovX = mpScene->getCamera()->getAspectRatio() * focalLengthToFovY(mpScene->getCamera()->getFocalLength(), mpScene->getCamera()->getFrameHeight());
    mLodKernelCB.ScreenResolutionWidth = gpFramework->getTargetFbo()->getWidth();
}

void AdaptiveSubdivision::LoadRenderKernel() {

    Program::Desc d;
    {
#ifdef DEBUG
        d.setCompilerFlags(Shader::CompilerFlags::GenerateDebugInfo | Shader::CompilerFlags::DumpIntermediates | Shader::CompilerFlags::TreatWarningsAsErrors);
#endif
        d.addShaderLibrary("AdaptiveSubdivision.hlsl").vsEntry("RenderKernelVS").psEntry("RenderKernelPS");
        d.setShaderModel("5_1");
        mpRenderKernelProgram = GraphicsProgram::create(d);
    }

    mpRenderKernelVars = GraphicsVars::create(mpRenderKernelProgram->getReflector());
    mpRenderKernelState = GraphicsState::create();
    mpRenderKernelState->setProgram(mpRenderKernelProgram);

    // create VAO
    {
        mpPerInstancedIndex = Buffer::create(sizeof(Indexes), Resource::BindFlags::Index | ResourceBindFlags::ShaderResource, Buffer::CpuAccess::None, &Indexes);
        Vao::SharedPtr TempVao = Vao::create(Vao::Topology::TriangleList, nullptr, Vao::BufferVec(), mpPerInstancedIndex, ResourceFormat::R16Uint);
        mpRenderKernelState->setVao(TempVao);
    }

    mpRenderKernelState->setDepthStencilState(DepthStencilStateGroup["LessEnableDepth"]);
    mpRenderKernelState->setRasterizerState(RasterizerStateGroup["SolidNoneCull"]);
    mpRenderKernelState->setBlendState(BlendState::create(BlendState::Desc()));

    mpRenderKernelCB = ConstantBuffer::create(mpRenderKernelProgram.get(), "RenderKernelCB", sizeof(RenderKernelConfig));
    mpRenderKernelVars->setConstantBuffer("RenderKernelCB", mpRenderKernelCB);
}

void AdaptiveSubdivision::LoadIndirectBatcherKernel() {
#ifdef DEBUG
    mpIndirectBatcherKernelProgram = ComputeProgram::createFromFile("AdaptiveSubdivision.hlsl", "IndirectBatcherKernel", Program::DefineList(), Shader::CompilerFlags::GenerateDebugInfo);
#else
    mpIndirectBatcherKernelProgram = ComputeProgram::createFromFile("AdaptiveSubdivision.hlsl", "IndirectBatcherKernel");
#endif
    mpIndirectBatcherKernelVars = ComputeVars::create(mpIndirectBatcherKernelProgram->getReflector());
    mpIndirectBatcherKernelVars->setConstantBuffer("LodKernelCB", mpLodKernelCB);
    mpIndirectBatcherKernelState = ComputeState::create();;
    mpIndirectBatcherKernelState->setProgram(mpIndirectBatcherKernelProgram);
}

void AdaptiveSubdivision::RenderModel(RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo,ModelRendererElements &inModelRendererElements) {
    if (mpScene)
    {
        Scene::RenderFlags renderFlags = Scene::RenderFlags::UserRasterizerState;
        inModelRendererElements.GraphicsState->setFbo(pTargetFbo);
        mpScene->render(pRenderContext, inModelRendererElements.GraphicsState.get(), inModelRendererElements.ProgramVars.get(), renderFlags);
    }
}

void AdaptiveSubdivision::onFrameRender(RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
{
    const vec4 ClearColor(0.3f, 0.3f, 0.3f, 1);
    pRenderContext->clearFbo(pTargetFbo.get(), ClearColor, 1.0f, 0, FboAttachmentType::All);

    mpScene->update(pRenderContext, gpFramework->getGlobalClock().now());
    if (mAppConfig.RenderSuzanne) {
        RenderModel(pRenderContext, pTargetFbo, mSuzanneModelRenderer);
    }

    //Setup Defines
    {
        mAppConfig.FreezeSubd ? mpLodKernelProgram->addDefine("FREEZE_SUBDIVISION") : mpLodKernelProgram->removeDefine("FREEZE_SUBDIVISION");
        mAppConfig.EnableCulling ? mpLodKernelProgram->addDefine("FRUSTUM_CULLING") : mpLodKernelProgram->removeDefine("FRUSTUM_CULLING");
        mAppConfig.Wireframe ? mpRenderKernelState->setRasterizerState(RasterizerStateGroup["WireframeNoneCull"]) : mpRenderKernelState->setRasterizerState(RasterizerStateGroup["SolidNoneCull"]);
        mAppConfig.Displace ? mpLodKernelProgram->addDefine("DISPLACE") : mpLodKernelProgram->removeDefine("DISPLACE");
        mAppConfig.Displace ? mpRenderKernelProgram->addDefine("DISPLACE") : mpRenderKernelProgram->removeDefine("DISPLACE");

        mpRenderKernelProgram->removeDefine("SHADING_LOD");
        mpRenderKernelProgram->removeDefine("SHADING_DIFFUSE");
        mpRenderKernelProgram->removeDefine("SHADING_NORMAL");
        if (mAppConfig.SM == ShadingMode::Lod) {
            mpRenderKernelProgram->addDefine("SHADING_LOD");
        }
        else if (mAppConfig.SM == ShadingMode::Diffuse) {
            mpRenderKernelProgram->addDefine("SHADING_DIFFUSE");
        }
        else if (mAppConfig.SM == ShadingMode::Normal) {
            mpRenderKernelProgram->addDefine("SHADING_NORMAL");
        }

        if (mAppConfig.TM == TessellationMode::Phong) {
            mpRenderKernelProgram->addDefine("PHONG_TESSELLATION");
        }
        else if(mAppConfig.TM == TessellationMode::None){
            mpRenderKernelProgram->removeDefine("PHONG_TESSELLATION");
        }
    }

    mLodKernelCB.TargetPixelSize = mAppConfig.TargetPixelSize;
    mLodKernelCB.DisplacementFactor = mRenderKernelCB.DisplacementFactor = mAppConfig.DisplacementFactor;
    mpLodKernelCB->setBlob(&mLodKernelCB, 0, sizeof(LodKernelConfig));
    mpRenderKernelCB->setBlob(&mRenderKernelCB, 0, sizeof(RenderKernelConfig));

    if (!mAppConfig.OnlyRender) {
        //LodKernel
        mpLodKernelVars->setParameterBlock("gScene", mpScene->getParameterBlock());
        mpLodKernelVars->setStructuredBuffer("SubdIn", (Pingping ? mpSubdBuffer_0 : mpSubdBuffer_1));
        mpLodKernelVars->setStructuredBuffer("SubdOut", (Pingping ? mpSubdBuffer_1 : mpSubdBuffer_0));
        mpLodKernelVars->setTypedBuffer("VertexBuffer", mpVertexBuffer);
        mpLodKernelVars->setTypedBuffer("IndexBuffer", mpIndexBuffer);
        mpLodKernelVars->setStructuredBuffer("SubdCulledOut", mpSubdCulledBuffer);
        mpLodKernelVars->setRawBuffer("IndirectDrawBuffer", mpIndirectDrawBuffer);
        mpLodKernelVars->setRawBuffer("IndirectDispatchBuffer", mpIndirectDispatchBuffer);
        mpLodKernelVars->setRawBuffer("BufferCounter", mpBufferCounter);
        pRenderContext->dispatchIndirect(mpLodKernelState.get(), mpLodKernelVars.get(), mpIndirectDispatchBuffer.get(), 0);

        //IndirectBatcherKernel
        mpIndirectBatcherKernelVars->setRawBuffer("IndirectDrawBuffer", mpIndirectDrawBuffer);
        mpIndirectBatcherKernelVars->setRawBuffer("IndirectDispatchBuffer", mpIndirectDispatchBuffer);
        mpIndirectBatcherKernelVars->setRawBuffer("BufferCounter", mpBufferCounter);
        pRenderContext->dispatch(mpIndirectBatcherKernelState.get(), mpIndirectBatcherKernelVars.get(), uvec3(1, 1, 1));
    }

    //RenderKernel
    pRenderContext->flush();
    mpRenderKernelVars->setTexture("HeightMapTexture", mpHeightMap);
    mpRenderKernelVars->setTexture("SlopeMapTexture", mpSlopeMap);
    mpRenderKernelVars->setSampler("HeightMapSampler", SamplerGroup["Linear"]);
    mpRenderKernelVars->setSampler("SlopeMapSampler", SamplerGroup["Linear"]);
    mpRenderKernelVars->setStructuredBuffer("SubdInstanced", mpSubdUV);
    mpRenderKernelVars->setStructuredBuffer("SubdIn", mpSubdCulledBuffer);
    mpRenderKernelVars->setTypedBuffer("VertexBuffer", mpVertexBuffer);
    mpRenderKernelVars->setTypedBuffer("IndexBuffer", mpIndexBuffer);
    mpRenderKernelVars->setParameterBlock("gScene", mpScene->getParameterBlock());
    mpRenderKernelState->setFbo(pTargetFbo);
    pRenderContext->drawIndexedIndirect(mpRenderKernelState.get(), mpRenderKernelVars.get(), 1, mpIndirectDrawBuffer.get(), 0, nullptr, 0);

    Pingping = !Pingping;
}

void AdaptiveSubdivision::onShutdown()
{
}

bool AdaptiveSubdivision::onKeyEvent(const KeyboardEvent& keyEvent)
{
    return mpScene ? mpScene->onKeyEvent(keyEvent) : false;
}

bool AdaptiveSubdivision::onMouseEvent(const MouseEvent& mouseEvent)
{
    return mpScene ? mpScene->onMouseEvent(mouseEvent) : false;
}

void AdaptiveSubdivision::onDataReload()
{

}

void AdaptiveSubdivision::onResizeSwapChain(uint32_t width, uint32_t height)
{

}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    AdaptiveSubdivision::UniquePtr pRenderer = std::make_unique<AdaptiveSubdivision>();
    SampleConfig config;
    config.windowDesc.title = "Adaptive Subdivision";
    config.windowDesc.resizableWindow = true;
    Sample::run(config, pRenderer);
    return 0;
}

void AdaptiveSubdivision::LoadRenderState() {
    RasterizerState::Desc RSDesc; {
        RSDesc.setFillMode(RasterizerState::FillMode::Solid).setCullMode(RasterizerState::CullMode::Back);
        RasterizerStateGroup.insert(std::pair<std::string, RasterizerState::SharedPtr>("SolidBackCull", RasterizerState::create(RSDesc)));
        RSDesc.setFillMode(RasterizerState::FillMode::Wireframe).setCullMode(RasterizerState::CullMode::Back);
        RasterizerStateGroup.insert(std::pair<std::string, RasterizerState::SharedPtr>("WireframeBackCull", RasterizerState::create(RSDesc)));
        RSDesc.setFillMode(RasterizerState::FillMode::Solid).setCullMode(RasterizerState::CullMode::None);
        RasterizerStateGroup.insert(std::pair<std::string, RasterizerState::SharedPtr>("SolidNoneCull", RasterizerState::create(RSDesc)));
        RSDesc.setFillMode(RasterizerState::FillMode::Wireframe).setCullMode(RasterizerState::CullMode::None);
        RasterizerStateGroup.insert(std::pair<std::string, RasterizerState::SharedPtr>("WireframeNoneCull", RasterizerState::create(RSDesc)));
    }

    DepthStencilState::Desc DSDesc; {
        DSDesc.setDepthFunc(ComparisonFunc::Less).setDepthEnabled(true);
        DepthStencilStateGroup.insert(std::pair<std::string, DepthStencilState::SharedPtr>("LessEnableDepth", DepthStencilState::create(DSDesc)));
        DSDesc.setDepthFunc(ComparisonFunc::Greater).setDepthEnabled(true);
        DepthStencilStateGroup.insert(std::pair<std::string, DepthStencilState::SharedPtr>("GreaterEnableDepth", DepthStencilState::create(DSDesc)));
        DSDesc.setDepthFunc(ComparisonFunc::GreaterEqual).setDepthEnabled(true);
        DepthStencilStateGroup.insert(std::pair<std::string, DepthStencilState::SharedPtr>("GreaterEqualEnableDepth", DepthStencilState::create(DSDesc)));
    }

    Sampler::Desc SamplerDesc; {
        SamplerDesc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point).setAddressingMode(Sampler::AddressMode::Mirror, Sampler::AddressMode::Mirror, Sampler::AddressMode::Mirror);
        SamplerGroup.insert(std::pair<std::string, Sampler::SharedPtr>("Point", Sampler::create(SamplerDesc)));
        SamplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Wrap);
        SamplerGroup.insert(std::pair<std::string, Sampler::SharedPtr>("Linear", Sampler::create(SamplerDesc)));
    }
}

Scene::SharedPtr AdaptiveSubdivision::GetRenderScene(ModelRendererElements &inModelRendererElements) {
    std::string ModelFileFullPath = "";
    SceneBuilder::SharedPtr pBuilder;
    if (findFileInDataDirectories(mSuzanneModelRenderer.ModelFilePath, ModelFileFullPath)) {
        pBuilder = SceneBuilder::create(ModelFileFullPath);
        if (!pBuilder)
        {
            msgBox("Could not load model");
            return nullptr;
        }
    }
    return pBuilder->getScene();
}

void AdaptiveSubdivision::LoadModelRenderer(ModelRendererElements &inModelRendererElements, const std::string &inRasterizerStateGroupName, const std::string &inDepthStencilStateGroupName) {
    inModelRendererElements.Program = GraphicsProgram::createFromFile(inModelRendererElements.ShaderFileName, inModelRendererElements.VertexShaderName, inModelRendererElements.PixelShaderName);
    inModelRendererElements.ProgramVars = GraphicsVars::create(inModelRendererElements.Program->getReflector());;
    inModelRendererElements.GraphicsState = GraphicsState::create();
    inModelRendererElements.GraphicsState->setProgram(inModelRendererElements.Program);
    inModelRendererElements.GraphicsState->setRasterizerState(RasterizerStateGroup[inRasterizerStateGroupName]);
    inModelRendererElements.GraphicsState->setDepthStencilState(DepthStencilStateGroup[inDepthStencilStateGroupName]);
}

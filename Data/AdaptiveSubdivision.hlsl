#include "Utils.hlsl"

[numthreads(32,1,1)]
void LodKernel(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint ThreadId = DispatchThreadId.x;

    if (ThreadId >= BufferCounter.Load(8))
        return;

    uint PrimitiveIndex = SubdIn[ThreadId].PrimitiveIndex;
    float4 InVertices[3] =
    {
        VertexBuffer[IndexBuffer[PrimitiveIndex*3]],
        VertexBuffer[IndexBuffer[PrimitiveIndex*3+1]],
        VertexBuffer[IndexBuffer[PrimitiveIndex*3+2]]
    };

    uint SubdBinaryKey = SubdIn[ThreadId].SubdBinaryKey;
    float4 OutVertices[3],OutParentVertices[3];
    Subd(SubdBinaryKey, InVertices, OutVertices, OutParentVertices);
    int TargetLod = ComputeLod(OutVertices);
    int ParentLod = ComputeLod(OutParentVertices);
#ifdef FREEZE_SUBDIVISION
    TargetLod = ParentLod = firstbithigh(SubdBinaryKey);
#endif
    UpdateSubdBuffer(SubdBinaryKey, TargetLod, ParentLod, PrimitiveIndex);

#ifdef FRUSTUM_CULLING
    float4 MinPosition = min(min(OutVertices[0], OutVertices[1]), OutVertices[2]);
    float4 MaxPosition = max(max(OutVertices[0], OutVertices[1]), OutVertices[2]);
#ifdef DISPLACE
    MinPosition.z = 0;
    MaxPosition.z = LDisplacementFactor;
#endif
    float4x4 ModelViewProjectionMatrix = gScene.camera.viewProjMat;
    if (FrustumCullingTest(ModelViewProjectionMatrix, MinPosition, MaxPosition))
    {
#else
    if (true)
    {
#endif
        PrimitiveData Data = { PrimitiveIndex, SubdBinaryKey };
        uint OriginValue = 0;
        BufferCounter.InterlockedAdd(0, 1u, OriginValue);
        SubdCulledOut[OriginValue] = Data;
    }
}

[numthreads(1,1,1)]
void IndirectBatcherKernel()
{
    uint SubdDataCount = BufferCounter.Load(4);
    IndirectDispatchBuffer.Store3(0, uint3(SubdDataCount / 32 + 1, 1, 1));
    IndirectDrawBuffer.Store(4, BufferCounter.Load(0));
    BufferCounter.Store3(0, uint3(0, 0, SubdDataCount));
}

struct VSIn
{
    uint VertexId : SV_VertexID;
    uint InstanceId : SV_InstanceID;
};

struct VSOut
{
    float4 PosH : SV_Position;
    uint InstanceId : TEXCOORD0;
    float2 Texc : TEXCOORD1;
};

VSOut RenderKernelVS(VSIn VsIn)
{
    VSOut ret;
    uint PrimitiveIndex = SubdIn[VsIn.InstanceId].PrimitiveIndex;
    float4 InVertices[3] =
    {
        VertexBuffer[IndexBuffer[PrimitiveIndex * 3]],
        VertexBuffer[IndexBuffer[PrimitiveIndex * 3 + 1]],
        VertexBuffer[IndexBuffer[PrimitiveIndex * 3 + 2]]
    };

    uint SubdBinaryKey = SubdIn[VsIn.InstanceId].SubdBinaryKey;
    float4 OutVertives[3];
    Subd(SubdBinaryKey, InVertices, OutVertives);
    float4 FinalVertex = Berp(OutVertives, SubdInstanced[VsIn.VertexId].BerpUV);

#ifdef DISPLACE
    FinalVertex.z += HeightMapTexture.SampleLevel(HeightMapSampler,FinalVertex.xy * 0.5f + 0.5f,0).x * RDisplacementFactor;
#endif

    ret.PosH = mul(FinalVertex, gScene.camera.viewProjMat);
    ret.InstanceId = VsIn.InstanceId;
#ifdef SHADING_LOD
    ret.Texc = intValToColor2(firstbithigh(SubdBinaryKey));
#else
    ret.Texc = FinalVertex.xy * 0.5f + 0.5f;
#endif
    return ret;
}

float4 RenderKernelPS(VSOut PSIn) : SV_Target
{
    float4 PSColor = float4(1,0,0,1);
    
#ifdef SHADING_LOD
    PSColor = float4(PSIn.Texc, 0.0f, 1.0f);
#endif
    
#ifdef SHADING_DIFFUSE
    float3 Normal = normalize(float3(-(SlopeMapTexture.SampleLevel(SlopeMapSampler,PSIn.Texc,0).xy) * RDisplacementFactor,1.0f));
    float d = clamp(Normal.z,0.0f,1.0f) / 3.1415926f;
    PSColor = float4(d,d,d,1.0f);
#endif
    
#ifdef SHADING_NORMAL
    float3 Normal = normalize(float3(-(SlopeMapTexture.SampleLevel(SlopeMapSampler,PSIn.Texc,0).xy) * RDisplacementFactor,1.0f));
    PSColor = float4(abs(Normal), 1.0f);
#endif

    return PSColor;
}

import Scene;

struct PrimitiveData
{
    uint PrimitiveIndex;
    uint SubdBinaryKey;
};

struct InstancedData
{
    float2 BerpUV;
};

struct FrustumPlane
{
    float3 Normal;
    float Intercept;
};

RWStructuredBuffer<PrimitiveData> SubdIn;
RWStructuredBuffer<PrimitiveData> SubdOut;
RWStructuredBuffer<PrimitiveData> SubdCulledOut;

Buffer<float4> VertexBuffer;
Buffer<uint> IndexBuffer;

RWByteAddressBuffer IndirectDrawBuffer;
RWByteAddressBuffer IndirectDispatchBuffer;
RWByteAddressBuffer BufferCounter;

StructuredBuffer<InstancedData> SubdInstanced;
Texture2D HeightMapTexture;
SamplerState HeightMapSampler;
Texture2D SlopeMapTexture;
SamplerState SlopeMapSampler;

cbuffer LodKernelCB
{
    float FovX;
    float TargetPixelSize;
    uint ScreenResolutionWidth;
    float LDisplacementFactor;
};

cbuffer RenderKernelCB
{
    float RDisplacementFactor;
};

static float4x4 Identitymatrix4x4 =
{
    float4(1.0f, 0.0f, 0.0f, 0.0f),
    float4(0.0f, 1.0f, 0.0f, 0.0f),
    float4(0.0f, 0.0f, 1.0f, 0.0f),
    float4(0.0f, 0.0f, 0.0f, 1.0f)
};

void GetChildrenKey(uint inSubdBinaryKey, out uint outChildrenKey[2])
{
    outChildrenKey[0] = (inSubdBinaryKey << 1u) | 0u;
    outChildrenKey[1] = (inSubdBinaryKey << 1u) | 1u;
}

uint GetParentKey(uint inSubdBinaryKey)
{
    return (inSubdBinaryKey >> 1u);
}

bool IsLeafKey(uint inSubdBinaryKey)
{
    return firstbithigh(inSubdBinaryKey) == 31;
}

bool IsRootKey(uint inSubdBinaryKey)
{
    return (inSubdBinaryKey == 1u);
}

bool IsChildZeroKey(uint inSubdBinaryKey)
{
    return (inSubdBinaryKey & 1u) == 0u;
}

float4x4 BitToTransform(uint inSubdBinaryBit)
{
    float DiffValue = float(inSubdBinaryBit) - 0.5f;
    float4x4 Mat =
    {
        float4(DiffValue, -0.5f,      0.0f, 0.0f),
        float4(-0.5f,     -DiffValue, 0.0f, 0.0f),
        float4(0.5f,      0.5f,       1.0f, 0.0f),
        float4(0.0f,      0.0f,       0.0f, 1.0f)
    };
    return Mat;
}

float4x4 KeyToTransform(uint inSubdBinaryKey)
{
    float4x4 Mat = Identitymatrix4x4;
    while (inSubdBinaryKey > 1u)
    {
        Mat = mul(Mat, BitToTransform(inSubdBinaryKey & 1u));
        inSubdBinaryKey = inSubdBinaryKey >> 1u;
    }
        
    return Mat;
}

float4x4 KeyToTransform(uint inSubdBinaryKey,out float4x4 ParentMatrix)
{
    ParentMatrix = KeyToTransform(GetParentKey(inSubdBinaryKey));
    return KeyToTransform(inSubdBinaryKey);
}

float3 GetProjectionPlaneVertex(float4 inTriangleVertice0, float4 inTriangleVertice1, float3 inNormal)
{
    return inTriangleVertice0.xyz - dot((inTriangleVertice0.xyz - inTriangleVertice1.xyz), inNormal) * inNormal;
}

float4 Berp(float4 inVertice[3], float2 inUV)
{
    float4 Result;
    float4 LinearPos = float4(inVertice[0].xyz + inUV.x * (inVertice[1] - inVertice[0]).xyz + inUV.y * (inVertice[2] - inVertice[0]).xyz, 1.0f);
    Result = LinearPos;
#ifdef PHONG_TESSELLATION
    float u = inUV.x;
    float v = inUV.y;
    float w = 1-inUV.x-inUV.y;
    float3 Normal = normalize(float3(-(SlopeMapTexture.SampleLevel(SlopeMapSampler, LinearPos.xy * 0.5f + 0.5f, 0).xy) * RDisplacementFactor, 1.0f));
    float4 PhongTessPos = float4(pow(u, 2) * inVertice[1].xyz + pow(v, 2) * inVertice[2].xyz + pow(w, 2) * inVertice[0].xyz
        + u * v * (GetProjectionPlaneVertex(inVertice[2], inVertice[1], Normal) + GetProjectionPlaneVertex(inVertice[1], inVertice[2], Normal))
        + v * w * (GetProjectionPlaneVertex(inVertice[2], inVertice[0], Normal) + GetProjectionPlaneVertex(inVertice[0], inVertice[2], Normal))
        + w * u * (GetProjectionPlaneVertex(inVertice[1], inVertice[0], Normal) + GetProjectionPlaneVertex(inVertice[0], inVertice[1], Normal)), 1.0f);
    Result = PhongTessPos;
#endif
    return Result;

}

void Subd(uint inSubdBinaryKey, float4 inVertices[3], out float4 outVertices[3])
{
    float4x4 ExtractionMat =
    {
        float4(0.0f, 0.0f, 1.0f, 0.0f),
        float4(1.0f, 0.0f, 1.0f, 0.0f),
        float4(0.0f, 1.0f, 1.0f, 0.0f),
        float4(0.0f, 0.0f, 0.0f, 1.0f)
    };
    
    float4x4 Transform = KeyToTransform(inSubdBinaryKey);
    Transform = mul(ExtractionMat, Transform);

    outVertices[0] = Berp(inVertices, Transform[0].xy);
    outVertices[1] = Berp(inVertices, Transform[1].xy);
    outVertices[2] = Berp(inVertices, Transform[2].xy);
}

void Subd(uint inSubdBinaryKey, float4 inVertices[3], out float4 outVertices[3], out float4 outParentVertices[3])
{
    float4x4 ParentMatrix = Identitymatrix4x4;
    float4x4 Transform = KeyToTransform(inSubdBinaryKey, ParentMatrix);
    float2 UV0 = mul(float4(0.0f, 0.0f, 1.0f, 0.0f), Transform).xy;
    float2 UV1 = mul(float4(1.0f, 0.0f, 1.0f, 0.0f), Transform).xy;
    float2 UV2 = mul(float4(0.0f, 1.0f, 1.0f, 0.0f), Transform).xy;

    outVertices[0] = Berp(inVertices, UV0);
    outVertices[1] = Berp(inVertices, UV1);
    outVertices[2] = Berp(inVertices, UV2);

    UV0 = mul(float4(0.0f, 0.0f, 1.0f, 0.0f), ParentMatrix).xy;
    UV1 = mul(float4(1.0f, 0.0f, 1.0f, 0.0f), ParentMatrix).xy;
    UV2 = mul(float4(0.0f, 1.0f, 1.0f, 0.0f), ParentMatrix).xy;

    outParentVertices[0] = Berp(inVertices, UV0);
    outParentVertices[1] = Berp(inVertices, UV1);
    outParentVertices[2] = Berp(inVertices, UV2);
}

float DistanceToLod(float inDistance)
{
    float ImagePlaneSize = 2 * inDistance * tan(FovX / 2) * TargetPixelSize / ScreenResolutionWidth;
    return -log2(clamp(ImagePlaneSize, 0.0f, 1.0f));
}

float ComputeLod(float4 inVertices[3])
{
    float MiddlePointToCamera = distance((inVertices[1] + inVertices[2]) / 2.0f, float4(gScene.camera.posW,1.0f));
    return DistanceToLod(MiddlePointToCamera);
}

void WriteKeyToSubdBuffer(uint inPrimitiveIndex,uint inSubdBinaryKey)
{
    uint OriginValue = 0;
    BufferCounter.InterlockedAdd(4, 1u, OriginValue);
    PrimitiveData Data = { inPrimitiveIndex, inSubdBinaryKey };
    SubdOut[OriginValue] = Data;
}

void UpdateSubdBuffer(uint inSubdBinaryKey, int inTargetLod, int inParentLod, uint inPrimitiveIndex)
{
    int KeyLod = firstbithigh(inSubdBinaryKey);
    if (KeyLod < inTargetLod && !IsLeafKey(inSubdBinaryKey))
    {
        uint ChildrenKey[2];
        GetChildrenKey(inSubdBinaryKey, ChildrenKey);
        WriteKeyToSubdBuffer(inPrimitiveIndex, ChildrenKey[0]);
        WriteKeyToSubdBuffer(inPrimitiveIndex, ChildrenKey[1]);
    }
    else if (KeyLod < (inParentLod+1))
    {
        WriteKeyToSubdBuffer(inPrimitiveIndex, inSubdBinaryKey);
    }
    else
    {
        if (IsRootKey(inSubdBinaryKey))
        {
            WriteKeyToSubdBuffer(inPrimitiveIndex, inSubdBinaryKey);
        }
        else if(IsChildZeroKey(inSubdBinaryKey))
        {
            WriteKeyToSubdBuffer(inPrimitiveIndex, GetParentKey(inSubdBinaryKey));
        }
    }
}

float2 intValToColor2(int keyLod)
{
    keyLod = keyLod % 64;

    int bx = (keyLod & 0x1) | ((keyLod >> 1) & 0x2) | ((keyLod >> 2) & 0x4);
    int by = ((keyLod >> 1) & 0x1) | ((keyLod >> 2) & 0x2) | ((keyLod >> 3) & 0x4);

    return float2(float(bx) / 7.0f, float(by) / 7.0f);
}

void GetFrustumPlane(float4x4 inModelViewProjection,out FrustumPlane outFrustumPlane[6])
{
    float NormalizedNormal = 0.0f;
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            outFrustumPlane[i * 2 + j].Normal.x = (i == 2 && j == 0 ? 0 : inModelViewProjection[0][3]) + (j == 0 ? inModelViewProjection[0][i] : -inModelViewProjection[0][i]);
            outFrustumPlane[i * 2 + j].Normal.y = (i == 2 && j == 0 ? 0 : inModelViewProjection[1][3]) + (j == 0 ? inModelViewProjection[1][i] : -inModelViewProjection[1][i]);
            outFrustumPlane[i * 2 + j].Normal.z = (i == 2 && j == 0 ? 0 : inModelViewProjection[2][3]) + (j == 0 ? inModelViewProjection[2][i] : -inModelViewProjection[2][i]);
            outFrustumPlane[i * 2 + j].Intercept = (i == 2 && j == 0 ? 0 : inModelViewProjection[3][3]) + (j == 0 ? inModelViewProjection[3][i] : -inModelViewProjection[3][i]);
            NormalizedNormal = length(outFrustumPlane[i * 2 + j].Normal);
            outFrustumPlane[i * 2 + j].Normal /= NormalizedNormal;
            outFrustumPlane[i * 2 + j].Intercept /= NormalizedNormal;
        }
    }
}

bool FrustumCullingTest(float4x4 inModelViewProjection,float4 MaxPosition,float4 MinPosition)
{
    float Result = 0.0f;
    FrustumPlane mFrustumPlane[6];
    GetFrustumPlane(inModelViewProjection, mFrustumPlane);
    for (int i = 0; i < 6 && Result >= 0.0f; i++)
    {
        float4 CompareResult = step(float4(0.0f, 0.0f, 0.0f, 0.0f), float4(mFrustumPlane[i].Normal, 0.0f));
        float3 NegativePos = lerp(MaxPosition, MinPosition, CompareResult).xyz;
        Result = dot(float4(mFrustumPlane[i].Normal, mFrustumPlane[i].Intercept), float4(NegativePos, 1.0f));
    }
    return (Result >= 0);
}

struct AddDataCB
{
    uint2 data;
};

StructuredBuffer<AddDataCB> gInData;
AppendStructuredBuffer<AddDataCB> gOutData;
//RWStructuredBuffer<AddDataCB> gOutData;

[numthreads(1,1,1)]
void main()
{
    uint numData = 0;
    uint stride;
    gInData.GetDimensions(numData,stride);

    AddDataCB Temp;
    
    for (uint i = 0; i < numData; i += 2)
    {
        Temp.data = gInData[i].data + gInData[i + 1].data;
        gOutData.Append(Temp);
    }
}

Texture2D gInput;
RWTexture2D<float4> gOutput;

groupshared float4 color[16][16];
groupshared float4 pixelColor;

[numthreads(16,16,1)]
void PixelateImage(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint2 StartPos = groupId.xy * 16;
    uint2 PixelPos = StartPos + groupThreadId.xy;
    color[groupThreadId.x][groupThreadId.y] = gInput[PixelPos];

    #ifdef _PIXELATE
    GroupMemoryBarrierWithGroupSync();
    pixelColor = 0;
    for(int i = 0;i < 16;i++){
        for(int j = 0;j < 16;j++){
            pixelColor += color[i][j];
        }
    }
    pixelColor/=16*16;
    GroupMemoryBarrierWithGroupSync();
    gOutput[PixelPos] = pixelColor.bgra;
    #else
    gOutput[PixelPos] = color[groupThreadId.x][groupThreadId.y].bgra;
    #endif
}

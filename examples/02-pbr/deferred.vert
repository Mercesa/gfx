float4 main(in uint idx : SV_VertexID) : SV_POSITION
{
    return 1.0f - float4(4.0f * (idx & 1), 4.0f * (idx >> 1), 1.0f, 0.0f);
}

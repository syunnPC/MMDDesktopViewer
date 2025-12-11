struct PSIn
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};
float4 PSMain(PSIn i) : SV_TARGET
{
    float4 c = i.color;
    return float4(c.rgb * c.a, c.a);
}
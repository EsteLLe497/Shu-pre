#include "common.hlsl"

void main(in VS_IN In, out UNLIT_PS_IN Out)
{
    matrix wvp = mul(mul(World, View), Projection);
    Out.Position = mul(In.Position, wvp);
    Out.TexCoord = In.TexCoord;
    Out.Diffuse = In.Diffuse * Material.Diffuse;
}


#include "common.hlsl"


void main(in VS_IN In, out PS_IN Out)
{
	Out.Position = mul(In.Position, World);
    Out.Position += In.InstanceID;
	
    Out.Position = mul(In.Position, View);
    Out.Position = mul(In.Position, Projection);
	
	Out.TexCoord = In.TexCoord;
	Out.Diffuse = In.Diffuse * Material.Diffuse;
}


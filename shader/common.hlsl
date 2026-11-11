


cbuffer WorldBuffer : register(b0)
{
	matrix World;
}
cbuffer ViewBuffer : register(b1)
{
	matrix View;
}
cbuffer ProjectionBuffer : register(b2)
{
	matrix Projection;
}

cbuffer CameraBuffer : register(b5)
{
	float4 CameraPosition;
}




struct MATERIAL
{
	float4 Ambient;
	float4 Diffuse;
	float4 Specular;
	float4 Emission;
	float Shininess;
	bool TextureEnable;
	float AlphaCutoff;
	bool AlphaBlend;
};

cbuffer MaterialBuffer : register(b3)
{
	MATERIAL Material;
}




struct LIGHT
{
	bool Enable;
	bool3 Dummy;
	float4 Direction;
	float4 Diffuse;
	float4 Ambient;
};

cbuffer LightBuffer : register(b4)
{
	LIGHT Light;
}





struct VS_IN
{
	float4 Position		: POSITION0;
	float4 Normal		: NORMAL0;
	float4 Diffuse		: COLOR0;
	float2 TexCoord		: TEXCOORD0;
    uint InstanceID     : SV_InstanceID;
};


struct PS_IN
{
	float4 Position		: SV_POSITION;
	float4 WorldPosition: POSITION0;
    float4 Normal		: NORMAL0;
	float4 Diffuse		: COLOR0;
	float2 TexCoord		: TEXCOORD0;
};
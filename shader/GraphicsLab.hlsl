#include "common.hlsl"
#include "LabLighting.hlsli"

cbuffer LabMaterial : register(b13)
{
    float4 Surface;
    float4 SurfaceMaps;
};

Texture2D<float4> LabImage : register(t11);
Texture2D BaseTexture : register(t0);
Texture2D LandscapeAlbedo : register(t12);
Texture2D LandscapeNormal : register(t13);
Texture2D LandscapeRoughness : register(t14);
Texture2D<float> GroveSceneDepth : register(t15);
Texture2D<float4> GroveScatter : register(t16);

static const int GroundSurface = 0;
static const int WoodSurface = 3;
static const int RoofSurface = 5;
static const int BarkSurface = 6;
static const int LeafSurface = 7;
static const int MountainSurface = 8;
static const int BallastSurface = 9;
static const int LensSurface = 10;
static const int StationSignSurface = 11;
static const int KeyakiBarkSurface = 12;
static const int SugiBarkSurface = 13;
SamplerState BaseSampler : register(s0);

struct ScreenVertex
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

ScreenVertex screenVS(uint id : SV_VertexID)
{
    ScreenVertex output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 1, 1);
    return output;
}

PS_IN surfaceVS(VS_IN input)
{
    PS_IN output;
    output.WorldPosition = mul(input.Position, World);
    output.Position = mul(mul(output.WorldPosition, View), Projection);
    output.Normal = float4(mul(input.Normal.xyz, (float3x3)World), 0);
    output.TexCoord = input.TexCoord;
    output.Diffuse = input.Diffuse;
    return output;
}

void shadowPS(PS_IN input)
{
    // 人物の影にも本描画と同じアルファしきい値を使う。
    if (Material.TextureEnable)
        clip(BaseTexture.Sample(BaseSampler, input.TexCoord).a * Material.Diffuse.a - Material.AlphaCutoff);
}

void leafShadowPS(PS_IN input)
{
    clip(LandscapeAlbedo.Sample(BaseSampler, input.TexCoord).a - SurfaceMaps.z);
}

float4 skyPS(ScreenVertex input) : SV_TARGET
{
    float2 ndc = input.uv * float2(2, -2) + float2(-1, 1);
    float4 farPoint = mul(float4(ndc, 1, 1), LabInverseViewProjection);
    float3 ray = normalize(farPoint.xyz / farPoint.w - CameraPosition.xyz);
    return float4(labSky(ray), 1);
}

float roadCenter(float z)
{
    return 0.34 + 10.0 * smoothstep(17.0, 63.0, z);
}

float gravelCoverage(float2 p)
{
    float wheelRoad = abs(p.x - roadCenter(p.y));
    float irregularEdge = labNoise(float2(p.y * 1.5, p.x * 2.6)) * 0.34;
    return (1.0 - smoothstep(2.13, 2.79, wheelRoad + irregularEdge)) * (1.0 - smoothstep(72.0,91.0,p.y));
}

float3 terrainAlbedo(float3 position, float3 normal)
{
    float2 p = position.xz;
    float patches = labNoise(p * 0.29) * 0.64 + labNoise(p * 2.1) * 0.36;
    float road = gravelCoverage(p);
    float soil = max((1.0 - smoothstep(2.78, 3.13, abs(p.x - roadCenter(p.y)))) * (1.0 - smoothstep(72.0,91.0,p.y)), smoothstep(0.18, 0.5, 1.0 - normal.y));
    float3 meadow = lerp(float3(0.073, 0.11, 0.035), float3(0.15, 0.23, 0.065), patches);
    // 木の下だけ土や落ち葉の色がのぞく。林縁を均一な芝生に見せないための色の変化。
    float grove = 1.0 - smoothstep(0.61,1.13,length((p-float2(16.1,22.8)) / float2(11.4,13.3)));
    float litter = grove * smoothstep(0.24,0.64,patches) * 0.74;
    float fragments = labNoise(p * 33.0);
    meadow = lerp(meadow, lerp(float3(0.065,0.050,0.026),float3(0.143,0.101,0.036),fragments), litter);
    float3 shoulder = float3(0.21, 0.17, 0.103) * (0.78 + patches * 0.46);
    return lerp(lerp(meadow, shoulder, soil), float3(0.22, 0.20, 0.17), road);
}

float2 labHash2(float2 p)
{
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}

// 格子ごとにずらした点を樹冠の中心とし、最も近い中心からの距離で丸い樹冠を作る。
// x: 樹冠の高さ(0〜1), y: どの木かを表す乱数。
float2 crownDomes(float2 x)
{
    const float2 cell = floor(x), f = frac(x);
    float nearest = 9.0, identity = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int i = -1; i <= 1; ++i)
        {
            const float2 offset = float2(i, y);
            const float2 jitter = labHash2(cell + offset);
            const float2 center = offset + jitter * 0.8 + 0.1;
            const float distance = dot(center - f, center - f);
            if (distance < nearest)
            {
                nearest = distance;
                identity = frac(jitter.x * 17.0 + jitter.y * 31.0);
            }
        }
    return float2(sqrt(saturate(1.0 - nearest * 2.3)), identity);
}

// 山を覆う森の樹冠の高さ(0〜1)。数メートルの樹冠の上に小さな枝葉の房を重ねる。
float canopyHeight(float2 q)
{
    return crownDomes(q / 2.7).x * 0.70 + crownDomes(q / 1.1 + 13.1).x * 0.30;
}

void applyMeadowDetail(float3 p, inout float3 albedo, inout float3 n)
{
    const float2 q = p.xz;
    const float weight = 1.0 - gravelCoverage(q);
    // 細かい模様は画面上で潰れる距離から消し、遠くでちらつかないようにする。
    const float footprint = length(fwidth(q));
    const float fineFade = saturate(1.6 - footprint * 17.0 * 1.4);
    const float midFade = saturate(1.6 - footprint * 6.1 * 1.4);

    // 草地のむら。株ごとの濃淡と、乾いて黄ばんだ所を重ねる。
    const float fine = lerp(0.5, labNoise(q * 6.1), midFade) * 0.75 + lerp(0.5, labNoise(q * 17.0), fineFade) * 0.25;
    const float dry = smoothstep(0.55, 0.82, labNoise(q * 0.13 + 5.3));
    // 草の葉の下に見える地面は、葉の陰で葉そのものより暗い。
    float3 detailed = albedo * lerp(0.58, 0.92, fine);
    detailed = lerp(detailed, float3(0.15, 0.135, 0.06) * lerp(0.75, 1.05, fine), dry * 0.3);
    albedo = lerp(albedo, detailed, weight);

    // 株の凹凸として法線をわずかに揺らす。強くすると日なたで砂利のように見える。
    const float e = 0.06;
    const float gx = labNoise((q + float2(e, 0)) * 6.1) - labNoise((q - float2(e, 0)) * 6.1);
    const float gz = labNoise((q + float2(0, e)) * 6.1) - labNoise((q - float2(0, e)) * 6.1);
    n = normalize(n + float3(-gx, 0, -gz) * 0.18 * weight * midFade);
}

float2 materialUv(float3 p, float3 n, int kind, out float3 tangent, out float3 across)
{
    float3 axis = abs(n);
    float2 uv;
    if (axis.y > max(axis.x, axis.z))
    {
        tangent = float3(1, 0, 0);
        across = float3(0, 0, 1);
        uv = p.xz;
    }
    else if (axis.x > axis.z)
    {
        tangent = float3(0, 0, 1);
        across = float3(0, 1, 0);
        uv = p.zy;
    }
    else
    {
        tangent = float3(1, 0, 0);
        across = float3(0, 1, 0);
        uv = p.xy;
    }

    if (kind == GroundSurface) return uv * 0.64;
    if (kind == BallastSurface) return uv * 0.84;
    if (kind == WoodSurface) return uv * float2(0.93, 0.57);
    if (kind == RoofSurface) return uv * float2(0.87, 0.64);
    return uv * 0.65;
}

void applyLandscapeMaps(PS_IN input, int kind, inout float3 albedo, inout float3 n, inout float roughness)
{
    if (kind == StationSignSurface)
    {
        albedo = LandscapeAlbedo.Sample(BaseSampler, input.TexCoord).rgb;
        roughness = 0.72;
        return;
    }

    float3 tangent, across;
    float2 uv = materialUv(input.WorldPosition.xyz, n, kind, tangent, across);
    float3 sampled = LandscapeAlbedo.Sample(BaseSampler, uv).rgb;
    float3 normalMap = LandscapeNormal.Sample(BaseSampler, uv).xyz * 2.0 - 1.0;
    float physicalRoughness = LandscapeRoughness.Sample(BaseSampler, uv).r;
    float coverage = kind == GroundSurface ? gravelCoverage(input.WorldPosition.xz) : 1.0;

    // 砂利の中央は車輪で締まった帯、路肩は粒の粗い土が残る。
    if (kind == GroundSurface)
    {
        float x = input.WorldPosition.x - roadCenter(input.WorldPosition.z);
        float track = 1.0 - smoothstep(0.12, 0.46, abs(abs(x)-0.93));
        float value = dot(sampled,float3(0.2126,0.7152,0.0722));
        sampled = lerp(sampled, value.xxx, 0.42) * lerp(1.39,1.24,track);
    }
    else if (kind == BallastSurface)
        sampled *= 0.80;
    else
        sampled *= lerp(0.47, 1.18, saturate(albedo * 1.95));

    albedo = lerp(albedo, sampled, coverage);
    tangent = normalize(tangent - n * dot(tangent,n));
    float3 bitangent = across - n * dot(across,n) - tangent * dot(across,tangent);
    across = dot(bitangent,bitangent) > 0.000001 ? normalize(bitangent) : cross(n,tangent);
    const float bump = (kind == GroundSurface || kind == BallastSurface) ? 0.88 : 0.67;
    n = normalize(n * max(normalMap.z, 0.45) + (tangent*normalMap.x + across*normalMap.y) * (bump*coverage));
    roughness = lerp(roughness, saturate(physicalRoughness * (0.65 + roughness * 0.70)), coverage * 0.83);
}

void applyBarkMaps(PS_IN input, int kind, inout float3 albedo, inout float3 n, inout float roughness, inout float cavity)
{
    const float repeatHeight = kind == KeyakiBarkSurface ? 0.45 : (kind == SugiBarkSurface ? 0.32 : 0.42);
    const float2 uv = input.TexCoord * float2(1,repeatHeight);
    albedo = LandscapeAlbedo.Sample(BaseSampler,uv).rgb;
    const float4 relief = LandscapeNormal.Sample(BaseSampler,uv);
    cavity = relief.b;
    roughness = lerp(relief.a,roughness,0.24);

    // 曲がった枝でも溝の向きと光の当たり方が揃うよう、UV の微分から接線を求める。
    const float2 uvX = ddx(uv), uvY = ddy(uv);
    const float determinant = uvX.x*uvY.y-uvX.y*uvY.x;
    const float3 x = ddx(input.WorldPosition.xyz), y = ddy(input.WorldPosition.xyz);
    if (abs(determinant) > 0.00000001)
    {
        float3 tangent = (x*uvY.y-y*uvX.y)*sign(determinant);
        float3 along = (y*uvX.x-x*uvY.x)*sign(determinant);
        tangent = normalize(tangent-n*dot(tangent,n));
        along = normalize(along-n*dot(along,n)-tangent*dot(along,tangent));
        const float2 slopes = (relief.rg*2.0-1.0) * (kind == KeyakiBarkSurface ? 0.52 : 0.76);
        n = normalize(n*sqrt(saturate(1.0-dot(slopes,slopes))) + tangent*slopes.x + along*slopes.y);
    }
}

float3 treeLeafLight(float3 albedo, float3 n, float3 l, float3 v, float shadow, float skyVisibility, float thickness)
{
    // 葉の房の内側は環境光を抑え、逆光では太陽に面した薄い葉だけを暖かく透かす。
    float sky = saturate(skyVisibility);
    float3 ambient = albedo * labAmbient(n) * lerp(0.57, 1.11, sky) * lerp(0.80,1.0,shadow);
    float wrappedLight = saturate((dot(n, l) + 0.16) / 1.16);
    float facingLight = saturate(abs(dot(n,l))*0.78+0.22);
    float forward = pow(saturate(dot(-l,v)*0.78+0.28),2.1);
    float oppositeSides = saturate(-dot(n,l)*dot(n,v)*2.6);
    float transmission = facingLight*(forward*0.66+oppositeSides*0.34)*GroveLight.y;
    float specular = pow(saturate(dot(n,normalize(l+v))),20.0)*wrappedLight;
    float3 direct = albedo*wrappedLight / 3.141593 + 0.010*specular;
    float thinness = lerp(0.60,1.20,1.0-thickness);
    float3 transmitted = albedo*float3(1.62,1.36,0.26)*transmission*thinness*lerp(0.06,0.23,sky);
    return ambient + SunColor.rgb*SunColor.a*(direct*shadow + transmitted*lerp(shadow,1.0,0.10));
}

float4 surfacePS(PS_IN input) : SV_TARGET
{
    float3 n = normalize(input.Normal.xyz);
    const float3 geometricNormal = n;
    float3 p = input.WorldPosition.xyz;
    int kind = (int)Surface.z;
    float3 albedo = kind == GroundSurface ? terrainAlbedo(p, n) : input.Diffuse.rgb;
    float materialRoughness = Surface.x;
    float barkCavity = 1.0;
    const bool treeBark = kind == BarkSurface || kind == KeyakiBarkSurface || kind == SugiBarkSurface;
    const bool treeLeaves = SurfaceMaps.y > 0.5;
    if (treeLeaves)
    {
        const float4 leaf = LandscapeAlbedo.Sample(BaseSampler, input.TexCoord);
        clip(leaf.a - SurfaceMaps.z);
        const float3 l = SunDirection.xyz;
        const float3 v = normalize(CameraPosition.xyz - p);
        const float3 lit = treeLeafLight(albedo*(0.55+leaf.r*0.58),n,l,v,labShadow(p+l*0.045),input.Diffuse.a,leaf.r);
        return float4(labFog(lit,p), 1);
    }
    if (SurfaceMaps.x > 0.5 && treeBark)
        applyBarkMaps(input,kind,albedo,n,materialRoughness,barkCavity);
    else if (SurfaceMaps.x > 0.5)
        applyLandscapeMaps(input,kind,albedo,n,materialRoughness);
    float canopyCavity = 1.0;
    if (kind == MountainSurface)
    {
        const float2 q = p.xz;
        // 手前の里山には木が植わっているので、山肌は落ち葉と下草の暗い林床にする。
        // それより奥の稜線は木を置かないため、細かな樹冠の凹凸と色むらで森に見せる。
        const float distance = length(CameraPosition.xz - q);
        const float distantCanopy = smoothstep(70.0, 110.0, distance);
        const float e = 0.45, amplitude = lerp(0.5, 2.4, distantCanopy);
        const float h = canopyHeight(q);
        const float hx = canopyHeight(q + float2(e, 0)) - canopyHeight(q - float2(e, 0));
        const float hz = canopyHeight(q + float2(0, e)) - canopyHeight(q - float2(0, e));
        n = normalize(n + float3(-hx, 0, -hz) * (amplitude / (2.0 * e)));
        const float tree = crownDomes(q / 2.7).y;
        const float cedarStand = smoothstep(0.35, 0.65, labNoise(q * 0.045 + 11.0));
        const bool cedarTree = tree < lerp(0.15, 0.85, cedarStand);
        const float tone = lerp(0.5, frac(tree * 7.13), 0.5 * distantCanopy);
        const float3 broadleaf = lerp(float3(0.040, 0.064, 0.022), float3(0.082, 0.108, 0.036), tone);
        const float3 cedar = lerp(float3(0.026, 0.042, 0.022), float3(0.046, 0.062, 0.031), tone);
        const float3 canopy = cedarTree ? cedar : broadleaf;
        const float3 floorColor = lerp(float3(0.040, 0.038, 0.022), float3(0.060, 0.072, 0.030), labNoise(q * 0.7));
        albedo = lerp(floorColor, canopy, distantCanopy) * lerp(0.85, 1.12, labNoise(q * 0.9));
        canopyCavity = lerp(lerp(0.65, 1.0, h), lerp(0.22, 1.0, smoothstep(0.04, 0.55, h)), distantCanopy);
        albedo *= canopyCavity;
    }
    if (kind == GroundSurface)
        applyMeadowDetail(p, albedo, n);
    if (kind == LeafSurface)
    {
        // 草は根元ほど周りの葉に囲まれて暗い。頂点の a に根元0から先端1の高さが入っている。
        albedo *= lerp(0.55, 1.15, input.Diffuse.a);
    }
    float3 l = SunDirection.xyz;
    float3 v = normalize(CameraPosition.xyz - p);
    float3 h = normalize(v + l);
    float nl = saturate(dot(n, l));
    float nv = max(saturate(dot(n, v)), 0.001);
    // 凹凸法線を使って影の座標をずらすと、明暗の縞が幹に出る。面の法線で小さく補正する。
    const float receiverOffset = treeBark ? lerp(0.012,0.026,1.0-saturate(dot(geometricNormal,l))) : 0.012;
    const float shadow = labShadow(p+geometricNormal*receiverOffset);
    float3 ambient = labAmbient(n) * canopyCavity;
    if (treeBark)
    {
        ambient = labAmbient(geometricNormal) * barkCavity * input.Diffuse.a;
        nl *= saturate(dot(geometricNormal,l)*4.0+0.22) * lerp(0.72,1.0,barkCavity);
    }
    float3 color = albedo * ambient;

    if (Surface.w > 0.5)
    {
        // GGX 分布、Smith 可視項、Schlick Fresnel の直接光。
        float roughness = max(materialRoughness, 0.08);
        float a2 = pow(roughness, 4.0);
        float nh = saturate(dot(n, h));
        float denominator = nh * nh * (a2 - 1.0) + 1.0;
        float distribution = a2 / max(3.141593 * denominator * denominator, 0.00001);
        float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
        float visibility = (nl / max(nl * (1.0 - k) + k, 0.001)) * (nv / (nv * (1.0 - k) + k));
        float3 f0 = lerp(0.04.xxx, albedo, Surface.y);
        float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - saturate(dot(v, h)), 5.0);
        float3 diffuse = (1.0 - fresnel) * (1.0 - Surface.y) * albedo / 3.141593;
        float3 specular = distribution * visibility * fresnel / max(4.0 * nl * nv, 0.001);
        color = color * (1.0 - Surface.y) + (diffuse + specular) * SunColor.rgb * SunColor.a * nl * shadow;
        color += f0 * ambient * (1.0 - roughness) * 0.24;
    }
    else
    {
        color += albedo * SunColor.rgb * SunColor.a * (nl * shadow / 3.141593);
    }
    if (kind == LeafSurface)
    {
        // 葉を透る光を弱く加え、日なたと樹冠の内側に違いを出す。
        float transmit = saturate(dot(v, -l) * 0.56 + 0.31);
        color += albedo * SunColor.rgb * SunColor.a * transmit * 0.092 * lerp(0.34,1.0,shadow);
    }
    if (kind == LensSurface)
        color += albedo * 0.13;

    return float4(labFog(color, p), 1);
}

static const float GroveRange = 37.0;

float groveDepth(float depth)
{
    const float nearPlane = GroveLight.z, farPlane = GroveLight.w;
    return min(nearPlane*farPlane / max(farPlane-depth*(farPlane-nearPlane),0.0001),GroveRange) / GroveRange;
}

float groveDensity(float3 p)
{
    // 林縁と足元で霧を徐々に薄くし、開けた場所や梢より上まで光芒が続くのを防ぐ。
    const float2 q = p.xz;
    const float oak = 1.0-smoothstep(0.52,1.0,length((q-float2(17,23))/float2(15,16)));
    const float stationTree = 1.0-smoothstep(0.26,1.0,length((q-float2(-8,6))/float2(8,10)));
    const float cedar = 1.0-smoothstep(0.48,1.0,length((q-float2(-18,45))/float2(17,13)));
    const float height = smoothstep(-0.25,0.65,p.y)*(1.0-smoothstep(7.0,17.0,p.y));
    return max(oak,max(stationTree*0.58,cedar*0.69))*height;
}

float4 grovePS(ScreenVertex input) : SV_TARGET
{
    uint width, height;
    GroveSceneDepth.GetDimensions(width,height);
    const int2 fullPixel = min(int2(input.uv*float2(width,height)),int2(width,height)-1);
    const float depth = GroveSceneDepth.Load(int3(fullPixel,0));
    const float2 ndc = input.uv*float2(2,-2)+float2(-1,1);
    const float4 reconstructed = mul(float4(ndc,depth,1),LabInverseViewProjection);
    const float3 rayEnd = reconstructed.xyz/reconstructed.w-CameraPosition.xyz;
    const float rayLength = min(length(rayEnd),GroveRange);
    const float3 ray = normalize(rayEnd);

    // 視線上の空気のうち、枝葉のシャドウマップを通過した直射光だけを積算する。
    const int samples = 18;
    const float stepSize = rayLength/samples;
    const float jitter = frac(52.9829189*frac(dot(floor(input.position.xy),float2(0.06711056,0.00583715))));
    const float3 stepWorld = ray*stepSize;
    float3 p = CameraPosition.xyz+stepWorld*lerp(0.34,0.66,jitter);
    const float3 stepLight = mul(float4(stepWorld,0),SunViewProjection).xyz;
    float3 shadow = mul(float4(p,1),SunViewProjection).xyz;
    float litAir = 0.0;
    [loop] for (int i=0;i<samples;++i)
    {
        const float density = groveDensity(p);
        if (density > 0.004)
        {
            const float2 uv = shadow.xy*float2(0.5,-0.5)+0.5;
            const float visible = LabShadow.SampleCmpLevelZero(LabShadowSampler,uv,shadow.z-ShadowSettings.z);
            litAir += density*visible;
        }
        shadow += stepLight;
        p += stepWorld;
    }
    const float phase = 0.12+2.08*pow(saturate(dot(ray,SunDirection.xyz)),5.0);
    const float scattering = litAir*stepSize*phase*0.0095*GroveLight.x;
    return float4(SunColor.rgb*SunColor.a*scattering,groveDepth(depth));
}

float3 upsampleGroveLight(float2 uv, int2 pixel)
{
    uint width,height;
    GroveScatter.GetDimensions(width,height);
    const float2 samplePosition = uv*float2(width,height)-0.5;
    const int2 origin = int2(floor(samplePosition));
    const float2 f = frac(samplePosition);
    const float depth = groveDepth(GroveSceneDepth.Load(int3(pixel,0)));
    float3 result = 0.0;
    float total = 0.0;
    // 枝や人物の輪郭を越えて遠くの光芒をにじませないよう、深度の近い画素を使う。
    [unroll] for (int y=0;y<2;++y)
        [unroll] for (int x=0;x<2;++x)
        {
            const int2 index = clamp(origin+int2(x,y),int2(0,0),int2(width,height)-1);
            const float4 lit = GroveScatter.Load(int3(index,0));
            const float weight = (x ? f.x : 1.0-f.x)*(y ? f.y : 1.0-f.y)*exp2(-72.0*abs(lit.a-depth));
            result += lit.rgb*weight;
            total += weight;
        }
    return result/max(total,0.025);
}

// ---- 画面効果 ----------------------------------------------------------------
// 遮蔽(AO)は半解像度で計算し、x に遮蔽率、y に視点からの距離を入れる。
Texture2D<float2> AoImage : register(t17);
Texture2D<float4> BloomImage : register(t18);
SamplerState PostSampler : register(s5);

static const float3 LumaWeights = float3(0.2126, 0.7152, 0.0722);

float viewDistance(float depth)
{
    const float nearPlane = GroveLight.z, farPlane = GroveLight.w;
    return nearPlane*farPlane / max(farPlane-depth*(farPlane-nearPlane),0.0001);
}

float3 worldAtPixel(int2 pixel, float2 size)
{
    const float depth = GroveSceneDepth.Load(int3(pixel,0));
    const float2 ndc = (pixel+0.5)/size*float2(2,-2)+float2(-1,1);
    const float4 world = mul(float4(ndc,depth,1),LabInverseViewProjection);
    return world.xyz/world.w;
}

float4 ssaoPS(ScreenVertex input) : SV_TARGET
{
    uint width, height;
    GroveSceneDepth.GetDimensions(width,height);
    const float2 size = float2(width,height);
    const int2 limit = int2(width,height)-1;
    const int2 halfPixel = int2(input.position.xy);
    const int2 pixel = min(halfPixel*2,limit);
    const float depth = GroveSceneDepth.Load(int3(pixel,0));
    if (depth >= 1.0) return float4(1,GroveLight.w,0,1);

    // 隣の画素のうち奥行きの近い側で法線を作り、枝や人物の輪郭で法線が乱れるのを防ぐ。
    const float3 p = worldAtPixel(pixel,size);
    const float3 left = worldAtPixel(max(pixel-int2(1,0),0),size), right = worldAtPixel(min(pixel+int2(1,0),limit),size);
    const float3 up = worldAtPixel(max(pixel-int2(0,1),0),size), down = worldAtPixel(min(pixel+int2(0,1),limit),size);
    const float3 dx = dot(right-p,right-p) < dot(p-left,p-left) ? right-p : p-left;
    const float3 dy = dot(down-p,down-p) < dot(p-up,p-up) ? down-p : p-up;
    float3 n = normalize(cross(dy,dx));
    const float3 toEye = CameraPosition.xyz-p;
    if (dot(n,toEye) < 0.0) n = -n;
    const float distance = viewDistance(depth);

    // 4x4 の格子で回転を変え、後段の 4x4 ぼかしで模様が消えるようにする。
    static const float pattern[16] = {0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    const float angle = (pattern[(halfPixel.y&3)*4+(halfPixel.x&3)]+0.5)/16.0*6.283185;
    const float3 helper = abs(n.y) < 0.95 ? float3(0,1,0) : float3(1,0,0);
    float3 tangent = normalize(cross(helper,n));
    float3 bitangent = cross(n,tangent);
    const float s = sin(angle), c = cos(angle);
    const float3 rotated = tangent*c+bitangent*s;
    bitangent = cross(n,rotated);
    tangent = rotated;

    const int samples = 14;
    const float radius = AoSettings.y;
    float occlusion = 0.0;
    [unroll] for (int i=0;i<samples;++i)
    {
        // 余弦分布の半球。近くの点を多めに置いて、根元や軒下の接地感を強める。
        const float t = (i+0.5)/samples;
        const float phi = i*2.399963;
        const float r = sqrt(t);
        const float3 local = float3(r*cos(phi),r*sin(phi),sqrt(1.0-t));
        const float reach = lerp(0.12,1.0,pow(frac(i*0.618034+0.31),2.0));
        const float3 q = p+n*0.02+(tangent*local.x+bitangent*local.y+n*local.z)*radius*reach;
        const float4 clip = mul(float4(q,1),LabViewProjection);
        const float2 uv = clip.xy/clip.w*float2(0.5,-0.5)+0.5;
        if (any(uv <= 0.0) || any(uv >= 1.0)) continue;
        const float sceneDistance = viewDistance(GroveSceneDepth.Load(int3(min(int2(uv*size),limit),0)));
        const float inFront = step(sceneDistance,clip.w-0.025*(1.0+clip.w*0.02));
        // 遠く離れた手前の物体(視点近くの葉など)は遮蔽に数えない。
        const float range = smoothstep(0.0,1.0,radius/max(abs(distance-sceneDistance),0.0001));
        occlusion += inFront*range;
    }
    float ao = 1.0-occlusion/samples;
    ao = lerp(ao,1.0,smoothstep(AoSettings.z*0.55,AoSettings.z,distance));
    return float4(ao,distance,0,1);
}

float4 aoBlurPS(ScreenVertex input) : SV_TARGET
{
    uint width, height;
    AoImage.GetDimensions(width,height);
    const int2 pixel = int2(input.position.xy);
    const float center = AoImage.Load(int3(pixel,0)).y;
    float total = 0.0, weightSum = 0.0;
    [unroll] for (int y=-2;y<2;++y)
        [unroll] for (int x=-2;x<2;++x)
        {
            const int2 index = clamp(pixel+int2(x,y),int2(0,0),int2(width,height)-1);
            const float2 value = AoImage.Load(int3(index,0));
            const float weight = exp2(-abs(value.y-center)/(center*0.035+0.02));
            total += value.x*weight;
            weightSum += weight;
        }
    return float4(total/max(weightSum,0.0001),center,0,1);
}

float sampleAo(float2 uv, int2 fullPixel)
{
    uint width,height;
    AoImage.GetDimensions(width,height);
    const float2 samplePosition = uv*float2(width,height)-0.5;
    const int2 origin = int2(floor(samplePosition));
    const float2 f = frac(samplePosition);
    const float distance = viewDistance(GroveSceneDepth.Load(int3(fullPixel,0)));
    float result = 0.0, total = 0.0;
    // 半解像度の遮蔽を、奥行きの揃った画素だけで拡大する。葉の輪郭に暗い縁が出ないようにする。
    [unroll] for (int y=0;y<2;++y)
        [unroll] for (int x=0;x<2;++x)
        {
            const int2 index = clamp(origin+int2(x,y),int2(0,0),int2(width,height)-1);
            const float2 value = AoImage.Load(int3(index,0));
            const float weight = (x ? f.x : 1.0-f.x)*(y ? f.y : 1.0-f.y)
                *exp2(-abs(value.y-distance)/(distance*0.03+0.015))+0.0001;
            result += value.x*weight;
            total += weight;
        }
    return result/total;
}

float4 bloomPrefilterPS(ScreenVertex input) : SV_TARGET
{
    uint width,height;
    LabImage.GetDimensions(width,height);
    const float2 texel = 1.0/float2(width,height);
    // 4 点の重み付き平均で、葉の隙間のきらめきが一点だけ大きく光るのを抑える。
    float3 sum = 0.0;
    float weightSum = 0.0;
    [unroll] for (int i=0;i<4;++i)
    {
        const float2 offset = float2(i&1 ? 1 : -1,i&2 ? 1 : -1)*texel;
        const float3 rgb = max(LabImage.SampleLevel(PostSampler,input.uv+offset,0).rgb,0.0)*exp2(PostSettings.x);
        const float weight = 1.0/(1.0+dot(rgb,LumaWeights));
        sum += rgb*weight;
        weightSum += weight;
    }
    const float3 rgb = sum/weightSum;
    const float brightness = max(rgb.r,max(rgb.g,rgb.b));
    const float knee = BloomSettings.z;
    float soft = clamp(brightness-BloomSettings.y+knee,0.0,2.0*knee);
    soft = soft*soft/(4.0*knee+0.0001);
    const float contribution = max(soft,brightness-BloomSettings.y)/max(brightness,0.0001);
    return float4(rgb*contribution,1);
}

float3 sampleBloom(float2 uv)
{
    uint width,height,levels;
    BloomImage.GetDimensions(0,width,height,levels);
    float3 result = 0.0;
    float total = 0.0;
    // 段階的に縮小した像を重ね、近くは鋭く遠くへ柔らかく広がる光にする。
    [unroll] for (int level=1;level<6;++level)
    {
        if (level >= (int)levels) break;
        const float2 texel = float2(1u<<level,1u<<level)/float2(width,height);
        float3 tent = BloomImage.SampleLevel(PostSampler,uv+texel*float2(-0.5,-0.5),level).rgb
            + BloomImage.SampleLevel(PostSampler,uv+texel*float2(0.5,-0.5),level).rgb
            + BloomImage.SampleLevel(PostSampler,uv+texel*float2(-0.5,0.5),level).rgb
            + BloomImage.SampleLevel(PostSampler,uv+texel*float2(0.5,0.5),level).rgb;
        const float weight = 0.6+level*0.25;
        result += tent*0.25*weight;
        total += weight;
    }
    return result/max(total,0.0001);
}

float3 gradeColor(float3 rgb, float2 uv)
{
    // 鮮やかすぎる緑を黄みのオリーブへ寄せる。日なたの葉ほど強く効かせる。
    const float greenExcess = saturate(rgb.g-max(rgb.r,rgb.b));
    rgb.r += greenExcess*GradeSettings.z*0.62;
    rgb.b = max(rgb.b-greenExcess*GradeSettings.z*0.18,0.0);

    // 暗部は青緑、明部は暖色へわずかに寄せ、日なたと日陰の差を色でも分ける。
    const float luma = dot(rgb,LumaWeights);
    const float3 shadowTint = float3(0.86,1.0,1.07), highlightTint = float3(1.07,1.0,0.88);
    const float3 tint = lerp(shadowTint,highlightTint,smoothstep(0.04,0.62,luma));
    rgb *= lerp(1.0.xxx,tint,GradeSettings.x);

    uint width,height;
    LabImage.GetDimensions(width,height);
    float2 centered = (uv-0.5)*float2(width/(float)height,1.0);
    const float vignette = smoothstep(0.42,1.15,length(centered)*1.25);
    rgb *= 1.0-GradeSettings.y*vignette;
    return saturate(rgb);
}

float4 postPS(ScreenVertex input) : SV_TARGET
{
    const int2 pixel = int2(input.position.xy);
    float3 rgb = max(LabImage.Load(int3(pixel,0)).rgb,0.0);
    if (AoSettings.w > 0.5)
    {
        const float ao = sampleAo(input.uv,pixel);
        rgb *= lerp(1.0,ao*ao,AoSettings.x);
    }
    if (GroveLight.x > 0.0) rgb += upsampleGroveLight(input.uv,pixel);
    if (PostSettings.w > 0.5)
    {
        rgb *= exp2(PostSettings.x);
        if (BloomSettings.w > 0.5) rgb += sampleBloom(input.uv)*BloomSettings.x;
        rgb = saturate(rgb * (2.51 * rgb + 0.03) / (rgb * (2.43 * rgb + 0.59) + 0.14));
        rgb = lerp(dot(rgb, LumaWeights).xxx, rgb, PostSettings.y);
        rgb = saturate((rgb - 0.18) * PostSettings.z + 0.18);
    }
    if (GradeSettings.w > 0.5) rgb = gradeColor(rgb,input.uv);
    return float4(lerp(12.92 * rgb, 1.055 * pow(max(rgb,0.0), 1.0 / 2.4) - 0.055, step(0.0031308, rgb)), 1);
}

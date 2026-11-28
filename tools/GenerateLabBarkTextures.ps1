param([string]$OutputDirectory = (Join-Path $PSScriptRoot '..\asset\graphics-lab'))
$ErrorActionPreference = 'Stop'
$sourceDirectory = Join-Path $PSScriptRoot '..\asset\graphics-lab'
Add-Type -AssemblyName System.Drawing

# 写真を観察してまとめた樹皮の特徴から、色・接線空間法線・遮蔽・粗さを同じ凹凸場で作る。
Add-Type -ReferencedAssemblies ([Drawing.Bitmap].Assembly.Location) -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public static class LabBarkTextures
{
    const int Size = 1024;

    struct BarkSample
    {
        public float Height, Occlusion, Roughness, Red, Green, Blue;
    }

    struct Cell
    {
        public float Edge, Tone;
    }

    static float Mix(float a, float b, float t) { return a + (b-a)*t; }
    static float Clamp(float t) { return Math.Max(0,Math.Min(1,t)); }
    static float Smooth(float low, float high, float value)
    {
        float t = Clamp((value-low)/(high-low));
        return t*t*(3-2*t);
    }

    static int Modulo(int n, int period) { return (n%period+period)%period; }

    static float Hash(int x, int y, int seed)
    {
        unchecked
        {
            uint bits = (uint)(x*374761393 + y*668265263 + seed*1274126177);
            bits = (bits^(bits>>13))*1274126177;
            return ((bits^(bits>>16)) & 0x00ffffff) / 16777215f;
        }
    }

    // 端を周期的につなぎ、幹の背面や枝の途中に継ぎ目を出さない。
    static float Noise(float u, float v, int nx, int ny, int seed)
    {
        u *= nx; v *= ny;
        int x = (int)Math.Floor(u), y = (int)Math.Floor(v);
        float a = Smooth(0,1,u-x), b = Smooth(0,1,v-y);
        int x0 = Modulo(x,nx), x1 = Modulo(x+1,nx), y0 = Modulo(y,ny), y1 = Modulo(y+1,ny);
        return Mix(Mix(Hash(x0,y0,seed),Hash(x1,y0,seed),a),Mix(Hash(x0,y1,seed),Hash(x1,y1,seed),a),b);
    }

    static Cell Plates(float u, float v, int nx, int ny, int seed)
    {
        u *= nx; v *= ny;
        int x0 = (int)Math.Floor(u), y0 = (int)Math.Floor(v);
        float first = 10, second = 10, tone = 0;
        for (int y=y0-1; y<=y0+1; y++)
            for (int x=x0-1; x<=x0+1; x++)
            {
                int sx = Modulo(x,nx), sy = Modulo(y,ny);
                float dx = x + Mix(0.14f,0.86f,Hash(sx,sy,seed))-u;
                float dy = y + Mix(0.14f,0.86f,Hash(sx,sy,seed+1))-v;
                float d = dx*dx+dy*dy;
                if (d < first) { second=first; first=d; tone=Hash(sx,sy,seed+2); }
                else if (d < second) second=d;
            }
        return new Cell{ Edge=(float)(Math.Sqrt(second)-Math.Sqrt(first)), Tone=tone };
    }

    static BarkSample Keyaki(float u, float v)
    {
        // 灰白色の比較的なめらかな地肌に、薄い剥離跡と横長の皮目をまばらに置く。
        float warpX = (Noise(u,v,17,21,5)-0.5f)*0.067f + (Noise(u,v,45,59,8)-0.5f)*0.012f;
        float warpY = (Noise(u,v,19,23,11)-0.5f)*0.062f + (Noise(u,v,53,57,12)-0.5f)*0.018f;
        Cell cell = Plates(u+warpX,v+warpY,9,13,7);
        float fracture = cell.Edge + (Noise(u,v,51,58,28)-0.5f)*0.12f;
        float edge = Smooth(0.014f,0.091f,fracture);
        float patch = Smooth(0.42f,0.75f,cell.Tone) * Smooth(0.28f,0.59f,Noise(u,v,8,12,27));
        float exposed = patch*edge;
        float pores = Smooth(0.65f,0.87f,Noise(u,v,96,245,18));
        float mottles = Noise(u,v,36,47,21);
        float grain = Noise(u,v,218,220,25);
        float shade = 0.89f+0.18f*mottles+0.09f*grain-0.13f*pores;
        return new BarkSample{
            Height=0.15f*mottles + 0.060f*grain - 0.16f*exposed - 0.11f*pores,
            Occlusion=0.97f-0.10f*pores-0.09f*(1-edge)*patch,
            Roughness=0.79f-0.18f*exposed+0.07f*grain,
            Red=Mix(119,145,exposed)*shade, Green=Mix(118,133,exposed)*shade, Blue=Mix(111,117,exposed)*shade
        };
    }

    static BarkSample Fissured(float u, float v, bool cedar)
    {
        // コナラは浅い縦溝と細かな割れ。スギはさらに細長い繊維が裂け、赤褐色の薄片になる。
        int seed = cedar ? 65 : 37;
        float warp = (Noise(u,v,12,12,seed)-0.5f)*(cedar ? 0.023f : 0.052f);
        float bend = (Noise(u,v,33,10,seed+3)-0.5f)*0.014f;
        Cell cell = Plates(u+warp+bend,v,cedar ? 35 : 19,cedar ? 4 : 6,seed+5);
        float shelf = Smooth(0.022f,cedar ? 0.29f : 0.40f,cell.Edge);
        float narrow = Noise(u+warp*0.42f,v,cedar ? 137 : 72,cedar ? 16 : 28,seed+8);
        float split = Smooth(0.48f,0.83f,narrow);
        float grain = Noise(u+warp,v,208,cedar ? 52 : 145,seed+12);
        float cross = Noise(u,v,73,101,seed+10);
        float tone = Smooth(0.24f,0.76f,cell.Tone);
        float shade = Mix(0.64f,1.10f,shelf)*(0.83f+0.18f*cross+0.14f*grain-0.11f*split);
        return new BarkSample{
            Height=0.32f*shelf+0.16f*narrow+0.11f*grain+0.14f*cross,
            Occlusion=0.76f+0.21f*shelf-0.06f*split,
            Roughness=0.73f+0.15f*(1-shelf)+0.08f*grain,
            Red=Mix(cedar ? 107 : 102,cedar ? 151 : 135,tone)*shade,
            Green=Mix(cedar ? 72 : 94,cedar ? 101 : 125,tone)*shade,
            Blue=Mix(cedar ? 54 : 82,cedar ? 74 : 109,tone)*shade
        };
    }

    static byte Byte(float value) { return (byte)Math.Max(0,Math.Min(255,(int)(value+0.5f))); }

    static byte[] ReadPhoto(string path)
    {
        var pixels = new byte[Size*Size*4];
        using (var photo = new Bitmap(path))
        using (var bitmap = new Bitmap(Size,Size,PixelFormat.Format32bppArgb))
        {
            using (var graphics = Graphics.FromImage(bitmap))
            {
                graphics.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                graphics.DrawImage(photo,0,0,Size,Size);
            }
            var data = bitmap.LockBits(new Rectangle(0,0,Size,Size),ImageLockMode.ReadOnly,PixelFormat.Format32bppArgb);
            Marshal.Copy(data.Scan0,pixels,0,pixels.Length);
            bitmap.UnlockBits(data);
        }
        return pixels;
    }

    static float Lightness(byte[] pixels,int x,int y)
    {
        int i = (Modulo(x,Size)+Modulo(y,Size)*Size)*4;
        return pixels[i+2]*0.213f+pixels[i+1]*0.715f+pixels[i]*0.072f;
    }

    static void Save(string path, byte[] pixels)
    {
        using (var bitmap = new Bitmap(Size,Size,PixelFormat.Format32bppArgb))
        {
            var data = bitmap.LockBits(new Rectangle(0,0,Size,Size),ImageLockMode.WriteOnly,PixelFormat.Format32bppArgb);
            Marshal.Copy(pixels,0,data.Scan0,pixels.Length);
            bitmap.UnlockBits(data);
            bitmap.Save(path,ImageFormat.Png);
        }
    }

    static void WriteSpecies(string folder,string name,int species,byte[] photo,byte[] photoNormals)
    {
        var height = new float[Size*Size];
        var color = new byte[Size*Size*4];
        var relief = new byte[color.Length];
        for (int y=0;y<Size;y++)
            for (int x=0;x<Size;x++)
            {
                float u = (x+0.5f)/Size, v = (y+0.5f)/Size;
                BarkSample s = species == 0 ? Keyaki(u,v) : Fissured(u,v,species == 2);
                int index = x+y*Size, pixel = index*4;
                int photoX = species == 2 ? (x*2)%Size : x;
                int photoPixel = (photoX+y*Size)*4;
                float luma = Lightness(photo,photoX,y);
                // 実写由来の微細な割れを重ねる。樹種の色や剥がれ方は上の凹凸場で決める。
                if (species == 0)
                {
                    float nearby = (Lightness(photo,photoX-9,y)+Lightness(photo,photoX+9,y)
                        +Lightness(photo,photoX,y-9)+Lightness(photo,photoX,y+9))*0.25f;
                    float detail = (luma-nearby)*0.31f;
                    s.Red += detail; s.Green += detail; s.Blue += detail;
                    s.Height += detail*0.0045f;
                }
                else if (species == 1)
                {
                    s.Red = Mix(s.Red,photo[photoPixel+2]*1.07f+7,0.84f);
                    s.Green = Mix(s.Green,photo[photoPixel+1]*1.08f+7,0.84f);
                    s.Blue = Mix(s.Blue,photo[photoPixel]*1.11f+7,0.84f);
                }
                else
                {
                    s.Red = Mix(s.Red,luma*1.13f+10,0.69f);
                    s.Green = Mix(s.Green,luma*0.89f+10,0.69f);
                    s.Blue = Mix(s.Blue,luma*0.70f+12,0.69f);
                }
                height[index] = s.Height;
                color[pixel] = Byte(s.Blue); color[pixel+1] = Byte(s.Green); color[pixel+2] = Byte(s.Red); color[pixel+3] = 255;
                relief[pixel] = Byte(s.Occlusion*255); relief[pixel+3] = Byte(s.Roughness*255);
            }

        float amplitude = species == 0 ? 2.9f : 3.8f;
        for (int y=0;y<Size;y++)
            for (int x=0;x<Size;x++)
            {
                // RG は接線と枝方向、B は細い溝の遮蔽、A は粗さ。Z は描画時に復元する。
                float dx = (height[Modulo(x-1,Size)+y*Size]-height[Modulo(x+1,Size)+y*Size])*amplitude;
                float dy = (height[x+Modulo(y-1,Size)*Size]-height[x+Modulo(y+1,Size)*Size])*amplitude*0.80f;
                if (species != 0)
                {
                    int photoX = species == 2 ? (x*2)%Size : x;
                    int photoPixel = (photoX+y*Size)*4;
                    dx += (photoNormals[photoPixel+2]/127.5f-1.0f)*0.64f;
                    dy += (photoNormals[photoPixel+1]/127.5f-1.0f)*0.64f;
                }
                float length = (float)Math.Sqrt(1+dx*dx+dy*dy);
                int pixel = (x+y*Size)*4;
                relief[pixel+1] = Byte((0.5f+0.5f*dy/length)*255);
                relief[pixel+2] = Byte((0.5f+0.5f*dx/length)*255);
            }
        Save(Path.Combine(folder,name+"-bark-color.png"),color);
        Save(Path.Combine(folder,name+"-bark-relief.png"),relief);
    }

    public static void Write(string destination,string source)
    {
        byte[] photo = ReadPhoto(Path.Combine(source,"bark-diff.jpg"));
        byte[] normal = ReadPhoto(Path.Combine(source,"bark-nor_dx.jpg"));
        WriteSpecies(destination,"keyaki",0,photo,normal);
        WriteSpecies(destination,"konara",1,photo,normal);
        WriteSpecies(destination,"sugi",2,photo,normal);
    }
}
'@

$folder = (New-Item -ItemType Directory -Path $OutputDirectory -Force).FullName
[LabBarkTextures]::Write($folder,(Resolve-Path -LiteralPath $sourceDirectory).Path)

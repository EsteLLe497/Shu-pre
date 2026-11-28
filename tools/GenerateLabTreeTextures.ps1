# =========================================================
# ファイルの情報[GenerateLabTreeTextures.ps1]
#
# 制作者:ozawa souma        日付:2026/11/28
# =========================================================

param([string]$OutputDirectory = (Join-Path $PSScriptRoot '..\asset\graphics-lab'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

function Point([float]$x, [float]$y) {
    [Drawing.PointF]::new($x, $y)
}

function Leaf($graphics, $brush, $vein, [float]$x, [float]$y, [float]$angle, [float]$length, [bool]$oak) {
    # ケヤキは細い尖った葉、コナラは先端寄りに幅のある葉にする。
    $dirX = [Math]::Cos($angle)
    $dirY = [Math]::Sin($angle)
    $steps = if ($oak) {16} else {18}
    $outline = [Collections.Generic.List[Drawing.PointF]]::new()
    $outline.Add((Point $x $y))
    for ($side = 1; $side -ge -1; $side -= 2) {
        for ($i = 1; $i -le $steps; $i++) {
            $t = if ($side -eq 1) { $i / ($steps + 1) } else { 1 - $i / ($steps + 1) }
            $baseWidth = if ($oak) {0.93 + 0.33 * $t} else {1.16 - 0.38 * $t}
            $tooth = if ($i % 2 -eq 0) {0.83} else {1.0}
            $width = $length * $(if ($oak) {0.24} else {0.168}) * [Math]::Pow([Math]::Sin([Math]::PI * $t),0.83) * $baseWidth * $tooth * $side
            $outline.Add((Point ($x + $dirX * $length * $t - $dirY * $width) ($y + $dirY * $length * $t + $dirX * $width)))
        }
        if ($side -eq 1) { $outline.Add((Point ($x + $dirX * $length) ($y + $dirY * $length))) }
    }
    $graphics.FillPolygon($brush, $outline.ToArray())
    # 葉脈と片側の細い陰で、斜めから見ても平坦な色面にならないようにする。
    $graphics.DrawLine($vein, ($x+$dirX*$length*0.08), ($y+$dirY*$length*0.08),
        ($x+$dirX*$length*0.89), ($y+$dirY*$length*0.89))
    for ($n = 2; $n -le 6; $n++) {
        $base = $n * 0.107
        $outer = $base + 0.13
        $width = $length * $(if($oak){0.135}else{0.097}) * [Math]::Sin([Math]::PI*$outer)
        for ($side = -1; $side -le 1; $side += 2) {
            $graphics.DrawLine($vein, ($x+$dirX*$length*$base), ($y+$dirY*$length*$base),
                ($x+$dirX*$length*$outer-$dirY*$width*$side), ($y+$dirY*$length*$outer+$dirX*$width*$side))
        }
    }
}

function Needles($graphics, $pen, [float]$x, [float]$y, [float]$ex, [float]$ey, [float]$length) {
    # 針葉は平たい葉に置き換えず、枝に沿って前を向く鎌形の線を左右に重ねる。
    $dx = $ex - $x
    $dy = $ey - $y
    $r = [Math]::Sqrt($dx*$dx + $dy*$dy)
    if ($r -lt 1) {return}
    $nx = -$dy / $r
    $ny = $dx / $r
    $graphics.DrawLine($pen,$x,$y,$ex,$ey)
    $count = [Math]::Max(4,[int]($r / 2.9))
    for ($i = 1; $i -lt $count; $i++) {
        $t = $i / $count
        $side = if ($i%2 -eq 0) {1} else {-1}
        $w = $side * $length * (1-0.69*$t) * (0.72 + 0.28*[Math]::Cos($i*2.39996))
        $px = $x + $dx*$t
        $py = $y + $dy*$t
        $reach = $length * 0.64 / $r
        $graphics.DrawBezier($pen,(Point $px $py),
            (Point ($px+$nx*$w*0.48) ($py+$ny*$w*0.48)),
            (Point ($px+$dx*$reach*0.45+$nx*$w) ($py+$dy*$reach*0.45+$ny*$w)),
            (Point ($px+$dx*$reach+$nx*$w) ($py+$dy*$reach+$ny*$w)))
    }
}

foreach ($species in 'keyaki','konara','sugi') {
    $bitmap = [Drawing.Bitmap]::new(512,512,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([Drawing.Color]::Transparent)
    $branch = [Drawing.Pen]::new([Drawing.Color]::FromArgb(166,166,166),3)
    $needle = [Drawing.Pen]::new([Drawing.Color]::FromArgb(226,226,226),1.8)
    $vein = [Drawing.Pen]::new([Drawing.Color]::FromArgb(125,244,244,244),0.92)
    $brush = [Drawing.SolidBrush]::new([Drawing.Color]::White)
    $random = [Random]::new(61)
    try {
        $g.DrawBezier($branch,256,493,239,371,268,177,253,28)
        if ($species -eq 'sugi') {
            for ($i = 0; $i -lt 25; $i++) {
                $t = $i / 25.0
                $y = 459 - 395*$t
                $side = if ($i % 2 -eq 0) {1} else {-1}
                $span = (158 * [Math]::Sin([Math]::PI * ($t*0.88+0.10)) + 21) * (0.72 + 0.28*$random.NextDouble())
                $ex = 256 + $side*$span
                $ey = $y - (25+44*$random.NextDouble())
                Needles $g $needle 256 $y $ex $ey 13
                for ($fork = 1; $fork -le 4; $fork++) {
                    $f = $fork * 0.176
                    $fx = 256+($ex-256)*$f
                    $fy = $y+($ey-$y)*$f
                    Needles $g $needle $fx $fy ($fx + $side*(35-24*$f)) ($fy-29-8*$random.NextDouble()) 10
                }
            }
            Needles $g $needle 254 173 253 24 12
        } else {
            $oak = $species -eq 'konara'
            $shoots = if ($oak) {10} else {12}
            $leavesPerShoot = if ($oak) {11} else {15}
            for ($i=0;$i -lt $shoots;$i++) {
                $t = $i / [float]$shoots
                $side = if ($i%2 -eq 0) {1} else {-1}
                $y = 459 - 348*$t
                $stemX = 256 + 8*[Math]::Sin($t*7)
                $span = (124*[Math]::Sin([Math]::PI*($t*0.85+0.12))+48)*(0.80+0.2*$random.NextDouble())
                $endX = $stemX + $side*$span
                $endY = $y - (46+43*$random.NextDouble())
                $g.DrawLine($branch,$stemX,$y,$endX,$endY)
                for ($j=1;$j -le $leavesPerShoot;$j++) {
                    $u = $j / [float]$leavesPerShoot
                    $x = $stemX+($endX-$stemX)*$u
                    $ly = $y+($endY-$y)*$u
                    $angle = if ($j%2 -eq 0) {-.95 + .58*$side} else {2.03 - .71*$side}
                    $length = $(if($oak){68}else{49})*(0.55+0.45*$random.NextDouble())*(1-0.23*$u)
                    $shade = [int](156 + 90*$random.NextDouble())
                    $brush.Color = [Drawing.Color]::FromArgb($shade,$shade,$shade)
                    Leaf $g $brush $vein $x $ly ($angle+($random.NextDouble()-.5)*.58) $length $oak
                }
            }
            for ($i=0; $i -lt 8; $i++) {
                $a = if($i%2 -eq 0){-2.44}else{-.6}
                Leaf $g $brush $vein 252 (189-$i*21) $a $(if($oak){59}else{42}) $oak
            }
        }
        $bitmap.Save((Join-Path $OutputDirectory ($species + '-leaves.png')),[Drawing.Imaging.ImageFormat]::Png)
    } finally {
        foreach ($resource in $vein,$needle,$branch,$brush,$g,$bitmap) {$resource.Dispose()}
    }
}

# compare_png.ps1: compares two PNGs of the same size texel by texel and
# prints the largest channel difference and how many texels differ by more
# than 1 (the plan's Vulkan vs D3D12 bound). Used by compare_backends.cmd.
#   powershell -NoProfile -ExecutionPolicy Bypass -File compare_png.ps1 a.png b.png
# Exit 0 within 1 LSB, 1 otherwise or on an error.
param([string]$A, [string]$B)
$ErrorActionPreference = "Stop"
if (-not (Test-Path $A) -or -not (Test-Path $B)) {
    Write-Output "compare: missing $A or $B (did rd_replay_tool fail above?)"
    exit 1
}
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class IcoPngCompare {
    static byte[] Bytes(Bitmap b) {
        var r = new Rectangle(0, 0, b.Width, b.Height);
        var d = b.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        var a = new byte[d.Stride * b.Height];
        Marshal.Copy(d.Scan0, a, 0, a.Length);
        b.UnlockBits(d);
        return a;
    }
    // returns { max channel difference, texels over 1, first x, first y }
    public static int[] Run(string pa, string pb) {
        using (var a = new Bitmap(pa)) using (var b = new Bitmap(pb)) {
            if (a.Width != b.Width || a.Height != b.Height) return new int[] { -1, 0, a.Width, a.Height };
            var x = Bytes(a); var y = Bytes(b);
            int max = 0, over = 0, fx = -1, fy = -1, w = a.Width;
            for (int i = 0; i < x.Length; i += 4) {
                int m = 0;
                for (int k = 0; k < 4; k++) m = Math.Max(m, Math.Abs(x[i + k] - y[i + k]));
                if (m > max) max = m;
                if (m > 1) { if (over == 0) { fx = (i / 4) % w; fy = (i / 4) / w; } over++; }
            }
            return new int[] { max, over, fx, fy };
        }
    }
}
"@
$r = [IcoPngCompare]::Run((Resolve-Path $A).Path, (Resolve-Path $B).Path)
if ($r[0] -lt 0) {
    Write-Output "compare: sizes differ ($A vs $B)"
    exit 1
}
if ($r[1] -eq 0) {
    Write-Output ("compare: max difference {0} LSB: within 1 LSB ({1} vs {2})" -f $r[0], $A, $B)
    exit 0
}
Write-Output ("compare: max difference {0} LSB, {1} texels over 1 LSB, first at ({2},{3}) ({4} vs {5})" -f $r[0], $r[1], $r[2], $r[3], $A, $B)
exit 1

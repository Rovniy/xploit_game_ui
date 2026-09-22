namespace Xploit.GameUI
{
    /// <summary>How the native runtime delivers a view's pixels to Unity. Mirrors xgu_provider.</summary>
    public enum RenderProvider
    {
        /// <summary>D3D12 external texture when Unity runs D3D12, otherwise CPU.</summary>
        Auto = 0,
        /// <summary>Zero-copy: plugin-owned ID3D12Resource wrapped with Texture2D.CreateExternalTexture.</summary>
        D3D12External = 1,
        /// <summary>One GPU copy into a Unity-owned Texture2D (Stage 5+).</summary>
        D3D12Copy = 2,
        /// <summary>Skia raster + LoadRawTextureData (D3D11, -nographics, tests).</summary>
        Cpu = 3,
        /// <summary>No graphics device available.</summary>
        None = 4,
    }
}

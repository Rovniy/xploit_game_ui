using System;
using UnityEngine;

namespace Xploit.GameUI
{
    /// <summary>
    /// Unity-side texture of one native view. For the D3D12 provider it wraps
    /// the plugin-owned ID3D12Resource with <see cref="Texture2D.CreateExternalTexture"/>;
    /// for the CPU provider it uploads the pixel buffer with LoadRawTextureData.
    /// </summary>
    public sealed class WebTexture : IDisposable
    {
        readonly ulong m_handle;
        readonly RenderProvider m_provider;
        readonly Native.TextureFormat m_format;
        Texture2D m_texture;
        IntPtr m_nativePointer;
        uint m_width;
        uint m_height;

        internal WebTexture(ulong handle, RenderProvider provider, Native.TextureFormat format)
        {
            m_handle = handle;
            m_provider = provider;
            m_format = format;
        }

        /// <summary>The texture to display; may be null until the first frame arrives.</summary>
        public Texture Texture => m_texture;

        public int Width => (int)m_width;
        public int Height => (int)m_height;
        public RenderProvider Provider => m_provider;
        public bool IsGpu => m_provider == RenderProvider.D3D12External || m_provider == RenderProvider.D3D12Copy;

        /// <summary>Called once per frame from the main thread.</summary>
        internal void Update()
        {
            if (m_handle == Native.InvalidView)
            {
                return;
            }
            if (IsGpu)
            {
                UpdateExternal();
            }
            else
            {
                UpdateCpu();
            }
        }

        void UpdateExternal()
        {
            var ptr = Native.xgu_view_get_native_texture(m_handle, out var width, out var height);
            if (ptr == IntPtr.Zero || width == 0 || height == 0)
            {
                return;
            }
            if (m_texture != null && ptr == m_nativePointer)
            {
                return;
            }
            var format = m_format == Native.TextureFormat.RGBA8 ? TextureFormat.RGBA32 : TextureFormat.BGRA32;
            if (m_texture != null && width == m_width && height == m_height)
            {
                m_texture.UpdateExternalTexture(ptr);
            }
            else
            {
                if (m_texture != null)
                {
                    UnityEngine.Object.Destroy(m_texture);
                }
                // linear:false — Skia writes sRGB-encoded, premultiplied pixels.
                m_texture = Texture2D.CreateExternalTexture((int)width, (int)height, format, false, false, ptr);
                m_texture.name = "xploit_game_ui view";
                m_texture.wrapMode = TextureWrapMode.Clamp;
                m_texture.filterMode = FilterMode.Bilinear;
            }
            m_nativePointer = ptr;
            m_width = width;
            m_height = height;
        }

        void UpdateCpu()
        {
            var status = Native.xgu_view_status(m_handle);
            if ((status & Native.ViewStatus.PixelsReady) == 0)
            {
                return;
            }
            if (!Native.xgu_view_acquire_pixels(m_handle, out var data, out var size, out var width, out var height, out _))
            {
                return;
            }
            try
            {
                var format = m_format == Native.TextureFormat.BGRA8 ? TextureFormat.BGRA32 : TextureFormat.RGBA32;
                if (m_texture == null || width != m_width || height != m_height || m_nativePointer != IntPtr.Zero)
                {
                    if (m_texture != null)
                    {
                        UnityEngine.Object.Destroy(m_texture);
                    }
                    m_texture = new Texture2D((int)width, (int)height, format, false, false)
                    {
                        name = "xploit_game_ui view (cpu)",
                        wrapMode = TextureWrapMode.Clamp,
                        filterMode = FilterMode.Bilinear,
                    };
                    m_nativePointer = IntPtr.Zero;
                    m_width = width;
                    m_height = height;
                }
                m_texture.LoadRawTextureData(data, (int)size);
                m_texture.Apply(false, false);
            }
            finally
            {
                Native.xgu_view_release_pixels(m_handle);
            }
        }

        public void Dispose()
        {
            if (m_texture != null)
            {
                // Destroying an external texture does not release the native resource;
                // the native side owns and retires it.
                if (Application.isPlaying)
                {
                    UnityEngine.Object.Destroy(m_texture);
                }
                else
                {
                    UnityEngine.Object.DestroyImmediate(m_texture);
                }
                m_texture = null;
            }
            m_nativePointer = IntPtr.Zero;
        }
    }
}

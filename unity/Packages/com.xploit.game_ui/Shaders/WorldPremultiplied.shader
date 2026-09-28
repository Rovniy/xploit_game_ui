// Unlit, transparent, premultiplied-alpha shader for xploit_game_ui views shown
// on a mesh in the world (Skia writes premultiplied, sRGB-encoded pixels).
// HtmlView assigns it to its Target Renderer and sets _MainTex through a
// MaterialPropertyBlock. It has no LightMode tag, so the built-in pipeline and
// URP both draw it.
Shader "XploitGameUI/WorldPremultiplied"
{
    Properties
    {
        [PerRendererData] _MainTex ("View Texture", 2D) = "black" {}
        _Color ("Tint", Color) = (1,1,1,1)
        [Enum(UnityEngine.Rendering.CullMode)] _Cull ("Cull", Float) = 2
        [Enum(Off, 0, On, 1)] _ZWrite ("ZWrite", Float) = 0
    }

    SubShader
    {
        Tags
        {
            "Queue"="Transparent"
            "IgnoreProjector"="True"
            "RenderType"="Transparent"
            "PreviewType"="Plane"
        }

        Cull [_Cull]
        Lighting Off
        ZWrite [_ZWrite]
        Blend One OneMinusSrcAlpha

        Pass
        {
            Name "Unlit"
        CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_fog
            #pragma multi_compile_instancing
            #pragma target 2.0

            #include "UnityCG.cginc"

            struct appdata_t
            {
                float4 vertex   : POSITION;
                float2 texcoord : TEXCOORD0;
                UNITY_VERTEX_INPUT_INSTANCE_ID
            };

            struct v2f
            {
                float4 vertex   : SV_POSITION;
                float2 texcoord : TEXCOORD0;
                UNITY_FOG_COORDS(1)
                UNITY_VERTEX_OUTPUT_STEREO
            };

            sampler2D _MainTex;
            float4 _MainTex_ST;
            fixed4 _Color;

            v2f vert(appdata_t v)
            {
                v2f OUT;
                UNITY_SETUP_INSTANCE_ID(v);
                UNITY_INITIALIZE_VERTEX_OUTPUT_STEREO(OUT);
                OUT.vertex = UnityObjectToClipPos(v.vertex);
                OUT.texcoord = TRANSFORM_TEX(v.texcoord, _MainTex);
                UNITY_TRANSFER_FOG(OUT, OUT.vertex);
                return OUT;
            }

            fixed4 frag(v2f IN) : SV_Target
            {
                // Premultiplied source: tint RGB and scale everything by tint alpha.
                fixed4 color = tex2D(_MainTex, IN.texcoord);
                color.rgb *= _Color.rgb;
                color *= _Color.a;
                // The fog colour is scaled by alpha too: that is fading towards the fog
                // colour, written for premultiplied pixels.
                UNITY_APPLY_FOG_COLOR(IN.fogCoord, color, unity_FogColor * color.a);
                return color;
            }
        ENDCG
        }
    }
}

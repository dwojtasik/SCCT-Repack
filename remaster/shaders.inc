// ------------------------------------------------------------------------------------------------
// shaders.inc - HLSL sources of the bloom passes, compiled at runtime with d3dcompiler_47.dll
// (part of every Windows 10/11 install).
// ------------------------------------------------------------------------------------------------

static const char* HLSL_PS_COMMON =
    "sampler2D s0 : register(s0);\n"
    "sampler2D s1 : register(s1);\n"
    "float4 c0 : register(c0);\n"
    "float4 c1 : register(c1);\n";

// Full-screen quad, pre-transformed positions.
static const char* HLSL_VS =
    "struct O { float4 p : POSITION; float2 t : TEXCOORD0; };\n"
    "O main(float4 p : POSITION, float2 t : TEXCOORD0) { O o; o.p = p; o.t = t; return o; }\n";

// 13-tap downsample (Jimenez, CoD:AW). c0.xy = source texel size.
static const char* HLSL_DOWN =
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    "  float2 t = c0.xy;\n"
    "  float4 e = tex2D(s0, uv);\n"
    "  float4 a = tex2D(s0, uv + t * float2(-2,-2)), b = tex2D(s0, uv + t * float2(0,-2)), c = tex2D(s0, uv + t * float2(2,-2));\n"
    "  float4 d = tex2D(s0, uv + t * float2(-2, 0)), f = tex2D(s0, uv + t * float2(2, 0));\n"
    "  float4 g = tex2D(s0, uv + t * float2(-2, 2)), h = tex2D(s0, uv + t * float2(0, 2)), i = tex2D(s0, uv + t * float2(2, 2));\n"
    "  float4 j = tex2D(s0, uv + t * float2(-1,-1)), k = tex2D(s0, uv + t * float2(1,-1));\n"
    "  float4 l = tex2D(s0, uv + t * float2(-1, 1)), m = tex2D(s0, uv + t * float2(1, 1));\n"
    "  return e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;\n"
    "}\n";

// 13-tap downsample with Karis average on rgb (anti-firefly); alpha uses the plain weights.
static const char* HLSL_DOWN_KARIS =
    "float W(float4 x) { return 1.0 / (1.0 + dot(x.rgb, float3(0.2126, 0.7152, 0.0722))); }\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    "  float2 t = c0.xy;\n"
    "  float4 e = tex2D(s0, uv);\n"
    "  float4 a = tex2D(s0, uv + t * float2(-2,-2)), b = tex2D(s0, uv + t * float2(0,-2)), c = tex2D(s0, uv + t * float2(2,-2));\n"
    "  float4 d = tex2D(s0, uv + t * float2(-2, 0)), f = tex2D(s0, uv + t * float2(2, 0));\n"
    "  float4 g = tex2D(s0, uv + t * float2(-2, 2)), h = tex2D(s0, uv + t * float2(0, 2)), i = tex2D(s0, uv + t * float2(2, 2));\n"
    "  float4 j = tex2D(s0, uv + t * float2(-1,-1)), k = tex2D(s0, uv + t * float2(1,-1));\n"
    "  float4 l = tex2D(s0, uv + t * float2(-1, 1)), m = tex2D(s0, uv + t * float2(1, 1));\n"
    "  float4 B0 = (j + k + l + m) * 0.25, B1 = (a + b + d + e) * 0.25, B2 = (b + c + e + f) * 0.25;\n"
    "  float4 B3 = (d + e + g + h) * 0.25, B4 = (e + f + h + i) * 0.25;\n"
    "  float w0 = 0.5 * W(B0), w1 = 0.125 * W(B1), w2 = 0.125 * W(B2), w3 = 0.125 * W(B3), w4 = 0.125 * W(B4);\n"
    "  float3 rgb = (B0.rgb * w0 + B1.rgb * w1 + B2.rgb * w2 + B3.rgb * w3 + B4.rgb * w4) / (w0 + w1 + w2 + w3 + w4);\n"
    "  float al = B0.a * 0.5 + (B1.a + B2.a + B3.a + B4.a) * 0.125;\n"
    "  return float4(rgb, al);\n"
    "}\n";

// 9-tap tent upsample of s0 plus s1. c0.xy = s0 texel size * radius, c1.x = weight of s0, c1.y = weight of s1.
static const char* HLSL_UP =
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    "  float2 t = c0.xy;\n"
    "  float4 s = tex2D(s0, uv) * 4.0;\n"
    "  s += (tex2D(s0, uv + float2(0, -t.y)) + tex2D(s0, uv + float2(-t.x, 0)) + tex2D(s0, uv + float2(t.x, 0)) + tex2D(s0, uv + float2(0, t.y))) * 2.0;\n"
    "  s += tex2D(s0, uv - t) + tex2D(s0, uv + float2(t.x, -t.y)) + tex2D(s0, uv + float2(-t.x, t.y)) + tex2D(s0, uv + t);\n"
    "  return s * (c1.x / 16.0) + tex2D(s1, uv) * c1.y;\n"
    "}\n";

// Cubic B-spline upsample with 4 bilinear taps. c0 = (srcW, srcH, 1/srcW, 1/srcH).
static const char* HLSL_BICUBIC =
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    "  float2 p = uv * c0.xy - 0.5;\n"
    "  float2 f = frac(p), i = p - f;\n"
    "  float2 f2 = f * f, f3 = f2 * f;\n"
    "  float2 w0 = (1 - f) * (1 - f) * (1 - f) / 6.0;\n"
    "  float2 w1 = (3 * f3 - 6 * f2 + 4) / 6.0;\n"
    "  float2 w3 = f3 / 6.0;\n"
    "  float2 w2 = 1.0 - w0 - w1 - w3;\n"
    "  float2 g0 = w0 + w1, g1 = w2 + w3;\n"
    "  float2 h0 = (i + 0.5 - 1.0 + w1 / g0) * c0.zw;\n"
    "  float2 h1 = (i + 0.5 + 1.0 + w3 / g1) * c0.zw;\n"
    "  float4 r0 = tex2D(s0, h0) * g0.x + tex2D(s0, float2(h1.x, h0.y)) * g1.x;\n"
    "  float4 r1 = tex2D(s0, float2(h0.x, h1.y)) * g0.x + tex2D(s0, h1) * g1.x;\n"
    "  return r0 * g0.y + r1 * g1.y;\n"
    "}\n";

typedef HRESULT (WINAPI* D3DCompile_t)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT,
                                       ID3DBlob**, ID3DBlob**);
static D3DCompile_t g_D3DCompile;

static bool LoadCompiler() {
    static bool tried;
    if (tried) return g_D3DCompile != 0;
    tried = true;
    const char* dlls[] = {"d3dcompiler_47.dll", "d3dcompiler_46.dll", "d3dcompiler_43.dll"};
    for (const char* name : dlls) {
        HMODULE m = LoadLibraryA(name);
        if (!m) continue;
        g_D3DCompile = (D3DCompile_t)GetProcAddress(m, "D3DCompile");
        if (g_D3DCompile) break;
    }
    return g_D3DCompile != 0;
}

// Returns compiled bytecode (caller releases) or 0.
static ID3DBlob* CompileHLSL(const char* body, const char* target) {
    if (!LoadCompiler()) return 0;
    char src[8192];
    snprintf(src, sizeof src, "%s%s", target[0] == 'p' ? HLSL_PS_COMMON : "", body);
    ID3DBlob *code = 0, *err = 0;
    HRESULT hr = g_D3DCompile(src, strlen(src), 0, 0, 0, "main", target, 1 << 15 /* OPTIMIZATION_LEVEL3 */, 0, &code, &err);
    if (err) err->Release();
    if (FAILED(hr)) { if (code) code->Release(); return 0; }
    return code;
}

static bool MakePS(IDirect3DDevice9* dev, const char* hlsl, IDirect3DPixelShader9** out) {
    ID3DBlob* b = CompileHLSL(hlsl, "ps_3_0");
    if (!b) return false;
    HRESULT hr = dev->CreatePixelShader((const DWORD*)b->GetBufferPointer(), out);
    b->Release();
    return SUCCEEDED(hr);
}

static bool MakeVS(IDirect3DDevice9* dev, const char* hlsl, IDirect3DVertexShader9** out) {
    ID3DBlob* b = CompileHLSL(hlsl, "vs_3_0");
    if (!b) return false;
    HRESULT hr = dev->CreateVertexShader((const DWORD*)b->GetBufferPointer(), out);
    b->Release();
    return SUCCEEDED(hr);
}

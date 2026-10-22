// From MIT XenosRecomp SA2 adf3a4e (see LICENSE.md); MKvDCU bindings.
[[vk::binding(15, 4)]] ByteAddressBuffer g_NativeFetchData;
[[vk::binding(14, 4)]] RWByteAddressBuffer g_NativeExportData;
uint loadVertexFetchData(uint offset) { return g_NativeFetchData.Load(offset); }
uint4 loadVertexFetchData4(uint offset) { return g_NativeFetchData.Load4(offset); }
void memExportStore(uint offset, uint value) { g_NativeExportData.Store(offset,value); }
uint vfetchSwap(uint value, uint endian)
{
    const uint swap8in16 = ((value & 0x00FF00FFu) << 8) | ((value >> 8) & 0x00FF00FFu);
    const uint swap16in32 = (value << 16) | (value >> 16);
    const uint swap8in32 = (swap8in16 << 16) | (swap8in16 >> 16);
    return select(endian == 1u, swap8in16, select(endian == 2u, swap8in32, select(endian == 3u, swap16in32, value)));
}

// One packed integer component: signed/unsigned, normalized ("fraction") or integer.
float vfetchComponent(uint value, uint shift, uint bits, bool isSigned, bool isInteger, bool signedRfMode)
{
    uint raw = bits == 32 ? value : (value >> shift) & ((1u << bits) - 1u);
    if (isSigned)
    {
        int s = bits == 32 ? int(raw) : (int(raw << (32u - bits)) >> (32u - bits));
        if (isInteger)
            return float(s);
        float maxValue = bits == 32 ? 2147483647.0 : float((1u << (bits - 1u)) - 1u);
        return signedRfMode ? (float(s) * 2.0 + 1.0) / (maxValue * 2.0 + 1.0) : max(float(s) / maxValue, -1.0);
    }
    if (isInteger)
        return float(raw);
    return float(raw) / (bits == 32 ? 4294967295.0 : float((1u << bits) - 1u));
}

// slot = g_VertexFetchSlot(fetch index): copy byte offset | endian mode. Packed components count
// from the low bits of the swapped word, 16-bit ones too: under the usual 8in32 swap x is the
// second halfword in memory, which N&B's shaders swizzle back (.yx, .yxwz on 16-bit fetches).
// banjo-nx (package 27): 'rowsAllowed' (vfetchMemory, vfetchMemoryClamped: their offset and stride are literals at the
// call site, so the test below folds there) loads a whole-row fetch in one 16-byte load (BANJO_VFETCH_ROWS, on unless
// the build turns it off); 'clampAddress' (vfetchMemoryClamped) keeps the address inside the draw's fetch data
// ('fetchBytes', g_VertexFetchBytes, passed by the macro below: on DXIL the shared constants are declared after this
// header), so the predicated-block pass can run the fetch for every vertex and keep its result only where the
// predicate holds (banjo_pred_select.h): where it holds the address is already inside, and where it does not the value
// is discarded.
#ifndef BANJO_VFETCH_ROWS
#define BANJO_VFETCH_ROWS 1
#endif
float4 vfetchMemoryImpl(uint slot, float indexValue, bool roundIndex, int offsetDwords, uint strideDwords,
    uint format, bool isSigned, bool isInteger, bool signedRfMode, int expAdjust, bool rowsAllowed, bool clampAddress,
    uint fetchBytes)
{
    int index = int(roundIndex ? floor(indexValue + 0.5) : floor(indexValue));
    uint address = (slot & ~0x3u) + uint(index * int(strideDwords) + offsetDwords) * 4u;
    if (clampAddress)
        address = min(address, max(fetchBytes, 16u) - 16u);
    uint endian = slot & 0x3u;

    uint4 w;
    if (BANJO_VFETCH_ROWS && rowsAllowed && ((uint(offsetDwords) | strideDwords) & 3u) == 0u)
    {
        const uint4 v = loadVertexFetchData4(address);
        w = uint4(vfetchSwap(v.x, endian), vfetchSwap(v.y, endian), vfetchSwap(v.z, endian), vfetchSwap(v.w, endian));
    }
    else
    {
        w.x = vfetchSwap(loadVertexFetchData(address), endian);
        w.y = vfetchSwap(loadVertexFetchData(address + 4), endian);
        w.z = vfetchSwap(loadVertexFetchData(address + 8), endian);
        w.w = vfetchSwap(loadVertexFetchData(address + 12), endian);
    }

    float4 result = float4(0.0, 0.0, 0.0, 1.0);
    bool isFloat = false;
    switch (format)
    {
    case 6: // 8_8_8_8
        result = float4(vfetchComponent(w.x, 0, 8, isSigned, isInteger, signedRfMode), vfetchComponent(w.x, 8, 8, isSigned, isInteger, signedRfMode),
            vfetchComponent(w.x, 16, 8, isSigned, isInteger, signedRfMode), vfetchComponent(w.x, 24, 8, isSigned, isInteger, signedRfMode));
        break;
    case 7: // 2_10_10_10
        result = float4(vfetchComponent(w.x, 0, 10, isSigned, isInteger, signedRfMode), vfetchComponent(w.x, 10, 10, isSigned, isInteger, signedRfMode),
            vfetchComponent(w.x, 20, 10, isSigned, isInteger, signedRfMode), vfetchComponent(w.x, 30, 2, isSigned, isInteger, signedRfMode));
        break;
    case 16: // 10_11_11
        result.xyz = float3(vfetchComponent(w.x, 0, 11, isSigned, isInteger, signedRfMode), vfetchComponent(w.x, 11, 11, isSigned, isInteger, signedRfMode),
            vfetchComponent(w.x, 22, 10, isSigned, isInteger, signedRfMode));
        break;
    case 17: // 11_11_10
        result.xyz = float3(vfetchComponent(w.x, 0, 10, isSigned, isInteger, signedRfMode), vfetchComponent(w.x, 10, 11, isSigned, isInteger, signedRfMode),
            vfetchComponent(w.x, 21, 11, isSigned, isInteger, signedRfMode));
        break;
    case 25: // 16_16
        result.xy = float2(vfetchComponent(w.x, 0, 16, isSigned, isInteger, signedRfMode), vfetchComponent(w.x, 16, 16, isSigned, isInteger, signedRfMode));
        break;
    case 26: // 16_16_16_16
        result = float4(vfetchComponent(w.x, 0, 16, isSigned, isInteger, signedRfMode), vfetchComponent(w.x, 16, 16, isSigned, isInteger, signedRfMode),
            vfetchComponent(w.y, 0, 16, isSigned, isInteger, signedRfMode), vfetchComponent(w.y, 16, 16, isSigned, isInteger, signedRfMode));
        break;
    case 31: // 16_16_FLOAT
        result.xy = f16tof32(uint2(w.x & 0xFFFF, w.x >> 16));
        isFloat = true;
        break;
    case 32: // 16_16_16_16_FLOAT
        result = f16tof32(uint4(w.x & 0xFFFF, w.x >> 16, w.y & 0xFFFF, w.y >> 16));
        isFloat = true;
        break;
    case 33: // 32
        result.x = vfetchComponent(w.x, 0, 32, isSigned, isInteger, signedRfMode);
        break;
    case 34: // 32_32
        result.xy = float2(vfetchComponent(w.x, 0, 32, isSigned, isInteger, signedRfMode), vfetchComponent(w.y, 0, 32, isSigned, isInteger, signedRfMode));
        break;
    case 35: // 32_32_32_32
        result = float4(vfetchComponent(w.x, 0, 32, isSigned, isInteger, signedRfMode), vfetchComponent(w.y, 0, 32, isSigned, isInteger, signedRfMode),
            vfetchComponent(w.z, 0, 32, isSigned, isInteger, signedRfMode), vfetchComponent(w.w, 0, 32, isSigned, isInteger, signedRfMode));
        break;
    case 36: // 32_FLOAT
        result.x = asfloat(w.x);
        isFloat = true;
        break;
    case 37: // 32_32_FLOAT
        result.xy = asfloat(w.xy);
        isFloat = true;
        break;
    case 38: // 32_32_32_32_FLOAT
        result = asfloat(w);
        isFloat = true;
        break;
    case 57: // 32_32_32_FLOAT
        result.xyz = asfloat(w.xyz);
        isFloat = true;
        break;
    }

    if (!isFloat && expAdjust != 0)
        result *= exp2(float(expAdjust));

    return result;
}

float4 vfetchMemory(uint slot, float indexValue, bool roundIndex, int offsetDwords, uint strideDwords,
    uint format, bool isSigned, bool isInteger, bool signedRfMode, int expAdjust)
{
    return vfetchMemoryImpl(slot, indexValue, roundIndex, offsetDwords, strideDwords, format, isSigned, isInteger,
        signedRfMode, expAdjust, true, false, 0u);
}

#define vfetchMemoryClamped(slot, indexValue, roundIndex, offsetDwords, strideDwords, format, isSigned, isInteger, \
    signedRfMode, expAdjust) \
    vfetchMemoryImpl(slot, indexValue, roundIndex, offsetDwords, strideDwords, format, isSigned, isInteger, \
        signedRfMode, expAdjust, true, true, g_VertexFetchBytes)


uint memExportElement(float4 data, uint format, uint numFormat, out uint4 words)
{
    words = 0;
    switch (format)
    {
    case 36: // 32_FLOAT
        words.x = asuint(data.x);
        return 1;
    case 37: // 32_32_FLOAT
        words.xy = asuint(data.xy);
        return 2;
    case 38: // 32_32_32_32_FLOAT
        words = asuint(data);
        return 4;
    case 31: // 16_16_FLOAT
        words.x = f32tof16(data.x) | (f32tof16(data.y) << 16);
        return 1;
    case 32: // 16_16_16_16_FLOAT
        words.xy = f32tof16(data.xz) | (f32tof16(data.yw) << 16);
        return 2;
    }

    // Integer and normalized formats: component widths, in dword order.
    uint4 widths;
    uint dwords;
    switch (format)
    {
    case 6: widths = uint4(8, 8, 8, 8); dwords = 1; break;       // 8_8_8_8
    case 7: widths = uint4(10, 10, 10, 2); dwords = 1; break;    // 2_10_10_10
    case 25: widths = uint4(16, 16, 0, 0); dwords = 1; break;    // 16_16
    case 26: widths = uint4(16, 16, 16, 16); dwords = 2; break;  // 16_16_16_16
    case 33: widths = uint4(32, 0, 0, 0); dwords = 1; break;     // 32
    case 34: widths = uint4(32, 32, 0, 0); dwords = 2; break;    // 32_32
    case 35: widths = uint4(32, 32, 32, 32); dwords = 4; break;  // 32_32_32_32
    default: return 0;
    }
    const bool isSigned = (numFormat & 1u) != 0;
    const bool isInteger = (numFormat & 2u) != 0;
    uint bit = 0;
    for (uint c = 0; c < 4u; c++)
    {
        const uint width = widths[c];
        if (width == 0u)
            break;
        float v = data[c];
        uint packed;
        if (width == 32u)
        {
            packed = isInteger ? (isSigned ? uint(int(v)) : uint(max(v, 0.0)))
                               : (isSigned ? uint(int(clamp(v, -1.0, 1.0) * 2147483647.0))
                                           : uint(saturate(v) * 4294967295.0));
        }
        else
        {
            const float top = isSigned ? float((1u << (width - 1u)) - 1u) : float((1u << width) - 1u);
            const float scaled = isInteger ? v : round((isSigned ? clamp(v, -1.0, 1.0) : saturate(v)) * top);
            packed = uint(int(scaled)) & ((1u << width) - 1u);
        }
        const uint dword = bit >> 5;
        words[dword] |= packed << (bit & 31u);
        bit += width;
    }
    return dwords;
}

// The fetch/export endian swap of one dword (the 8in32 N&B uses, and the other 32-bit modes).
uint memExportSwap(uint value, uint endian)
{
    if (endian == 2u)
        return (value >> 24) | ((value >> 8) & 0xFF00u) | ((value << 8) & 0xFF0000u) | (value << 24);
    if (endian == 1u)
        return ((value >> 8) & 0x00FF00FFu) | ((value << 8) & 0xFF00FF00u);
    if (endian == 3u)
        return (value >> 16) | (value << 16);
    return value;
}

// stream0..3 = g_MemExportStream(0..3), passed by the caller: on DXIL they live in the shader's
// own constant buffer, declared after this header.
void memExport(float4 eA, float4 data, uint element, uint4 stream0, uint4 stream1, uint4 stream2,
    uint4 stream3)
{
    const float firstIndex = eA.y - 8388608.0;
    const float count = eA.w - 8388608.0;
    if (!(firstIndex >= 0.0) || !(count >= 0.0))
        return;
    const uint index = uint(firstIndex) + element;
    if (index >= uint(count))
        return;

    const uint info = asuint(eA.z);
    uint4 words;
    const uint dwords = memExportElement(data, (info >> 8) & 0x3Fu, (info >> 16) & 7u, words);
    if (dwords == 0u || (info & 7u) > 3u)
        return;

    const uint bytes = dwords * 4u;
    const uint address = (asuint(eA.x) & 0x3FFFFFFFu) * 4u + index * bytes;
    const uint4 streams[4] = { stream0, stream1, stream2, stream3 };
    for (uint i = 0; i < 4u; i++)
    {
        const uint4 stream = streams[i];
        if (address < stream.x || address + bytes > stream.x + stream.y)
            continue;
        const uint offset = stream.z + (address - stream.x);
        for (uint d = 0; d < dwords; d++)
            memExportStore(offset + d * 4u, memExportSwap(words[d], info & 7u));
        return;
    }
}

// MKvDCU texture instruction helpers.
float4 nativeRawFetch(Texture2D<float4> t, SamplerState s, float2 c, float2 off, bool denorm, float bias) { uint2 d; t.GetDimensions(d.x,d.y); c = denorm ? (c+off)/float2(d) : c+off/float2(d); return t.SampleBias(s,c,bias); }
float4 nativeRawFetchLevel(Texture2D<float4> t, SamplerState s, float2 c, float2 off, bool denorm, float level) { uint2 d; t.GetDimensions(d.x,d.y); c = denorm ? (c+off)/float2(d) : c+off/float2(d); return t.SampleLevel(s,c,level); }
float4 nativeRawFetchGrad(Texture2D<float4> t, SamplerState s, float2 c, float2 off, bool denorm, float3 gx, float3 gy) { uint2 d; t.GetDimensions(d.x,d.y); c = denorm ? (c+off)/float2(d) : c+off/float2(d); if(denorm) { gx.xy /= float2(d); gy.xy /= float2(d); } return t.SampleGrad(s,c,gx.xy,gy.xy); }
float4 nativeLod(Texture2D<float4> t, SamplerState s, float2 c, float2 off, bool denorm) { uint2 d; t.GetDimensions(d.x,d.y); c = denorm ? (c+off)/float2(d) : c+off/float2(d); return float4(t.CalculateLevelOfDetailUnclamped(s,c),0,0,0); }
float4 nativeWeights(Texture2D<float4> t, SamplerState s, float2 c, float2 off, bool denorm) { uint2 d; t.GetDimensions(d.x,d.y); c = denorm ? (c+off)/float2(d) : c+off/float2(d); return float4(frac(c*float2(d)-0.5),0,0); }
float4 nativeRawFetch(Texture3D<float4> t, SamplerState s, float3 c, float3 off, bool denorm, float bias) { uint3 d; t.GetDimensions(d.x,d.y,d.z); c = denorm ? (c+off)/float3(d) : c+off/float3(d); return t.SampleBias(s,c,bias); }
float4 nativeRawFetchLevel(Texture3D<float4> t, SamplerState s, float3 c, float3 off, bool denorm, float level) { uint3 d; t.GetDimensions(d.x,d.y,d.z); c = denorm ? (c+off)/float3(d) : c+off/float3(d); return t.SampleLevel(s,c,level); }
float4 nativeRawFetchGrad(Texture3D<float4> t, SamplerState s, float3 c, float3 off, bool denorm, float3 gx, float3 gy) { uint3 d; t.GetDimensions(d.x,d.y,d.z); c = denorm ? (c+off)/float3(d) : c+off/float3(d); if(denorm) { gx.xyz /= float3(d); gy.xyz /= float3(d); } return t.SampleGrad(s,c,gx,gy); }
float4 nativeLod(Texture3D<float4> t, SamplerState s, float3 c, float3 off, bool denorm) { uint3 d; t.GetDimensions(d.x,d.y,d.z); c = denorm ? (c+off)/float3(d) : c+off/float3(d); return float4(t.CalculateLevelOfDetailUnclamped(s,c),0,0,0); }
float4 nativeWeights(Texture3D<float4> t, SamplerState s, float3 c, float3 off, bool denorm) { uint3 d; t.GetDimensions(d.x,d.y,d.z); c = denorm ? (c+off)/float3(d) : c+off/float3(d); return float4(frac(c*float3(d)-0.5),0); }
float4 nativeRawFetch(TextureCube<float4> t, SamplerState s, float3 c, float bias) { return t.SampleBias(s,c,bias); }
float4 nativeRawFetchLevel(TextureCube<float4> t, SamplerState s, float3 c, float level) { return t.SampleLevel(s,c,level); }
float4 nativeRawFetchGrad(TextureCube<float4> t, SamplerState s, float3 c, float3 gx, float3 gy) { return t.SampleGrad(s,c,gx,gy); }
float4 nativeLod(TextureCube<float4> t, SamplerState s, float3 c) { return float4(t.CalculateLevelOfDetailUnclamped(s,c),0,0,0); }

// Select the UBO component explicitly: uam's dynamic UBO component reads .x.
uint mkvFetchSlot(uint slot) {
    uint4 v=asuint(g_UboCompartidas.v[31+slot/4]);
    return select((slot&3)==0,v.x,select((slot&3)==1,v.y,select((slot&3)==2,v.z,v.w)));
}
float4 mkvBoundFetch(uint4 e, float index, bool rounded) {
    float4 value=vfetchMemoryImpl(mkvFetchSlot(e.x),index,rounded,int(e.y),e.z,e.w&63,
        (e.w&256)!=0,(e.w&512)!=0,(e.w&(1u<<22))!=0,int(e.w<<3)>>26,false,false,0);
    return remapInput(value,(e.w>>10)&4095);
}

// Exact Xenos PWL gamma conversion, matching ReXGlue's SPIR-V translator.
float mkvGamma(float g) {
    g=saturate(g);
    precise float scale=select(g>=192.0/255.0,8.0/1024.0,select(g>=96.0/255.0,4.0/1024.0,select(g>=64.0/255.0,2.0/1024.0,1.0/1024.0)));
    precise float offset=select(g>=192.0/255.0,-1024.0,select(g>=96.0/255.0,-256.0,select(g>=64.0/255.0,-64.0,0.0)));
    precise float linearValue=(g*(255.0*1024.0))*scale+offset;
    linearValue+=trunc(linearValue*scale);
    return linearValue*(1.0/1023.0);
}
float4 mkvAdjustTexture(float4 u,float4 s,uint control) {
    float4 result;
    [unroll] for(uint lane=0;lane<4;lane++) {
        uint sign=(control>>(lane*2))&3;
        result[lane]=select(sign==1,s[lane],select(sign==2,u[lane]*2.0-1.0,select(sign==3,mkvGamma(u[lane]),u[lane])));
    }
    return result*exp2(float(int(control<<18)>>26))*select((control&(1u<<14))!=0,2.0,1.0);
}
float4 nativeFetch(Texture2D<float4> t, Texture2D<float4> st, SamplerState s, float2 c, float2 off, bool denorm, float bias, uint control) {
uint signs=control&255; bool any_signed=((signs&85)&~(signs>>1))!=0;
float4 u=0, v=0; [branch] if(signs!=85) u=nativeRawFetch(t, s, c, off, denorm, bias);
[branch] if(any_signed) v=nativeRawFetch(st, s, c, off, denorm, bias);
return mkvAdjustTexture(u,v,control); }
float4 nativeFetchLevel(Texture2D<float4> t, Texture2D<float4> st, SamplerState s, float2 c, float2 off, bool denorm, float level, uint control) {
uint signs=control&255; bool any_signed=((signs&85)&~(signs>>1))!=0;
float4 u=0, v=0; [branch] if(signs!=85) u=nativeRawFetchLevel(t, s, c, off, denorm, level);
[branch] if(any_signed) v=nativeRawFetchLevel(st, s, c, off, denorm, level);
return mkvAdjustTexture(u,v,control); }
float4 nativeFetchGrad(Texture2D<float4> t, Texture2D<float4> st, SamplerState s, float2 c, float2 off, bool denorm, float3 gx, float3 gy, uint control) {
uint signs=control&255; bool any_signed=((signs&85)&~(signs>>1))!=0;
float4 u=0, v=0; [branch] if(signs!=85) u=nativeRawFetchGrad(t, s, c, off, denorm, gx, gy);
[branch] if(any_signed) v=nativeRawFetchGrad(st, s, c, off, denorm, gx, gy);
return mkvAdjustTexture(u,v,control); }
float4 nativeFetch(Texture3D<float4> t, Texture3D<float4> st, SamplerState s, float3 c, float3 off, bool denorm, float bias, uint control) {
uint signs=control&255; bool any_signed=((signs&85)&~(signs>>1))!=0;
float4 u=0, v=0; [branch] if(signs!=85) u=nativeRawFetch(t, s, c, off, denorm, bias);
[branch] if(any_signed) v=nativeRawFetch(st, s, c, off, denorm, bias);
return mkvAdjustTexture(u,v,control); }
float4 nativeFetchLevel(Texture3D<float4> t, Texture3D<float4> st, SamplerState s, float3 c, float3 off, bool denorm, float level, uint control) {
uint signs=control&255; bool any_signed=((signs&85)&~(signs>>1))!=0;
float4 u=0, v=0; [branch] if(signs!=85) u=nativeRawFetchLevel(t, s, c, off, denorm, level);
[branch] if(any_signed) v=nativeRawFetchLevel(st, s, c, off, denorm, level);
return mkvAdjustTexture(u,v,control); }
float4 nativeFetchGrad(Texture3D<float4> t, Texture3D<float4> st, SamplerState s, float3 c, float3 off, bool denorm, float3 gx, float3 gy, uint control) {
uint signs=control&255; bool any_signed=((signs&85)&~(signs>>1))!=0;
float4 u=0, v=0; [branch] if(signs!=85) u=nativeRawFetchGrad(t, s, c, off, denorm, gx, gy);
[branch] if(any_signed) v=nativeRawFetchGrad(st, s, c, off, denorm, gx, gy);
return mkvAdjustTexture(u,v,control); }
float4 nativeFetch(TextureCube<float4> t, TextureCube<float4> st, SamplerState s, float3 c, float bias, uint control) {
uint signs=control&255; bool any_signed=((signs&85)&~(signs>>1))!=0;
float4 u=0, v=0; [branch] if(signs!=85) u=nativeRawFetch(t, s, c, bias);
[branch] if(any_signed) v=nativeRawFetch(st, s, c, bias);
return mkvAdjustTexture(u,v,control); }
float4 nativeFetchLevel(TextureCube<float4> t, TextureCube<float4> st, SamplerState s, float3 c, float level, uint control) {
uint signs=control&255; bool any_signed=((signs&85)&~(signs>>1))!=0;
float4 u=0, v=0; [branch] if(signs!=85) u=nativeRawFetchLevel(t, s, c, level);
[branch] if(any_signed) v=nativeRawFetchLevel(st, s, c, level);
return mkvAdjustTexture(u,v,control); }
float4 nativeFetchGrad(TextureCube<float4> t, TextureCube<float4> st, SamplerState s, float3 c, float3 gx, float3 gy, uint control) {
uint signs=control&255; bool any_signed=((signs&85)&~(signs>>1))!=0;
float4 u=0, v=0; [branch] if(signs!=85) u=nativeRawFetchGrad(t, s, c, gx, gy);
[branch] if(any_signed) v=nativeRawFetchGrad(st, s, c, gx, gy);
return mkvAdjustTexture(u,v,control); }

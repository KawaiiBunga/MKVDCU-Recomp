#include "shader_recompiler.h"
#include "shader_common.h"
#include <stdexcept>

static constexpr char SWIZZLES[] = 
{ 
    'x',
    'y', 
    'z', 
    'w', 
    '0', 
    '1',
    '_',
    '_'
};

static constexpr const char* USAGE_TYPES[] =
{
    "float4", // POSITION
    "float4", // BLENDWEIGHT
    "uint4", // BLENDINDICES
#ifdef NFSMW_RECOMP
    "float4", // NORMAL: NFSMW stores them as 16-bit integers or as float
#else
    "uint4", // NORMAL
#endif
    "float4", // PSIZE
    "float4", // TEXCOORD
#ifdef NFSMW_RECOMP
    "float4", // TANGENT
    "float4", // BINORMAL
#else
    "uint4", // TANGENT
    "uint4", // BINORMAL
#endif
    "float4", // TESSFACTOR
    "float4", // POSITIONT
    "float4", // COLOR
    "float4", // FOG
    "float4", // DEPTH
    "float4", // SAMPLE
};

static constexpr const char* USAGE_VARIABLES[] =
{
    "Position",
    "BlendWeight",
    "BlendIndices",
    "Normal",
    "PointSize",
    "TexCoord",
    "Tangent",
    "Binormal",
    "TessFactor",
    "PositionT",
    "Color",
    "Fog",
    "Depth",
    "Sample"
};

static constexpr const char* USAGE_SEMANTICS[] =
{
    "POSITION",
    "BLENDWEIGHT",
    "BLENDINDICES",
    "NORMAL",
    "PSIZE",
    "TEXCOORD",
    "TANGENT",
    "BINORMAL",
    "TESSFACTOR",
    "POSITIONT",
    "COLOR",
    "FOG",
    "DEPTH",
    "SAMPLE"
};

struct DeclUsageLocation
{
    DeclUsage usage;
    uint32_t usageIndex;
    uint32_t location;
};

// NOTE: These are specialized Vulkan locations for Unleashed Recompiled. Change as necessary. Likely not going to work with other games.
static constexpr DeclUsageLocation USAGE_LOCATIONS[] =
{
    { DeclUsage::Position, 0, 0 },
    { DeclUsage::Normal, 0, 1 },
    { DeclUsage::Tangent, 0, 2 },
    { DeclUsage::Binormal, 0, 3 },
    { DeclUsage::TexCoord, 0, 4 },
    { DeclUsage::TexCoord, 1, 5 },
    { DeclUsage::TexCoord, 2, 6 },
    { DeclUsage::TexCoord, 3, 7 },
    { DeclUsage::Color, 0, 8 },
    { DeclUsage::BlendIndices, 0, 9 },
    { DeclUsage::BlendWeight, 0, 10 },
    { DeclUsage::Color, 1, 11 },
    { DeclUsage::TexCoord, 4, 12 },
    { DeclUsage::TexCoord, 5, 13 },
    { DeclUsage::TexCoord, 6, 14 },
    { DeclUsage::TexCoord, 7, 15 },
    { DeclUsage::Position, 1, 15 },
};

static constexpr std::pair<DeclUsage, size_t> INTERPOLATORS[] =
{
    { DeclUsage::TexCoord, 0 },
    { DeclUsage::TexCoord, 1 },
    { DeclUsage::TexCoord, 2 },
    { DeclUsage::TexCoord, 3 },
    { DeclUsage::TexCoord, 4 },
    { DeclUsage::TexCoord, 5 },
    { DeclUsage::TexCoord, 6 },
    { DeclUsage::TexCoord, 7 },
    { DeclUsage::TexCoord, 8 },
    { DeclUsage::TexCoord, 9 },
    { DeclUsage::TexCoord, 10 },
    { DeclUsage::TexCoord, 11 },
    { DeclUsage::TexCoord, 12 },
    { DeclUsage::TexCoord, 13 },
    { DeclUsage::TexCoord, 14 },
    { DeclUsage::TexCoord, 15 },
    { DeclUsage::Color, 0 },
    { DeclUsage::Color, 1 }
#ifdef NFSMW_RECOMP
    // NFSMW declares and consumes COLOR2..5 in the original 2005 shaders.
    , { DeclUsage::Color, 2 }
    , { DeclUsage::Color, 3 }
    , { DeclUsage::Color, 4 }
    , { DeclUsage::Color, 5 }
#endif
};

static constexpr std::string_view TEXTURE_DIMENSIONS[] = 
{
    "2D",
    "3D", 
    "Cube" 
};

static FetchDestinationSwizzle getDestSwizzle(uint32_t dstSwizzle, uint32_t index)
{
    return FetchDestinationSwizzle((dstSwizzle >> (index * 3)) & 0x7);
}

void ShaderRecompiler::printDstSwizzle(uint32_t dstSwizzle, bool operand)
{
    for (size_t i = 0; i < 4; i++)
    {
        const auto swizzle = getDestSwizzle(dstSwizzle, i);
        if (swizzle >= FetchDestinationSwizzle::X && swizzle <= FetchDestinationSwizzle::W)
            out += SWIZZLES[operand ? uint32_t(swizzle) : i];
    }
}

void ShaderRecompiler::printDstSwizzle01(uint32_t dstRegister, uint32_t dstSwizzle)
{
    for (size_t i = 0; i < 4; i++)
    {
        const auto swizzle = getDestSwizzle(dstSwizzle, i);
        if (swizzle == FetchDestinationSwizzle::Zero)
        {
            indent();
            println("r{}.{} = 0.0;", dstRegister, SWIZZLES[i]);
        }
        else if (swizzle == FetchDestinationSwizzle::One)
        {
            indent();
            println("r{}.{} = 1.0;", dstRegister, SWIZZLES[i]);
        }
    }
}

void ShaderRecompiler::recompile(const VertexFetchInstruction& instr, uint32_t address)
{
    if (instr.isPredicated) abrirPredicado(instr.predicateCondition); else cerrarPredicado();
    if (!instr.isMiniFetch) {
        lastFullFetch = instr;
        lastFullOtherIndex=instr.srcRegister!=0 || instr.srcSwizzle!=0 || vertexIndexOverwritten;
        indent(); println("vfetchIndex = r{}.{};",instr.srcRegister,SWIZZLES[instr.srcSwizzle]);
    }
    if(instr.dstRegister==0 && getDestSwizzle(instr.dstSwizzle,0)<=FetchDestinationSwizzle::One)
        vertexIndexOverwritten=true;
    if (auto found=vertexElements.find(address); found!=vertexElements.end() &&
        (lastFullOtherIndex || vertexSemanticCounts[(uint32_t(found->second.usage)<<8)|found->second.usageIndex]>1 ||
         instr.expAdjust || instr.signedRfModeAll)) {
        const uint32_t element=vertexElementIndices.at(address);
        if(element>=32) throw std::runtime_error("More than 32 bound vertex fetches");
        boundFetchMask|=1u<<element;
        indent(); print("r{}.",instr.dstRegister); printDstSwizzle(instr.dstSwizzle,false);
        print(" = mkvBoundFetch(g_BoundFetch({}), vfetchIndex, {}).",element,lastFullFetch.isIndexRounded ? "true" : "false");
        printDstSwizzle(instr.dstSwizzle,true); out += ";\n";
        printDstSwizzle01(instr.dstRegister,instr.dstSwizzle);
        return;
    }
    if (!vertexElements.contains(address)) {
        const auto& full = lastFullFetch;
        const uint32_t slot = full.constIndex * 3 + full.constIndexSelect;
        if (slot >= 96) throw std::runtime_error("Vertex fetch slot outside register file");
        rawFetchMask[slot / 32] |= 1u << (slot % 32);
        if (instr.isPredicated) abrirPredicado(instr.predicateCondition); else cerrarPredicado();
        std::string swizzle;
        for (size_t i = 0; i < 4; ++i)
            if (getDestSwizzle(instr.dstSwizzle,i) <= FetchDestinationSwizzle::W)
                swizzle += SWIZZLES[uint32_t(getDestSwizzle(instr.dstSwizzle,i))];
        if (!swizzle.empty()) {
            indent(); print("r{}.",instr.dstRegister); printDstSwizzle(instr.dstSwizzle,false);
            println(" = vfetchMemory(g_VertexFetchSlot({}), vfetchIndex, {}, {}, {}, {}, {}, {}, {}, {}).{};",
                slot,full.isIndexRounded ? "true" : "false",
                int32_t(instr.offset),uint32_t(full.stride),uint32_t(instr.format),
                instr.formatCompAll ? "true" : "false",instr.numFormatAll ? "true" : "false",
                instr.signedRfModeAll ? "true" : "false",int32_t(instr.expAdjust),swizzle);
        }
        printDstSwizzle01(instr.dstRegister,instr.dstSwizzle);
        return;
    }
    if (instr.isPredicated)
        abrirPredicado(instr.predicateCondition);
    else
        cerrarPredicado();
    const size_t marcaPredicado = out.size();

    indent();
    print("r{}.", instr.dstRegister);
    printDstSwizzle(instr.dstSwizzle, false);

    out += " = ";

    auto findResult = vertexElements.find(address);
    if (findResult == vertexElements.end())
        throw std::runtime_error(fmt::format("FETCH without declaration: address {} slot {} stride {} format {} source r{}.{}",address,instr.constIndex*3+instr.constIndexSelect,instr.stride,uint32_t(instr.format),instr.srcRegister,instr.srcSwizzle));

#ifdef NFSMW_RECOMP
    // NFSMW: D3D patches the fetch swizzle according to the vertex declaration; the
    // native renderer passes the mapping through g_InputRemap.
    int32_t remapLocation = -1;
    for (auto& usageLocation : USAGE_LOCATIONS)
    {
        if (usageLocation.usage == findResult->second.usage && usageLocation.usageIndex == findResult->second.usageIndex)
        {
            remapLocation = int32_t(usageLocation.location);
            break;
        }
    }
    if (remapLocation >= 0)
        out += "remapInput(float4(";
#endif

    switch (findResult->second.usage)
    {
    // NFSMW declares normals, tangents and binormals as float4 (USAGE_TYPES): the
    // native renderer supplies them already normalized through the Vulkan format.
#ifndef NFSMW_RECOMP
    case DeclUsage::Normal:
    case DeclUsage::Tangent:
    case DeclUsage::Binormal:
        specConstantsMask |= SPEC_CONSTANT_R11G11B10_NORMAL;
        print("tfetchR11G11B10(");
        break;
#endif

    case DeclUsage::TexCoord:
        print("tfetchTexcoord(g_SwappedTexcoords, ");
        break;
    }

    print("i{}{}", USAGE_VARIABLES[uint32_t(findResult->second.usage)], uint32_t(findResult->second.usageIndex));

    switch (findResult->second.usage)
    {
#ifndef NFSMW_RECOMP
    case DeclUsage::Normal:
    case DeclUsage::Tangent:
    case DeclUsage::Binormal:
        out += ')';
        break;
#endif

    case DeclUsage::TexCoord:
        print(", {})", uint32_t(findResult->second.usageIndex));
        break;
    }

#ifdef NFSMW_RECOMP
    if (remapLocation >= 0)
        print("), g_InputRemap({}))", remapLocation);
#endif

    out += '.';
    printDstSwizzle(instr.dstSwizzle, true);

    out += ";\n";

    printDstSwizzle01(instr.dstRegister, instr.dstSwizzle);

    cerrarSiEscribePredicado(marcaPredicado);
}

void ShaderRecompiler::recompile(const TextureFetchInstruction& instr, bool bicubic)
{
    (void)bicubic;
    if (instr.isPredicated) abrirPredicado(instr.predCondition); else cerrarPredicado();
    const auto source = [&](uint32_t components) {
        std::string r = fmt::format("r{}.",instr.srcRegister);
        for (uint32_t i=0;i<components;++i) r += SWIZZLES[(instr.srcSwizzle>>(i*2))&3];
        return r;
    };
    if (instr.opcode == FetchOpcode::SetTextureLod) { indent(); println("lod = {};",source(1)); return; }
    if (instr.opcode == FetchOpcode::GetTextureGradients) {
        if (!isPixelShader) throw std::runtime_error("Derivatives requested by a vertex shader");
        const auto operand=source(2);
        indent(); print("r{}.",instr.dstRegister); printDstSwizzle(instr.dstSwizzle,false);
        print(" = float4(ddx_coarse(({}).x), ddy_coarse(({}).x), ddx_coarse(({}).y), ddy_coarse(({}).y)).",operand,operand,operand,operand);
        printDstSwizzle(instr.dstSwizzle,true); out += ";\n";
        printDstSwizzle01(instr.dstRegister,instr.dstSwizzle); return;
    }
    if (instr.opcode == FetchOpcode::SetTextureGradientsHorz || instr.opcode == FetchOpcode::SetTextureGradientsVert) {
        indent(); println("{} = {};",instr.opcode == FetchOpcode::SetTextureGradientsHorz ? "gradX" : "gradY",source(3)); return;
    }
    if (instr.opcode != FetchOpcode::TextureFetch && instr.opcode != FetchOpcode::GetTextureWeights &&
        instr.opcode != FetchOpcode::GetTextureComputedLod)
        throw std::runtime_error(fmt::format("Unsupported texture opcode {}",uint32_t(instr.opcode)));
    const uint32_t dim=instr.dimension == TextureDimension::Texture1D ? 1u : uint32_t(instr.dimension);
    const uint32_t description=0x80000000u|instr.constIndex|(dim<<5)|(instr.magFilter<<7)|(instr.minFilter<<9)|
        (instr.mipFilter<<11)|(instr.anisoFilter<<13)|(instr.volMagFilter<<16)|(instr.volMinFilter<<18);
    auto [binding,added]=nativeTextures.try_emplace(description,uint32_t(nativeTextures.size()));
    if (binding->second>=16) throw std::runtime_error("More than 16 dual-view texture/sampler pairs");
    textureFetch[binding->second*2]=description;
    textureFetch[binding->second*2+1]=description|0x40000000u;
    const char* dimension = instr.dimension == TextureDimension::Texture3D ? "3D" :
        instr.dimension == TextureDimension::TextureCube ? "Cube" : "2D";
    std::string coordinate = source(instr.dimension == TextureDimension::Texture1D ? 1 :
        instr.dimension == TextureDimension::Texture2D ? 2 : 3);
    if (instr.dimension == TextureDimension::Texture1D) coordinate = "float2("+coordinate+",0.5)";
    if (instr.dimension == TextureDimension::TextureCube) coordinate = "cubeMapData.cubeMapDirections[uint(("+coordinate+").z)]";
    const auto offset = instr.dimension == TextureDimension::Texture3D
        ? fmt::format("float3({},{},{})",instr.offsetX*0.5f,instr.offsetY*0.5f,instr.offsetZ*0.5f)
        : fmt::format("float2({},{})",instr.offsetX*0.5f,instr.dimension == TextureDimension::Texture1D ? 0.0f : instr.offsetY*0.5f);
    const bool explicit_lod = !isPixelShader || instr.useRegLod || !instr.useCompLod;
    const char* operation = instr.opcode == FetchOpcode::GetTextureWeights ? "nativeWeights" :
        instr.opcode == FetchOpcode::GetTextureComputedLod ? "nativeLod" :
        instr.useRegGradients ? "nativeFetchGrad" : explicit_lod ? "nativeFetchLevel" : "nativeFetch";
    indent(); print("r{}.",instr.dstRegister); printDstSwizzle(instr.dstSwizzle,false);
    print(" = {}(mkvTexture{}, ",operation,binding->second);
    if(instr.opcode==FetchOpcode::TextureFetch) print("mkvSignedTexture{}, ",binding->second);
    print("mkvSampler{}, {}",binding->second,coordinate);
    if (instr.dimension != TextureDimension::TextureCube) print(", {}, {}",offset,instr.texCoordDenorm ? "true" : "false");
    if (instr.opcode == FetchOpcode::TextureFetch) {
        if (instr.useRegGradients) out += ", gradX, gradY";
        else if (explicit_lod) print(", {} + {}",instr.useRegLod ? "lod" : "0.0",instr.lodBias/16.0f);
        else print(", {}",instr.lodBias/16.0f);
        print(", NFSMW_COMPARTIDA_UINT({})",(isPixelShader ? 1120 : 992)+binding->second*4);
    }
    out += ")."; printDstSwizzle(instr.dstSwizzle,true); out += ";\n";
    printDstSwizzle01(instr.dstRegister,instr.dstSwizzle);
}

void ShaderRecompiler::recompile(const AluInstruction& instr)
{
    if(!isPixelShader && !instr.exportData &&
       ((instr.vectorDest==0 && !instr.vectorDestRelative && (instr.vectorWriteMask&1)) ||
        (instr.scalarDest==0 && !instr.scalarDestRelative && (instr.scalarWriteMask&1))))
        vertexIndexOverwritten=true;
    if (instr.isPredicated)
        abrirPredicado(instr.predicateCondition);
    else
        cerrarPredicado();
    const size_t marcaPredicado = out.size();

    enum
    {
        VECTOR_0,
        VECTOR_1,
        VECTOR_2,
        SCALAR_0,
        SCALAR_1,
        SCALAR_CONSTANT_0,
        SCALAR_CONSTANT_1
    };

    auto op = [&](size_t operand)
        {
            size_t reg = 0;
            size_t swizzle = 0;
            bool select = true;
            bool negate = false;
            bool abs = false;

            switch (operand)
            {
            case SCALAR_CONSTANT_0:
                reg = instr.src3Register;
                swizzle = instr.src3Swizzle;
                select = false;
                negate = instr.src3Negate;
                abs = instr.absConstants;
                break;

            case SCALAR_CONSTANT_1:
                reg = (uint32_t(instr.scalarOpcode) & 1) | (instr.src3Select << 1) | (instr.src3Swizzle & 0x3C);
                swizzle = instr.src3Swizzle;
                select = true;
                negate = instr.src3Negate;
                abs = instr.absConstants;
                break;

            default:
                switch (operand)
                {
                case VECTOR_0:
                    reg = instr.src1Register;
                    swizzle = instr.src1Swizzle;
                    select = instr.src1Select;
                    negate = instr.src1Negate;
                    break;
                case VECTOR_1:
                    reg = instr.src2Register;
                    swizzle = instr.src2Swizzle;
                    select = instr.src2Select;
                    negate = instr.src2Negate;
                    break;
                case VECTOR_2:
                case SCALAR_0:
                case SCALAR_1:
                    reg = instr.src3Register;
                    swizzle = instr.src3Swizzle;
                    select = instr.src3Select;
                    negate = instr.src3Negate;
                    break;
                }

                if (select)
                {
                    abs = (reg & 0x80) != 0;
                    reg &= 0x3F;
                }
                else
                {
                    abs = instr.absConstants;
                }

                break;
            }

            std::string regFormatted;

            if (select)
            {
                regFormatted = fmt::format("r{}", reg);
            }
            else
            {
                // The bits correspond to the first constant and the following ones,
                // not to the whole instruction. Temporary operands do not count.
                const bool relativa = operand == VECTOR_0 ? instr.const0Relative :
                    operand == VECTOR_1 ? (instr.src1Select ? instr.const0Relative : instr.const1Relative) :
                    (instr.src1Select && instr.src2Select ? instr.const0Relative : instr.const1Relative);
                auto findResult = float4Constants.find(reg);
                if (findResult != float4Constants.end())
                {
                    const char* constantName = reinterpret_cast<const char*>(constantTableData + findResult->second->name);
                    if (findResult->second->registerCount > 1)
                    {
                    #ifdef UNLEASHED_RECOMP
                        if (hasMtxProjection && strcmp(constantName, "g_MtxProjection") == 0)
                        {
                            regFormatted = fmt::format("(iterationIndex == 0 ? mtxProjectionReverseZ[{0}] : mtxProjection[{0}])",
                                reg - findResult->second->registerIndex);
                        }
                        else
                    #endif
                        {
                            regFormatted = fmt::format("{}({}{})", constantName,
                                reg - findResult->second->registerIndex, relativa ? (instr.constAddressRegisterRelative ? " + a0" : " + aL") : "");
                        }
                    }
                    else
                    {
                        if (relativa)
                            throw std::runtime_error("direccionamiento relativo de constante escalar pendiente");
                        regFormatted = constantName;
                    }
                }
                else
                {
                    if (relativa)
                        throw std::runtime_error("direccionamiento relativo sin constante declarada pendiente");
                    regFormatted = literalRegisters.contains(uint32_t(reg)) ? fmt::format("c{}",reg)
                        : fmt::format("{}.v[{}]",isPixelShader ? "g_UboPixel" : "g_UboVertex",reg);
                }
            }

            std::string result;

            if (negate)
                result += '-';

            if (abs)
                result += "abs(";

            result += regFormatted;
            result += '.';

            switch (operand)
            {
            case VECTOR_0:
            case VECTOR_1:
            case VECTOR_2:
            {
                uint32_t mask;

                switch (instr.vectorOpcode)
                {
                case AluVectorOpcode::Dp2Add:
                    mask = (operand == VECTOR_2) ? 0b1 : 0b11;
                    break;

                case AluVectorOpcode::Dp3:
                    mask = 0b111;
                    break;

                case AluVectorOpcode::Dp4:
                case AluVectorOpcode::Max4:
                    mask = 0b1111;
                    break;

                default:
                    mask = instr.vectorWriteMask != 0 ? instr.vectorWriteMask : 0b1;
                    break;
                }

                for (size_t i = 0; i < 4; i++)
                {
                    if ((mask >> i) & 0x1)
                        result += SWIZZLES[((swizzle >> (i * 2)) + i) & 0x3];
                }

                break;
            }

            case SCALAR_0:
            case SCALAR_CONSTANT_0:
                result += SWIZZLES[((swizzle >> 6) + 3) & 0x3];
                break;

            case SCALAR_1:
            case SCALAR_CONSTANT_1:
                result += SWIZZLES[swizzle & 0x3];
                break;
            }

            if (abs)
                result += ")";

            return result;
        };

    switch (instr.vectorOpcode)
    {
    case AluVectorOpcode::KillEq:
        indent();
        println("clip(any({} == {}) ? -1 : 1);", op(VECTOR_0), op(VECTOR_1));
        break;
    
    case AluVectorOpcode::KillGt:
        indent();
        println("clip(any({} > {}) ? -1 : 1);", op(VECTOR_0), op(VECTOR_1));
        break;
    
    case AluVectorOpcode::KillGe:
        indent();
        println("clip(any({} >= {}) ? -1 : 1);", op(VECTOR_0), op(VECTOR_1));
        break;
    
    case AluVectorOpcode::KillNe:
        indent();
        println("clip(any({} != {}) ? -1 : 1);", op(VECTOR_0), op(VECTOR_1));
        break;
    }

    bool closeIfBracket = false;

    std::string_view exportRegister;
    if (instr.exportData)
    {
        if (isPixelShader)
        {
            switch (ExportRegister(instr.vectorDest))
            {
            case ExportRegister::PSColor0:
                exportRegister = "oC0";
                break;        
            case ExportRegister::PSColor1:
                exportRegister = "oC1";
                break;        
            case ExportRegister::PSColor2:
                exportRegister = "oC2";
                break;            
            case ExportRegister::PSColor3:
                exportRegister = "oC3";
                break;           
            case ExportRegister::PSDepth:
                exportRegister = "oDepth";
                break;
            }
        }
        else
        {
            switch (ExportRegister(instr.vectorDest))
            {
            case ExportRegister::VSPosition:
                exportRegister = "oPos";

            #ifdef UNLEASHED_RECOMP
                if (hasMtxProjection)
                {
                    indent();
                    out += "if ((g_SpecConstants() & SPEC_CONSTANT_REVERSE_Z) == 0 || iterationIndex == 0)\n";
                    indent();
                    out += "{\n";
                    ++indentation;

                    closeIfBracket = true;
                }
            #endif

                break;

            case ExportRegister::ExportAddress:
                exportRegister = "eA";
                if (instr.vectorOpcode == AluVectorOpcode::Mad && !instr.src3Select)
                    memExportConstants[instr.src3Register / 32] |= 1u << (instr.src3Register % 32);
                break;
            case ExportRegister::ExportData0:
            case ExportRegister::ExportData1:
            case ExportRegister::ExportData2:
            case ExportRegister::ExportData3:
            case ExportRegister::ExportData4:
                {
                    static constexpr std::string_view names[] = {"eM0","eM1","eM2","eM3","eM4"};
                    exportRegister = names[uint32_t(instr.vectorDest)-33];
                }
                hasMemExport = true;
                break;
            default:
            {
                auto findResult = interpolators.find(instr.vectorDest);
                if (findResult == interpolators.end())
                    throw std::runtime_error(fmt::format("VS export without declared interpolator: {}",instr.vectorDest));
                exportRegister = findResult->second;
                break;
            }
            }
        }
    }

    if (instr.vectorOpcode >= AluVectorOpcode::SetpEqPush && instr.vectorOpcode <= AluVectorOpcode::SetpGePush)
    {
        indent();
        print("p0 = {} == 0.0 && {} ", op(VECTOR_0), op(VECTOR_1));

        switch (instr.vectorOpcode)
        {
        case AluVectorOpcode::SetpEqPush:
            out += "==";
            break;
        case AluVectorOpcode::SetpNePush:
            out += "!=";
            break;
        case AluVectorOpcode::SetpGtPush:
            out += ">";
            break;
        case AluVectorOpcode::SetpGePush:
            out += ">=";
            break;
        }

        out += " 0.0;\n";
    }
    else if (instr.vectorOpcode >= AluVectorOpcode::MaxA)
    {
        indent();
        println("a0 = (int)clamp(floor(({}).w + 0.5), -256.0, 255.0);", op(VECTOR_0));
    }

    uint32_t vectorWriteMask = instr.vectorWriteMask;
    if (instr.exportData)
        vectorWriteMask &= ~instr.scalarWriteMask;

    if (vectorWriteMask != 0)
    {
        indent();
        if (!exportRegister.empty())
        {
            out += exportRegister;
            out += '.';
        }
        else
        {
            print("r{}.", instr.vectorDest);
        }

        for (size_t i = 0; i < 4; i++)
        {
            if ((vectorWriteMask >> i) & 0x1)
                out += SWIZZLES[i];
        }

        out += " = ";

        if (instr.vectorSaturate)
            out += "saturate(";

        switch (instr.vectorOpcode)
        {
        case AluVectorOpcode::Add:
            print("{} + {}", op(VECTOR_0), op(VECTOR_1));
            break;

        case AluVectorOpcode::Mul:
            print("{} * {}", op(VECTOR_0), op(VECTOR_1));
            break;

        /*
         * The Xenos MOV is a max with itself.
         *
         * The Xenos has no copy instruction: the assembler writes it as MAX dst, src, src
         * (and MAXs for the scalar channel). The translator emitted it as is, so the library
         * had 452 `max(a, a)` in the pixel shaders and 482 in the vertex shaders: almost a
         * thousand operations that are a copy. max(a, a) == a exactly (also with NaN and with
         * both zeros, which is the only thing that sets IEEE min/max apart), so the operand is
         * emitted directly.
         *
         * The `a0 = ...` of MaxA/MaxAs/MaxAsf is not touched: it is handled separately and
         * stays the same.
         */
        case AluVectorOpcode::Max:
        case AluVectorOpcode::MaxA:
        case AluVectorOpcode::Min:
        {
            std::string a = op(VECTOR_0);
            std::string b = op(VECTOR_1);
            if (a == b)
                out += a;
            else
                print("{}({}, {})", instr.vectorOpcode == AluVectorOpcode::Min ? "min" : "max", a, b);
            break;
        }

        case AluVectorOpcode::Seq:
            print("{} == {}", op(VECTOR_0), op(VECTOR_1));
            break;

        case AluVectorOpcode::Sgt:
            print("{} > {}", op(VECTOR_0), op(VECTOR_1));
            break;

        case AluVectorOpcode::Sge:
            print("{} >= {}", op(VECTOR_0), op(VECTOR_1));
            break;

        case AluVectorOpcode::Sne:
            print("{} != {}", op(VECTOR_0), op(VECTOR_1));
            break;

        case AluVectorOpcode::Frc:
            print("frac({})", op(VECTOR_0));
            break;

        case AluVectorOpcode::Trunc:
            print("trunc({})", op(VECTOR_0));
            break;

        case AluVectorOpcode::Floor:
            print("floor({})", op(VECTOR_0));
            break;

        case AluVectorOpcode::Mad:
            print("{} * {} + {}", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
            break;

        case AluVectorOpcode::CndEq:
            print("select({} == 0.0, {}, {})", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
            break;

        case AluVectorOpcode::CndGe:
            print("select({} >= 0.0, {}, {})", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
            break;

        case AluVectorOpcode::CndGt:
            print("select({} > 0.0, {}, {})", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
            break;

        case AluVectorOpcode::Dp4:
        case AluVectorOpcode::Dp3:
            print("dot({}, {})", op(VECTOR_0), op(VECTOR_1));
            break;

        case AluVectorOpcode::Dp2Add:
            print("dot({}, {}) + {}", op(VECTOR_0), op(VECTOR_1), op(VECTOR_2));
            break;

        case AluVectorOpcode::Cube:
            print("cube(r{}, cubeMapData)", instr.src1Register);
            break;

        case AluVectorOpcode::Max4:
            print("max4({})", op(VECTOR_0));
            break;

        case AluVectorOpcode::SetpEqPush:
        case AluVectorOpcode::SetpNePush:
        case AluVectorOpcode::SetpGtPush:
        case AluVectorOpcode::SetpGePush:
            print("p0 ? 0.0 : {} + 1.0", op(VECTOR_0));
            break;

        case AluVectorOpcode::KillEq:
            print("any({} == {})", op(VECTOR_0), op(VECTOR_1));
            break;

        case AluVectorOpcode::KillGt:
            print("any({} > {})", op(VECTOR_0), op(VECTOR_1));
            break;

        case AluVectorOpcode::KillGe:
            print("any({} >= {})", op(VECTOR_0), op(VECTOR_1));
            break;

        case AluVectorOpcode::KillNe:
            print("any({} != {})", op(VECTOR_0), op(VECTOR_1));
            break;

        case AluVectorOpcode::Dst:
            print("dst({}, {})", op(VECTOR_0), op(VECTOR_1));
            break;
        }

        if (instr.vectorSaturate)
            out += ')';

        out += ";\n";
    }

    if (instr.scalarOpcode != AluScalarOpcode::RetainPrev)
    {
        if (instr.scalarOpcode >= AluScalarOpcode::SetpEq && instr.scalarOpcode <= AluScalarOpcode::SetpRstr)
        {
            indent();
            out += "p0 = ";

            switch (instr.scalarOpcode)
            {
            case AluScalarOpcode::SetpEq:
                print("{} == 0.0", op(SCALAR_0));
                break;

            case AluScalarOpcode::SetpNe:
                print("{} != 0.0", op(SCALAR_0));
                break;

            case AluScalarOpcode::SetpGt:
                print("{} > 0.0", op(SCALAR_0));
                break;

            case AluScalarOpcode::SetpGe:
                print("{} >= 0.0", op(SCALAR_0));
                break;

            case AluScalarOpcode::SetpInv:
                print("{} == 1.0", op(SCALAR_0));
                break;

            case AluScalarOpcode::SetpPop:
                print("{} - 1.0 <= 0.0", op(SCALAR_0));
                break;

            case AluScalarOpcode::SetpClr:
                out += "false";
                break;

            case AluScalarOpcode::SetpRstr:
                print("{} == 0.0", op(SCALAR_0));
                break;
            }

            out += ";\n";
        }

        indent();
        out += "ps = ";
        if (instr.scalarSaturate)
            out += "saturate(";

        switch (instr.scalarOpcode)
        {
        case AluScalarOpcode::Adds:
            print("{} + {}", op(SCALAR_0), op(SCALAR_1));
            break;

        case AluScalarOpcode::AddsPrev:
            print("{} + ps", op(SCALAR_0));
            break;

        case AluScalarOpcode::Muls:
            print("{} * {}", op(SCALAR_0), op(SCALAR_1));
            break;

        case AluScalarOpcode::MulsPrev:
        case AluScalarOpcode::MulsPrev2:
            print("{} * ps", op(SCALAR_0));
            break;

        // Same as above, for the scalar channel: MAXs dst, src, src is the Xenos MOV.
        case AluScalarOpcode::Maxs:
        case AluScalarOpcode::MaxAs:
        case AluScalarOpcode::MaxAsf:
        case AluScalarOpcode::Mins:
        {
            std::string a = op(SCALAR_0);
            std::string b = op(SCALAR_1);
            if (a == b)
                out += a;
            else
                print("{}({}, {})", instr.scalarOpcode == AluScalarOpcode::Mins ? "min" : "max", a, b);
            break;
        }

        case AluScalarOpcode::Seqs:
            print("{} == 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::Sgts:
            print("{} > 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::Sges:
            print("{} >= 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::Snes:
            print("{} != 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::Frcs:
            print("frac({})", op(SCALAR_0));
            break;

        case AluScalarOpcode::Truncs:
            print("trunc({})", op(SCALAR_0));
            break;

        case AluScalarOpcode::Floors:
            print("floor({})", op(SCALAR_0));
            break;

        case AluScalarOpcode::Exp:
            print("exp2({})", op(SCALAR_0));
            break;

        case AluScalarOpcode::Logc:
        case AluScalarOpcode::Log:
            print("clamp(log2({}), FLT_MIN, FLT_MAX)", op(SCALAR_0));
            break;

        case AluScalarOpcode::Rcpc:
        case AluScalarOpcode::Rcpf:
        case AluScalarOpcode::Rcp:
            print("clamp(rcp({}), FLT_MIN, FLT_MAX)", op(SCALAR_0));
            break;

        case AluScalarOpcode::Rsqc:
        case AluScalarOpcode::Rsqf:
        case AluScalarOpcode::Rsq:
            print("clamp(rsqrt({}), FLT_MIN, FLT_MAX)", op(SCALAR_0));
            break;

        case AluScalarOpcode::Subs:
            print("{} - {}", op(SCALAR_0), op(SCALAR_1));
            break;

        case AluScalarOpcode::SubsPrev:
            print("{} - ps", op(SCALAR_0));
            break;

        case AluScalarOpcode::SetpEq:
        case AluScalarOpcode::SetpNe:
        case AluScalarOpcode::SetpGt:
        case AluScalarOpcode::SetpGe:
            out += "p0 ? 0.0 : 1.0";
            break;

        case AluScalarOpcode::SetpInv:
#ifdef NFSMW_RECOMP
            // Xenos: with src == 1 the counter goes back to 0 (Xenia's ucode.h, kSetpInv). Without that
            // case the counter stayed at 1 and later p0 blocks did not run.
            print("p0 ? 0.0 : ({0} == 0.0 ? 1.0 : {0})", op(SCALAR_0));
#else
            print("{0} == 0.0 ? 1.0 : {0}", op(SCALAR_0));
#endif
            break;

        case AluScalarOpcode::SetpPop:
            print("p0 ? 0.0 : ({} - 1.0)", op(SCALAR_0));
            break;

        case AluScalarOpcode::SetpClr:
            out += "FLT_MAX";
            break;

        case AluScalarOpcode::SetpRstr:
            print("p0 ? 0.0 : {}", op(SCALAR_0));
            break;

        case AluScalarOpcode::KillsEq:
            print("{} == 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::KillsGt:
            print("{} > 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::KillsGe:
            print("{} >= 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::KillsNe:
            print("{} != 0.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::KillsOne:
            print("{} == 1.0", op(SCALAR_0));
            break;

        case AluScalarOpcode::Sqrt:
            print("sqrt({})", op(SCALAR_0));
            break;

        case AluScalarOpcode::Mulsc0:
        case AluScalarOpcode::Mulsc1:
            print("{} * {}", op(SCALAR_CONSTANT_0), op(SCALAR_CONSTANT_1));
            break;

        case AluScalarOpcode::Addsc0:
        case AluScalarOpcode::Addsc1:
            print("{} + {}", op(SCALAR_CONSTANT_0), op(SCALAR_CONSTANT_1));
            break;

        case AluScalarOpcode::Subsc0:
        case AluScalarOpcode::Subsc1:
            print("{} - {}", op(SCALAR_CONSTANT_0), op(SCALAR_CONSTANT_1));
            break;

        case AluScalarOpcode::Sin:
            print("sin({})", op(SCALAR_0));
            break;

        case AluScalarOpcode::Cos:
            print("cos({})", op(SCALAR_0));
            break;
        }

        if (instr.scalarSaturate)
            out += ')';

        out += ";\n";

        switch (instr.scalarOpcode)
        {
        case AluScalarOpcode::MaxAs:
            indent();
            println("a0 = (int)clamp(floor({} + 0.5), -256.0, 255.0);", op(SCALAR_0));
            break;     
        case AluScalarOpcode::MaxAsf:
            indent();
            println("a0 = (int)clamp(floor({}), -256.0, 255.0);", op(SCALAR_0));
            break;
        }
    }

    uint32_t scalarWriteMask = instr.scalarWriteMask;
    if (instr.exportData)
        scalarWriteMask &= ~instr.vectorWriteMask;

    if (scalarWriteMask != 0)
    {
        indent();
        if (!exportRegister.empty())
        {
            out += exportRegister;
            out += '.';
        }
        else
        {
            print("r{}.", instr.scalarDest);
        }

        for (size_t i = 0; i < 4; i++)
        {
            if ((scalarWriteMask >> i) & 0x1)
                out += SWIZZLES[i];
        }

        out += " = ps;\n";
    }

    if (instr.exportData)
    {
        uint32_t zeroMask = instr.scalarDestRelative ? (0b1111 & ~(instr.vectorWriteMask | instr.scalarWriteMask)) : 0;
        uint32_t oneMask = instr.vectorWriteMask & instr.scalarWriteMask;

        for (size_t i = 0; i < 4; i++)
        {
            uint32_t mask = 1 << i;
            if (zeroMask & mask)
            {
                indent();
                println("{}.{} = 0.0;", exportRegister, SWIZZLES[i]);
            }
            else if (oneMask & mask)
            {
                indent();
                println("{}.{} = 1.0;", exportRegister, SWIZZLES[i]);
            }
        }
    }

    if (instr.scalarOpcode >= AluScalarOpcode::KillsEq && instr.scalarOpcode <= AluScalarOpcode::KillsOne)
    {
        indent();
        out += "clip(ps != 0.0 ? -1 : 1);\n";
    }

    if (!isPixelShader && instr.exportData && instr.vectorDest >= 33 && instr.vectorDest <= 37) {
        indent();
        println("memExport(eA, eM{0}, {0}, g_MemExportStream(0), g_MemExportStream(1), g_MemExportStream(2), g_MemExportStream(3));",uint32_t(instr.vectorDest)-33);
    }
    if (closeIfBracket)
    {
        --indentation;
        indent();
        out += "}\n";
    }

    cerrarSiEscribePredicado(marcaPredicado);
}

#ifdef NFSMW_RECOMP
void ShaderRecompiler::printDeko3dConstants(const ConstantTableContainer* constantTableContainer)
{
    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);

        switch (constantInfo->registerSet)
        {
        case RegisterSet::Float4:
        {
            const char* shaderName = isPixelShader ? "Pixel" : "Vertex";

            if (constantInfo->registerCount > 1)
            {
                uint32_t tailCount = (isPixelShader ? 224 : 256) - constantInfo->registerIndex;
                println("#define {}(INDEX) select((INDEX) < {}, g_Ubo{}.v[{} + min(INDEX, {})], 0.0)",
                    constantName, tailCount, shaderName, constantInfo->registerIndex.get(), tailCount - 1);
            }
            else
            {
                println("#define {} g_Ubo{}.v[{}]", constantName, shaderName, constantInfo->registerIndex.get());
            }
            break;
        }

        case RegisterSet::Sampler:
        {
            // One texture per register with the type the constant table gives it (as the renderer reads it:
            // nfsmw_nativo_shaders.cpp), at binding R; its sampler at binding R too (another set: GLSL's
            // combined sampler takes the texture's binding, nfsmw_dk_glsl.cpp). deko3d has 32 texture
            // bindings per stage: 0-15 for the registers and 16-31 for the pairs of the 2D ones.
            const uint32_t reg = constantInfo->registerIndex;
            const auto typeInfo = reinterpret_cast<const TypeInfo*>(constantTableData + constantInfo->typeInfo);
            const char* dimension = "2D";
            switch (typeInfo->parameterType)
            {
            case ParameterType::Texture3D:
            case ParameterType::Sampler3D:
                dimension = "3D";
                break;
            case ParameterType::TextureCube:
            case ParameterType::SamplerCube:
                dimension = "Cube";
                break;
            default:
                break;
            }

            if (reg >= 16)
                println("#error \"{}: sampler register {} has no deko3d binding (0-15)\"", constantName, reg);

            // Separate dimensional views of the same guest sampler. Fixed 32-slot ABI:
            // primary at R, alternate at 16+R. DCE removes unused dimensions.
            const bool cube = strcmp(dimension,"Cube") == 0;
            println("[[vk::binding({}, 0)]] Texture2D<float4> {}_Native2D;",cube ? 16+reg : reg,constantName);
            println("[[vk::binding({}, 0)]] Texture3D<float4> {}_Native3D;",16+reg,constantName);
            println("[[vk::binding({}, 0)]] TextureCube<float4> {}_NativeCube;",cube ? reg : 16+reg,constantName);
            println("[[vk::binding({}, 1)]] SamplerState {}_Muestreo;",reg,constantName);
            println("#define {0}_Texture2DDescriptorIndex {0}_Native2D",constantName);
            println("#define {0}_Texture3DDescriptorIndex {0}_Native3D",constantName);
            println("#define {0}_TextureCubeDescriptorIndex {0}_NativeCube",constantName);
            println("#define {0}_SamplerDescriptorIndex {0}_Muestreo",constantName);

            const uint32_t invBase = 360 + reg * 8;
            println("#define {}_InvTamano float2(NFSMW_COMPARTIDA_FLOAT({}), NFSMW_COMPARTIDA_FLOAT({}))",
                constantName, invBase, invBase + 4);
            break;
        }

        default:
            break;
        }
    }
}
#endif

void ShaderRecompiler::recompile(const uint8_t* shaderData, const std::string_view& include)
{
    const auto shaderContainer = reinterpret_cast<const ShaderContainer*>(shaderData);

    /*
     * Most Wanted (2005) has 0x102A0E00; Unleashed (2008), 0x102A1100.
     * Both are accepted: only the two high bytes are compared.
     */
    assert((shaderContainer->flags & 0xFFFF0000) == 0x102A0000);
    assert(shaderContainer->constantTableOffset != NULL);

    out += include;
    out += '\n';
    const size_t native_resources_at=out.size();

    isPixelShader = (shaderContainer->flags & 0x1) == 0;

    const auto constantTableContainer = reinterpret_cast<const ConstantTableContainer*>(shaderData + shaderContainer->constantTableOffset);
    constantTableData = reinterpret_cast<const uint8_t*>(&constantTableContainer->constantTable);

    out += "#ifdef __spirv__\n\n";

#ifdef NFSMW_RECOMP
    // NFSMW_DEKO3D (shader_common.h): the same constants for deko3d, written after this block. Without the
    // define DXC sees exactly what it saw before that mode existed.
    out += "#ifndef NFSMW_DEKO3D\n\n";
#endif

#ifdef UNLEASHED_RECOMP
    bool isMetaInstancer = false;
    bool hasIndexCount = false;
#endif

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);

    #ifdef UNLEASHED_RECOMP
        if (!isPixelShader)
        {
            if (strcmp(constantName, "g_MtxProjection") == 0)
                hasMtxProjection = true;
            else if (strcmp(constantName, "g_InstanceTypes") == 0)
                isMetaInstancer = true;
            else if (strcmp(constantName, "g_IndexCount") == 0)
                hasIndexCount = true;
        }
        else
        {
            if (strcmp(constantName, "g_MtxPrevInvViewProjection") == 0)
                hasMtxPrevInvViewProjection = true;
        }
    #endif

        switch (constantInfo->registerSet)
        {
        case RegisterSet::Float4:
        {
            const char* shaderName = isPixelShader ? "Pixel" : "Vertex";

            if (constantInfo->registerCount > 1)
            {
                uint32_t tailCount = (isPixelShader ? 224 : 256) - constantInfo->registerIndex;

                // NFSMW: dynamic UBO or pointer, depending on SPEC_CONSTANT_CONSTANTES_UBO.
                println("#define {}(INDEX) select((INDEX) < {}, (NFSMW_UBO ? g_Ubo{}.v[{} + min(INDEX, {})] : vk::RawBufferLoad<float4>(g_PushConstants.{}ShaderConstants + ({} + min(INDEX, {})) * 16, 0x10)), 0.0)",
                    constantName, tailCount, shaderName, constantInfo->registerIndex.get(), tailCount - 1,
                    shaderName, constantInfo->registerIndex.get(), tailCount - 1);
            }
            else
            {
                println("#define {} (NFSMW_UBO ? g_Ubo{}.v[{}] : vk::RawBufferLoad<float4>(g_PushConstants.{}ShaderConstants + {}, 0x10))",
                    constantName, shaderName, constantInfo->registerIndex.get(), shaderName, constantInfo->registerIndex * 16);
            }
            
            for (uint16_t j = 0; j < constantInfo->registerCount; j++)
                float4Constants.emplace(constantInfo->registerIndex + j, constantInfo);

            break;
        }

        case RegisterSet::Sampler:
        {
            for (size_t j = 0; j < std::size(TEXTURE_DIMENSIONS); j++)
            {
                println("#define {}_Texture{}DescriptorIndex (NFSMW_UBO ? NFSMW_COMPARTIDA_UINT({}) : vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + {}))",
                    constantName, TEXTURE_DIMENSIONS[j], j * 64 + constantInfo->registerIndex * 4, j * 64 + constantInfo->registerIndex * 4);
            }

            println("#define {}_SamplerDescriptorIndex (NFSMW_UBO ? NFSMW_COMPARTIDA_UINT({}) : vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + {}))",
                constantName, std::size(TEXTURE_DIMENSIONS) * 64 + constantInfo->registerIndex * 4, std::size(TEXTURE_DIMENSIONS) * 64 + constantInfo->registerIndex * 4);

            // 1/size of the host image of that slot, which the renderer writes at byte
            // 360 + slot * 8 of the shared constants (right after g_InputRemap).
            {
                const uint32_t invBase = 360 + constantInfo->registerIndex * 8;
                println("#define {}_InvTamano (NFSMW_UBO ? float2(NFSMW_COMPARTIDA_FLOAT({}), NFSMW_COMPARTIDA_FLOAT({})) : vk::RawBufferLoad<float2>(g_PushConstants.SharedConstants + {}))",
                    constantName, invBase, invBase + 4, invBase);
            }

            samplers.emplace(constantInfo->registerIndex, constantName);
            break;
        }

        }
    }

#ifdef NFSMW_RECOMP
    out += "\n#else\n\n";
    printDeko3dConstants(constantTableContainer);
    out += "\n#endif\n";
#endif

    out += "\n#else\n\n";

    println("cbuffer {}ShaderConstants : register(b{}, space4)", isPixelShader ? "Pixel" : "Vertex", isPixelShader ? 1 : 0);
    out += "{\n";

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        if (constantInfo->registerSet == RegisterSet::Float4)
        {
            const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);

            print("\tfloat4 {}", constantName);

            if (constantInfo->registerCount > 1)
                print("[{}]", constantInfo->registerCount.get());

            println(" : packoffset(c{});", constantInfo->registerIndex.get());

            if (constantInfo->registerCount > 1)
            {
                uint32_t tailCount = (isPixelShader ? 224 : 256) - constantInfo->registerIndex;
                println("#define {0}(INDEX) select((INDEX) < {1}, {0}[min(INDEX, {2})], 0.0)", constantName, tailCount, tailCount - 1);
            }
        }
    }

    out += "};\n\n";

    out += "cbuffer SharedConstants : register(b2, space4)\n";
    out += "{\n";

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        if (constantInfo->registerSet == RegisterSet::Sampler)
        {
            const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);

            for (size_t j = 0; j < std::size(TEXTURE_DIMENSIONS); j++)
            {
                println("\tuint {}_Texture{}DescriptorIndex : packoffset(c{}.{});",
                    constantName, TEXTURE_DIMENSIONS[j], j * 4 + constantInfo->registerIndex / 4, SWIZZLES[constantInfo->registerIndex % 4]);
            }

            println("\tuint {}_SamplerDescriptorIndex : packoffset(c{}.{});",
                constantName, 4 * std::size(TEXTURE_DIMENSIONS) + constantInfo->registerIndex / 4, SWIZZLES[constantInfo->registerIndex % 4]);
        }
    }

    out += "\tDEFINE_SHARED_CONSTANTS();\n";
    out += "};\n\n";

    out += "#endif\n";

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        if (constantInfo->registerSet == RegisterSet::Bool)
        {
            const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);
            println("\t#define {} (1 << {})", constantName, constantInfo->registerIndex + (isPixelShader ? 16 : 0));
            // In NFSMW CTAB numbers b0.. and CF uses 128.. for pixels.
            // The g_Booleans packing keeps its 16 bits per stage.
#ifdef NFSMW_RECOMP
            boolConstants.emplace(constantInfo->registerIndex + (isPixelShader ? 128 : 0), constantName);
#else
            boolConstants.emplace(constantInfo->registerIndex, constantName);
#endif
        }
    }

    out += '\n';

    const auto shader = reinterpret_cast<const Shader*>(shaderData + shaderContainer->shaderOffset);
    if (!isPixelShader) {
        const auto* vs=reinterpret_cast<const VertexShader*>(shader);
        for(uint32_t i=0;i<vs->vertexElementCount;++i) {
            union { VertexElement element; uint32_t word; } entry;
            entry.word=vs->vertexElementsAndInterpolators[vs->field18+i];
            vertexElements.emplace(uint32_t(entry.element.address),entry.element);
            vertexElementIndices.emplace(uint32_t(entry.element.address),i);
            ++vertexSemanticCounts[(uint32_t(entry.element.usage)<<8)|entry.element.usageIndex];
        }
    }

    out += "#ifndef __spirv__\n";

    if (isPixelShader)
        out += "[shader(\"pixel\")]\n";
    else
        out += "[shader(\"vertex\")]\n";

    out += "#endif\n";

    out += "void main(\n";

    if (isPixelShader)
    {
        out += "\tin float4 iPos : SV_Position,\n";

        for (auto& [usage, usageIndex] : INTERPOLATORS)
            println("\tin float4 i{0}{1} : {2}{1},", USAGE_VARIABLES[uint32_t(usage)], usageIndex, USAGE_SEMANTICS[uint32_t(usage)]);

        out += "#ifdef __spirv__\n";
        out += "\tin bool iFace : SV_IsFrontFace\n";
        out += "#else\n";
        out += "\tin uint iFace : SV_IsFrontFace\n";
        out += "#endif\n";

        auto pixelShader = reinterpret_cast<const PixelShader*>(shader);
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR0)
            out += ",\n\tout float4 oC0 : SV_Target0";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR1)
            out += ",\n\tout float4 oC1 : SV_Target1";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR2)
            out += ",\n\tout float4 oC2 : SV_Target2";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR3)
            out += ",\n\tout float4 oC3 : SV_Target3";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_DEPTH)
            out += ",\n\tout float oDepth : SV_Depth";
    }
    else
    {
        auto vertexShader = reinterpret_cast<const VertexShader*>(shader);
        std::unordered_set<uint32_t> declared_semantics;
        for (uint32_t i = 0; i < vertexShader->vertexElementCount; i++)
        {
            union
            {
                VertexElement vertexElement;
                uint32_t value;
            };

            value = vertexShader->vertexElementsAndInterpolators[vertexShader->field18 + i];
            if (!declared_semantics.insert((uint32_t(vertexElement.usage)<<8)|vertexElement.usageIndex).second) continue;

            const char* usageType = USAGE_TYPES[uint32_t(vertexElement.usage)];

        #ifdef UNLEASHED_RECOMP
            if ((vertexElement.usage == DeclUsage::TexCoord && vertexElement.usageIndex == 2 && isMetaInstancer) ||
                (vertexElement.usage == DeclUsage::Position && vertexElement.usageIndex == 1))
            {
                usageType = "uint4";
            }
        #endif

            out += '\t';

            for (auto& usageLocation : USAGE_LOCATIONS)
            {
                if (usageLocation.usage == vertexElement.usage && usageLocation.usageIndex == vertexElement.usageIndex)
                {
                    print("[[vk::location({})]] ", usageLocation.location);
                    break;
                }
            }

            println("in {0} i{1}{2} : {3}{2},", usageType, USAGE_VARIABLES[uint32_t(vertexElement.usage)],
                uint32_t(vertexElement.usageIndex), USAGE_SEMANTICS[uint32_t(vertexElement.usage)]);

            vertexElements.emplace(uint32_t(vertexElement.address), vertexElement);
        }

    #ifdef UNLEASHED_RECOMP
        if (hasIndexCount)
        {
            out += "\tin uint iVertexId : SV_VertexID,\n";
            out += "\tin uint iInstanceId : SV_InstanceID,\n";
        }
    #endif

        out += "\tin uint iVertexId : SV_VertexID,\n\tout float4 oPos : SV_Position";

        for (auto& [usage, usageIndex] : INTERPOLATORS)
            print(",\n\tout float4 o{0}{1} : {2}{1}", USAGE_VARIABLES[uint32_t(usage)], usageIndex, USAGE_SEMANTICS[uint32_t(usage)]);
    }

    out += ")\n";
    out += "{\n";

#ifdef UNLEASHED_RECOMP
    if (hasMtxProjection)
    {
        specConstantsMask |= SPEC_CONSTANT_REVERSE_Z;

        out += "\toPos = 0.0;\n";

        out += "\tfloat4x4 mtxProjection = float4x4(g_MtxProjection(0), g_MtxProjection(1), g_MtxProjection(2), g_MtxProjection(3));\n";
        out += "\tfloat4x4 mtxProjectionReverseZ = mul(mtxProjection, float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 1, 1));\n";

        out += "\t[unroll] for (int iterationIndex = 0; iterationIndex < 2; iterationIndex++)\n";
        out += "\t{\n";
    }
#endif

    if (shaderContainer->definitionTableOffset != NULL)
    {
        auto definitionTable = reinterpret_cast<const DefinitionTable*>(shaderData + shaderContainer->definitionTableOffset);
        auto definitions = definitionTable->definitions;
        while (*definitions != 0)
        {
            auto definition = reinterpret_cast<const Float4Definition*>(definitions);
            auto value = reinterpret_cast<const be<uint32_t>*>(shaderData + shaderContainer->virtualSize + definition->physicalOffset);
            for (uint16_t i = 0; i < (definition->count + 3) / 4; i++)
            {
                literalRegisters.insert(definition->registerIndex + i - (isPixelShader ? 256 : 0));
                println("\tfloat4 c{} = asfloat(uint4(0x{:X}, 0x{:X}, 0x{:X}, 0x{:X}));",
                    definition->registerIndex + i - (isPixelShader ? 256 : 0), value[0].get(), value[1].get(), value[2].get(), value[3].get());

                value += 4;
            }
            definitions += 2;
        }
        ++definitions;
        while (*definitions != 0)
        {
            auto definition = reinterpret_cast<const Int4Definition*>(definitions);
            for (uint16_t i = 0; i < definition->count; i++)
            {
                /*
                 * Named: GCC does not accept the anonymous local union member
                 * that MSVC and clang accept with -fms-extensions.
                 */
                union Bytes4
                {
                    uint32_t value;
                    struct { int8_t x, y, z, w; } b;
                } u;

                u.value = definition->values[i].get();

                println("\tint4 i{} = int4({}, {}, {}, {});",
                    (definition->registerIndex - 8992) / 4 + i, u.b.x, u.b.y, u.b.z, u.b.w);
            }
            definitions += 2;
            definitions += definition->count;
        }

        out += "\n";
    }

    bool printedRegisters[32]{};

    uint32_t interpolatorCount = (shader->interpolatorInfo >> 5) & 0x1F;

    for (uint32_t i = 0; i < interpolatorCount; i++)
    {
        union
        {
            Interpolator interpolator;
            uint32_t value;
        };
    
        if (isPixelShader)
        {
            value = reinterpret_cast<const PixelShader*>(shader)->interpolators[i];
            println("\tfloat4 r{} = i{}{};", uint32_t(interpolator.reg), USAGE_VARIABLES[uint32_t(interpolator.usage)], uint32_t(interpolator.usageIndex));
            printedRegisters[interpolator.reg] = true;
        }
        else
        {
            auto vertexShader = reinterpret_cast<const VertexShader*>(shader);
            value = vertexShader->vertexElementsAndInterpolators[vertexShader->field18 + vertexShader->vertexElementCount + i];
            interpolators.emplace(i, fmt::format("o{}{}", USAGE_VARIABLES[uint32_t(interpolator.usage)], uint32_t(interpolator.usageIndex)));
        }
    }

    if (!isPixelShader)
    {
    #ifdef UNLEASHED_RECOMP
        if (!hasMtxProjection)
            out += "\toPos = 0.0;\n";
    #endif

        for (auto& [usage, usageIndex] : INTERPOLATORS)
            println("\to{}{} = 0.0;", USAGE_VARIABLES[uint32_t(usage)], usageIndex);

        out += "\n";
    }

    for (size_t i = 0; i < 32; i++)
    {
        if (!printedRegisters[i])
        {
            print("\tfloat4 r{} = ", i);
            if (isPixelShader && i == ((shader->fieldC >> 8) & 0xFF))
            {
                out += "float4((iPos.xy - 0.5) * float2(iFace ? 1.0 : -1.0, 1.0), 0.0, 0.0);\n";
            }
        #ifdef UNLEASHED_RECOMP
            else if (!isPixelShader && hasIndexCount && i == 0)
            {
                out += "float4(iVertexId + g_IndexCount.x * iInstanceId, 0.0, 0.0, 0.0);\n";
            }
        #endif
            else if (!isPixelShader && i == 0)
            {
                out += "float4(iVertexId + g_VertexIndexBase, 0.0, 0.0, 0.0);\n";
            }
            else
            {
                out += "0.0;\n";
            }
        }
    }

    if (!isPixelShader)
        out += "\tfloat4 eA=0, eM0=0, eM1=0, eM2=0, eM3=0, eM4=0; CubeMapData cubeMapData=(CubeMapData)0;\n";
    out += "\tfloat lod = 0.0, vfetchIndex=0.0; float3 gradX=0, gradY=0;\n";
    out += "\tint a0 = 0;\n";
    out += "\tint aL = 0;\n";
    out += "\tbool p0 = false;\n";
    out += "\tfloat ps = 0.0;\n";
    if (isPixelShader)
    {
#ifdef UNLEASHED_RECOMP
        out += "\tfloat2 pixelCoord = 0.0;\n";
#endif
        out += "\tCubeMapData cubeMapData = (CubeMapData)0;\n";
    }

    const be<uint32_t>* code = reinterpret_cast<const be<uint32_t>*>(shaderData + shaderContainer->virtualSize + shader->physicalOffset);

    /*
     * Named, with references: GCC does not accept the anonymous struct inside a local
     * union that MSVC and clang accept. The references leave the rest of the body
     * untouched, which uses code0..code3 eighteen times.
     */
    union ControlFlowWords
    {
        ControlFlowInstruction controlFlow[2];
        struct { uint32_t code0, code1, code2, code3; } w;
    } cfu;
    uint32_t& code0 = cfu.w.code0;
    uint32_t& code1 = cfu.w.code1;
    uint32_t& code2 = cfu.w.code2;
    uint32_t& code3 = cfu.w.code3;

    auto controlFlowCode = code;
    uint32_t instrAddress = 0;
    uint32_t instrSize = shader->size;
    bool simpleControlFlow = true;

    while (instrAddress < instrSize)
    {
        code0 = controlFlowCode[0];
        code1 = controlFlowCode[1] & 0xFFFF;
        code2 = (controlFlowCode[1] >> 16) | (controlFlowCode[2] << 16);
        code3 = controlFlowCode[2] >> 16;

        for (auto& cfInstr : cfu.controlFlow)
        {
            uint32_t address = 0;

            switch (cfInstr.opcode)
            {
            case ControlFlowOpcode::Exec:
            case ControlFlowOpcode::ExecEnd:
                address = cfInstr.exec.address;
                break;

            case ControlFlowOpcode::CondExec:
            case ControlFlowOpcode::CondExecEnd:
            case ControlFlowOpcode::CondExecPredClean:
            case ControlFlowOpcode::CondExecPredCleanEnd:
                address = cfInstr.condExec.address;
                break;

            case ControlFlowOpcode::CondExecPred:
            case ControlFlowOpcode::CondExecPredEnd:
                address = cfInstr.condExecPred.address;
                break;

            case ControlFlowOpcode::CondJmp:
            {
                if (cfInstr.condJmp.isUnconditional || cfInstr.condJmp.direction)
                    simpleControlFlow = false;
                else
                    ++ifEndLabels[cfInstr.condJmp.address];

                break;
            }
            }

            if (address != 0)
                instrSize = std::min<uint32_t>(instrSize, address * 12);
        }

        controlFlowCode += 3;
        instrAddress += 12;
    }

    if (simpleControlFlow)
    {
        out += '\n';
        indentation = 1;
    }
    else
    {
        out += "\n\tuint pc = 0;\n";
        out += "\twhile (true)\n";
        out += "\t{\n";
        out += "\t\tswitch (pc)\n";
        out += "\t\t{\n";
    }

    controlFlowCode = code;
    instrAddress = 0;
    uint32_t pc = 0;

    while (instrAddress < instrSize)
    {
        code0 = controlFlowCode[0];
        code1 = controlFlowCode[1] & 0xFFFF;
        code2 = (controlFlowCode[1] >> 16) | (controlFlowCode[2] << 16);
        code3 = controlFlowCode[2] >> 16;

        for (auto& cfInstr : cfu.controlFlow)
        {
            if (!simpleControlFlow)
            {
                indentation = 3;
                println("\t\tcase {}:", pc);
            }
            else
            {
                auto findResult = ifEndLabels.find(pc);
                if (findResult != ifEndLabels.end())
                {
                    for (uint32_t i = 0; i < findResult->second; i++)
                    {
                        --indentation;
                        indent();
                        out += "}\n";
                    }
                }
            }

            ++pc;

            uint32_t address = 0;
            uint32_t count = 0;
            uint32_t sequence = 0;
            bool shouldReturn = false;
            bool shouldCloseCurlyBracket = false;
            /*
             * If this CF opens its own `if (p0)` (CondExecPred), its condition. It serves
             * two purposes: reusing the identical block that was just closed, and leaving its
             * own brace recorded so the next one can reuse it. See reabrirCierre().
             */
            int condicionPredicadoCf = -1;

            // Control flow boundary. An open predicated block does not cross this point.
            cerrarPredicado();

            switch (cfInstr.opcode)
            {
            case ControlFlowOpcode::Exec:
            case ControlFlowOpcode::ExecEnd:
                address = cfInstr.exec.address;
                count = cfInstr.exec.count;
                sequence = cfInstr.exec.sequence;
                shouldReturn = (cfInstr.opcode == ControlFlowOpcode::ExecEnd);
                break;

            case ControlFlowOpcode::CondExec:
            case ControlFlowOpcode::CondExecEnd:
            case ControlFlowOpcode::CondExecPredClean:
            case ControlFlowOpcode::CondExecPredCleanEnd:
                address = cfInstr.condExec.address;
                count = cfInstr.condExec.count;
                sequence = cfInstr.condExec.sequence;
                shouldReturn = (cfInstr.opcode == ControlFlowOpcode::CondExecEnd || cfInstr.opcode == ControlFlowOpcode::CondExecPredCleanEnd);
                {
                    // The condition belongs to the whole EXEC block, including its
                    // return. Per-instruction ALU predication does not replace it.
                    const auto booleano = boolConstants.find(cfInstr.condExec.boolAddress);
                    if (booleano == boolConstants.end())
                        throw std::runtime_error("EXEC condicional sin constante booleana declarada");
                    indent();
                    println("if ((g_Booleans & {}) {}= 0)", booleano->second, cfInstr.condExec.condition ? "!" : "=");
                    indent();
                    out += "{\n";
                    ++indentation;
                    shouldCloseCurlyBracket = true;
                }
                break;

            case ControlFlowOpcode::CondExecPred:
            case ControlFlowOpcode::CondExecPredEnd:
                address = cfInstr.condExecPred.address;
                count = cfInstr.condExecPred.count;
                sequence = cfInstr.condExecPred.sequence;
                shouldReturn = (cfInstr.opcode == ControlFlowOpcode::CondExecPredEnd);
                condicionPredicadoCf = cfInstr.condExecPred.condition ? 1 : 0;
                /*
                 * The previous block was this same `if (p0)` and nothing was emitted between
                 * the two: its brace is deleted and this EXEC continues inside. This way runs of
                 * CondExecPred with the same predicate (the nine PCF taps, the six car lights)
                 * end up in one basic block instead of three or four.
                 */
                if (!reabrirCierre(condicionPredicadoCf))
                {
                    indent();
                    println("if ({}p0)", condicionPredicadoCf ? "" : "!");
                    indent();
                    out += "{\n";
                    ++indentation;
                }
                shouldCloseCurlyBracket = true;
                // This `if` governs the whole block, so the predicated instructions inside
                // do not have to test it again. See predGarantizado_.
                predGarantizado_ = condicionPredicadoCf;
                break;

            case ControlFlowOpcode::LoopStart:
                if (simpleControlFlow)
                {
                    indent();
                #ifdef UNLEASHED_RECOMP
                    print("[unroll] ");
                #endif
                    println("for (aL = 0; aL < i{}.x; aL++)", uint32_t(cfInstr.loopStart.loopId));
                    indent();
                    out += "{\n";
                    ++indentation;
                }
                else 
                {
                    out += "\t\t\taL = 0;\n";
                }
                break;

            case ControlFlowOpcode::LoopEnd:
                if (simpleControlFlow)
                {
                    --indentation;
                    indent();
                    out += "}\n";
                }
                else
                {
                    out += "\t\t\t++aL;\n";
                    println("\t\t\tif (aL < i{}.x)", uint32_t(cfInstr.loopEnd.loopId));
                    out += "\t\t\t{\n";
                    println("\t\t\t\tpc = {};", uint32_t(cfInstr.loopEnd.address));
                    out += "\t\t\t\tcontinue;\n";
                    out += "\t\t\t}\n";
                }
                break;

            case ControlFlowOpcode::CondJmp:
            {
                if (cfInstr.condJmp.isUnconditional)
                {
                    assert(!simpleControlFlow);
                    println("\t\t\tpc = {};", uint32_t(cfInstr.condJmp.address));
                    out += "\t\t\tcontinue;\n";
                }
                else
                {
                    indent();
                    if (cfInstr.condJmp.isPredicated)
                    {
                        println("if ({}p0)", cfInstr.condJmp.condition ^ simpleControlFlow ? "" : "!");
                    }
                    else
                    {
                        auto findResult = boolConstants.find(cfInstr.condJmp.boolAddress);
                        if (findResult != boolConstants.end())
                            println("if ((g_Booleans & {}) {}= 0)", findResult->second, cfInstr.condJmp.condition ^ simpleControlFlow ? "!" : "=");
                        else
                            println("if (b{} {}= 0)", uint32_t(cfInstr.condJmp.boolAddress), cfInstr.condJmp.condition ^ simpleControlFlow ? "!" : "=");
                    }

                    if (simpleControlFlow)
                    {
                        indent();
                        out += "{\n";
                        ++indentation;
                    }
                    else
                    {
                        out += "\t\t\t{\n";
                        println("\t\t\t\tpc = {};", uint32_t(cfInstr.condJmp.address));
                        out += "\t\t\t\tcontinue;\n";
                        out += "\t\t\t}\n";
                    }
                }
                break;
            }
            }

            auto instructionCode = code + address * 3;
            
            for (uint32_t i = 0; i < count; i++)
            {
                /*
                 * Named, for the same reason as the other two: GCC does not accept
                 * the anonymous struct inside a local union.
                 */
                union InstrWords
                {
                    VertexFetchInstruction vertexFetch;
                    TextureFetchInstruction textureFetch;
                    AluInstruction alu;
                    struct { uint32_t code0, code1, code2; } w;
                } iu;
                VertexFetchInstruction& vertexFetch = iu.vertexFetch;
                TextureFetchInstruction& textureFetch = iu.textureFetch;
                AluInstruction& alu = iu.alu;

                iu.w.code0 = instructionCode[0];
                iu.w.code1 = instructionCode[1];
                iu.w.code2 = instructionCode[2];
            
                if ((sequence & 0x1) != 0)
                {
                    if (vertexFetch.opcode == FetchOpcode::VertexFetch)
                    {
                        recompile(vertexFetch, address + i);
                    }
                    else
                    {
                    #ifdef UNLEASHED_RECOMP
                        if (textureFetch.constIndex == 10) // g_GISampler
                        {
                            specConstantsMask |= SPEC_CONSTANT_BICUBIC_GI_FILTER;

                            indent();
                            out += "if (g_SpecConstants() & SPEC_CONSTANT_BICUBIC_GI_FILTER)";
                            indent();
                            out += '{';

                            ++indentation;
                            recompile(textureFetch, true);
                            --indentation;

                            indent();
                            out += "}";
                            indent();
                            out += "else";
                            indent();
                            out += '{';

                            ++indentation;
                            recompile(textureFetch, false);
                            --indentation;

                            indent();
                            out += '}';
                        }
                        else
                    #endif
                        {
                            recompile(textureFetch, false);
                        }
                    }
                }
                else
                {
                    recompile(alu);
                }
            
                sequence >>= 2;
                instructionCode += 3;
            }

            cerrarPredicado();  // end of the EXEC instructions

            if (shouldReturn)
            {
                if (isPixelShader)
                {
                    if (reinterpret_cast<const PixelShader*>(shader)->outputs & PIXEL_SHADER_OUTPUT_DEPTH)
                        out += "oDepth *= asfloat(NFSMW_COMPARTIDA_UINT(964));\n";
                    specConstantsMask |= SPEC_CONSTANT_ALPHA_TEST;

                    indent();
                    out += "[branch] if (g_SpecConstants() & SPEC_CONSTANT_ALPHA_TEST)";
                    indent();
                    out += '{';

                    indent();
                #ifdef NFSMW_RECOMP
                    // NFSMW: the 8 comparison functions (alphaTestValue, shader_common.h).
                    out += "\tclip(alphaTestValue(oC0.w));\n";
                #else
                    out += "\tclip(oC0.w - g_AlphaThreshold);\n";
                #endif

                    indent();
                    out += "}";

                #ifdef UNLEASHED_RECOMP
                    specConstantsMask |= SPEC_CONSTANT_ALPHA_TO_COVERAGE;

                    indent();
                    out += "else if (g_SpecConstants() & SPEC_CONSTANT_ALPHA_TO_COVERAGE)";
                    indent();
                    out += '{';

                    indent();
                    out += "\toC0.w *= 1.0 + computeMipLevel(pixelCoord) * 0.25;\n";
                    indent();
                    out += "\toC0.w = 0.5 + (oC0.w - g_AlphaThreshold) / max(fwidth(oC0.w), 1e-6);\n";

                    indent();
                    out += '}';
                #endif
                }
                else
                {
                #ifdef UNLEASHED_RECOMP
                    if (!hasMtxProjection)
                #endif
                    {
                    #ifdef NFSMW_RECOMP
                        // To host clip space (ndc_scale/ndc_offset from
                        // draw.cpp): draws in pixels with clipping disabled.
                        out += "\toPos.xy = oPos.xy * g_NdcScale + g_NdcOffset * oPos.w;\n";
                    #endif
                        out += "\toPos.xy += g_HalfPixelOffset * oPos.w;\n";
                    }
                }

                if (simpleControlFlow)
                {
                    indent();
                #ifdef UNLEASHED_RECOMP
                    if (hasMtxProjection)
                    {
                        out += "continue;\n";
                    }
                    else
                #endif
                    {
                        out += "return;\n";
                    }
                }
                else
                {
                    out += "\t\t\tbreak;\n";
                }
            }

            if (shouldCloseCurlyBracket)
            {
                --indentation;
                const size_t marcaLlaveCf = out.size();
                indent();
                out += "}\n";
                /*
                 * It is recorded in case the next CondExecPred is identical. Not with a
                 * `return` inside, since the block does not continue; and anotarCierreCf() also
                 * refuses if something wrote p0 in here (predGarantizado_ was set to -1): the
                 * next block has to test the new p0.
                 */
                anotarCierreCf(marcaLlaveCf, shouldReturn ? -1 : condicionPredicadoCf);
            }
            predGarantizado_ = -1;  // end of the EXEC block: it no longer guarantees anything
        }

        controlFlowCode += 3;
        instrAddress += 12;
    }

    if (!simpleControlFlow)
    {
        out += "\t\t\tbreak;\n";
        out += "\t\t}\n";
        out += "\t\tbreak;\n";
        out += "\t}\n";
    }

#ifdef UNLEASHED_RECOMP
    if (hasMtxProjection)
        out += "\t}\n";

    if (!isPixelShader && hasMtxProjection)
        out += "\toPos.xy += g_HalfPixelOffset * oPos.w;\n";
#endif

    out += "}";
    std::string declarations;
    for (const auto& [description,binding] : nativeTextures) {
        const uint32_t dim=(description>>5)&3;
        declarations+=fmt::format("[[vk::binding({},0)]] Texture{}<float4> mkvTexture{};\n[[vk::binding({},0)]] Texture{}<float4> mkvSignedTexture{};\n[[vk::binding({},1)]] SamplerState mkvSampler{};\n",
            binding*2,dim==2 ? "3D" : dim==3 ? "Cube" : "2D",binding,
            binding*2+1,dim==2 ? "3D" : dim==3 ? "Cube" : "2D",binding,binding*2,binding);
    }
    out.insert(native_resources_at,declarations);
}

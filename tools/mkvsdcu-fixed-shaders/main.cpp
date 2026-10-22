// MKvDCU fixed-binding shader path. Translation uses the MIT XenosRecomp fork.
// No NFSMW shader replacements or pass rewrites.
#include "XenosRecomp/shader_recompiler.h"
#include <fstream>
#include <iterator>
#include <span>
#include <vector>
#include <stdexcept>

static std::vector<uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(f), {}};
}
static uint32_t BE(std::span<const uint8_t> d, size_t p) {
    if (p > d.size() || d.size() - p < 4) throw std::runtime_error("Truncated container");
    return uint32_t(d[p]) << 24 | uint32_t(d[p+1]) << 16 | uint32_t(d[p+2]) << 8 | d[p+3];
}
int main(int argc, char** argv) try {
    if (argc != 4 && argc != 5) throw std::runtime_error("Usage: mkvsdcu-fixed-shaders packed-input-root fresh-output shader_common.h [--overwrite]");
    const std::filesystem::path output(argv[2]);
    if (std::filesystem::exists(output) && !std::filesystem::is_empty(output) &&
        !(argc == 5 && std::string_view(argv[4]) == "--overwrite"))
        throw std::runtime_error("Output must be empty");
    std::filesystem::create_directories(output);
    auto common = Read(argv[3]);
    const auto memory = Read(std::filesystem::path(argv[3]).parent_path()/"mkvsdcu_memory.hlsl");
    common.insert(common.end(),memory.begin(),memory.end());
    std::ofstream metadata(output.parent_path()/"memory-metadata.jsonl",std::ios::binary);
    size_t count = 0, failed = 0;
    for (const auto& file : std::filesystem::recursive_directory_iterator(argv[1])) {
        if (!file.is_regular_file() || file.path().extension() != ".bin") continue;
        const auto bytes = Read(file.path());
        for (size_t pos = 0; pos < bytes.size();) {
            const std::span<const uint8_t> remaining(bytes.data()+pos, bytes.size()-pos);
            const uint32_t flags = BE(remaining, 0), vs = BE(remaining, 4), ps = BE(remaining, 8);
            const size_t length = size_t(vs) + ps;
            if ((flags & ~1u) != 0x102A1100u || vs < 36 || vs >= 65536 || !ps ||
                length > remaining.size() || (length & 3)) throw std::runtime_error("Invalid input container");
            const auto data = remaining.first(length);
            const uint32_t sh = BE(data,24);
            if (sh < 36 || sh > vs-24 || uint64_t(BE(data,sh))+BE(data,sh+4) != ps)
                throw std::runtime_error("Invalid shader range");
            const auto name = fmt::format("{}_{:016x}", (flags&1) ? "vs" : "ps", XXH3_64bits(data.data(),data.size()));
            ShaderRecompiler compiler;
            try {
                compiler.recompile(data.data(), std::string_view(reinterpret_cast<const char*>(common.data()), common.size()));
            } catch (const std::exception& error) {
                std::fprintf(stderr,"%s: %s\n",name.c_str(),error.what());
                ++failed; pos += length; ++count; continue;
            }
            std::ofstream f(output/(name+".hlsl"),std::ios::binary);
            if (!f.write(compiler.out.data(),compiler.out.size())) throw std::runtime_error("HLSL write failed");
            metadata << fmt::format("{{\"shader\":\"{}\",\"raw_fetch\":[{},{},{}],\"memexport\":{},\"export_constants\":[{},{},{},{},{},{},{},{}],\"textures\":[",
                name,compiler.rawFetchMask[0],compiler.rawFetchMask[1],compiler.rawFetchMask[2],compiler.hasMemExport ? "true" : "false",
                compiler.memExportConstants[0],compiler.memExportConstants[1],compiler.memExportConstants[2],compiler.memExportConstants[3],
                compiler.memExportConstants[4],compiler.memExportConstants[5],compiler.memExportConstants[6],compiler.memExportConstants[7]);
            for (unsigned i=0;i<32;++i) metadata << (i ? "," : "") << compiler.textureFetch[i];
            metadata << "],\"bound_fetch\":" << compiler.boundFetchMask << "}\n";
            pos += length;
            if ((++count & 255) == 0) { std::printf("Translated %zu\n", count); std::fflush(stdout); }
        }
    }
    std::printf("Attempted %zu containers, %zu failed\n",count,failed);
    return count && !failed ? 0 : 2;
} catch (const std::exception& error) {
    std::fprintf(stderr,"%s\n",error.what()); return 1;
}

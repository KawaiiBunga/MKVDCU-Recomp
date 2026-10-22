// One compiler thread per process: uam's resident frontend is not reentrant.
// TSV input: shader name, stage (vert/frag), GLSL path, DKSH path.
// JSONL output records every shader, including failures and complete diagnostics.
#include <uam_api.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static std::string Quote(const std::string& value) {
  std::string out = "\"";
  const char hex[] = "0123456789abcdef";
  for (unsigned char c : value) {
    if (c == '\\' || c == '"') { out += '\\'; out += char(c); }
    else if (c < 0x20) {
      out += "\\u00";
      out += hex[c >> 4];
      out += hex[c & 15];
    } else out += char(c);
  }
  return out + '"';
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: mkvsdcu-uam-audit input.tsv report.jsonl\n";
    return 2;
  }
  std::ifstream manifest(argv[1]);
  std::ofstream report(argv[2]);
  if (!manifest || !report) return 2;
  uam::init(true);
  unsigned failed = 0, count = 0;
  std::string line;
  while (std::getline(manifest, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    std::istringstream row(line);
    std::string name, stage, source_path, output_path;
    std::getline(row, name, '\t');
    std::getline(row, stage, '\t');
    std::getline(row, source_path, '\t');
    std::getline(row, output_path, '\t');
    const auto start = std::chrono::steady_clock::now();
    bool ok = false, deterministic = false;
    size_t size = 0;
    std::string log;
    try {
      if (name.empty() || source_path.empty() || output_path.empty() ||
          (stage != "vert" && stage != "frag" && stage != "geom")) throw std::runtime_error("Invalid manifest row");
      std::ifstream source_file(source_path, std::ios::binary);
      if (!source_file) throw std::runtime_error("Cannot read GLSL");
      const std::string source{std::istreambuf_iterator<char>(source_file), std::istreambuf_iterator<char>()};
      const auto kind = stage == "vert" ? uam::Stage::Vertex : stage == "geom" ? uam::Stage::Geometry : uam::Stage::Fragment;
      auto first = uam::compile(kind, source.c_str());
      log = first.log;
      if (!first.ok) throw std::runtime_error("uam compile failed");
      auto repeat = uam::compile(kind, source.c_str());
      log += repeat.log;
      deterministic = repeat.ok && first.dksh == repeat.dksh;
      if (!deterministic) throw std::runtime_error("Repeated resident compile differs");
      if (first.dksh.size() < 4 || std::string(first.dksh.begin(), first.dksh.begin() + 4) != "DKSH")
        throw std::runtime_error("Invalid DKSH header");
      std::ofstream output(output_path, std::ios::binary);
      if (!output) throw std::runtime_error("Cannot write DKSH");
      output.write(reinterpret_cast<const char*>(first.dksh.data()), first.dksh.size());
      output.close();
      if (!output) throw std::runtime_error("Short DKSH write");
      size = first.dksh.size();
      ok = true;
    } catch (const std::exception& error) {
      log += std::string("\n") + error.what();
    }
    if (!ok) ++failed;
    ++count;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    report << "{\"shader\":" << Quote(name) << ",\"ok\":" << (ok ? "true" : "false")
           << ",\"deterministic\":" << (deterministic ? "true" : "false")
           << ",\"dksh_bytes\":" << size << ",\"seconds\":" << seconds
           << ",\"log\":" << Quote(log) << "}\n";
    report.flush();
    if (count % 256 == 0) std::cout << count << " shaders, " << failed << " failed\n" << std::flush;
  }
  uam::shutdown();
  if (!manifest.eof() || !report || count == 0) return 2;
  std::cout << count << " shaders, " << failed << " failed\n";
  return failed ? 1 : 0;
}

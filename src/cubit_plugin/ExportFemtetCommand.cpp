#include "ExportFemtetCommand.hpp"
#include "FemtetPdt.hpp"
#include "MeshData.hpp"
#include "LearnEditionMessageFilter.hpp"
#include "CubitMessage.hpp"
#include "CubitInterface.hpp"
#include "utf8_path.hpp"
#include "femtet_project_template.inc"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;

// Femtet stores the mesh of project X.femprj as X.Results/<analysis model>.pdt;
// this is Femtet's default analysis model name (UTF-8).
static const char ANALYSIS_MODEL_PDT[] = "\xe8\xa7\xa3\xe6\x9e\x90\xe3\x83\xa2\xe3\x83\x87\xe3\x83\xab.pdt";

std::vector<std::string> ExportFemtetCommand::get_syntax()
{
  return {"export femtet <string:label='filename',help='<filename.femprj>'>"};
}

std::vector<std::string> ExportFemtetCommand::get_syntax_help()
{
  return {"export femtet \"filename.femprj\""};
}

std::vector<std::string> ExportFemtetCommand::get_help()
{
  return {
    "Export the mesh as a new Femtet project and its paired mesh file.\n"
    "Writes X.femprj and X.Results/<analysis model>.pdt; neither may exist.\n"
    "Blocks must hold linear HEX8 elements and sidesets QUAD4 faces.\n"
    "Block names become body attributes (material Air), sideset names\n"
    "become named boundaries. Coordinates keep the Cubit model unit.\n"};
}

static void write_new(const fs::path &path, const char *data, size_t size)
{
  std::ofstream out;
  // Exclusive create: fail if the file appeared after the existence check.
  if (fs::exists(path)) throw std::runtime_error("file exists: " + path.u8string());
  out.open(path, std::ios::binary);
  if (!out) throw std::runtime_error("cannot create " + path.u8string());
  out.write(data, (std::streamsize)size);
  if (!out) throw std::runtime_error("write failed: " + path.u8string());
}

bool ExportFemtetCommand::execute(CubitCommandData &data)
{
  cubit_mesh_export::ScopedLearnEditionFilter _lef_guard;
  std::string filename;
  data.get_string("filename", filename);
  try {
    fs::path project = u8_string_to_path(filename);
    std::wstring ext = project.extension().wstring();
    for (auto &c : ext) c = (wchar_t)towlower(c);
    if (ext != L".femprj") throw std::runtime_error("output path must end in .femprj");
    fs::path results = project.parent_path() / (project.stem().wstring() + L".Results");
    if (fs::exists(project) || fs::exists(results))
      throw std::runtime_error("choose an unused Femtet project name: " + project.u8string());

    MeshData mesh;
    if (!mesh.extract(1)) throw std::runtime_error("mesh extraction failed");

    // Number nodes in order of first use by the block elements, as Cubit's
    // Exodus export does; this is the numbering checked against Femtet.
    std::unordered_map<int, int> index;
    std::vector<femtet_pdt::Block> blocks;
    for (int bid : mesh.block_ids) {
      femtet_pdt::Block b;
      b.name = CubitInterface::get_block_name(bid);
      for (const auto &e : mesh.elements) {
        if (e.group_id != bid) continue;
        if (e.nv != 8 || (e.type != HEX8 && e.type != HEX))
          throw std::runtime_error("Femtet export supports linear HEX8 blocks only; block " +
                                   std::to_string(bid) + " has another element type");
        femtet_pdt::Hex h;
        for (int i = 0; i < 8; ++i) {
          auto it = index.emplace(e.conn[i], (int)index.size() + 1).first;
          h[i] = it->second;
        }
        b.cells.push_back(h);
      }
      blocks.push_back(std::move(b));
    }
    std::vector<femtet_pdt::Sideset> sidesets;
    for (const auto &sg : mesh.sidesets) {
      femtet_pdt::Sideset s;
      s.name = sg.name;
      for (const auto &f : sg.faces) {
        if (f.nv != 4 || (f.type != QUAD4 && f.type != QUAD))
          throw std::runtime_error("Femtet export supports QUAD4 sidesets only; sideset " +
                                   std::to_string(sg.id) + " has another face type");
        femtet_pdt::Quad q;
        for (int i = 0; i < 4; ++i) {
          auto it = index.find(f.conn[i]);
          if (it == index.end())
            throw std::runtime_error("sideset " + std::to_string(sg.id) +
                                     " has a node that is on no block element");
          q[i] = it->second;
        }
        s.faces.push_back(q);
      }
      sidesets.push_back(std::move(s));
    }
    std::vector<femtet_pdt::Point> points(index.size());
    for (const auto &[node_id, n] : index) points[n - 1] = mesh.get_node_coords(node_id);

    const std::string pdt = femtet_pdt::encode(points, blocks, sidesets);
    fs::create_directory(results);
    write_new(results / fs::u8path(ANALYSIS_MODEL_PDT), pdt.data(), pdt.size());
    write_new(project, reinterpret_cast<const char *>(FEMTET_PROJECT_TEMPLATE),
              sizeof(FEMTET_PROJECT_TEMPLATE));

    PRINT_INFO("Exported Femtet project: %s (%d nodes, %d blocks, %d sidesets)\n",
               filename.c_str(), (int)points.size(), (int)blocks.size(), (int)sidesets.size());
    return true;
  } catch (const std::exception &ex) {
    PRINT_ERROR("export femtet: %s\n", ex.what());
    return false;
  }
}

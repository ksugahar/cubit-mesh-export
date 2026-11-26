#include "ExportNodeCountCommand.hpp"
#include "utf8_path.hpp"

#include "CubitInterface.hpp"
#include "CubitMessage.hpp"
#include "MeshExportInterface.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

std::vector<std::string> ExportNodeCountCommand::get_syntax()
{
  // `export <new keyword>` extends the built-in export verb.  Pick a keyword
  // no built-in exporter uses.
  return {
    "export nodecount <string:label='filename',help='<filename>'> [overwrite]"
  };
}

std::vector<std::string> ExportNodeCountCommand::get_syntax_help()
{
  return {"export nodecount \"filename\" [overwrite]"};
}

std::vector<std::string> ExportNodeCountCommand::get_help()
{
  return {"Write node, element and block counts of the current mesh.\n"};
}

bool ExportNodeCountCommand::execute(CubitCommandData& data)
{
  std::string filename;
  data.get_string("filename", filename);
  const bool overwrite = data.find_keyword("overwrite");

  // Cubit hands over UTF-8 bytes; build the path from UTF-16 on Windows or
  // a Japanese directory name fails to open.
  const std::filesystem::path path = u8_string_to_path(filename);
  std::error_code ec;
  if (!overwrite && std::filesystem::exists(path, ec)) {
    PRINT_ERROR("File exists: %s (add 'overwrite')\n", filename.c_str());
    return false;
  }

  // Interfaces from CubitInterface must be released; an RAII holder keeps
  // every early return correct.
  std::unique_ptr<CubitBaseInterface, void (*)(CubitBaseInterface*)> raw(
      CubitInterface::get_interface("MeshExport"),
      [](CubitBaseInterface* p) { if (p) CubitInterface::release_interface(p); });
  auto* iface = dynamic_cast<MeshExportInterface*>(raw.get());
  if (!iface) {
    PRINT_ERROR("MeshExport interface is not available\n");
    return false;
  }
  iface->set_use_sequential_ids(false);
  iface->initialize_export();

  const int nodes = iface->get_num_nodes();
  if (nodes <= 0) {
    PRINT_ERROR("No mesh nodes to export\n");
    return false;
  }
  std::vector<BlockHandle> blocks;
  iface->get_block_list(blocks);
  long long elements = 0;
  for (BlockHandle block : blocks) {
    int n = 0;
    iface->get_block_size(block, n);
    elements += n;
  }

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    PRINT_ERROR("Cannot open %s for writing\n", filename.c_str());
    return false;
  }
  out << "nodes " << nodes << "\n"
      << "blocks " << blocks.size() << "\n"
      << "block_elements " << elements << "\n";
  out.close();
  if (!out) {
    PRINT_ERROR("Write failed: %s\n", filename.c_str());
    return false;
  }
  PRINT_INFO("MYPLUGIN_EXPORT nodes=%d blocks=%zu elements=%lld\n",
             nodes, blocks.size(), elements);
  return true;
}

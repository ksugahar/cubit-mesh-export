#ifndef EXPORT_FEMTET_COMMAND_HPP
#define EXPORT_FEMTET_COMMAND_HPP

#include "CubitCommandInterface.hpp"
#include <string>
#include <vector>

// export femtet "X.femprj": writes a new Femtet project X.femprj and its
// paired mesh X.Results/<analysis model>.pdt (linear HEX8 blocks, QUAD4
// sidesets). Neither may exist.
class ExportFemtetCommand : public CubitCommand
{
public:
  std::vector<std::string> get_syntax();
  std::vector<std::string> get_syntax_help();
  std::vector<std::string> get_help();
  bool execute(CubitCommandData &data);
};

#endif

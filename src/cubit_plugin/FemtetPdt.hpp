#ifndef FEMTET_PDT_HPP
#define FEMTET_PDT_HPP

// Femtet mesh file (.pdt) encoder for linear HEX8 meshes.
//
// Pure function of the mesh: no Cubit or Femtet dependency, so it can be
// compiled and checked on its own. The file carries the mesh, the block names
// as body attributes and the sidesets as named boundaries (node and face
// membership). Analysis conditions live in the Femtet project, not here.
// Coordinates are written unchanged in the model unit.

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace femtet_pdt {

using Hex = std::array<int, 8>;   // 1-based node indices, Exodus HEX8 order
using Quad = std::array<int, 4>;  // 1-based node indices
using Point = std::array<double, 3>;

struct Block { std::string name; std::vector<Hex> cells; };
struct Sideset { std::string name; std::vector<Quad> faces; };

// Every body gets this material; the exported project defines it as air.
inline const char *const MATERIAL = "Air";

namespace detail {

constexpr int HEX8 = 896;
constexpr int EDGES[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
                              {0,4},{1,5},{2,6},{3,7}};
constexpr int FACES[6][4] = {{0,1,2,3},{4,7,6,5},{0,4,5,1},{1,5,6,2},{2,6,7,3},{3,7,4,0}};

inline void put_i(std::string &out, std::int32_t v)
{
  char b[4];
  std::memcpy(b, &v, 4);  // x86/x64: little endian
  out.append(b, 4);
}

inline void put_d(std::string &out, double v)
{
  char b[8];
  std::memcpy(b, &v, 8);
  out.append(b, 8);
}

inline void put_ints(std::string &out, const std::vector<int> &v)
{
  for (int x : v) put_i(out, x);
}

template <class Rows>
inline void put_records(std::string &out, const Rows &rows)
{
  put_i(out, (int)rows.size());
  for (const auto &row : rows) {
    put_i(out, (int)row.size());
    for (int x : row) put_i(out, x);
  }
}

inline Quad rotate(const Quad &q, int k)
{
  Quad r;
  for (int i = 0; i < 4; ++i) r[i] = q[(i + k) % 4];
  return r;
}

inline void check_names(const char *kind, const std::vector<std::string> &names)
{
  std::set<std::string> seen;
  for (const auto &n : names) {
    bool ok = !n.empty() && n.size() <= 31 &&
              (std::isalpha((unsigned char)n[0]) || n[0] == '_');
    for (char c : n) ok = ok && (std::isalnum((unsigned char)c) || c == '_') && (unsigned char)c < 128;
    if (!ok)
      throw std::runtime_error(std::string(kind) + " name '" + n +
                               "' is not a plain ASCII identifier of at most 31 characters");
    if (!seen.insert(n).second)
      throw std::runtime_error(std::string(kind) + " names are not unique: " + n);
  }
}

}  // namespace detail

// Return the .pdt bytes. Every sideset quad must be a boundary face of a block
// (exterior or between blocks), and a face may belong to one sideset only.
inline std::string encode(const std::vector<Point> &points, const std::vector<Block> &blocks,
                          const std::vector<Sideset> &sidesets)
{
  using namespace detail;
  for (const auto &p : points)
    for (double x : p)
      if (!std::isfinite(x)) throw std::runtime_error("node coordinates must be finite 3D points");
  if (blocks.empty()) throw std::runtime_error("no HEX8 blocks");
  {
    std::vector<std::string> bn, sn;
    for (const auto &b : blocks) bn.push_back(b.name);
    for (const auto &s : sidesets) sn.push_back(s.name);
    check_names("block", bn);
    check_names("sideset", sn);
  }
  const int npoints = (int)points.size();

  // Edges: sorted node pair -> (number, first-seen direction).
  std::map<std::pair<int,int>, int> edge_number;
  std::vector<std::pair<int,int>> edge_list;
  // Faces: canonical loop -> number; original loop per number.
  std::map<Quad, int> face_number;
  std::vector<Quad> face_loop;                 // by number-1
  std::vector<std::vector<int>> loops;         // edge-signed loops, by number-1
  std::vector<std::vector<int>> solids;        // 6 signed faces per cell
  std::vector<Hex> cells;
  std::vector<std::vector<int>> regions;
  std::vector<Quad> exterior;

  for (const auto &block : blocks) {
    if (block.cells.empty()) throw std::runtime_error("a block has no HEX8 elements");
    std::map<int,int> count;      // face number -> uses in this block
    std::vector<int> order;       // first-use order in this block
    std::map<int, Quad> last_loop;
    std::vector<int> region;
    for (const auto &nodes : block.cells) {
      std::set<int> distinct(nodes.begin(), nodes.end());
      if (distinct.size() != 8) throw std::runtime_error("degenerate or non-HEX8 element");
      if (*distinct.begin() < 1 || *distinct.rbegin() > npoints)
        throw std::runtime_error("HEX8 connectivity references an absent node");
      cells.push_back(nodes);
      region.push_back((int)cells.size());
      for (const auto &e : EDGES) {
        int a = nodes[e[0]], b = nodes[e[1]];
        auto key = std::make_pair(std::min(a, b), std::max(a, b));
        if (edge_number.emplace(key, (int)edge_list.size() + 1).second)
          edge_list.emplace_back(a, b);
      }
      std::vector<int> incident;
      for (const auto &f : FACES) {
        Quad loop{nodes[f[0]], nodes[f[1]], nodes[f[2]], nodes[f[3]]};
        Quad rev{loop[3], loop[2], loop[1], loop[0]};
        Quad key = loop;
        for (int k = 0; k < 4; ++k) {
          key = std::min(key, rotate(loop, k));
          key = std::min(key, rotate(rev, k));
        }
        auto it = face_number.find(key);
        int number;
        if (it == face_number.end()) {
          number = (int)face_loop.size() + 1;
          face_number.emplace(key, number);
          face_loop.push_back(loop);
          std::vector<int> linked;
          for (int i = 0; i < 4; ++i) {
            int a = loop[i], b = loop[(i + 1) % 4];
            int k = edge_number.at({std::min(a, b), std::max(a, b)});
            linked.push_back(edge_list[k - 1] == std::make_pair(a, b) ? k : -k);
          }
          loops.push_back(linked);
        } else {
          number = it->second;
        }
        const Quad &orig = face_loop[number - 1];
        bool forward = false;
        for (int k = 0; k < 4; ++k) forward = forward || rotate(loop, k) == orig;
        int sign = forward ? number : -number;
        incident.push_back(sign);
        if (count[number]++ == 0) order.push_back(number);
        last_loop[number] = loop;
      }
      solids.push_back(incident);
    }
    for (int k : order) {
      if (count[k] > 2) throw std::runtime_error("nonmanifold HEX8 face");
    }
    regions.push_back(region);
    for (int k : order)
      if (count[k] == 1) exterior.push_back(last_loop[k]);
  }

  // Region-local boundary faces; a face between two blocks has two owners.
  std::map<int, std::vector<std::pair<int,int>>> owners;  // face -> (cell, side)
  std::vector<int> region_of(cells.size() + 1, 0);
  for (size_t r = 0; r < regions.size(); ++r) {
    std::map<int,int> counts;
    for (int c : regions[r])
      for (int f : solids[c - 1]) ++counts[std::abs(f)];
    for (int c : regions[r]) {
      region_of[c] = (int)r;
      for (int side = 1; side <= 6; ++side) {
        int f = std::abs(solids[c - 1][side - 1]);
        if (counts[f] == 1) owners[f].emplace_back(c, side);
      }
    }
  }
  std::map<Quad, int> face_index;
  for (size_t i = 0; i < face_loop.size(); ++i) {
    Quad s = face_loop[i];
    std::sort(s.begin(), s.end());
    face_index[s] = (int)i + 1;
  }
  std::set<std::pair<int,int>> node_bindings;
  std::map<int,int> face_bindings;
  for (size_t g = 0; g < sidesets.size(); ++g) {
    const auto &ss = sidesets[g];
    if (ss.faces.empty()) throw std::runtime_error("sideset '" + ss.name + "' has no quad faces");
    for (const auto &q : ss.faces) {
      Quad s = q;
      std::sort(s.begin(), s.end());
      auto it = face_index.find(s);
      if (it == face_index.end() || !owners.count(it->second))
        throw std::runtime_error("sideset '" + ss.name + "' has a face outside the block boundaries");
      if (!face_bindings.emplace(it->second, (int)g + 1).second)
        throw std::runtime_error("a face belongs to more than one sideset");
      for (int n : q) node_bindings.emplace(n, (int)g + 1);
    }
  }

  std::vector<int> corners;
  std::vector<std::array<int,4>> corner_edges;
  std::vector<std::vector<int>> facets;
  std::vector<std::vector<int>> region_facets(regions.size());
  int owned = 0;
  for (const auto &[face, own] : owners) {
    owned += (int)own.size();
    const auto &loop = loops[face - 1];
    std::set<int> rs;
    for (const auto &co : own) rs.insert(region_of[co.first]);
    for (int r : rs) {
      std::map<int,int> pos;
      for (int e : loop) {
        const auto &pr = edge_list[std::abs(e) - 1];
        int node = e > 0 ? pr.first : pr.second;
        corners.push_back(node);
        pos[node] = (int)corners.size();
      }
      std::vector<int> row{(int)loop.size()};
      for (int e : loop) {
        const auto &pr = edge_list[std::abs(e) - 1];
        corner_edges.push_back({pos[pr.first], pos[pr.second], 1, std::abs(e)});
        row.push_back((int)corner_edges.size());
      }
      row.push_back(1);
      row.push_back(face);
      facets.push_back(row);
      region_facets[r].push_back((int)facets.size());
    }
  }

  std::vector<int> tail{(int)node_bindings.size()};
  for (const auto &[n, g] : node_bindings) { tail.push_back(n); tail.push_back(g); }
  tail.push_back(0);
  tail.push_back((int)face_bindings.size());
  for (const auto &[f, g] : face_bindings) {
    tail.push_back(f);
    tail.push_back(owners[f][0].first);
    tail.push_back(owners[f][0].second);
    tail.push_back(g);
  }
  tail.insert(tail.end(), {0, 0, 0, 0});
  tail.push_back((int)corners.size());
  tail.insert(tail.end(), corners.begin(), corners.end());
  tail.push_back((int)corner_edges.size());
  for (const auto &ce : corner_edges) tail.insert(tail.end(), ce.begin(), ce.end());
  tail.push_back((int)facets.size());
  for (const auto &row : facets) tail.insert(tail.end(), row.begin(), row.end());
  tail.push_back((int)regions.size());
  for (size_t r = 0; r < regions.size(); ++r) {
    tail.push_back((int)region_facets[r].size());
    tail.insert(tail.end(), region_facets[r].begin(), region_facets[r].end());
    tail.push_back((int)regions[r].size());
    tail.insert(tail.end(), regions[r].begin(), regions[r].end());
  }
  tail.push_back((int)regions.size());
  for (int r = 1; r <= (int)regions.size(); ++r) { tail.push_back(1); tail.push_back(r); }
  tail.insert(tail.end(), {0, 0});

  std::string xyz;
  for (const auto &p : points) for (double x : p) put_d(xyz, x);

  std::string mes;
  put_ints(mes, {250, 5});
  mes.append("test\0", 5);
  put_i(mes, npoints);
  mes += xyz;
  {
    std::vector<std::vector<int>> rows;
    for (const auto &e : edge_list) rows.push_back({e.first, e.second});
    put_records(mes, rows);
  }
  put_records(mes, loops);
  put_records(mes, solids);
  put_i(mes, (int)cells.size());
  for (size_t i = 0; i < cells.size(); ++i) {
    put_ints(mes, {HEX8, (int)i + 1, 8});
    for (int n : cells[i]) put_i(mes, n);
  }
  put_records(mes, regions);
  put_ints(mes, tail);

  std::string tbl;
  auto line = [&tbl](const std::string &s) { tbl += s; tbl += "\r\n"; };
  line(std::to_string(blocks.size()));
  for (size_t i = 0; i < blocks.size(); ++i)
    line(blocks[i].name + "   " + MATERIAL + "   5601   " + std::to_string(i));
  line(std::to_string(sidesets.size()));
  for (const auto &s : sidesets) line(s.name + "   0");
  const std::pair<int,int> tables[] = {{6 * (int)cells.size(), 5001}, {6 * (int)cells.size(), 5002},
                                       {owned, 5004}, {(int)blocks.size(), 5006}};
  for (const auto &[n, kind] : tables) {
    line(std::to_string(n));
    for (int i = 0; i < n; ++i) line("0   " + std::to_string(kind) + "   0");
  }
  line("100");

  std::string fac;
  put_ints(fac, {100, npoints});
  fac += xyz;
  put_i(fac, 4 * (int)exterior.size());
  for (const auto &q : exterior)
    for (int i = 0; i < 4; ++i) put_ints(fac, {1, q[i], q[(i + 1) % 4]});
  put_i(fac, (int)exterior.size());
  for (const auto &q : exterior) put_ints(fac, {2, q[0], q[1], q[2], q[2], q[3], q[0]});

  std::string out;
  put_i(out, 101);
  const std::pair<const char *, const std::string *> chunks[] = {{".mes", &mes}, {".tbl", &tbl}, {".fac", &fac}};
  for (const auto &[tag, data] : chunks) {
    char t[5] = {0, 0, 0, 0, 0};
    std::memcpy(t, tag, 4);
    out.append(t, 5);
    std::uint64_t n = data->size();
    char b[8];
    std::memcpy(b, &n, 8);
    out.append(b, 8);
    out += *data;
  }
  return out;
}

}  // namespace femtet_pdt

#endif  // FEMTET_PDT_HPP

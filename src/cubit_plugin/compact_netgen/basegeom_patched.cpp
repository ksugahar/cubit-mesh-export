// Modified from Netgen libsrc/meshing/basegeom.cpp at v6.2.2608 (96e5682f6ea43ba77ba3bb2ae4bb4bd1791f506e) plus upstream 13b47c3a9fb2e7d53ca4be7227bdd4acbd6c6e73 (pyramid apex clamp).
// Local modifications present in the source history on 2026-10-15.
// Netgen licensing and attribution are provided with the distribution.

#include <set>

#include <mystdlib.h>
// Compact Netgen: exclude meshing algorithm headers
// Only need basegeom virtual method stubs
#include "meshclass.hpp"
#include "basegeom.hpp"
#include "curvedelems.hpp"
#include "bisect.hpp"           // Refinement complete type for unique_ptr dtor
#include <core/register_archive.hpp>

namespace netgen
{
  struct PointTree
  {
      std::map<int, BoxTree<3, PointIndex>> tree;
      Box<3> bounding_box;

      PointTree( Box<3> bb ) : bounding_box(bb) {}

      void Insert(Point<3> p, PointIndex n, int index)
      {
        if(tree.count(index) == 0)
          tree.emplace(index, bounding_box);
        tree.at(index).Insert(p, p, n);
      }

      PointIndex Find(Point<3> p, int index) const
      {
          ArrayMem<PointIndex, 1> points;
          tree.at(index).GetIntersecting(p, p, points);
          if(points.Size()==0)
              throw Exception("cannot find mapped point " + ToString(p));
          return points[0];
      }
  };

  namespace {
    struct Line
    {
      Point<3> p0, p1;
      inline double Length() const { return (p1-p0).Length(); }
      inline double Dist(const Line& other) const
      {
        Vec<3> n = p1-p0;
        Vec<3> q = other.p1-other.p0;
        double nq = n*q;
        Point<3> p = p0 + 0.5*n;
        double lambda = (p-other.p0)*n / (nq + 1e-10);
        if (lambda >= 0 && lambda <= 1)
          return (p-other.p0-lambda*q).Length();
        return 1e99;
      }
    };
  }

  void NetgenGeometry :: ProcessIdentifications()
  {
      for(auto i : Range(vertices))
          vertices[i]->nr = i;
      for(auto i : Range(edges))
          edges[i]->nr = i;
      for(auto i : Range(faces))
          faces[i]->nr = i;
      for(auto i : Range(solids))
          solids[i]->nr = i;

      auto mirror_identifications = [&] ( auto & shapes )
      {
          for(auto i : Range(shapes))
          {
              auto &s = shapes[i];
              s->nr = i;
              for(auto & ident : s->identifications)
                if(s.get() == ident.from && s.get() != ident.to)
                  ident.to->identifications.Append(ident);
          }
      };

      auto tol = 1e-6 * bounding_box.Diam();
      for(auto & f : faces)
        for(auto & ident: f->identifications)
          for(auto e : static_cast<GeometryFace*>(ident.from)->edges)
            for(auto e_other : static_cast<GeometryFace*>(ident.to)->edges)
              if(ident.trafo && e->IsMappedShape(*e_other, *ident.trafo, tol))
                e->identifications.Append( {e, e_other, ident.trafo, ident.type, ident.name} );

      for(auto & e : edges)
        for(auto & ident: e->identifications)
          {
              auto & from = static_cast<GeometryEdge&>(*ident.from);
              auto & to = static_cast<GeometryEdge&>(*ident.to);

              GeometryVertex * pfrom[] = { &from.GetStartVertex(), &from.GetEndVertex() };
              GeometryVertex * pto[] = { &to.GetStartVertex(), &to.GetEndVertex() };

              if(!ident.trafo) continue;

              // swap points of other edge if necessary
              Point<3> p_from0 = (*ident.trafo)(from.GetStartVertex().GetPoint());
              Point<3> p_from1 = (*ident.trafo)(from.GetEndVertex().GetPoint());
              Point<3> p_to0 = to.GetStartVertex().GetPoint();

              if(Dist(p_from1, p_to0) < Dist(p_from0, p_to0))
                  swap(pto[0], pto[1]);

              for(auto i : Range(2))
                  pfrom[i]->identifications.Append( {pfrom[i], pto[i], ident.trafo, ident.type, ident.name} );
          }

      mirror_identifications(vertices);
      mirror_identifications(edges);
      mirror_identifications(faces);


      auto find_primary = [&] (auto & shapes)
      {
          for(auto &s : shapes)
              s->primary = s.get();

          bool changed = true;

          while(changed) {
            changed = false;
            for(auto &s : shapes)
            {
              for(auto & ident : s->identifications)
              {
                  bool need_inverse = ident.from == s.get();
                  auto other = need_inverse ? ident.to : ident.from;
                  if(other->primary->nr < s->primary->nr)
                  {
                      s->primary = other->primary;
                      if(ident.trafo)
                      {
                        auto trafo = *ident.trafo;
                        if(need_inverse)
                            trafo = trafo.CalcInverse();
                        if(!s->primary_to_me)
                          s->primary_to_me = Transformation<3>( Vec<3>{0., 0., 0.} );
                        if(!other->primary_to_me)
                          other->primary_to_me = Transformation<3>( Vec<3>{0., 0., 0.} );
                        s->primary_to_me->Combine(trafo, *other->primary_to_me);
                        changed = true;
                    }
                  }
              }
            }
          }
      };

      find_primary(vertices);
      find_primary(edges);
      find_primary(faces);
  }

  // Stubs for virtual functions (compact_netgen: no mesher, only curving)
  void NetgenGeometry :: Analyse(Mesh& mesh,
                                 const MeshingParameters& mparam) const
  { /* stub - mesher not available in compact build */ }

  void NetgenGeometry :: FindEdges(Mesh& mesh,
                                   const MeshingParameters& mparam) const
  { /* stub - mesher not available in compact build */ }


  // Compact Netgen stubs: mesher-dependent virtual methods
  bool NetgenGeometry :: MeshFace(Mesh&, const MeshingParameters&, int, FlatArray<int, PointIndex>) const { return true; }
  void NetgenGeometry :: MeshSurface(Mesh&, const MeshingParameters&) const {}
  void NetgenGeometry :: MapSurfaceMesh(Mesh&, const GeometryFace&, std::map<std::tuple<PointIndex,int>, PointIndex>&) const {}
  void NetgenGeometry :: OptimizeSurface(Mesh&, const MeshingParameters&) const {}
  void NetgenGeometry :: FinalizeMesh(Mesh&) const {}
  int NetgenGeometry :: GenerateMesh(shared_ptr<Mesh>&, MeshingParameters&) { return 0; }

  void NetgenGeometry :: Save (const filesystem::path & filename) const
  {
    throw NgException("Cannot save geometry - no geometry available");
  }

  static RegisterClassForArchive<NetgenGeometry> regnggeo;
}

// Modified from Netgen libsrc/meshing/meshclass.cpp at v6.2.2608 (96e5682f6ea43ba77ba3bb2ae4bb4bd1791f506e) plus upstream 13b47c3a9fb2e7d53ca4be7227bdd4acbd6c6e73 (pyramid apex clamp).
// Local modifications present in the source history on 2026-10-15.
// Netgen licensing and attribution are provided with the distribution.

#include <algorithm>
#include <mystdlib.h>
#include <atomic>
#include <map>
#include <set>
#include "core/array.hpp"
#include "meshing.hpp"
// Compact Netgen: use stub gzstream (no zlib)
#include "general/gzstream.h"

#include <core/register_archive.hpp>
#include "basegeom.hpp"

namespace netgen
{
  static mutex& get_buildsearchtree_mutex() {
    // Intentional leak: avoid mutex destructor during DLL unload (atexit)
    // which crashes in MSVCP140!mtx_do_lock after CRT shutdown.
    static mutex* m = new mutex;
    return *m;
  }

  Mesh :: Mesh ()
    : topology(*this), surfarea(*this)
  {
    lochfunc = {nullptr};
    for(auto i : Range(4))
      elementsearchtreets[i] = NextTimeStamp();
    majortimestamp = timestamp = NextTimeStamp();
    hglob = 1e10;
    hmin = 0;
    numvertices = -1;
    dimension = 3;

    curvedelems = make_unique<CurvedElements> (*this);
    clusters = make_unique<AnisotropicClusters> (*this);
    ident = make_unique<Identifications> (*this);

    ps_startelement = 0;

    geomtype = NO_GEOM;

  }


  Mesh :: ~Mesh()
  {
    for(int i = 0; i < userdata_int.Size(); i++)
      delete userdata_int[i];
    for(int i = 0; i < userdata_double.Size(); i++)
      delete userdata_double[i];

    // #ifdef PARALLEL
    // delete paralleltop;
    // #endif
  }

  shared_ptr<NetgenGeometry> Mesh :: GetGeometry() const
  {
    static auto global_geometry = make_shared<NetgenGeometry>();
    return geometry ? geometry : global_geometry;
  }

  template <typename TIndex> static size_t NumElements (const Mesh & mesh);
  template <> size_t NumElements<ElementIndex> (const Mesh & mesh) { return mesh.GetNE(); }
  template <> size_t NumElements<SurfaceElementIndex> (const Mesh & mesh) { return mesh.GetNSE(); }
  template <> size_t NumElements<SegmentIndex> (const Mesh & mesh) { return mesh.GetNSeg(); }

  template <typename TIndex>
  void Mesh :: AllocateHPInfo ()
  {
    auto & info = HPInfo<TIndex>();
    size_t n = NumElements<TIndex>(*this);
    if (info.Size() >= n) return;
    size_t oldsize = info.Size();
    info.SetSize (n);
    for (size_t i = oldsize; i < n; i++)
      info[TIndex::FromNr0(i)] = HPElementInfo();
  }
  template void Mesh :: AllocateHPInfo<ElementIndex> ();
  template void Mesh :: AllocateHPInfo<SurfaceElementIndex> ();
  template void Mesh :: AllocateHPInfo<SegmentIndex> ();

  template <typename TIndex>
  void Mesh :: SetHPInfo (TIndex i, HPElementInfo val)
  {
    auto & info = HPInfo<TIndex>();
    if (!info.Range().Contains(i))
      {
        if (val.hp_elnr == -1 && val.orderx == 1 && val.ordery == 1 && val.orderz == 1) return;
        AllocateHPInfo<TIndex>();
      }
    info[i] = val;
  }
  template void Mesh :: SetHPInfo<ElementIndex> (ElementIndex, HPElementInfo);
  template void Mesh :: SetHPInfo<SurfaceElementIndex> (SurfaceElementIndex, HPElementInfo);
  template void Mesh :: SetHPInfo<SegmentIndex> (SegmentIndex, HPElementInfo);

  // apply the deletion order of Mesh::Compress (the last element moves into the hole) to a side array
  template <typename TIndex, typename T, typename TELS, typename DELETED>
  static void CompressSideArray (Array<T,TIndex> & data, const TELS & els, DELETED deleted)
  {
    if (data.Size() == 0) return;
    size_t n = els.Size();
    if (data.Size() < n)
      {
        size_t oldsize = data.Size();
        data.SetSize (n);
        for (size_t i = oldsize; i < n; i++) data[TIndex::FromNr0(i)] = T();
      }
    Array<size_t> old_of_new (n);
    for (size_t i = 0; i < n; i++) old_of_new[i] = i;
    for (size_t i = 0; i < n; )
      if (deleted (els[TIndex::FromNr0(old_of_new[i])]))
        old_of_new[i] = old_of_new[--n];
      else
        i++;
    Array<T,TIndex> ndata (n);
    for (size_t i = 0; i < n; i++)
      ndata[TIndex::FromNr0(i)] = data[TIndex::FromNr0(old_of_new[i])];
    data = std::move (ndata);
  }


  Mesh & Mesh :: operator= (const Mesh & mesh2)
  {
    geometry = mesh2.geometry;
    dimension = mesh2.dimension;
    points = mesh2.points;
    segments = mesh2.segments;
    surfelements = mesh2.surfelements;
    volelements = mesh2.volelements;
    hp_seginfo = mesh2.hp_seginfo;
    hp_surfinfo = mesh2.hp_surfinfo;
    hp_volinfo = mesh2.hp_volinfo;
    lockedpoints = mesh2.lockedpoints;
    regions = mesh2.regions;
    dimension = mesh2.dimension;
    hglob = mesh2.hglob;
    hmin = mesh2.hmin;
    maxhdomain = mesh2.maxhdomain;
    pointelements = mesh2.pointelements;

    numvertices = mesh2.numvertices;

    return *this;
  }





  PointIndex Mesh :: AddPoint (const netgen::Point<3> & p, int layer)
  { 
    return AddPoint (p, layer, INNERPOINT);
  }

  PointIndex Mesh :: AddPoint (const netgen::Point<3> & p, int layer, POINTTYPE type)
  { 

    // PointIndex pi = points.End();
    PointIndex pi = *points.Range().end();
    if (points.Size() == points.AllocSize())
      {
        std::lock_guard<std::mutex> lock(mutex);
        points.Append ( MeshPoint (p, layer, type) ); 
      }
    else
      {
        points.Append ( MeshPoint (p, layer, type) ); 
      }

    timestamp = NextTimeStamp();

    return pi;
  }


  SegmentIndex Mesh :: AddSegment (const Segment & s)
  { 
    std::lock_guard<std::mutex> lock(mutex);
    timestamp = NextTimeStamp();

    // int maxn = max2 (s[0], s[1]);
    // maxn += 1-PointIndex::BASE;
    int maxn = max2 (s[0].Nr1(),
                     s[1].Nr1());

    /*
      if (maxn > ptyps.Size())
      {
      int maxo = ptyps.Size();
      ptyps.SetSize (maxn);
      for (int i = maxo; i < maxn; i++)
      ptyps[i] = INNERPOINT;
      }

      if (ptyps[s[0]] > EDGEPOINT) ptyps[s[0]] = EDGEPOINT;
      if (ptyps[s[1]] > EDGEPOINT) ptyps[s[1]] = EDGEPOINT;
    */

    if (maxn <= points.Size())
      {
        if (points[s[0]].Type() > EDGEPOINT)
          points[s[0]].SetType (EDGEPOINT);
        if (points[s[1]].Type() > EDGEPOINT)
          points[s[1]].SetType (EDGEPOINT);
      }
    /*
      else
      {
      cerr << "edge points nrs > points.Size" << endl;
      }
    */

    SegmentIndex si = IndexBASE<SegmentIndex>() + segments.Size();
    segments.Append (s); 
    return si;
  }

  SurfaceElementIndex Mesh :: AddSurfaceElement (const Element2dRef & el)
  {     
    timestamp = NextTimeStamp();

    PointIndex maxn = el[0];
    for (int i = 1; i < el.GetNP(); i++)
      if (el[i] > maxn) maxn = el[i];

    /*
    maxn += 1-PointIndex::BASE;
    if (maxn <= points.Size())
      {
        for (int i = 0; i < el.GetNP(); i++)
          if (points[el[i]].Type() > SURFACEPOINT)
            points[el[i]].SetType(SURFACEPOINT);
      }
    */
    // if (maxn < points.End())
    if (maxn < *points.Range().end())
      for (PointIndex pi : el.PNums())
        if (points[pi].Type() > SURFACEPOINT)
          points[pi].SetType(SURFACEPOINT);

    
    SurfaceElementIndex si = IndexBASE<SurfaceElementIndex>() + surfelements.Size();
    if (surfelements.AllocSize() == surfelements.Size())
      {
        std::lock_guard<std::mutex> lock(mutex);
        surfelements.Append (el);
      }
    else
      {
        surfelements.Append (el);        
      }

    if (!HasFaceDescriptor(el))
      cerr << "has no face descriptor: fd.size = " << Regions<2>().Size() << ", ind = " << el.GetIndex() << endl;

    surfelements.Last().Header().next = Regions<2>()[el.GetIndex()].firstelement;
    Regions<2>()[el.GetIndex()].firstelement = si;

    if (SurfaceArea().Valid())
      SurfaceArea().Add (el);

    if (PreviewEnabled())
      PreviewAppend (el);

    return si;
  }

  static void PreviewAddTrigs (const Mesh & mesh, const Element2dRef & el,
                               std::vector<float> & coords, std::vector<int> & faces,
                               std::vector<uint8_t> & edges)
  {
    int nv = el.GetNV();
    int fi = el.GetIndex().Nr0();
    for (int i = 1; i+1 < nv; i++)
      {
        for (int j : { 0, i, i+1 })
          {
            const auto & p = mesh[el[j]];
            for (int k = 0; k < 3; k++)
              coords.push_back (p(k));
          }
        faces.push_back (fi);
        edges.push_back (uint8_t((i == 1 ? 1 : 0) | 2 | (i+2 == nv ? 4 : 0)));
      }
  }

  void Mesh :: PreviewAppend (const Element2dRef & el)
  {
    std::lock_guard<std::mutex> lock(preview.mutex);
    PreviewAddTrigs (*this, el, preview.coords, preview.faces, preview.edges);
  }


  ElementIndex Mesh :: AddVolumeElement (const ElementRef & el)
  { 
    /*
    int maxn = el[0];
    for (int i = 1; i < el.GetNP(); i++)
      if (el[i] > maxn) maxn = el[i];

    maxn += 1-PointIndex::BASE;
    */
    
    /*
      if (maxn > ptyps.Size())
      {
      int maxo = ptyps.Size();
      ptyps.SetSize (maxn);
      for (i = maxo+PointIndex::BASE; 
      i < maxn+PointIndex::BASE; i++)
      ptyps[i] = INNERPOINT;
      }
    */
    /*
      if (maxn > points.Size())
      {
      cerr << "add vol element before point" << endl;
      }
    */

    ElementIndex ve = IndexBASE<ElementIndex>() + volelements.Size();

    if (volelements.Size() == volelements.AllocSize())
      {
        std::lock_guard<std::mutex> lock(mutex);
        volelements.Append (el);
      }
    else
      {
        volelements.Append (el);
      }
    volelements.Last().Touch();
    volelements.Last().Flags().fixed = 0;
    volelements.Last().Flags().deleted = 0;

    // while (volelements.Size() > eltyps.Size())
    // eltyps.Append (FREEELEMENT);

    timestamp = NextTimeStamp();

    return ve;
  }





  void Mesh :: Save (const filesystem::path & filename) const
  {
    string ext0 = filename.stem().extension().string();
    string ext = filename.extension().string();

    if (ext0 == ".vol" && ext == ".bin")
    {
        BinaryOutArchive in(filename);
        in & const_cast<Mesh&>(*this);
        return;
    }

    ostream * outfile;
    if (ext0 == ".vol" && ext == ".gz")
      outfile = new ogzstream(filename);
    else if (ext == ".vol")
      outfile = new ofstream(filename);
    else
      outfile = new ogzstream(filesystem::path(filename).concat(".vol.gz"));

    Save(*outfile);
    delete outfile;
  }



  void Mesh :: Save (ostream & outfile) const
  {
    static Timer timer("Mesh::Save"); RegionTimer rt(timer);
    /*
    auto seg_fdi = [this](const Segment& s) -> int {
      if (HasEdgeDescriptor(s))
        { auto fdi = Regions<1>()[s.GetIndex()].GetIndex(); if (fdi.IsValid()) return fdi.Nr1(); }
      return -1;
    };
    */

    double scale = 1;  // globflags.GetNumFlag ("scale", 1);
    int inverttets = 0;  // globflags.GetDefineFlag ("inverttets");
    int invertsurf = 0;  // globflags.GetDefineFlag ("invertsurfacemesh");

    outfile << "# Generated by NETGEN " << GetLibraryVersion("netgen") << endl << endl;


    outfile << "mesh3d" << "\n";

    outfile << "dimension\n" << GetDimension() << "\n";

    outfile << "geomtype\n" << int(geomtype) << "\n";

    outfile << "\n";
    outfile << "# surfnr\tdomin\tdomout\ttlosurf\tbcprop\n";
    outfile << "facedescriptors\n";
    outfile << GetNFD() << "\n";
    for(auto & fd : FaceDescriptors())
        outfile << fd.SurfNr() << ' ' << fd.DomainIn() << ' ' << fd.DomainOut() << ' ' << fd.TLOSurface() << ' ' << fd.BCProperty() << '\n';


    outfile << "\n";
    outfile << "# surfnr    bcnr   domin  domout      np      p1      p2      p3"
            << "\n";


    switch (geomtype)
      {
      case GEOM_STL:
        outfile << "surfaceelementsgi" << "\n";
        break;
      case GEOM_OCC: case GEOM_ACIS:
        outfile << "surfaceelementsuv" << "\n";
        break;
      default:
        outfile << "surfaceelements" << "\n";
      }

    outfile << GetNSE() << "\n";

    for (auto el : SurfaceElements())
      {
        if (el.GetIndex().IsValid())
          {
            outfile << " " << GetFaceDescriptor(el.GetIndex ()).SurfNr()+1;
            outfile << " " << GetFaceDescriptor(el.GetIndex ()).BCProperty();
            outfile << " " << GetFaceDescriptor(el.GetIndex ()).DomainIn();
            outfile << " " << GetFaceDescriptor(el.GetIndex ()).DomainOut();
          }
        else
          outfile << " 0 0 0";

        Element2d sel (el);
        if (invertsurf)
          sel.Invert();

        outfile << " " << sel.GetNP();
        for (int j = 0; j < sel.GetNP(); j++)
          outfile << " " << sel[j];

        switch (geomtype)
          {
          case GEOM_STL:
            for (int j = 1; j <= sel.GetNP(); j++)
              outfile << " " << sel.GeomInfoPi(j).trignum;
            break;
          case GEOM_OCC: case GEOM_ACIS:
            for (int j = 1; j <= sel.GetNP(); j++)
              {
                outfile << " " << sel.GeomInfoPi(j).u;
                outfile << " " << sel.GeomInfoPi(j).v;
              }
            break;
          default:
            ;
          }
        outfile << "\n";
      }

    outfile << "\n" << "\n";
    outfile << "#  matnr      np      p1      p2      p3      p4" << "\n";
    outfile << "volumeelements" << "\n";
    outfile << GetNE() << "\n";

    for (auto el2 : VolumeElements())
      {
        outfile << el2.GetIndex();
        outfile << " " << el2.GetNP();

        Element el (el2);
        if (inverttets) el.Invert();

        for (int j = 0; j < el.GetNP(); j++)
          outfile << " " << el[j];
        outfile << "\n";
      }


    outfile << "\n" << "\n";
    //     outfile << "   surf1   surf2      p1      p2" << "\n";
    outfile << "# p1   p2   trignum1   trignum2   dist1   dist2   edsi \n";
    outfile << "edgesegmentsgi3" << "\n";
    outfile << GetNSeg() << "\n";

    for (auto & seg : LineSegments())
      {
        outfile.width(8);
        outfile << seg[0];
        outfile.width(8);
        outfile << seg[1];
        outfile << " ";
        outfile.width(8);
        outfile << seg.GeomInfo(0).trignum;
        outfile << " ";
        outfile.width(8);
        outfile << seg.GeomInfo(1).trignum;
        outfile << " ";
        outfile.width(12);
        outfile.precision(16);
        outfile << seg.EPGeomInfo(0).dist;
        outfile << " ";
        outfile.width(12);
        outfile << seg.EPGeomInfo(1).dist;
        outfile << " ";
        outfile.width(8);
        outfile << seg.GetIndex() - 1;
        outfile << "\n";
      }


    outfile << "\n" << "\n";
    outfile << "#          X             Y             Z" << "\n";
    outfile << "points" << "\n";
    outfile << GetNP() << "\n";
    outfile.precision(16);
    outfile.setf (ios::fixed, ios::floatfield);
    outfile.setf (ios::showpoint);

    /*
    for (pi = PointIndex::BASE; 
         pi < GetNP()+PointIndex::BASE; pi++)
    */
    for (PointIndex pi : (*this).Points().Range())
      {
        outfile.width(22);
        outfile << (*this)[pi](0)/scale << "  ";
        outfile.width(22);
        outfile << (*this)[pi](1)/scale << "  ";
        outfile.width(22);
        outfile << (*this)[pi](2)/scale << "\n";
      }

    outfile << "\n" << "\n";
    outfile << "#          pnum             index" << "\n";
    outfile << "pointelements" << "\n";
    outfile << pointelements.Size() << "\n";

    for (int i = 0; i < pointelements.Size(); i++)
      {
        outfile.width(8);
        outfile << pointelements[i].pnum << "  ";
        outfile.width(8);
        outfile << pointelements[i].index << "\n";
      }

    if (ident -> GetMaxNr() > 0)
      {
        outfile << "identifications\n";
        Array<PointIndices<2>> identpairs;
        int cnt = 0;
        for (int i = 1; i <= ident -> GetMaxNr(); i++)
          {
            ident -> GetPairs (i, identpairs);
            cnt += identpairs.Size();
          }
        outfile << cnt << "\n";
        for (int i = 1; i <= ident -> GetMaxNr(); i++)
          {
            ident -> GetPairs (i, identpairs);
            for (auto pair : identpairs)
              {
                outfile.width (8);
                outfile << pair[0];
                outfile.width (8);
                outfile << pair[1];
                outfile.width (8);
                outfile << i << "\n";
              }
          }

        outfile << "identificationtypes\n";
        outfile << ident -> GetMaxNr() << "\n";
        for (int i = 1; i <= ident -> GetMaxNr(); i++)
          {
            int type = ident -> GetType(i);
            outfile << " " << type;
          }
        outfile << "\n";
        outfile << "identificationnames\n";
        outfile << ident -> GetMaxNr() << "\n";
        for (int i = 1; i <= ident -> GetMaxNr(); i++)
          {
            string name = ident -> GetName(i);
            if(name == "")
              name = "default";
            outfile << name << "\n";
          }
      }

    {
      auto domnames = DomainNames();
      int cntmat = 0;
      for (auto & n : domnames)
        if (n && n->length())
          cntmat++;

      if (cntmat)
        {
          outfile << "materials" << endl;
          outfile << cntmat << endl;
          for (int i = 0; i < domnames.Size(); i++)
            if (domnames[i] && domnames[i]->length())
              outfile << i+1 << " " << *domnames[i] << endl;
        }
    }


    {
      auto names = BCNamesByNumber();
      if (names.Size())
        {
          outfile << "\n\nbcnames" << endl << names.Size() << endl;
          for (int i = 0; i < names.Size(); i++)
            outfile << i+1 << "\t" << names[i] << endl;
          outfile << endl << endl;
        }
    }
    int ncd2 = dimension >= 2 ? GetNRegions(dimension-2) : 0;
    int cntcd2names = 0;
    for (int ii = 0; ii < ncd2; ii++)
      {
        auto n = GetRegionName(dimension-2, ii+1);
        if (n != "default" && !n.empty()) cntcd2names++;
      }

    if(cntcd2names)
      {
        outfile << "\n\ncd2names" << endl << ncd2 << endl;
        for (int i=0; i<ncd2; i++)
          outfile << i+1 << "\t" << GetRegionName(dimension-2, i+1) << endl;
        outfile << endl << endl;
      }

    if (Regions<1>().Size())
      {
        outfile << "\n\nedgedescriptors" << endl << Regions<1>().Size() << endl;
        for (int ii = 0; ii < Regions<1>().Size(); ii++)
          {
            const EdgeRegion & ed = Regions<1>()[EdgeRegionIndex::FromNr0(ii)];
            outfile << ed.EdgeNr() << " "
                    << ed.SurfNr(0) << " " << ed.SurfNr(1) << " "
                    << ed.SingEdgeLeft() << " " << ed.SingEdgeRight() << " "
                    << ed.TLOSurface() << " "
                    << ed.DomainIn() << " " << ed.DomainOut() << " "
                    << ed.GetName() << endl;
          }
        outfile << endl << endl;
      }

    int ncd3 = GetNCD3Names();
    int cntcd3names = 0;
    for (int ii = 0; ii < ncd3; ii++)
      if (Regions<0>()[VertexRegionIndex::FromNr0(ii)].HasName()) cntcd3names++;

    if(cntcd3names)
      {
        outfile << "\n\ncd3names" << endl << ncd3 << endl;
        for (int i=0; i<ncd3; i++)
          outfile << i+1 << "\t" << GetCD3Name(i) << endl;
        outfile << endl << endl;
      }

    /*
      if ( GetDimension() == 2 )
      {
      for (i = 1; i <= GetNSeg(); i++)
      {
      const Segment & seg = LineSegment (i);
      if ( ! bcprops.Contains(seg.si) && seg.GetBCName() != "" )
      {
      bcprops.Append(seg.si);
      cntbcnames++;
      }
      }
      }
      else
      {
      for (sei = 0; sei < GetNSE(); sei++)
      {
      if ((*this)[sei].GetIndex())
      {
      int bcp = GetFaceDescriptor((*this)[sei].GetIndex ()).BCProperty();
      string name = GetFaceDescriptor((*this)[sei].GetIndex ()).BCName();
      if ( !bcprops.Contains(bcp) &&
      name != "" )
      {
      bcprops.Append(bcp);
      cntbcnames++;
      }
      }
      }
      }

      bcprops.SetSize(0);
      if ( cntbcnames )
      {
      outfile << "\nbcnames" << endl << cntbcnames << endl;
      if ( GetDimension() == 2 )
      {
      for (i = 1; i <= GetNSeg(); i++)
      {
      const Segment & seg = LineSegment (i);
      if ( ! bcprops.Contains(seg.si) && seg.GetBCName() != "" )
      {
      bcprops.Append(seg.si);
      outfile << seg.si << "\t" << seg.GetBCName() << endl;
      }
      }
      }
      else
      {
      for (sei = 0; sei < GetNSE(); sei++)
      {
      if ((*this)[sei].GetIndex())
      {
      int bcp = GetFaceDescriptor((*this)[sei].GetIndex ()).BCProperty();
      string name = GetFaceDescriptor((*this)[sei].GetIndex ()).BCName();
      if ( !bcprops.Contains(bcp) &&
      name != "" )
      {
      bcprops.Append(bcp);
      outfile << bcp << "\t" << name << endl;
      }
      }
      }
      }
      outfile << endl << endl;
      }
    */

    int cnt_sing = 0;
    // for (PointIndex pi = points.Begin(); pi < points.End(); pi++)
    // if ((*this)[pi].Singularity()>=1.) cnt_sing++;
    for (auto & p : points)
      if (p.Singularity() >= 1.) cnt_sing++;
      
    if (cnt_sing)
      {
        outfile << "singular_points" << endl << cnt_sing << endl;
        // for (PointIndex pi = points.Begin(); pi < points.End(); pi++)
        for (PointIndex pi : points.Range())
          if ((*this)[pi].Singularity()>=1.) 
            outfile << pi << "\t" << (*this)[pi].Singularity() << endl;
      }

    auto sing_left = [&](SegmentIndex si)
    { return HasEdgeDescriptor(segments[si]) ? GetEdgeDescriptor(segments[si].GetIndex()).SingEdgeLeft() : 0.0; };
    auto sing_right = [&](SegmentIndex si)
    { return HasEdgeDescriptor(segments[si]) ? GetEdgeDescriptor(segments[si].GetIndex()).SingEdgeRight() : 0.0; };

    cnt_sing = 0;
    for (SegmentIndex si : LineSegments().Range())
      if (sing_left(si)) cnt_sing++;
    if (cnt_sing)
      {
        outfile << "singular_edge_left" << endl << cnt_sing << endl;
        for (SegmentIndex si : LineSegments().Range())
          if (sing_left(si))
            outfile << si << "\t" << sing_left(si) << endl;
      }

    cnt_sing = 0;
    for (SegmentIndex si : LineSegments().Range())
      if (sing_right(si)) cnt_sing++;
    if (cnt_sing)
      {
        outfile << "singular_edge_right" << endl << cnt_sing << endl;
        for (SegmentIndex si : LineSegments().Range())
          if (sing_right(si))
            outfile << si << "\t" << sing_right(si) << endl;
      }


    cnt_sing = 0;
    for (auto el : SurfaceElements())
      if ( GetFaceDescriptor (el.GetIndex()).domin_singular) 
        cnt_sing++;

    if (cnt_sing)
      {
        outfile << "singular_face_inside" << endl << cnt_sing << endl;
        for (SurfaceElementIndex sei : SurfaceElements().Range())
          if ( GetFaceDescriptor ((*this)[sei].GetIndex()).domin_singular) 
            outfile << sei  << "\t" << 
              GetFaceDescriptor ((*this)[sei].GetIndex()).domin_singular  << endl;
      }

    cnt_sing = 0;
    for (auto el : SurfaceElements())
      if ( GetFaceDescriptor (el.GetIndex()).domout_singular) cnt_sing++;
    if (cnt_sing)
      {
        outfile << "singular_face_outside" << endl << cnt_sing << endl;
        for (SurfaceElementIndex sei : SurfaceElements().Range())
          if ( GetFaceDescriptor ((*this)[sei].GetIndex()).domout_singular) 
            outfile << sei << "\t" 
                    << GetFaceDescriptor ((*this)[sei].GetIndex()).domout_singular << endl;
      }


    // Philippose - 09/07/2009
    // Add mesh face colours to Netgen Vol file format
    // The colours are saved in RGB triplets
    int cnt_facedesc = GetNFD();
    if (cnt_facedesc)
    {
       outfile << endl << endl << "#   Surfnr     Red     Green     Blue" << endl;
       outfile << "face_colours" << endl << cnt_facedesc << endl;

       outfile.precision(8);
       outfile.setf(ios::fixed, ios::floatfield);
       outfile.setf(ios::showpoint);

       for(int i = 1; i <= cnt_facedesc; i++)
       {
          outfile.width(8);
          outfile << GetFaceDescriptor(FaceRegionIndex::FromNr1(i)).SurfNr()+1 << " ";
          outfile.width(12);
          outfile << GetFaceDescriptor(FaceRegionIndex::FromNr1(i)).SurfColour()[0] << " ";
          outfile.width(12);
          outfile << GetFaceDescriptor(FaceRegionIndex::FromNr1(i)).SurfColour()[1] << " ";
          outfile.width(12);
          outfile << GetFaceDescriptor(FaceRegionIndex::FromNr1(i)).SurfColour()[2];
          outfile << endl;
       }

       outfile << "face_transparencies" << endl << cnt_facedesc << endl;
       for(int i = 1; i <= cnt_facedesc; i++)
         {
           outfile.width(8);
           outfile << GetFaceDescriptor(FaceRegionIndex::FromNr1(i)).SurfNr()+1 << " ";
           outfile.width(12);
           outfile << GetFaceDescriptor(FaceRegionIndex::FromNr1(i)).SurfColour()[3] << endl;
         }
    }

    // Curved elements section (text archive, upstream Netgen master)
    if (curvedelems && curvedelems->IsHighOrder())
    {
      outfile << "\ncurvedelements" << endl;
      // The face colours above leave the stream at precision(8), which
      // rounds the curving coefficients to ~1e-8 and caps the geometric
      // accuracy of every order (upstream Netgen through master 5bb3cbc2).
      // 17 significant digits round-trip a double exactly.
      outfile.precision(17);
      shared_ptr<std::ostream> spoutfile(&outfile, [](void*) noexcept {});
      TextOutArchive out(std::move(spoutfile));
      out & (*curvedelems);
    }



    
    outfile << endl << endl << "endmesh" << endl << endl;
    if (geometry)
      geometry -> SaveToMeshFile (outfile);
  }



  void Mesh :: Load (const filesystem::path & filename)
  {
    PrintMessage (1, "filename = ", filename);

    string ext0 = filename.stem().extension().string();
    string ext = filename.extension().string();

    if (ext0 == ".vol" && ext == ".bin")
    {
        BinaryInArchive in(filename);
        in & (*this);
        return;
    }

    istream * infile = NULL;

    if (ext0 == ".vol" && ext == ".gz")
      infile = new igzstream (filename);
    else
      infile = new ifstream (filename);

    if (! (infile -> good()) )
      throw NgException ("mesh file not found");

    Load(*infile);
    delete infile;
  }



  // Reads mandatory integer and optional string token from input stream
  // used for parsing bcnames, cd2names etc.
  void ReadNumberAndName( istream & infile, int & i, string & s )
  {
    string line;
    std::istringstream iline;

    bool empty_line = true;

    while(empty_line && infile)
      {
        std::getline(infile, line);
        iline = std::istringstream{line};
        if(iline >> i)
          {
            empty_line = false;
            // skip a single whitespace character after the number, then read the rest
            if(iline.peek() == ' ' || iline.peek() == '\t')
              iline.get();
            std::getline(iline, s);
          }
      }

    if(!infile)
        throw Exception("Reached end of file while parsing");
  }

  void Mesh :: Load (istream & infile)
  {
    static Timer timer("Mesh::Load"); RegionTimer rt(timer);
    if (! (infile.good()) )
      {
        cout << "cannot load mesh" << endl;
        throw NgException ("mesh file not found");
      }

    // int rank = GetCommunicator().Rank();
    int ntasks = GetCommunicator().Size();
    
    char str[100];
    int n;

    double scale = 1;  // globflags.GetNumFlag ("scale", 1);
    int inverttets = 0;  // globflags.GetDefineFlag ("inverttets");
    int invertsurf = 0;  // globflags.GetDefineFlag ("invertsurfacemesh");


    Regions<2>().SetSize(0);

    bool endmesh = false;

    bool has_facedescriptors = false;
    // per-segment data read alongside the segments (edgesegmentsgi2 format)
    Array<std::pair<int,int>, SegmentIndex> seg_surfnrs;
    Array<int, SegmentIndex> seg_edgenrs;
    Array<int, SegmentIndex> seg_sis;
    Array<string> bcnames2d;
    

    while (infile.good() && !endmesh)
      {
        infile >> str;
        if (strcmp (str, "dimension") == 0)
          {
            infile >> dimension;
          }

        if (strcmp (str, "geomtype") == 0)
          {
            int hi;
            infile >> hi;
            geomtype = GEOM_TYPE(hi);
          }

        if (strcmp (str, "facedescriptors") == 0)
          {
            has_facedescriptors = true;
            int nfd;
            infile >> nfd;
            for([[maybe_unused]] auto i : Range(nfd))
            {
                int surfnr, domin, domout, tlosurf, bcprop;
                infile >> surfnr >> domin >> domout >> tlosurf >> bcprop;
                auto faceind = AddFaceDescriptor (FaceRegion(surfnr, domin, domout, tlosurf));
                GetFaceDescriptor(faceind).SetBCProperty(bcprop);
            }
          }


        if (strcmp (str, "surfaceelements") == 0 || strcmp (str, "surfaceelementsgi")==0 || strcmp (str, "surfaceelementsuv") == 0)
          {
            static Timer t1("read surface elements"); RegionTimer rt1(t1);
            infile >> n;
            PrintMessage (3, n, " surface elements");

            bool geominfo = strcmp (str, "surfaceelementsgi") == 0;
            bool uv = strcmp (str, "surfaceelementsuv") == 0;


            for (int i = 0; i < n; i++)
              {
                int surfnr, bcp, domin, domout, nep, faceind = 0;

                infile >> surfnr >> bcp >> domin >> domout;
                surfnr--;

                bool invert_el = false;
                /*
                if (domin == 0) 
                  {
                    invert_el = true;
                    Swap (domin, domout);
                  }
                */
                
                for (int j = 1; j <= Regions<2>().Size(); j++)
                  if (GetFaceDescriptor(FaceRegionIndex::FromNr1(j)).SurfNr() == surfnr &&
                      GetFaceDescriptor(FaceRegionIndex::FromNr1(j)).BCProperty() == bcp &&
                      GetFaceDescriptor(FaceRegionIndex::FromNr1(j)).DomainIn() == domin &&
                      GetFaceDescriptor(FaceRegionIndex::FromNr1(j)).DomainOut() == domout)
                    faceind = j;

                // if (Regions<2>().Size()) faceind = 1;   // for timing 

                if (!faceind)
                  {
                    faceind = AddFaceDescriptor (FaceRegion(surfnr, domin, domout, 0)).Nr1();
                    GetFaceDescriptor(FaceRegionIndex::FromNr1(faceind)).SetBCProperty (bcp);
                  }

                infile >> nep;
                if (!nep) nep = 3;

                Element2d tri(nep);
                tri.SetIndex(FaceRegionIndex::FromNr1(faceind));

                for (int j = 1; j <= nep; j++)
                  infile >> tri.PNum(j);

                if (geominfo)
                  for (int j = 1; j <= nep; j++)
                    infile >> tri.GeomInfoPi(j).trignum;

                if (uv)
                  for (int j = 1; j <= nep; j++)
                    infile >> tri.GeomInfoPi(j).u >> tri.GeomInfoPi(j).v;
                
                if (invertsurf) tri.Invert();
                if (invert_el) tri.Invert();

                AddSurfaceElement (tri);
              }
          }

        if (strcmp (str, "volumeelements") == 0)
          {
            static Timer t1("read volume elements"); RegionTimer rt1(t1);
            infile >> n;
            PrintMessage (3, n, " volume elements");
            for (int i = 0; i < n; i++)
              {
                Element el(TET);
                int hi, nep;
                infile >> hi;
                if (hi == 0) hi = 1;
                el.SetIndex(VolumeRegionIndex::FromNr1(hi));
                infile >> nep;
                el.SetNP(nep);
                el.SetCurved (nep != 4);
                for (int j = 0; j < nep; j++)
                  infile >> el[j];

                if (inverttets)
                  el.Invert();

                AddVolumeElement (el);
              }
          }


        if (strcmp (str, "edgesegments") == 0)
          {
            static Timer t1("read edge segments"); RegionTimer rt1(t1);
            infile >> n;
            for (int i = 0; i < n; i++)
              {
                Segment seg;
                int hi;
                int si_tmp;
                infile >> si_tmp >> hi >> seg[0] >> seg[1];
                seg.SetIndex(EdgeRegionIndex::FromNr1(si_tmp));
                AddSegment (seg);
              }
          }



        if (strcmp (str, "edgesegmentsgi") == 0)
          {
            static Timer t1("read edge segmentsgi"); RegionTimer rt1(t1);
            infile >> n;
            for (int i = 0; i < n; i++)
              {
                Segment seg;
                int hi;
                int si_tmp;
                infile >> si_tmp >> hi >> seg[0] >> seg[1]
                       >> seg.GeomInfo(0).trignum
                       >> seg.GeomInfo(1).trignum;
                seg.SetIndex(EdgeRegionIndex::FromNr1(si_tmp));
                AddSegment (seg);
              }
          }

        if (strcmp (str, "edgesegmentsgi2") == 0)
          {
            static Timer t1("read edge segmentsgi2"); RegionTimer rt1(t1);
            int a; 
            infile >> a;
            n=a; 

            PrintMessage (3, n, " curve elements");

            for (int i = 0; i < n; i++)
              {
                Segment seg;
                int hi;
                int surfnr1_tmp, surfnr2_tmp;
                int edgenr_tmp;
                int si_tmp;
                int epgi_edgenr_tmp;
                infile >> si_tmp >> hi >> seg[0] >> seg[1]
                       >> seg.GeomInfo(0).trignum
                       >> seg.GeomInfo(1).trignum
                       >> surfnr1_tmp >> surfnr2_tmp
                       >> edgenr_tmp
                       >> seg.EPGeomInfo(0).dist
                       >> epgi_edgenr_tmp
                       >> seg.EPGeomInfo(1).dist;

                if (geomtype == GEOM_OCC)
                  seg.SetIndex(EdgeRegionIndex::FromNr1(edgenr_tmp));
                else if (geomtype == GEOM_CSG)
                  seg.SetIndex(EdgeRegionIndex::FromNr1(edgenr_tmp));
                else
                  seg.SetIndex(EdgeRegionIndex::FromNr1(si_tmp));

                surfnr1_tmp--;
                surfnr2_tmp--;

                seg_edgenrs.Append(edgenr_tmp);
                seg_surfnrs.Append({surfnr1_tmp, surfnr2_tmp});
                seg_sis.Append(si_tmp);
                AddSegment (seg);
              }
          }

        if (strcmp (str, "edgesegmentsgi3") == 0)
          {
            static Timer t1("read edge segmentsgi3"); RegionTimer rt1(t1);
            infile >> n;
            PrintMessage (3, n, " curve elements (gi3)");

            for (int i = 0; i < n; i++)
              {
                Segment seg;
                int edsi;
                infile >> seg[0] >> seg[1]
                       >> seg.GeomInfo(0).trignum
                       >> seg.GeomInfo(1).trignum
                       >> seg.EPGeomInfo(0).dist
                       >> seg.EPGeomInfo(1).dist
                       >> edsi;
                seg.SetIndex(EdgeRegionIndex::FromNr0(edsi));
                AddSegment (seg);
              }
          }

        if (strcmp (str, "points") == 0)
          {
            static Timer t1("read points"); RegionTimer rt1(t1);
            infile >> n;
            PrintMessage (3, n, " points");
            for (int i = 0; i < n; i++)
              {
                netgen::Point<3> p;
                infile >> p(0) >> p(1) >> p(2);
                p(0) *= scale;
                p(1) *= scale;
                p(2) *= scale;
                AddPoint (p);
              }
            PrintMessage (3, n, " points done");
          }

        if (strcmp (str, "pointelements") == 0)
          {
            static Timer t1("read point elements"); RegionTimer rt1(t1);
            infile >> n;
            PrintMessage (3, n, " pointelements");
            for (int i = 0; i < n; i++)
              {
                Element0d el;
                int index;
                infile >> el.pnum >> index;
                el.SetIndex(VertexRegionIndex::FromNr1(index));
                pointelements.Append (el);
              }
            PrintMessage (3, n, " pointelements done");
          }

        if (strcmp (str, "identifications") == 0)
          {
            infile >> n;
            PrintMessage (3, n, " identifications");
            for (int i = 0; i < n; i++)
              {
                PointIndex pi1, pi2;
                int ind;
                infile >> pi1 >> pi2 >> ind;
                ident -> Add (pi1, pi2, ind);
              }
          }

        if (strcmp (str, "identificationtypes") == 0)
          {
            infile >> n;
            PrintMessage (3, n, " identificationtypes");
            for (int i = 1; i <= n; i++)
              {
                int type;
                infile >> type;
                ident -> SetType(i,Identifications::ID_TYPE(type));
              }
          }
        if (strcmp (str, "identificationnames") == 0)
          {
            infile >> n;
            PrintMessage (3, n, " identificationnames");
            for (int i = 1; i <= n; i++)
              {
                string name;
                infile >> name;
                ident -> SetName(i,name);
              }
          }

        if (strcmp (str, "materials") == 0)
          {
            infile >> n;
            for ([[maybe_unused]] auto i : Range(n) )
              {
                int nr;
                string mat;
                ReadNumberAndName( infile, nr, mat );
                SetMaterial (nr, mat.c_str());
              }
          }

        if ( strcmp (str, "bcnames" ) == 0 )
          {
            infile >> n;
            Array<string> names(n);
            names = "default";
            for ( [[maybe_unused]] auto i : Range(n) )
              {
                int nr;
                string nextbcname;
                ReadNumberAndName( infile, nr, nextbcname );
                if (nr >= 1 && nr <= n) names[nr-1] = nextbcname;
              }

            if ( GetDimension() == 3 )
              {
                // the file keys names by bc number
                for (auto el : SurfaceElements())
                  if (el.GetIndex().IsValid())
                    {
                      int bcp = GetFaceDescriptor(el.GetIndex ()).BCProperty();
                      GetFaceDescriptor(el.GetIndex ()).SetBCName((bcp >= 1 && bcp <= n) ? names[bcp-1] : "default");
                    }
              }
            else if ( GetDimension() == 2 )
              bcnames2d = std::move(names);
            else
              for (auto i : Range(n))
                SetBCName(i, names[i]);
          }

        if ( strcmp (str, "cd2names" ) == 0)
          {
            infile >> n;
            Array<int> cd2nrs(n);
            for ( auto i : Range(n) )
              {
                string nextcd2name;
                ReadNumberAndName( infile, cd2nrs[i], nextcd2name );
                SetCD2NameCompat(cd2nrs[i], nextcd2name);
              }
            if (GetDimension() < 2)
              {
                throw NgException("co dim 2 elements not implemented for dimension < 2");
              }
          }

        if ( strcmp (str, "edgedescriptors" ) == 0)
          {
            infile >> n;
            Regions<1>().SetSize(n);
            for (int ii = 0; ii < n; ii++)
              {
                EdgeRegion & ed = Regions<1>()[EdgeRegionIndex::FromNr0(ii)];
                int ednr, s0, s1, tlo;
                double sl, sr;
                infile >> ednr >> s0 >> s1 >> sl >> sr >> tlo;
                ed.SetEdgeNr(ednr);
                ed.SetSurfNr(0, s0);
                ed.SetSurfNr(1, s1);
                ed.SetSingEdgeLeft(sl);
                ed.SetSingEdgeRight(sr);
                ed.SetTLOSurface(tlo);
                // try to read domin/domout (new format) or name (old format)
                int di;
                if (infile >> di)
                  {
                    ed.SetDomainIn(di);
                    int dout;
                    infile >> dout;
                    ed.SetDomainOut(dout);
                    string nm;
                    infile >> nm;
                    ed.SetName(nm);
                    // consume rest of line (may contain legacy fdindex - discard)
                    { string rest; getline(infile, rest); }
                  }
                else
                  {
                    // old format: next token is the name (not an int)
                    infile.clear();
                    string nm;
                    infile >> nm;
                    ed.SetName(nm);
                  }
              }
          }

        if ( strcmp (str, "cd3names" ) == 0)
          {
            infile >> n;
            Array<int> cd3nrs(n);
            SetNCD3Names(n);
            for( auto i : Range(n) )
              {
                string nextcd3name;
                ReadNumberAndName( infile, cd3nrs[i], nextcd3name );
                Regions<0>()[VertexRegionIndex::FromNr1(cd3nrs[i])].SetName(nextcd3name);
              }
            if (GetDimension() < 3)
              {
                throw NgException("co dim 3 elements not implemented for dimension < 3");
              }
          }

        if (strcmp (str, "singular_points") == 0)
          {
            infile >> n;
            for (int i = 0; i < n; i++)
              {
                PointIndex pi;
                double s; 
                infile >> pi;
                infile >> s; 
                (*this)[pi].Singularity (s);
              }
          }

        if (strcmp (str, "singular_edge_left") == 0)
          {
            infile >> n;
            for (int i = 0; i < n; i++)
              {
                SegmentIndex si;
                double s; 
                infile >> si;
                infile >> s; 
                auto & seg = (*this)[si];
                if (HasEdgeDescriptor(seg))
                  GetEdgeDescriptor(seg).SetSingEdgeLeft(s);
              }
          }
        if (strcmp (str, "singular_edge_right") == 0)
          {
            infile >> n;
            for (int i = 0; i < n; i++)
              {
                SegmentIndex si;
                double s; 
                infile >> si;
                infile >> s; 
                auto & seg = (*this)[si];
                if (HasEdgeDescriptor(seg))
                  GetEdgeDescriptor(seg).SetSingEdgeRight(s);
              }
          }

        if (strcmp (str, "singular_face_inside") == 0)
          {
            infile >> n;
            for (int i = 0; i < n; i++)
              {
                SurfaceElementIndex sei;
                double s; 
                infile >> sei;
                infile >> s; 
                GetFaceDescriptor((*this)[sei].GetIndex()).domin_singular = s;
              }
          }

        if (strcmp (str, "singular_face_outside") == 0)
          {
            infile >> n;
            for (int i = 0; i < n; i++)
              {
                SurfaceElementIndex sei;
                double s; 
                infile >> sei;
                infile >> s; 
                GetFaceDescriptor((*this)[sei].GetIndex()).domout_singular = s;
              }
          }

        // Philippose - 09/07/2009
        // Add mesh face colours to Netgen Vol file format
        // The colours are read in as RGB triplets
        if (strcmp (str, "face_colours") == 0)
        {
           int cnt_facedesc = GetNFD();
           infile >> n;
           if(n == cnt_facedesc)
           {
              for(int i = 1; i <= n; i++)
              {
                 int surfnr = 0;
                 Vec<4> surfcolour(0.0,1.0,0.0,1.0);

                 infile >> surfnr 
                        >> surfcolour[0]
                        >> surfcolour[1]
                        >> surfcolour[2];

                 surfnr--;

                 if(has_facedescriptors)
                 {
                    GetFaceDescriptor(FaceRegionIndex::FromNr1(i)).SetSurfColour(surfcolour);
                 }
                 else if(surfnr > 0)
                 {
                    for(int facedesc = 1; facedesc <= cnt_facedesc; facedesc++)
                    {
                       if(surfnr == GetFaceDescriptor(FaceRegionIndex::FromNr1(facedesc)).SurfNr())
                       {
                          GetFaceDescriptor(FaceRegionIndex::FromNr1(facedesc)).SetSurfColour(surfcolour);
                       }
                    }
                 }
              }
           }
        }

        if (strcmp (str, "face_transparencies") == 0)
          {
            int cnt_facedesc = GetNFD();
            infile >> n;
            // int index = 1;
            if(n == cnt_facedesc)
              {
                for(int index = 1; index <= n; index++)
                  {
                    int surfnr;
                    double transp;
                    infile >> surfnr >> transp;
                    surfnr--;
                    if(has_facedescriptors)
                    {
                       auto& fd = GetFaceDescriptor(FaceRegionIndex::FromNr1(index));
                       auto scol = fd.SurfColour();
                       scol[3] = transp;
                       fd.SetSurfColour(scol);
                    }
                    else if(surfnr > 0)
                      {
                        for(int facedesc = 1; facedesc <= cnt_facedesc; facedesc++)
                          {
                            if(surfnr == GetFaceDescriptor(FaceRegionIndex::FromNr1(facedesc)).SurfNr())
                              {
                                auto& fd = GetFaceDescriptor(FaceRegionIndex::FromNr1(facedesc));
                                auto scol = fd.SurfColour();
                                scol[3] = transp;
                                fd.SetSurfColour(scol);
                              }
                          }
                      }
                  }
              }
          }

        if (strcmp (str, "curvedelements") == 0)
          {
            topology.Update();
            shared_ptr<std::istream> spinfile(&infile, [](void*) noexcept {});
            TextInArchive in(std::move(spinfile));
            in & (*curvedelems);


            for (SurfaceElementIndex sei : SurfaceElements().Range())
              (*this)[sei].SetCurved (GetCurvedElements().IsCurved (sei));
            for (ElementIndex ei : VolumeElements().Range())
              (*this)[ei].SetCurved (GetCurvedElements().IsCurved (ei));
          }



        
        if (strcmp (str, "endmesh") == 0)
          endmesh = true;



        strcpy (str, "");
      }




    CalcSurfacesOfNode ();
 
    if (ntasks == 1) // sequential run only
      {
        topology.Update();
        clusters -> Update();
      }

    // reconstruct edge descriptors from segment data if not loaded from file
    if (Regions<1>().Size() == 0)
      ReconstructEdgeDescriptors(&seg_surfnrs, &seg_edgenrs);
    else if (seg_edgenrs.Size() > 0)
      {
        // edgesegmentsgi2 (legacy): match each segment to its ED using
        // the redundant per-segment data that was saved alongside.
        // With per-refedge EDs, multiple EDs can share the same edgenr,
        // so we match by (edgenr, surfnr1, surfnr2) using the temp surfnr data.
        for (auto segi : segments.Range())
          {
            auto & seg = segments[segi];
            int seg_edgenr = seg_edgenrs.Range().Contains(segi) ? seg_edgenrs[segi] : -1;
            int snr1 = -1, snr2 = -1;
            if (seg_surfnrs.Range().Contains(segi))
              {
                snr1 = seg_surfnrs[segi].first;
                snr2 = seg_surfnrs[segi].second;
              }
            // Find matching ED: prefer exact (edgenr, surfnr1, surfnr2, fdindex) match
            int best = -1;
            int best_no_fdi = -1;
            for (int j = 0; j < Regions<1>().Size(); j++)
              {
                const auto & ed = Regions<1>()[EdgeRegionIndex::FromNr0(j)];
                if (ed.EdgeNr() == seg_edgenr)
                  {
                    if (ed.SurfNr(0) == snr1 && ed.SurfNr(1) == snr2)
                      {
                        int seg_si_val = seg_sis.Range().Contains(segi) ? seg_sis[segi] : -1;
                        if (ed.GetIndex().IsValid() && ed.GetIndex().Nr1() == seg_si_val)
                          { best = j; break; }  // exact match including index
                        if (best_no_fdi < 0)
                          best_no_fdi = j;  // surfnr match without fdindex
                      }
                    if (best < 0 && best_no_fdi < 0)
                      best_no_fdi = j;  // first edgenr match as fallback
                  }
              }
            if (best < 0) best = best_no_fdi;
            if (best >= 0)
              seg.SetIndex(EdgeRegionIndex::FromNr0(best));
          }
      }
    // else: edgesegmentsgi3 - segments already have correct indices

    for (auto i : Range(bcnames2d))
      SetBCName(i, bcnames2d[i]);

    RebuildFDIndices();

    SetNextMajorTimeStamp();
    //  PrintMemInfo (cout);
  }


  static const string names_in_descriptors_version = "v6.2.2607-105";

  static std::array<Array<optional<string>>, 4> ReadRegionNamesCompat (Archive & archive)
  {
    std::array<Array<string*>, 4> tmp;
    for (auto & t : tmp)
      archive & t;
    std::array<Array<optional<string>>, 4> names;
    std::set<string*> owned;
    for (int k = 0; k < 4; k++)
      {
        names[k].SetSize(tmp[k].Size());
        for (int i = 0; i < tmp[k].Size(); i++)
          {
            if (tmp[k][i]) { names[k][i] = *tmp[k][i]; owned.insert(tmp[k][i]); }
            else names[k][i] = nullopt;
          }
      }
    for (auto p : owned) delete p;
    return names;
  }

  void Mesh :: DoArchive (Archive & archive)
  {
    static Timer t("Mesh::Archive"); RegionTimer r(t);

    
    
    archive & dimension;
    archive & points;
    archive & surfelements;
    archive & volelements;
    archive & segments;
    archive & pointelements;
    archive & Regions<2>();
    Array<optional<string>> bcnames2d_compat;
    if (archive.GetVersion("netgen") >= names_in_descriptors_version)
      {
        archive.NeedsVersion("netgen", names_in_descriptors_version);
        ArchiveRegionNames<3>(archive);
        ArchiveRegionNames<0>(archive);
      }
    else
      {
        PrintWarning("Mesh archive written by netgen ", archive.GetVersion("netgen"),
                     " uses the old layout of region names. It is converted now, but loading it might not be",
                     " supported by future versions. Save it again to update the file.");
        auto [mats, bcnames, cd2names, cd3names] = ReadRegionNamesCompat(archive);
        SetDomainNames(std::move(mats));
        if (dimension == 1)
          SetRegionNames<0>(bcnames);   // bc names of 1D meshes are the vertex names
        if (dimension == 2)
          bcnames2d_compat = std::move(bcnames);
        if (dimension == 3)
          SetRegionNames<0>(cd3names);
        for (int i = 0; i < cd2names.Size(); i++)
          if (cd2names[i])
            SetCD2NameCompat(i+1, *cd2names[i]);
      }
    archive & numvertices;

    archive & *ident;

    // cout << "archive, ngsversion = " << archive.GetVersion("netgen") << endl;
    if(archive.GetVersion("netgen") >= "v6.2.2103-1")
      {
        // cout << "do the partition" << endl;
        archive.NeedsVersion("netgen", "v6.2.2103-1");
        archive & vol_partition & surf_partition & seg_partition;
      }
    // else
    // cout << "no partition" << endl;
    
    archive.Shallow(geometry);
    archive & *curvedelems;

    if(archive.GetVersion("netgen") >= "v6.2.2603-26")
      {
        archive.NeedsVersion("netgen", "v6.2.2603-26");
        archive & Regions<1>();
      }

    if (archive.Input())
      {
        // int rank = GetCommunicator().Rank();
        int ntasks = GetCommunicator().Size();
        
        RebuildSurfaceElementLists();
        if (Regions<1>().Size() == 0)
          ReconstructEdgeDescriptors(nullptr, nullptr);
        for (int i = 0; i < bcnames2d_compat.Size(); i++)
          if (bcnames2d_compat[i])
            SetBCName(i, *bcnames2d_compat[i]);
        RebuildFDIndices();
        
        CalcSurfacesOfNode ();
        if (ntasks == 1) // sequential run only
          {
            topology.Update();
            clusters -> Update();
          }
        SetNextMajorTimeStamp();
      }
  }


  // --- COMPACT_NETGEN: ~5000 lines removed ---
  // Merge, SetAllocSize, BuildBoundaryEdges, CalcSurfacesOfNode,
  // FindOpenElements, FindOpenSegments, RemoveOneLayerSurfaceElements,
  // FreeOpenElements, LocalH, ElementError, Compress, OrderElements,
  // Check*, FindIllegalTrigs, CalcTotalBad, SurfaceMeshOrientation,
  // Split2Tets, BuildElementSearchTree, SplitIntoParts, ZRefine, etc.
  // These are mesher/optimizer functions not needed for mesh I/O + curving.
  // -----------------------------------------------

  // Upstream 6.2.2608 edge-region reconstruction, kept outside the removed block below.
  void Mesh :: ReconstructEdgeDescriptors (const Array<std::pair<int,int>, SegmentIndex> * seg_surfnrs,
                                           const Array<int, SegmentIndex> * seg_edgenrs)
  {
    Array<string> oldnames;   // names set before the reconstruction (readers name first)
    for (const auto & ed : Regions<1>()) oldnames.Append(ed.GetName());
    Regions<1>().SetSize(0);

    // find the max index value across all segments
    int maxindex = 0;
    for (auto & seg : segments)
      if (seg.GetIndex().Nr1() > maxindex)
        maxindex = seg.GetIndex().Nr1();

    if (maxindex < 1) return;

    // create edge descriptors indexed by seg.GetIndex() (1-based)
    Regions<1>().SetSize(maxindex);

    // mark which indices are used
    Array<bool> used(maxindex);
    used = false;

    for (auto segi : segments.Range())
    {
      auto & seg = segments[segi];
      int idx = seg.GetIndex().Nr1();
      if (idx < 1 || idx > maxindex) continue;

      seg.SetIndex(EdgeRegionIndex::FromNr1(idx));

      if (!used[idx-1])
      {
        used[idx-1] = true;
        int snr1 = -1, snr2 = -1;
        if (seg_surfnrs && seg_surfnrs->Range().Contains(segi))
          {
            snr1 = (*seg_surfnrs)[segi].first;
            snr2 = (*seg_surfnrs)[segi].second;
          }
        auto & ed = Regions<1>()[EdgeRegionIndex::FromNr1(idx)];
        int ednr = -1;
        if (seg_edgenrs && seg_edgenrs->Range().Contains(segi))
          ednr = (*seg_edgenrs)[segi];
        ed.SetEdgeNr(ednr);
        ed.SetSurfNr(0, snr1);
        ed.SetSurfNr(1, snr2);
        ed.SetSingEdgeLeft(0);
        ed.SetSingEdgeRight(0);
        ed.SetTLOSurface(-1);
        ed.SetDomainIn(snr1);
        ed.SetDomainOut(snr2);
      }
    }

    for (int i = 0; i < min(oldnames.Size(), Regions<1>().Size()); i++)
      if (oldnames[i] != "default")
        Regions<1>()[EdgeRegionIndex::FromNr0(i)].SetName(oldnames[i]);

    RebuildFDIndices();
  }

  void Mesh :: RebuildFDIndices ()
  {
    // Recompute EdgeRegion::index_ from surfnr + domin/domout vs face descriptors.
    for (int edi = 0; edi < Regions<1>().Size(); edi++)
      {
        auto & ed = Regions<1>()[EdgeRegionIndex::FromNr0(edi)];
        ed.SetIndex(FaceRegionIndex::INVALID);
        for (auto k : FaceDescriptors().Range())
          {
            const auto & fd = GetFaceDescriptor(k);
            if ((fd.SurfNr() == ed.SurfNr(0) || fd.SurfNr() == ed.SurfNr(1)) &&
                fd.DomainIn() == ed.DomainIn()+1 &&
                fd.DomainOut() == ed.DomainOut()+1)
              {
                ed.SetIndex(k);
                break;
              }
          }
        // fallback: match surfnr only (OCC, STL - domin/domout may be unset)
        if (!ed.GetIndex().IsValid())
          {
            for (auto k : FaceDescriptors().Range())
              {
                const auto & fd = GetFaceDescriptor(k);
                if (fd.SurfNr() == ed.SurfNr(0) || fd.SurfNr() == ed.SurfNr(1))
                  {
                    ed.SetIndex(k);
                    break;
                  }
              }
          }
      }
  }

  // Stub: CalcSurfacesOfNode (called by Load, not needed for compact_netgen)
  void Mesh :: CalcSurfacesOfNode ()
  {
  }


  void Mesh :: SetDimension (int dim)
  {
    // domain names of 2D/1D meshes live in the face/edge descriptors, vertex names stay
    if (dim != 3)
      Regions<3>().SetSize(0);
    dimension = dim;
  }


  void Mesh :: RebuildSurfaceElementLists ()
  {
    static Timer t("Mesh::LinkSurfaceElements"); RegionTimer reg (t);    
    
    for (int i = 0; i < Regions<2>().Size(); i++)
      Regions<2>()[FaceRegionIndex::FromNr0(i)].firstelement = SurfaceElementIndex::INVALID;
    for (int i = surfelements.Size()-1; i >= 0; i--)
      {
        SurfaceElementIndex sei = SurfaceElementIndex::FromNr0(i);
        auto ind = surfelements[sei].GetIndex();
        surfelements[sei].Header().next = Regions<2>()[ind].firstelement;
        Regions<2>()[ind].firstelement = sei;
      }
  }

  void Mesh :: GetSurfaceElementsOfFace (FaceRegionIndex fi, Array<SurfaceElementIndex> & sei) const
  {
    static Timer timer("GetSurfaceElementsOfFace");
    RegionTimer reg (timer);

    if(!fi.IsValid())
    {
        sei.SetSize(GetNSE());
        ParallelForRange( IntRange(GetNSE()), [&sei] (auto myrange)
            {
                for(auto i : myrange)
                    sei[i] = SurfaceElementIndex::FromNr0(i);
            });
        return;
    }

     sei.SetSize(0);

     SurfaceElementIndex si = Regions<2>()[fi].firstelement;
     while (si.IsValid())
     {
       if ( (*this)[si].GetIndex() == fi && (*this)[si][0].IsValid() &&
            !(*this)[si].IsDeleted() )
        {
           sei.Append (si);
        }

        si = (*this)[si].Header().next;
     }
  }





  void Mesh :: ComputeNVertices ()
  {

    numvertices = 0;
    /*
    for (const Element & el : VolumeElements())
      for (PointIndex v : el.Vertices())
        if (v > numvertices) numvertices = v;
        
    for (const Element2dRef & el : SurfaceElements())
      for (PointIndex v : el.Vertices())
        if (v > numvertices) numvertices = v;

    numvertices += 1-PointIndex::BASE;
    */
    numvertices = -1;
    numvertices =
      ParallelReduce (VolumeElements().Size(),
                      [&](size_t nr)
                      {
                        return Max((*this)[ElementIndex::FromNr0(nr)].Vertices()) - IndexBASE<PointIndex>();
                      },
                      [](auto a, auto b) { return a > b ?  a : b; },
                      numvertices);
    numvertices =
      ParallelReduce (SurfaceElements().Size(),
                      [&](size_t nr)
                      {
                        return Max((*this)[SurfaceElementIndex::FromNr0(nr)].Vertices()) - IndexBASE<PointIndex>();
                      },
                      [](auto a, auto b) { return a > b ?  a : b; },
                      numvertices);
    numvertices =
      ParallelReduce (LineSegments().Size(),
                      [&](size_t nr)
                      {
                        return Max((*this)[SegmentIndex::FromNr0(nr)].Vertices()) - IndexBASE<PointIndex>();
                      },
                      [](auto a, auto b) { return a > b ?  a : b; },
                      numvertices);
    numvertices += 1;
  }

  int Mesh :: GetNV () const
  {
    if (numvertices < 0)
      return GetNP();
    else
      return numvertices;
  }


  Table<ElementIndex, PointIndex> Mesh :: CreatePoint2ElementTable(std::optional<TBitArray<PointIndex>> points, int domain) const
  {
    static Timer timer("Mesh::CreatePoint2VolumeElementTable"); RegionTimer rt(timer);
    
    if(points)
      {
        const auto & free_points = *points;
        return ngcore::CreateSortedTable<ElementIndex, PointIndex>( volelements.Range(),
               [&](auto & table, ElementIndex ei)
               {
                 const auto & el = (*this)[ei];
                 if(el.IsDeleted())
                     return;

                 if(domain && el.GetIndex().Nr1() != domain)
                     return;

                 for (PointIndex pi : el.PNums())
                   if(free_points[pi])
                     table.Add (pi, ei);
               }, GetNP());
      }
    else
        return ngcore::CreateSortedTable<ElementIndex, PointIndex>( volelements.Range(),
               [&](auto & table, ElementIndex ei)
               {
                 const auto & el = (*this)[ei];
                 if(el.IsDeleted())
                     return;

                 if(domain && el.GetIndex().Nr1() != domain)
                     return;

                 for (PointIndex pi : el.PNums())
                   table.Add (pi, ei);
               }, GetNP());
  }

  Table<SurfaceElementIndex, PointIndex> Mesh :: CreatePoint2SurfaceElementTable( int faceindex ) const
  {
    static Timer timer("Mesh::CreatePoint2SurfaceElementTable"); RegionTimer rt(timer);

    if(faceindex==0)
      {
        return ngcore::CreateSortedTable<SurfaceElementIndex, PointIndex>( surfelements.Range(),
               [&](auto & table, SurfaceElementIndex ei)
               {
                 for (PointIndex pi : (*this)[ei].PNums())
                   table.Add (pi, ei);
               }, GetNP());
      }

    Array<SurfaceElementIndex> face_els;
    GetSurfaceElementsOfFace(faceindex, face_els);
    return ngcore::CreateSortedTable<SurfaceElementIndex, PointIndex>( face_els.Range(),
           [&](auto & table, size_t i)
           {
             for (PointIndex pi : (*this)[face_els[i]].PNums())
               table.Add (pi, face_els[i]);
           }, GetNP());
  }




  

  void Mesh :: UpdateTopology ()
  {
    static Timer t("Update Topology"); RegionTimer reg(t);
    ComputeNVertices();
    topology.Update();
    static Timer t_call_update_clusters("call update clusters"); t_call_update_clusters.Start();
    clusters->Update();
    t_call_update_clusters.Stop();
    updateSignal.Emit();
  }

  void Mesh :: BuildCurvedElements  (const Refinement * ref, int aorder, bool arational)
  {
    GetCurvedElements().BuildCurvedElements (ref, aorder, arational);


    for (SurfaceElementIndex sei : SurfaceElements().Range())
      (*this)[sei].SetCurved (GetCurvedElements().IsCurved (sei));
    for (ElementIndex ei : VolumeElements().Range())
      (*this)[ei].SetCurved (GetCurvedElements().IsCurved (ei));
    
    SetNextMajorTimeStamp();
  }

  void Mesh :: BuildCurvedElements (int aorder)
  {
    if (!GetGeometry())
      throw NgException ("don't have a geometry for mesh curving");
    
    GetCurvedElements().BuildCurvedElements (&GetGeometry()->GetRefinement(), aorder, false);


    for (SurfaceElementIndex sei : SurfaceElements().Range())
      (*this)[sei].SetCurved (GetCurvedElements().IsCurved (sei));
    for (ElementIndex ei : VolumeElements().Range())
      (*this)[ei].SetCurved (GetCurvedElements().IsCurved (ei));
    
    SetNextMajorTimeStamp();
  }


  void Mesh :: SetMaterial (int domnr, const string & mat)
  {
    if (domnr < 1) throw RangeException("Illegal domain number ", domnr, 1, domnr);
    if (dimension == 2)
      {
        while (Regions<2>().Size() < domnr)
          {
            FaceRegion fd(0, 0, 0, 0);
            fd.SetBCProperty(Regions<2>().Size()+1);
            Regions<2>().Append(fd);
          }
        Regions<2>()[FaceRegionIndex::FromNr1(domnr)].SetBCName(mat);
      }
    else if (dimension == 1)
      {
        while (Regions<1>().Size() < domnr)
          Regions<1>().Append(EdgeRegion());
        Regions<1>()[EdgeRegionIndex::FromNr1(domnr)].SetName(mat);
      }
    else
      {
        auto & vols = Regions<3>();
        while (vols.Size() < domnr)
          vols.Append(VolumeRegion(defaultmat));   // set, like the old code
        vols[VolumeRegionIndex::FromNr1(domnr)].SetName(mat);
      }
  }

  string Mesh :: defaultmat = "default";
  string_view Mesh :: defaultmat_sv = "default";  
  Array<optional<string>> Mesh :: DomainNames () const
  {
    Array<optional<string>> names;
    if (dimension == 3) return RegionNames<3>();
    if (dimension == 2)
      for (const auto & fd : Regions<2>()) names.Append(fd.GetBCName());
    else
      for (const auto & ed : Regions<1>()) names.Append(ed.GetName());
    return names;
  }

  void Mesh :: SetDomainNames (Array<optional<string>> names)
  {
    if (dimension == 3) { SetRegionNames<3>(names); return; }
    for (int i = 0; i < names.Size(); i++)
      if (names[i]) SetMaterial(i+1, *names[i]);
  }

  void Mesh ::SetNBCNames ( int nbcn )
  {
    if (dimension >= 2) return;   // boundary names live on the descriptors
    Regions<0>() = RegionArray<0>(nbcn);
  }

  void Mesh ::SetBCName ( int bcnr, const string & abcname )
  {
    if (bcnr < 0) throw RangeException("Illegal bc number ", bcnr, 0, bcnr);
    if (dimension == 3)
      {
        while (Regions<2>().Size() <= bcnr)
          {
            FaceRegion fd(0, 0, 0, 0);
            fd.SetBCProperty(Regions<2>().Size()+1);
            Regions<2>().Append(fd);
          }
        Regions<2>()[FaceRegionIndex::FromNr0(bcnr)].SetBCName(abcname);
      }
    else if (dimension == 2)
      {
        while (Regions<1>().Size() <= bcnr)
          Regions<1>().Append(EdgeRegion());
        Regions<1>()[EdgeRegionIndex::FromNr0(bcnr)].SetName(abcname);
      }
    else
      {
        auto & verts = Regions<0>();
        while (verts.Size() <= bcnr)
          verts.Append(VertexRegion(default_bc));
        verts[VertexRegionIndex::FromNr0(bcnr)].SetName(abcname);
      }
  }

  const string & Mesh ::GetBCName ( int bcnr ) const
  {
    return *GetBCNamePtr(bcnr);
  }

  // boundary names keyed by bc number (BCProperty), the format of files and archives:
  // the name of the first face descriptor with that bc number, empty if nothing is named
  Array<string> Mesh :: BCNamesByNumber () const
  {
    Array<string> names;
    if (dimension == 3)
      {
        int nbc = 0;
        bool named = false;
        for (const auto & fd : Regions<2>())
          {
            nbc = max(nbc, fd.BCProperty());
            if (fd.GetBCName() != "default") named = true;
          }
        if (!named) return names;
        names.SetSize(nbc);
        names = "default";
        Array<bool> done(nbc); done = false;
        for (const auto & fd : Regions<2>())
          if (fd.BCProperty() >= 1 && !done[fd.BCProperty()-1])
            { names[fd.BCProperty()-1] = fd.GetBCName(); done[fd.BCProperty()-1] = true; }
      }
    else if (dimension == 2)
      {
        for (const auto & ed : Regions<1>())
          names.Append(ed.GetName());
      }
    else
      for (auto & vd : Regions<0>())
        names.Append(vd.GetName());
    return names;
  }

  EdgeRegion & Mesh :: EnsureEdgeDescriptor (int nr)
  {
    while (Regions<1>().Size() < nr)
      Regions<1>().Append(EdgeRegion());
    return Regions<1>()[EdgeRegionIndex::FromNr1(nr)];
  }

  // cd2names of files and archives: edge names in 3D, vertex names in 2D
  void Mesh :: SetCD2NameCompat (int cd2nr, const string & name)
  {
    if (dimension == 3)
      EnsureEdgeDescriptor(cd2nr).SetName((name != "default" && !name.empty()) ? name : "default");
    else if (dimension == 2)
      SetCD2Name(cd2nr, name);
  }

  void Mesh :: SetCD2Name ( int cd2nr, const string & abcname )
  {
    if (dimension != 2) throw Exception("SetCD2Name names vertices of 2D meshes only");
    auto & verts = Regions<0>();
    while (verts.Size() < cd2nr)
      verts.Append(VertexRegion(cd2_default_name));
    verts[VertexRegionIndex::FromNr1(cd2nr)].SetName(abcname.empty() ? cd2_default_name : abcname);
  }

  string Mesh :: cd2_default_name = "default";
  string Mesh :: default_bc = "default";
  void Mesh :: SetNCD3Names( int ncd3n )
  {
    Regions<0>() = RegionArray<0>(ncd3n);
  }

  int Mesh :: AddCD3Name (const string & aname)
  {
    for (auto i : Regions<0>().Range())
      if (Regions<0>()[i].HasName() && Regions<0>()[i].GetName() == aname)
        return i.Nr0();
    return AddRegion(VertexRegion(aname)).Nr0();
  }
  
  string Mesh :: cd3_default_name = "default";
  static string defaultstring  = "default";
  const string & Mesh :: GetCD3Name (int cd3nr) const
  {
    if (cd3nr < 0 || cd3nr >= Regions<0>().Size())
      return defaultstring;
    return Regions<0>()[VertexRegionIndex::FromNr0(cd3nr)].GetName();
  }



}

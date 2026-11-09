// compact_stubs.cpp — Stub implementations for symbols referenced
// by meshclass.cpp/basegeom.cpp but never actually called in our
// compact Netgen workflow (mesh construction + curving + save only).
//
// These stubs resolve linker errors without pulling in the full
// Netgen meshing/optimization/linalg subsystems.

#include <mystdlib.h>
#include "meshing.hpp"


namespace netgen
{

// DenseMatrix: now using real densemat.cpp (no longer stubbed)
// Mat<3,3> CalcInverse still needed (template specialization, not in densemat.cpp)
void CalcInverse(const Mat<3,3,double>& m, Mat<3,3,double>& inv) {
  double det = m(0,0)*(m(1,1)*m(2,2)-m(1,2)*m(2,1))
              -m(0,1)*(m(1,0)*m(2,2)-m(1,2)*m(2,0))
              +m(0,2)*(m(1,0)*m(2,1)-m(1,1)*m(2,0));
  if (fabs(det) < 1e-40) { inv = 0.0; return; }
  double idet = 1.0/det;
  inv(0,0) =  idet*(m(1,1)*m(2,2)-m(1,2)*m(2,1));
  inv(0,1) = -idet*(m(0,1)*m(2,2)-m(0,2)*m(2,1));
  inv(0,2) =  idet*(m(0,1)*m(1,2)-m(0,2)*m(1,1));
  inv(1,0) = -idet*(m(1,0)*m(2,2)-m(1,2)*m(2,0));
  inv(1,1) =  idet*(m(0,0)*m(2,2)-m(0,2)*m(2,0));
  inv(1,2) = -idet*(m(0,0)*m(1,2)-m(0,2)*m(1,0));
  inv(2,0) =  idet*(m(1,0)*m(2,1)-m(1,1)*m(2,0));
  inv(2,1) = -idet*(m(0,0)*m(2,1)-m(0,1)*m(2,0));
  inv(2,2) =  idet*(m(0,0)*m(1,1)-m(0,1)*m(1,0));
}
double Det(const Mat<2,2,double>& m) {
  return m(0,0)*m(1,1) - m(0,1)*m(1,0);
}

// ============================================================
// Meshing algorithm stubs (from basegeom.cpp virtual methods)
// NetgenGeometry::GenerateMesh/MeshFace/OptimizeSurface are
// virtual methods that we override in CallbackGeometry. The
// base class implementations reference the full mesher.
// ============================================================

// Meshing2: too many virtual methods to stub. Skip entirely.
// Instead, we patch basegeom.cpp to remove mesher dependencies.

// Volume meshing stubs (referenced by basegeom)
MESHING3_RESULT MeshVolume(const MeshingParameters&, Mesh&) { return MESHING3_OK; }
MESHING3_RESULT OptimizeVolume(const MeshingParameters&, Mesh&) { return MESHING3_OK; }
void MeshQuality3d(const Mesh&, Array<int>*) {}

// ============================================================
// Mesh utility stubs (from meshclass.cpp)
// ============================================================
double CalcTetBadness(const netgen::Point<3>&, const netgen::Point<3>&, const netgen::Point<3>&, const netgen::Point<3>&,
                      double, const MeshingParameters&) { return 0.0; }
int CheckMesh3D(const Mesh&) { return 0; }
int IntersectTriangleTriangle(const Point<3>**, const Point<3>**) { return 0; }
double ComputeCylinderRadius(const netgen::Point<3>&, const netgen::Point<3>&, const netgen::Point<3>&, const netgen::Point<3>&) { return 0.0; }

// Mesh functions removed from meshclass_patched.cpp (compact build)
void Mesh::BuildElementSearchTree(int) {}
bool Mesh::PointContainedIn3DElementOld(const netgen::Point<3>&, double* const, ElementIndex, double) const { return false; }
void Mesh::SetLocalH(netgen::Point<3> , netgen::Point<3> , double, int) {}
void Mesh::RestrictLocalH(const netgen::Point<3>&, double, int) {}
void Mesh::RestrictLocalHLine(const netgen::Point<3>&, const netgen::Point<3>&, double, int) {}
void Mesh::LoadLocalMeshSize(const filesystem::path&) {}
void Mesh::SetGlobalH(double h) { hglob = h; }
void Mesh::SetMinimalH(double h) { hmin = h; }
double Mesh::GetH(const netgen::Point<3>&, int) const { return hglob; }
bool Mesh::PointContainedIn2DElement(const netgen::Point<3>&, double* const, SurfaceElementIndex, bool) const { return false; }
bool Mesh::PointContainedIn3DElement(const netgen::Point<3>&, double* const, ElementIndex, double) const { return false; }

// Marked-element classes of upstream 6.2.2608 bisect.cpp (not compiled here), verbatim:
// BisectionInfo owns unique_ptr arrays of them, so its constructor and destructor need complete types.
  class MarkedTet
  {
  public:
    /// pnums of tet
    PointIndex pnums[4];
    /// material number
    VolumeRegionIndex matindex;
    /// element marked for refinement
    /// marked = 1: marked by element marker, marked = 2 due to closure
    unsigned int marked:2;
    /// flag of Arnold-Mukherjee algorithm
    unsigned int flagged:1;
    /// tetedge (local coordinates 0..3)
    unsigned int tetedge1:3;
    unsigned int tetedge2:3;
    // marked edge of faces
    // face_j : face without node j,
    // mark_k : edge without node k
    
    char faceedges[4];
    // unsigned char faceedges[4];
    bool incorder;
    unsigned int order:6;
    int8_t newest_vertex;

    MarkedTet() = default;
    /*
    { 
      for (int i = 0; i < 4; i++) { faceedges[i] = 127; }
    }
    */
    MarkedTet (const MarkedTet&) = default;
    MarkedTet (MarkedTet &&) = default;
    MarkedTet & operator= (const MarkedTet&) = default;
    MarkedTet & operator= (MarkedTet&&) = default;
    
  };
  class MarkedPrism
  {
  public:
    /// 6 point numbers
    PointIndex pnums[6];
    /// material number
    VolumeRegionIndex matindex;
    /// marked for refinement
    int marked;
    /// edge without node k (0,1,2)
    int markededge;

    bool incorder;
    unsigned int order:6;
  };
  class MarkedIdentification
  {
  public:
    // number of points of one face (3 or 4) - or edge (in 2d)
    int np;
    /// 6 or 8 point numbers - or 4 in 2d
    PointIndex pnums[8];
    /// marked for refinement
    int marked;
    /// edge starting with node k (0,1,2, or 3)
    int markededge;

    bool incorder;
    unsigned int order:6;
  };
  class MarkedTri
  {
  public:
    MarkedTri () = default;
    MarkedTri (const MarkedTri&) = default;
    MarkedTri (MarkedTri &&) = default;
    MarkedTri & operator= (const MarkedTri&) = default;
    MarkedTri & operator= (MarkedTri&&) = default;
    
    /// three point numbers
    PointIndex pnums[3];
    /// three geominfos
    PointGeomInfo pgeominfo[3];
    /// marked for refinement
    int marked;
    /// edge without node k
    int markededge;
    /// surface id
    FaceRegionIndex surfid;

    bool incorder;
    unsigned int order:6;
    int8_t newest_vertex;
  };
  class MarkedQuad
  {
  public:
    /// point numbers
    PointIndex pnums[4];
    ///
    PointGeomInfo pgeominfo[4];
    /// marked for refinement
    int marked;
    /// marked edge: 0/2 = vertical, 1/3 = horizontal
    int markededge;
    /// surface id
    FaceRegionIndex surfid;

    bool incorder;
    unsigned int order:6;
  };

// BisectionInfo (mesh refinement), as upstream
BisectionInfo::BisectionInfo()
{
  mtets = make_unique<T_MTETS>();
  mprisms = make_unique<T_MPRISMS>();
  mids = make_unique<T_MIDS>();
  mtris = make_unique<T_MTRIS>();
  mquads = make_unique<T_MQUADS>();
}
BisectionInfo::~BisectionInfo() {}

// AnisotropicClusters (mesh quality) — has reference member
// NOTE: no static Mesh here — static init of Mesh causes issues
AnisotropicClusters::AnisotropicClusters(const Mesh& m) : mesh(m) {}
AnisotropicClusters::~AnisotropicClusters() {}
void AnisotropicClusters::Update() {}

// ============================================================
// Global variables
// ============================================================
DLL_HEADER size_t timestamp = 0;

// ============================================================
// Message handler
// ============================================================
void Ng_PrintDest(const char*) {}

// netrule destructor (needed by Meshing2 destructor via unique_ptr)
netrule::~netrule() {}

// ============================================================
// GeometryRegisterArray (from basegeom.cpp, used by Mesh::Load)
// ============================================================
shared_ptr<NetgenGeometry> GeometryRegisterArray::LoadFromMeshFile(istream&) const { return nullptr; }

// ============================================================
// AdFront stubs (from localh.cpp)
// ============================================================
} // namespace netgen

// Forward-declared in headers but never used in our workflow
namespace netgen {
  bool AdFront2::SameSide(const Point<2>&, const Point<2>&, const FlatArray<int>*) const { return false; }
  void AdFront3::GetFaceBoundingBox(int, Box3d&) const {}
  int AdFront3::SameSide(const Point<3>&, const Point<3>&, const Array<int>*) const { return 0; }
}

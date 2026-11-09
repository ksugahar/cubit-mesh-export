// Modified from Netgen libsrc/meshing/curvedelems.hpp at v6.2.2608 (96e5682f6ea43ba77ba3bb2ae4bb4bd1791f506e) plus upstream 13b47c3a9fb2e7d53ca4be7227bdd4acbd6c6e73 (pyramid apex clamp).
// Local modifications present in the source history on 2026-10-28.
// Netgen licensing and attribution are provided with the distribution.

#ifndef NETGEN_CURVEDELEMS_HPP
#define NETGEN_CURVEDELEMS_HPP

/**************************************************************************/
/* File:   curvedelems.hpp                                                */
/* Author: Robert Gaisbauer (first version)                               */
/*         redesign by Joachim Schoeberl                                  */
/* Date:   27. Sep. 02, Feb 2006                                          */
/**************************************************************************/

#include <mydefs.hpp>
#include <gprim/geomobjects.hpp>

#include "meshtype.hpp"
#include "meshclass.hpp"
#include <cmath>
#include <stdexcept>

namespace netgen
{
class Refinement;
class Mesh;

class CurvedElements
{
  const Mesh & mesh;

  Array<int, EdgeIndex> edgeorder;
  Array<int, FaceIndex> faceorder;

  Array<int, EdgeIndex> edgecoeffsindex;
  Array<int, FaceIndex> facecoeffsindex;

  Array< Vec<3> > edgecoeffs;
  Array< Vec<3> > facecoeffs;

  Array<double, EdgeIndex> edgeweight;  // for rational 2nd order splines

  int order;
  bool rational;

  bool ishighorder;

public:
  DLL_HEADER CurvedElements (const Mesh & amesh);
  DLL_HEADER ~CurvedElements();

  // bool IsHighOrder() const { return order > 1; }
  bool IsHighOrder() const { return ishighorder; }

  // void SetHighOrder (int aorder) { order=aorder; }
  void SetIsHighOrder (bool ho) { ishighorder = ho; }
  
  DLL_HEADER void BuildCurvedElements(const Refinement * ref, int aorder, bool arational = false);

  int GetOrder () { return order; }

  void DoArchive(Archive& ar);

  DLL_HEADER bool IsSegmentCurved (SegmentIndex segnr) const;
  DLL_HEADER bool IsSurfaceElementCurved (SurfaceElementIndex sei) const;
  DLL_HEADER bool IsElementCurved (ElementIndex ei) const;
  DLL_HEADER bool IsElementHighOrder (ElementIndex ei) const;

  // cubit-mesh-export local patch: coefficient access for the plugin's
  // geometric refit pass (src/cubit_plugin/NetgenGeometricRefit.cpp).
  // Indices are 0-based topology edge/face numbers; call the pointer
  // accessors only when the matching count is positive.
  int NumEdgeCoefficients (int edgenr) const
  { auto e = IndexBASE<EdgeIndex>()+edgenr; return edgecoeffsindex[e+1] - edgecoeffsindex[e]; }
  Vec<3> * EdgeCoefficients (int edgenr)
  { return &edgecoeffs[edgecoeffsindex[IndexBASE<EdgeIndex>()+edgenr]]; }
  int NumFaceCoefficients (int facenr) const
  { auto f = IndexBASE<FaceIndex>()+facenr; return facecoeffsindex[f+1] - facecoeffsindex[f]; }
  Vec<3> * FaceCoefficients (int facenr)
  { return &facecoeffs[facecoeffsindex[IndexBASE<FaceIndex>()+facenr]]; }

  int PeriodicEdgeOrder (int edge) const { return edgeorder[IndexBASE<EdgeIndex>()+edge]; }
  int PeriodicFaceOrder (int face) const { return faceorder[IndexBASE<FaceIndex>()+face]; }
  bool PeriodicRational () const { return rational; }
  static void ValidatePeriodicLayout (const int * orders, size_t norders,
      const int * indices, size_t nindices, const Vec<3> * coefficients, size_t ncoefficients,
      int maximum_order, bool triangular_faces)
  {
    if (maximum_order < 1 || maximum_order > 3 ||
        nindices != norders+1 || indices[0] != 0)
      throw std::runtime_error("Invalid periodic coefficient layout dimensions");
    for (size_t i = 0; i < norders; ++i) {
      const int degree = orders[i];
      if (degree < 1 || degree > maximum_order)
        throw std::runtime_error("Unsupported periodic entity coefficient order");
      const int expected = triangular_faces ? (degree-1)*(degree-2)/2 : degree-1;
      if (indices[i] < 0 || indices[i+1] < indices[i] ||
          size_t(indices[i+1]) > ncoefficients || indices[i+1]-indices[i] != expected)
        throw std::runtime_error("Periodic entity coefficient span/order mismatch");
    }
    // Compression can retain a finite unused tail. Do not require its removal.
    for (size_t i = 0; i < ncoefficients; ++i)
      for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(coefficients[i](axis)))
          throw std::runtime_error("Nonfinite periodic coefficient storage");
  }
  void ValidatePeriodicStorage (size_t edges, size_t faces) const
  {
    if (rational || order < 1 || order > 3 ||
        edgeorder.Size() != edges || faceorder.Size() != faces ||
        edgecoeffsindex.Size() != edges+1 || facecoeffsindex.Size() != faces+1)
      throw std::runtime_error("Unsupported periodic curved coefficient storage");
    ValidatePeriodicLayout(edgeorder.Data(), edgeorder.Size(), edgecoeffsindex.Data(), edgecoeffsindex.Size(),
                           edgecoeffs.Data(), edgecoeffs.Size(), order, false);
    ValidatePeriodicLayout(faceorder.Data(), faceorder.Size(), facecoeffsindex.Data(), facecoeffsindex.Size(),
                           facecoeffs.Data(), facecoeffs.Size(), order, true);
  }

  /// the index type says which kind of element it is
  bool IsCurved (ElementIndex ei) const        { return IsElementCurved(ei); }
  bool IsCurved (SurfaceElementIndex sei) const { return IsSurfaceElementCurved(sei); }
  bool IsCurved (SegmentIndex segnr) const      { return IsSegmentCurved(segnr); }

  void CalcSegmentTransformation (double xi, SegmentIndex segnr,
                                  Point<3> & x)
  { CalcSegmentTransformation<double> (xi, segnr, &x, NULL); };

  void CalcSegmentTransformation (double xi, SegmentIndex segnr,
                                  Vec<3> & dxdxi)
  { CalcSegmentTransformation<double> (xi, segnr, NULL, &dxdxi); };

  void CalcSegmentTransformation (double xi, SegmentIndex segnr,
                                  Point<3> & x, Vec<3> & dxdxi)
  { CalcSegmentTransformation<double> (xi, segnr, &x, &dxdxi, NULL); };

  void CalcSegmentTransformation (double xi, SegmentIndex segnr,
                                  Point<3> & x, Vec<3> & dxdxi, bool & curved)
  { CalcSegmentTransformation (xi, segnr, &x, &dxdxi, &curved); };



  void CalcSurfaceTransformation (const Point<2> & xi, SurfaceElementIndex elnr,
                                  Point<3> & x)
  { CalcSurfaceTransformation (xi, elnr, &x, NULL); };

  void CalcSurfaceTransformation (const Point<2> & xi, SurfaceElementIndex elnr,
                                  Mat<3,2> & dxdxi)
  { CalcSurfaceTransformation (xi, elnr, NULL, &dxdxi); };

  void CalcSurfaceTransformation (const Point<2> & xi, SurfaceElementIndex elnr,
                                  Point<3> & x, Mat<3,2> & dxdxi)
  { CalcSurfaceTransformation (xi, elnr, &x, &dxdxi, NULL); };

  void CalcSurfaceTransformation (const Point<2> & xi, SurfaceElementIndex elnr,
                                  Point<3> & x, Mat<3,2> & dxdxi, bool & curved)
  { CalcSurfaceTransformation (xi, elnr, &x, &dxdxi, &curved); };





  void CalcElementTransformation (const Point<3> & xi, ElementIndex elnr,
                                  Point<3> & x)
  { CalcElementTransformation (xi, elnr, &x, NULL); };

  void CalcElementTransformation (const Point<3> & xi, ElementIndex elnr,
                                  Mat<3,3> & dxdxi)
  { CalcElementTransformation (xi, elnr, NULL, &dxdxi); };

  void CalcElementTransformation (const Point<3> & xi, ElementIndex elnr,
                                  Point<3> & x, Mat<3,3> & dxdxi)
  { CalcElementTransformation (xi, elnr, &x, &dxdxi /* , NULL */ ); };

  void CalcElementTransformation (const Point<3> & xi, ElementIndex elnr,
                                  Point<3> & x, Mat<3,3> & dxdxi,
                                  void * buffer, bool valid)
  { CalcElementTransformation (xi, elnr, &x, &dxdxi, /* NULL, */ buffer, valid ); };

  // void CalcElementTransformation (const Point<3> & xi, ElementIndex elnr,
  //                              Point<3> & x, Mat<3,3> & dxdxi) // , bool & curved)
  //   { CalcElementTransformation (xi, elnr, &x, &dxdxi /* , &curved * ); }


  /*
  void CalcMultiPointSegmentTransformation (Array<double> * xi, SegmentIndex segnr,
                                            Array<Point<3> > * x,
                                            Array<Vec<3> > * dxdxi);
  */
  
  template <int DIM_SPACE, typename T>
  void CalcMultiPointSegmentTransformation (SegmentIndex elnr, int n,
                                            const T * xi, size_t sxi,
                                            T * x, size_t sx,
                                            T * dxdxi, size_t sdxdxi);

  DLL_HEADER void CalcMultiPointSurfaceTransformation (Array< Point<2> > * xi, SurfaceElementIndex elnr,
                                            Array< Point<3> > * x,
                                            Array< Mat<3,2> > * dxdxi);

  template <int DIM_SPACE, typename T>
  void CalcMultiPointSurfaceTransformation (SurfaceElementIndex elnr, int n,
                                            const T * xi, size_t sxi,
                                            T * x, size_t sx,
                                            T * dxdxi, size_t sdxdxi);

  DLL_HEADER void CalcMultiPointElementTransformation (Array< Point<3> > * xi, ElementIndex elnr,
                                            Array< Point<3> > * x,
                                            Array< Mat<3,3> > * dxdxi);

  template <typename T>
  void CalcMultiPointElementTransformation (ElementIndex elnr, int n,
                                            const T * xi, size_t sxi,
                                            T * x, size_t sx,
                                            T * dxdxi, size_t sdxdxi);


  

private:

  template <typename T>
  DLL_HEADER void CalcSegmentTransformation (const T & xi, SegmentIndex segnr,
                                  Point<3,T> * x = NULL, Vec<3,T> * dxdxi = NULL, bool * curved = NULL);

  DLL_HEADER void CalcSurfaceTransformation (Point<2> xi, SurfaceElementIndex elnr,
                                  Point<3> * x = NULL, Mat<3,2> * dxdxi = NULL, bool * curved = NULL);

  DLL_HEADER void CalcElementTransformation (Point<3> xi, ElementIndex elnr,
                                  Point<3> * x = NULL, Mat<3,3> * dxdxi = NULL, // bool * curved = NULL,
                                  void * buffer = NULL, bool valid = 0);






  class SegmentInfo
  {
  public:
    SegmentIndex elnr;
    int order;
    int nv;
    int ndof;
    EdgeIndex edgenr;
  };

  template <typename T>
  void CalcElementShapes (SegmentInfo &  elnr, T xi, TFlatVector<T> shapes) const;
  void GetCoefficients (SegmentInfo & elnr, Array<Vec<3> > & coefs) const;
  template <typename T>
  void CalcElementDShapes (SegmentInfo & elnr, T xi, TFlatVector<T> dshapes) const;


  class ElementInfo
  {
  public:
    ElementIndex elnr;
    int order;
    int nv;
    int ndof;
    int nedges;
    int nfaces;
    EdgeIndex edgenrs[12];
    FaceIndex facenrs[6];
    Mat<3> hdxdxi;
    Vec<3> hcoefs[10]; // enough for second order tets

    void SetEdges (FlatArray<const EdgeIndex> edges)
    {
      nedges = edges.Size();
      for (int i = 0; i < edges.Size(); i++)
        edgenrs[i] = edges[i];
    }
    
    auto GetEdges() const
    { return FlatArray(nedges, edgenrs); }

    void SetFaces (FlatArray<const FaceIndex> faces)
    {
      nfaces = faces.Size();
      for (int i = 0; i < faces.Size(); i++)
        facenrs[i] = faces[i];
    }

    auto GetFaces() const
    { return FlatArray(nfaces, facenrs); }
  };

  template <typename T>
  void CalcElementShapes (ElementInfo & info, Point<3,T> xi, TFlatVector<T> shapes) const;
  void GetCoefficients (ElementInfo & info, Vec<3> * coefs) const;
  template <typename T>  
  void CalcElementDShapes (ElementInfo & info, const Point<3,T> xi, MatrixFixWidth<3,T> & dshapes) const;

  template <typename T>
  bool EvaluateMapping (ElementInfo & info, const Point<3,T> xi, Point<3,T> & x, Mat<3,3,T> & jac) const;  
  
  class SurfaceElementInfo
  {
  public:
    SurfaceElementIndex elnr;
    int order;
    int nv;
    int ndof;
    ArrayMem<EdgeIndex,4> edgenrs;
    FaceIndex facenr;

    void SetEdges (FlatArray<const EdgeIndex> edges)
    {
      edgenrs.SetSize(edges.Size());
      for (int i = 0; i < edges.Size(); i++)
        edgenrs[i] = edges[i];
    }
    
  };

  template <typename T>
  void CalcElementShapes (SurfaceElementInfo & elinfo, const Point<2,T> xi, TFlatVector<T> shapes) const;
  template <int DIM_SPACE>
  void GetCoefficients (SurfaceElementInfo & elinfo, Array<Vec<DIM_SPACE> > & coefs) const;
  template <typename T>
  void CalcElementDShapes (SurfaceElementInfo & elinfo, const Point<2,T> xi, MatrixFixWidth<2,T> & dshapes) const;

  template <int DIM_SPACE, typename T>
  bool EvaluateMapping (SurfaceElementInfo & info, const Point<2,T> xi, Point<DIM_SPACE,T> & x, Mat<DIM_SPACE,2,T> & jac) const;  
};

} //namespace netgen
#endif // NETGEN_CURVEDELEMS_HPP

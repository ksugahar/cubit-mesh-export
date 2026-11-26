// Modified from Netgen libsrc/meshing/meshtype.cpp at v6.2.2608 (96e5682f6ea43ba77ba3bb2ae4bb4bd1791f506e) plus upstream 13b47c3a9fb2e7d53ca4be7227bdd4acbd6c6e73 (pyramid apex clamp).
// Local modifications present in the source history on 2026-10-15.
// Netgen licensing and attribution are provided with the distribution.

#include <mystdlib.h>

#include "meshing.hpp"  

namespace netgen
{





 void Element0d :: DoArchive (Archive & ar)
 {
   ar & pnum & index;
 }

  Segment :: Segment() 
  {
    pnums[0] = PointIndex::INVALID;
    pnums[1] = PointIndex::INVALID;
    pnums[2] = PointIndex::INVALID;
    index = EdgeRegionIndex::INVALID;
  }    

  void Segment :: DoArchive (Archive & ar)
  {
    string * bcname_dummy = nullptr;
    int index_compat = ar.Output() ? GetIndex().Nr1() : 0; // archive stores 1-based
    int domin_compat = -1, domout_compat = -1;
    int tlosurf_compat = -1;
    double singedge_left_compat = 0, singedge_right_compat = 0;
    int surfnr1_compat = -1, surfnr2_compat = -1;
    int edgenr_compat = -1;
    int si_compat = -1; // backward compat: archive still has si field
    int epgi_edgenr_compat = -1; // backward compat: edgenr removed from EdgePointGeomInfo
    ar & pnums[0] & pnums[1] & pnums[2]
      & edgenr_compat & singedge_left_compat & singedge_right_compat
      & si_compat & index_compat & domin_compat & domout_compat & tlosurf_compat
      & surfnr1_compat & surfnr2_compat
      & bcname_dummy // keep this for backward compatibility
      & epgi_edgenr_compat & epgi_edgenr_compat;
    if (!ar.Output())
      {
        SetIndex(EdgeRegionIndex::FromNr1(index_compat)); // archive stores 1-based, same as in-memory
      }
  }


  ostream & operator<<(ostream  & s, const Segment & seg)
  {
    s << seg[0] << "(gi=" << seg.GeomInfo(0).trignum << ") - "
      << seg[1] << "(gi=" << seg.GeomInfo(1).trignum << ")"
      << ", index = " << seg.GetIndex();
    return s;
  }

  Element2d :: Element2d ()
    : Element2dRef(&hstore, pnstore, gistore, ELEMENT2D_MAXPOINTS)
  {
    for (int i = 0; i < ELEMENT2D_MAXPOINTS; i++)
      {
        pnstore[i].Invalidate();
        gistore[i].trignum = 0;
      }
    hstore.typ = TRIG;
    hstore.index = FaceRegionIndex::INVALID;
    hstore.refflag = 1;
    hstore.is_curved = 0;
    hstore.flags.badel = 0;
    hstore.flags.deleted = 0;
    hstore.flags.visible = 1;
    hstore.flags.strongrefflag = false;
  }

  Element2d :: Element2d (int anp)
    : Element2d()
  {
    hstore.typ = Element2dTypeFromNP (anp);
    hstore.is_curved = (GetNP() >= 4);
  }

  Element2d :: Element2d (ELEMENT_TYPE atyp)
    : Element2d()
  {
    SetType (atyp);
  }

  Element2d :: Element2d (PointIndex pi1, PointIndex pi2, PointIndex pi3)
    : Element2d()
  {
    pnstore[0] = pi1;
    pnstore[1] = pi2;
    pnstore[2] = pi3;
    hstore.typ = TRIG;
  }

  Element2d :: Element2d (PointIndex pi1, PointIndex pi2, PointIndex pi3, PointIndex pi4)
    : Element2d()
  {
    pnstore[0] = pi1;
    pnstore[1] = pi2;
    pnstore[2] = pi3;
    pnstore[3] = pi4;
    hstore.typ = QUAD;
    hstore.is_curved = true;
  }

  void Element2dRef :: SetType (ELEMENT_TYPE atyp)
  {
    if (element_info::np[atyp] == 0)
      PrintSysError ("Element2d::SetType, illegal type ", int(atyp));
    if (element_info::np[atyp] > maxnp)
      throw Exception ("Element2dRef::SetType: element with " + ToString(int(element_info::np[atyp])) +
                       " points does not fit into " + ToString(maxnp) + " slots, use Mesh::SetSurfaceElement");
    h->typ = atyp;
    h->is_curved = (GetNP() >= 4);
  }

  Element2dRef & Element2dRef :: operator= (const Element2dRef & el2)
  {
    if (el2.GetNP() > maxnp)
      throw Exception ("Element2dRef: element with " + ToString(el2.GetNP()) + " points does not fit into " +
                       ToString(maxnp) + " slots, use Mesh::SetSurfaceElement");
    *h = *el2.h;
    for (int i = 0; i < el2.GetNP(); i++)
      {
        pn[i] = el2.pn[i];
        gi[i] = el2.gi[i];
      }
    return *this;
  }

  void Element2dRef :: DoArchive (Archive & ar)
  {
    short _np, _typ;
    bool _curved, _vis, _deleted;
    if (ar.Output())
      { _np = GetNP(); _typ = h->typ; _curved = h->is_curved;
        _vis = h->flags.visible; _deleted = h->flags.deleted; }
    ar.DoPacked (_np, _typ, h->index, _curved, _vis, _deleted);
    if (ar.Input())
      {
        h->typ = ELEMENT_TYPE(_typ); h->is_curved = _curved;
        h->flags.visible = _vis; h->flags.deleted = _deleted;
        h->flags.badel = 0; h->flags.strongrefflag = 0;
        h->refflag = 1;
        h->newest_vertex = -1;
        h->next.Invalidate();
      }

    // archive stores 1-based point numbers, independent of BASE
    int nr1[ELEMENT2D_MAXPOINTS];
    if (ar.Output())
      for (int k = 0; k < GetNP(); k++) nr1[k] = pn[k].Nr1();
    ar.Do (nr1, GetNP());
    if (ar.Input())
      for (int k = 0; k < GetNP(); k++) pn[k] = PointIndex::FromNr1(nr1[k]);
  }

  bool Element2dRef :: operator==(const Element2dRef & el2) const
  {
    bool retval = (el2.GetNP() == GetNP());
    for(int i= 0; retval && i<GetNP(); i++)
      retval = (el2[i] == (*this)[i]);

    return retval;
  }


  void Element2dRef :: Invert2()
  {
    switch (h->typ)
      {
      case TRIG:
        {
          Swap (pn[1], pn[2]);
          break;
        }
      case TRIG6:
        {
          Swap (pn[1], pn[2]);
          Swap (pn[4], pn[5]);
          break;
        }
      case QUAD:
        {
          Swap (pn[0], pn[3]);
          Swap (pn[1], pn[2]);
          break;
        }
      default:
        {
          cerr << "Element2d::Invert2, illegal element type " << int(h->typ) << endl;
        }
      }
  }




  Array<IntegrationPointData*> ipdtrig;
  Array<IntegrationPointData*> ipdquad;


  int Element2dRef :: GetNIP () const
  {
    int nip;
    switch (GetNP())
      {
      case 3: nip = 1; break;
      case 4: nip = 4; break;
      default: nip = 0; break;
      }
    return nip;
  }

  void Element2dRef :: 
  GetIntegrationPoint (int ip, Point<2> & p, double & weight) const
  {
    static double eltriqp[1][3] =
      {
        { 1.0/3.0, 1.0/3.0, 0.5 }
      };

    static double elquadqp[4][3] =
      { 
        { 0, 0, 0.25 },
        { 0, 1, 0.25 },
        { 1, 0, 0.25 },
        { 1, 1, 0.25 }
      };
  
    double * pp = 0;
    switch (h->typ)
      {
      case TRIG: pp = &eltriqp[0][0]; break;
      case QUAD: pp = &elquadqp[ip-1][0]; break;
      default:
        PrintSysError ("Element2d::GetIntegrationPoint, illegal type ", int(h->typ));
      }

    p[0] = pp[0];
    p[1] = pp[1];
    weight = pp[2];
  }

  void Element2dRef :: 
  GetTransformation (int ip, class DenseMatrix & pmat,
                     class DenseMatrix & trans) const
  {
    //  int np = GetNP();

#ifdef DEBUG
    if (pmat.Width() != np || pmat.Height() != 2)
      {
        (*testout) << "GetTransofrmation: pmat doesn't fit" << endl;
        return;
      }
#endif

    ComputeIntegrationPointData ();
    DenseMatrix * dshapep = NULL;
    switch (h->typ)
      {
      case TRIG: dshapep = &ipdtrig[ip-1]->dshape; break;
      case QUAD: dshapep = &ipdquad[ip-1]->dshape; break;
      default:
        PrintSysError ("Element2d::GetTransformation, illegal type ", int(h->typ));
      }
  
    CalcABt (pmat, *dshapep, trans);
  }


  void Element2dRef :: GetShape (const Point<2> & p, Vector & shape) const
  {
    if (shape.Size() != GetNP())
      {
        cerr << "Element::GetShape: Length not fitting" << endl;
        return;
      }

    switch (h->typ)
      {
      case TRIG:
        shape(0) = 1 - p[0] - p[1];
        shape(1) = p[0];
        shape(2) = p[1];
        break;
      case QUAD:
        shape(0) = (1-p[0]) * (1-p[1]);
        shape(1) = p[0] * (1-p[1]);
        shape(2) = p[0] * p[1];
        shape(3) = (1-p[0]) * p[1];
        break;
      default:
        PrintSysError ("Element2d::GetShape, illegal type ", int(h->typ));
      }
  }



  void Element2dRef :: GetShapeNew (const Point<2> & p, FlatVector & shape) const
  {
    switch (h->typ)
      {
      case TRIG:
        {
          shape(0) = p(0);
          shape(1) = p(1);
          shape(2) = 1-p(0)-p(1);
          break;
        }

      case QUAD:
        {
          shape(0) = (1-p(0))*(1-p(1));
          shape(1) =    p(0) *(1-p(1));
          shape(2) =    p(0) *   p(1) ;
          shape(3) = (1-p(0))*   p(1) ;
          break;
        }

      default:
        throw NgException ("illegal element type in GetShapeNew");
      }
  }

  template <typename T>
  void Element2dRef :: GetShapeNew (const Point<2,T> & p, TFlatVector<T> shape) const
  {
    switch (h->typ)
      {
      case TRIG:
        {
          shape(0) = p(0);
          shape(1) = p(1);
          shape(2) = 1-p(0)-p(1);
          break;
        }

      case QUAD:
        {
          shape(0) = (1-p(0))*(1-p(1));
          shape(1) =    p(0) *(1-p(1));
          shape(2) =    p(0) *   p(1) ;
          shape(3) = (1-p(0))*   p(1) ;
          break;
        }
      default:
        throw NgException ("illegal element type in GetShapeNew");
      }
  }









  void Element2dRef :: 
  GetDShape (const Point<2> & p, DenseMatrix & dshape) const
  {
#ifdef DEBUG
    if (dshape.Height() != 2 || dshape.Width() != np)
      {
        PrintSysError ("Element::DShape: Sizes don't fit");
        return;
      }
#endif

    switch (h->typ)
      {
      case TRIG:
        dshape.Elem(1, 1) = -1;
        dshape.Elem(1, 2) = 1;
        dshape.Elem(1, 3) = 0;
        dshape.Elem(2, 1) = -1;
        dshape.Elem(2, 2) = 0;
        dshape.Elem(2, 3) = 1;
        break;
      case QUAD:
        dshape.Elem(1, 1) = -(1-p[1]);
        dshape.Elem(1, 2) = (1-p[1]);
        dshape.Elem(1, 3) = p[1];
        dshape.Elem(1, 4) = -p[1];
        dshape.Elem(2, 1) = -(1-p[0]);
        dshape.Elem(2, 2) = -p[0];
        dshape.Elem(2, 3) = p[0];
        dshape.Elem(2, 4) = (1-p[0]);
        break;

      default:
        PrintSysError ("Element2d::GetDShape, illegal type ", int(h->typ));
      }
  }


  template <typename T>
  void Element2dRef :: 
  GetDShapeNew (const Point<2,T> & p, MatrixFixWidth<2,T> & dshape) const
  {
    switch (h->typ)
      {
      case TRIG:
        {
          dshape = T(0.0);
          dshape(0,0) = 1;
          dshape(1,1) = 1;
          dshape(2,0) = -1;
          dshape(2,1) = -1;
          break;
        }
      case QUAD:
        {
          dshape(0,0) = -(1-p(1));
          dshape(0,1) = -(1-p(0));

          dshape(1,0) =  (1-p(1));
          dshape(1,1) =  -p(0);

          dshape(2,0) = p(1);
          dshape(2,1) = p(0);

          dshape(3,0) = -p(1);
          dshape(3,1) = (1-p(0));
          break;
        }
      default:
        throw NgException ("illegal element type in GetDShapeNew");
      }
  }











  static const int qip_table[4][4] =
    { { 0, 1, 0, 3 },
      { 0, 1, 1, 2 },
      { 3, 2, 0, 3 },
      { 3, 2, 1, 2 }
    };






  void Element2dRef :: ComputeIntegrationPointData () const
  {
    switch (GetNP())
      {
      case 3: if (ipdtrig.Size()) return; break;
      case 4: if (ipdquad.Size()) return; break;
      }

    for (int i = 1; i <= GetNIP(); i++)
      {
        IntegrationPointData * ipd = new IntegrationPointData;
        Point<2> hp;
        GetIntegrationPoint (i, hp, ipd->weight);
        ipd->p(0) = hp[0];
        ipd->p(1) = hp[1];
        ipd->p(2) = 0;

        ipd->shape.SetSize(GetNP());
        ipd->dshape.SetSize(2, GetNP());

        GetShape (hp, ipd->shape);
        GetDShape (hp, ipd->dshape);

        switch (GetNP())
          {
          case 3: ipdtrig.Append (ipd); break;
          case 4: ipdquad.Append (ipd); break;
          }
      }
  }







  ostream & operator<<(ostream  & s, const Element0d & el)
  {
    s << el.pnum << ", index = " << el.index;
    return s;
  }


  ostream & operator<<(ostream  & s, const Element2dRef & el)
  {
    s << "np = " << el.GetNP();
    for (int j = 0; j < el.GetNP(); j++)
      s << " " << el[j];
    return s;
  }


  ostream & operator<<(ostream  & s, const ElementRef & el)
  {
    s << "np = " << el.GetNP();
    for (int j = 0; j < el.GetNP(); j++)
      s << " " << el[j];
    return s;
  }

  /*
  Element :: Element ()
  {
    typ = TET;
    np = 4;
    for (int i = 0; i < ELEMENT_MAXPOINTS; i++)
      pnum[i] = 0;
    index = 0;
    flags.marked = 1;
    flags.badel = 0;
    flags.reverse = 0;
    flags.illegal = 0;
    flags.illegal_valid = 0;
    refflag = 1;
    flags.strongrefflag = false;
    flags.deleted = 0;
    flags.fixed = 0;
    is_curved = false;
  }
  */

  Element :: Element ()
    : ElementRef(&hstore, pnstore, ELEMENT_MAXPOINTS)
  { }

  Element :: Element (int anp)
    : ElementRef(&hstore, pnstore, ELEMENT_MAXPOINTS)
  {
    h->typ = Element3dTypeFromNP (anp);
    for (int i = 0; i < ELEMENT_MAXPOINTS; i++)
        pn[i].Invalidate();
    h->index = VolumeRegionIndex::INVALID;
    h->flags.marked = 1;
    h->flags.badel = 0;
    h->flags.reverse = 0;
    h->flags.illegal = 0;
    h->flags.illegal_valid = 0;
    h->refflag = 1;
    h->flags.strongrefflag = false;
    h->flags.deleted = 0;
    h->flags.fixed = 0;

    if (h->typ == ELEMENT_TYPE(0))
      cerr << "Element::Element: unknown element with " << anp << " points" << endl;
    h->is_curved = h->typ != TET; // false;
  }





  Element :: Element (ELEMENT_TYPE type)
    : ElementRef(&hstore, pnstore, ELEMENT_MAXPOINTS)
  {
    SetType (type);

    for (int i = 0; i < ELEMENT_MAXPOINTS; i++)
        pn[i].Invalidate();
    h->index = VolumeRegionIndex::INVALID;
    h->flags.marked = 1;
    h->flags.badel = 0;
    h->flags.reverse = 0;
    h->flags.illegal = 0;
    h->flags.illegal_valid = 0;
    h->refflag = 1;
    h->flags.strongrefflag = false;
    h->flags.deleted = 0;
    h->flags.fixed = 0;
    h->is_curved =  h->typ != TET; // false;
    // #ifdef PARALLEL
    // partitionNumber = -1;
    // #endif
  }




  ElementRef & ElementRef :: operator= (const ElementRef & el2)
  {
    if (el2.GetNP() > maxnp)
      throw Exception ("ElementRef: element with " + ToString(el2.GetNP()) + " points does not fit into " +
                       ToString(maxnp) + " slots, use Mesh::SetVolumeElement");
    *h = *el2.h;
    for (int i = 0; i < el2.GetNP(); i++) pn[i] = el2.pn[i];
    return *this;
  }

  void ElementRef :: DoArchive (Archive & ar)
  {
    short _np, _typ;
    bool _curved;
    if (ar.Output())
      { _np = GetNP(); _typ = h->typ; _curved = h->is_curved; }
    ar.DoPacked (_np, _typ, h->index, _curved);

    if (ar.Input())
      {
        h->typ = ELEMENT_TYPE(_typ);
        h->is_curved = _curved;
        h->flags.marked = 1;
        h->flags.badel = 0;
        h->flags.reverse = 0;
        h->flags.illegal = 0;
        h->flags.illegal_valid = 0;
        h->refflag = 1;
        h->flags.strongrefflag = false;
        h->flags.deleted = 0;
        h->flags.fixed = 0;
      }

    // archive stores 1-based point numbers, independent of BASE
    int nr1[ELEMENT_MAXPOINTS];
    if (ar.Output())
      for (int k = 0; k < GetNP(); k++) nr1[k] = pn[k].Nr1();
    ar.Do (nr1, GetNP());
    if (ar.Input())
      for (int k = 0; k < GetNP(); k++) pn[k] = PointIndex::FromNr1(nr1[k]);
  }

  void ElementRef :: SetNP (int anp)
  {
    h->typ = Element3dTypeFromNP (anp);
    if (h->typ == ELEMENT_TYPE(0))
      cerr << "Element::SetNP unknown element with " << anp << " points" << endl;
  }



  void ElementRef :: SetType (ELEMENT_TYPE atyp)
  {
    if (element_info::np[atyp] == 0)
      cerr << "Element::SetType unknown type  " << int(atyp) << endl;
    if (element_info::np[atyp] > maxnp)
      throw Exception ("ElementRef::SetType: element with " + ToString(int(element_info::np[atyp])) +
                       " points does not fit into " + ToString(maxnp) + " slots, use Mesh::SetVolumeElement");
    h->typ = atyp;
    h->is_curved = (GetNP() > 4); 
  }





  void ElementRef :: GetFace2 (int i, Element2dRef face) const
  {
    static const int tetfaces[][5] = 
      { { 3, 2, 3, 4, 0 },
        { 3, 3, 1, 4, 0 },
        { 3, 1, 2, 4, 0 },
        { 3, 2, 1, 3, 0 } };

    static const int tet10faces[][7] = 
      { { 3, 2, 3, 4, 10, 9, 8 },
        { 3, 3, 1, 4, 7, 10, 6 },
        { 3, 1, 2, 4, 9, 7, 5 },
        { 3, 2, 1, 3, 6, 8, 5 } };

    static const int pyramidfaces[][5] =
      { { 4, 1, 4, 3, 2 },
        { 3, 1, 2, 5, 0 },
        { 3, 2, 3, 5, 0 },
        { 3, 3, 4, 5, 0 },
        { 3, 4, 1, 5, 0 } };
    
    static const int prismfaces[][5] =
      {
        { 3, 1, 3, 2, 0 },
        { 3, 4, 5, 6, 0 },
        { 4, 1, 2, 5, 4 },
        { 4, 2, 3, 6, 5 },
        { 4, 3, 1, 4, 6 }
      };
    
    static const int hex7faces[][5] =
      {
        { 4, 4, 3, 2, 1 },
        { 4, 3, 7, 6, 2 },
        { 3, 7, 5, 6 },
        { 4, 7, 4, 1, 5 },
        { 4, 1, 2, 6, 5 },
        { 3, 3, 4, 7 }
      };
    
    static const int hexfaces[][5] =
      {
        { 4, 4, 3, 2, 1 },
        { 4, 3, 7, 6, 2 },
        { 4, 7, 8, 5, 6 },
        { 4, 8, 4, 1, 5 },
        { 4, 1, 2, 6, 5 },
        { 4, 3, 4, 8, 7 }
      };

    switch (GetNP())
      {
      case 4: // tet
        {
          face.SetType(TRIG);
          for (int j = 1; j <= 3; j++)
            face.PNum(j) = PNum(tetfaces[i-1][j]);
          break;
        }

      case 10: // tet10
        {
          face.SetType(TRIG6);
          for (int j = 1; j <= 6; j++)
            face.PNum(j) = PNum(tet10faces[i-1][j]);
          break;
        }

      case 5: // pyramid
        {
          // face.SetNP(pyramidfaces[i-1][0]);
          face.SetType ( (i == 1) ? QUAD : TRIG);
          for (int j = 1; j <= face.GetNP(); j++)
            face.PNum(j) = PNum(pyramidfaces[i-1][j]);
          break;
        }
      case 6: // prism
        {
          //    face.SetNP(prismfaces[i-1][0]);
          face.SetType ( (i >= 3) ? QUAD : TRIG);
          for (int j = 1; j <= face.GetNP(); j++)
            face.PNum(j) = PNum(prismfaces[i-1][j]);
          break;
        }
      case 7: // hex7
        {
          //    face.SetNP(prismfaces[i-1][0]);
          face.SetType ( ((i == 3) || (i==6)) ? TRIG : QUAD);
          for (int j = 1; j <= face.GetNP(); j++)
            face.PNum(j) = PNum(hex7faces[i-1][j]);
          break;
        }
      case 8:
        {
          face.SetType(QUAD);
          for (int j = 1; j <= 4; j++)
            face.PNum(j) = PNum(hexfaces[i-1][j]);
          break;
        }
      }
  }



  bool ElementRef :: operator==(const ElementRef & el2) const
  {
    bool retval = (el2.GetNP() == GetNP());
    for(int i= 0; retval && i<GetNP(); i++)
      retval = (el2[i] == (*this)[i]);

    return retval;
  }




























  Array< shared_ptr < IntegrationPointData > > ipdtet;
  Array< shared_ptr < IntegrationPointData > > ipdtet10;



  int ElementRef :: GetNIP () const
  {
    int nip;
    switch (h->typ)
      {
      case TET: nip = 1; break;
      case TET10: nip = 8; break;
      default: nip = 0; break;
      }
    return nip;
  }

  void ElementRef :: 
  GetIntegrationPoint (int ip, Point<3> & p, double & weight) const
  {
    static double eltetqp[1][4] =
      {
        { 0.25, 0.25, 0.25, 1.0/6.0 }
      };

    static double eltet10qp[8][4] =
      {
        { 0.585410196624969, 0.138196601125011, 0.138196601125011, 1.0/24.0 },
        { 0.138196601125011, 0.585410196624969, 0.138196601125011, 1.0/24.0 },
        { 0.138196601125011, 0.138196601125011, 0.585410196624969, 1.0/24.0 },
        { 0.138196601125011, 0.138196601125011, 0.138196601125011, 1.0/24.0 },
        { 1, 0, 0, 1 },
        { 0, 1, 0, 1 },
        { 0, 0, 1, 1 },
        { 0, 0, 0, 1 },
      };
    
    double * pp = NULL;
    switch (h->typ)
      {
      case TET: pp = &eltetqp[0][0]; break;
      case TET10: pp = &eltet10qp[ip-1][0]; break;
      default:
        throw NgException ("illegal element shape in GetIntegrationPoint");
      }

    p(0) = pp[0];
    p(1) = pp[1];
    p(2) = pp[2];
    weight = pp[3];
  }

  void ElementRef :: 
  GetTransformation (int ip, class DenseMatrix & pmat,
                     class DenseMatrix & trans) const
  {
    int np = GetNP();

    if (pmat.Width() != GetNP() || pmat.Height() != 3)
      {
        (*testout) << "GetTransofrmation: pmat doesn't fit" << endl;
        return;
      }

    ComputeIntegrationPointData ();
    DenseMatrix * dshapep = 0;
    switch (GetType())
      {
      case TET: dshapep = &ipdtet[ip-1]->dshape; break;
      case TET10: dshapep = &ipdtet10[ip-1]->dshape; break;
      default:
        PrintSysError ("Element::GetTransformation, illegal type ", int(h->typ));
      }
  
    CalcABt (pmat, *dshapep, trans);
  }


  void ElementRef :: GetShape (const Point<3> & hp, Vector & shape) const
  {
    if (shape.Size() != GetNP())
      {
        cerr << "Element::GetShape: Length not fitting" << endl;
        return;
      }

    switch (h->typ)
      {
      case TET:
        {
          shape(0) = 1 - hp[0] - hp[1] - hp[2]; 
          shape(1) = hp[0];
          shape(2) = hp[1];
          shape(3) = hp[2];
          break;
        }
      case TET10:
        {
          double lam1 = 1 - hp[0] - hp[1] - hp[2];
          double lam2 = hp[0];
          double lam3 = hp[1];
          double lam4 = hp[2];
        
          shape(4) = 4 * lam1 * lam2;
          shape(5) = 4 * lam1 * lam3;
          shape(6) = 4 * lam1 * lam4;
          shape(7) = 4 * lam2 * lam3;
          shape(8) = 4 * lam2 * lam4;
          shape(9) = 4 * lam3 * lam4;
        
          shape(0) = lam1 - 0.5 * (shape(4) + shape(5) + shape(6));
          shape(1) = lam2 - 0.5 * (shape(4) + shape(7) + shape(8));
          shape(2) = lam3 - 0.5 * (shape(5) + shape(7) + shape(9));
          shape(3) = lam4 - 0.5 * (shape(6) + shape(8) + shape(9));
          break;
        }

      case PRISM:
        {
          shape(0) = hp(0) * (1-hp(2));
          shape(1) = hp(1) * (1-hp(2));
          shape(2) = (1-hp(0)-hp(1)) * (1-hp(2));
          shape(3) = hp(0) * hp(2);
          shape(4) = hp(1) * hp(2);
          shape(5) = (1-hp(0)-hp(1)) * hp(2);
          break;
        }
      case HEX:
        {
          shape(0) = (1-hp(0))*(1-hp(1))*(1-hp(2));
          shape(1) = (  hp(0))*(1-hp(1))*(1-hp(2));
          shape(2) = (  hp(0))*(  hp(1))*(1-hp(2));
          shape(3) = (1-hp(0))*(  hp(1))*(1-hp(2));
          shape(4) = (1-hp(0))*(1-hp(1))*(  hp(2));
          shape(5) = (  hp(0))*(1-hp(1))*(  hp(2));
          shape(6) = (  hp(0))*(  hp(1))*(  hp(2));
          shape(7) = (1-hp(0))*(  hp(1))*(  hp(2));
          break;
        }
      default:
        throw NgException("Element :: GetShape not implemented for that element");
      }
  }


  template <typename T>
  void ElementRef :: GetShapeNew (const Point<3,T> & p, TFlatVector<T> shape) const
  {
    /*
      if (shape.Size() < GetNP())
      {
      cerr << "Element::GetShape: Length not fitting" << endl;
      return;
      }
    */

    switch (h->typ)
      {
      case TET:
        {
          shape(0) = p(0);
          shape(1) = p(1);
          shape(2) = p(2);
          shape(3) = 1-p(0)-p(1)-p(2);
          break;
        }

      case TET10:
        {
          T lam1 = p(0);
          T lam2 = p(1);
          T lam3 = p(2);
          T lam4 = 1-p(0)-p(1)-p(2);
        
          shape(0) = 2 * lam1 * (lam1-0.5);
          shape(1) = 2 * lam2 * (lam2-0.5);
          shape(2) = 2 * lam3 * (lam3-0.5);
          shape(3) = 2 * lam4 * (lam4-0.5);

          shape(4) = 4 * lam1 * lam2;
          shape(5) = 4 * lam1 * lam3;
          shape(6) = 4 * lam1 * lam4;
          shape(7) = 4 * lam2 * lam3;
          shape(8) = 4 * lam2 * lam4;
          shape(9) = 4 * lam3 * lam4;
        
          break;
        }


      case PYRAMID:
        {
          T noz = 1-ClampPyramidApex (p(2));
          T den = noz;

          T xi  = p(0) / den;
          T eta = p(1) / den;
          shape(0) = (1-xi)*(1-eta) * (noz);
          shape(1) = (  xi)*(1-eta) * (noz);
          shape(2) = (  xi)*(  eta) * (noz);
          shape(3) = (1-xi)*(  eta) * (noz);
          shape(4) = p(2);
          break;
        }
      case PYRAMID13:
        {
          T x = p(0);
          T y = p(1);
          T z = p(2);
          z = ClampPyramidApex (z);
          T den = 1-z;
          shape[0] = (-z + z*(2*x + z - 1)*(2*y + z - 1)/den + (-2*x - z + 2)*(-2*y - z + 2))*(-0.5*x - 0.5*y - 0.5*z + 0.25);
          shape[1] = (0.5*x - 0.5*y - 0.25)*(-z - z*(2*x + z - 1)*(2*y + z - 1)/den + (2*x + z)*(-2*y - z + 2));
          shape[2] = (-z + z*(2*x + z - 1)*(2*y + z - 1)/den + (2*x + z)*(2*y + z))*(0.5*x + 0.5*y + 0.5*z - 0.75);
          shape[3] = (-0.5*x + 0.5*y - 0.25)*(-z - z*(2*x + z - 1)*(2*y + z - 1)/den + (2*y + z)*(-2*x - z + 2));
          shape[4] = z*(2*z - 1);
          shape[5] = 2*x*(-2*x - 2*z + 2)*(-2*y - 2*z + 2)/(2*den);
          shape[6] = 4*x*y*(-2*x - 2*z + 2)/(2*den);
          shape[7] = 2*y*(-2*x - 2*z + 2)*(-2*y - 2*z + 2)/(2*den);
          shape[8] = 4*x*y*(-2*y - 2*z + 2)/(2*den);
          shape[9] = z*(-2*x - 2*z + 2)*(-2*y - 2*z + 2)/den;
          shape[10] = 2*x*z*(-2*y - 2*z + 2)/den;
          shape[11] = 4*x*y*z/den;
          shape[12] = 2*y*z*(-2*x - 2*z + 2)/den;
          break;
        }
      case PRISM:
        {
          shape(0) = p(0) * (1-p(2));
          shape(1) = p(1) * (1-p(2));
          shape(2) = (1-p(0)-p(1)) * (1-p(2));
          shape(3) = p(0) * p(2);
          shape(4) = p(1) * p(2);
          shape(5) = (1-p(0)-p(1)) * p(2);
          break;
        }
      case PRISM15:
        {
          T x = p(0);
          T y = p(1);
          T z = p(2);
          T lam = 1-x-y;
          T lamz = 1-z;
          shape[0] = (2*x*x-x) * (2*lamz*lamz-lamz);
          shape[1] = (2*y*y-y) * (2*lamz*lamz-lamz);
          shape[2] = (2*lam*lam-lam) * (2*lamz*lamz-lamz);
          shape[3] = (2*x*x-x) * (2*z*z-z);
          shape[4] = (2*y*y-y) * (2*z*z-z);
          shape[5] = (2*lam*lam-lam) * (2*z*z-z);
          shape[6] = 4 * x * y * (2*lamz*lamz-lamz);
          shape[7] = 4 * x * lam * (2*lamz*lamz-lamz);
          shape[8] = 4 * y * lam * (2*lamz*lamz-lamz);
          shape[9] = x * 4 * z * (1-z);
          shape[10] = y * 4 * z * (1-z);
          shape[11] = lam * 4 * z * (1-z);
          shape[12] = 4 * x * y * (2*z*z-z);
          shape[13] = 4 * x * lam * (2*z*z-z);
          shape[14] = 4 * y * lam * (2*z*z-z);
          break;
        }
      case HEX7:
        {
          T y = p(1);
          T z = p(2);
          // T den = (1-y)*(1-z);
          T den = (1-y*z);
          den += T(1e-12);
          
          T x = p(0) / den;

          shape(0) = (1-x)*(1-y)*(1-z);
          shape(1) = (  x)*(1-y)*(1-z);
          shape(2) = (  x)*(  y)*(1-z);
          shape(3) = (1-x)*(  y)*(1-z);
          shape(4) = (1-x)*(1-y)*(  z);
          shape(5) = (  x)*(1-y)*(  z);
          shape(6) =       (  y)*(  z);
          break;
        }

      case HEX:
        {
          shape(0) = (1-p(0))*(1-p(1))*(1-p(2));
          shape(1) = (  p(0))*(1-p(1))*(1-p(2));
          shape(2) = (  p(0))*(  p(1))*(1-p(2));
          shape(3) = (1-p(0))*(  p(1))*(1-p(2));
          shape(4) = (1-p(0))*(1-p(1))*(  p(2));
          shape(5) = (  p(0))*(1-p(1))*(  p(2));
          shape(6) = (  p(0))*(  p(1))*(  p(2));
          shape(7) = (1-p(0))*(  p(1))*(  p(2));
          break;
        }
      case HEX20:
        {
          T x = p(0);
          T y = p(1);
          T z = p(2);
          shape[0] = (1-x)*(1-y)*(1-z);
          shape[1] =    x *(1-y)*(1-z);
          shape[2] =    x *   y *(1-z);
          shape[3] = (1-x)*   y *(1-z);
          shape[4] = (1-x)*(1-y)*(z);
          shape[5] =    x *(1-y)*(z);
          shape[6] =    x *   y *(z);
          shape[7] = (1-x)*   y *(z);

          T sigma[8]={(1-x)+(1-y)+(1-z),x+(1-y)+(1-z),x+y+(1-z),(1-x)+y+(1-z),
                      (1-x)+(1-y)+z,x+(1-y)+z,x+y+z,(1-x)+y+z};

          static const int e[12][2] =
            {
              { 0, 1 }, { 2, 3 }, { 3, 0 }, { 1, 2 },
              { 4, 5 }, { 6, 7 }, { 7, 4 }, { 5, 6 },
              { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
            };

          for (int i = 0; i < 12; i++)
            {
              T lame = shape[e[i][0]]+shape[e[i][1]];
              T xi = sigma[e[i][1]]-sigma[e[i][0]];
              shape[8+i] = (1-xi*xi)*lame;
            }
          for (int i = 0; i < 12; i++)
            {
              shape[e[i][0]] -= 0.5 * shape[8+i];
              shape[e[i][1]] -= 0.5 * shape[8+i];
            }
          break;
        }
      default:
        throw NgException("Element :: GetNewShape not implemented for that element");
      }
  }



  void ElementRef :: 
  GetDShape (const Point<3> & hp, DenseMatrix & dshape) const
  {
    int np = GetNP();
    if (dshape.Height() != 3 || dshape.Width() != np)
      {
        cerr << "Element::DShape: Sizes don't fit" << endl;
        return;
      }

    double eps = 1e-6;
    Vector shaper(np), shapel(np);

    for (auto i : Range(3))
      {
        Point<3> pr(hp), pl(hp);
        pr[i] += eps;
        pl[i] -= eps;
      
        GetShape (pr, shaper);
        GetShape (pl, shapel);
        for (int j = 0; j < np; j++)
          dshape(i, j) = (shaper(j) - shapel(j)) / (2 * eps);
      }
  }

  template <typename T>
  void ElementRef :: 
  GetDShapeNew (const Point<3,T> & p, MatrixFixWidth<3,T> & dshape) const
  {
    switch (h->typ)
      {
      case TET:
        {
          dshape = T(0.0);
          dshape(0,0) = 1;
          dshape(1,1) = 1;
          dshape(2,2) = 1;
          dshape(3,0) = -1;
          dshape(3,1) = -1;
          dshape(3,2) = -1;
          break;
        }
      case PRISM:
        {
          dshape = T(0.0);
          dshape(0,0) = 1-p(2);
          dshape(0,2) = -p(0);
          dshape(1,1) = 1-p(2);
          dshape(1,2) = -p(1);
          dshape(2,0) = -(1-p(2));
          dshape(2,1) = -(1-p(2));
          dshape(2,2) = -(1-p(0)-p(1));

          dshape(3,0) = p(2);
          dshape(3,2) = p(0);
          dshape(4,1) = p(2);
          dshape(4,2) = p(1);
          dshape(5,0) = -p(2);
          dshape(5,1) = -p(2);
          dshape(5,2) = 1-p(0)-p(1);
          break;
        }

      default:
        {
          /*
          int np = GetNP();
          double eps = 1e-6;
          ArrayMem<T,100> mem(2*np);
          TFlatVector<T> shaper(np, &mem[0]);
          TFlatVector<T> shapel(np, &mem[np]);
          // Vector shaper(np), shapel(np);
        
          for (int i = 0; i < 3; i++)
            {
              Point<3,T> pr(p), pl(p);
              pr(i) += eps;
              pl(i) -= eps;
            
              GetShapeNew (pr, shaper);
              GetShapeNew (pl, shapel);
              for (int j = 0; j < np; j++)
                dshape(j, i) = (shaper(j) - shapel(j)) / (2 * eps);
            }
          */

          AutoDiff<3,T> adx(p(0), 0);
          AutoDiff<3,T> ady(p(1), 1);
          AutoDiff<3,T> adz(p(2), 2);
          Point<3,AutoDiff<3,T>> adp{adx, ady, adz};
          ArrayMem<AutoDiff<3,T>,100> mem(GetNP());
          TFlatVector<AutoDiff<3,T>> adshape(GetNP(), &mem[0]);
          GetShapeNew (adp, adshape);
          for (int j = 0; j < GetNP(); j++)
            for (int k = 0; k < 3; k++)
              dshape(j,k) = adshape(j).DValue(k);
        }
      }
  }

  template void Element2dRef :: GetShapeNew (const Point<2,double> & p, TFlatVector<double> shape) const;
  template void Element2dRef :: GetShapeNew (const Point<2,SIMD<double>> & p, TFlatVector<SIMD<double>> shape) const;

  template void Element2dRef::GetDShapeNew<double> (const Point<2> &, MatrixFixWidth<2> &) const;
  template void Element2dRef::GetDShapeNew<SIMD<double>> (const Point<2,SIMD<double>> &, MatrixFixWidth<2,SIMD<double>> &) const;


  template DLL_HEADER void ElementRef :: GetShapeNew (const Point<3,double> & p, TFlatVector<double> shape) const;
  template DLL_HEADER void ElementRef :: GetShapeNew (const Point<3,SIMD<double>> & p, TFlatVector<SIMD<double>> shape) const;
  
  template void ElementRef::GetDShapeNew<double> (const Point<3> &, MatrixFixWidth<3> &) const;
  template void ElementRef::GetDShapeNew<SIMD<double>> (const Point<3,SIMD<double>> &, MatrixFixWidth<3,SIMD<double>> &) const;









  void ElementRef :: ComputeIntegrationPointData () const
  {
    switch (GetType())
      {
      case TET: if (ipdtet.Size()) return; break;
      case TET10: if (ipdtet10.Size()) return; break;
      default:
        PrintSysError ("Element::ComputeIntegrationPoint, illegal type ", int(h->typ));
      }

    switch (GetType())
      {
      case TET: ipdtet.SetSize(GetNIP()); break;
      case TET10: ipdtet10.SetSize(GetNIP()); break;
      default:
        PrintSysError ("Element::ComputeIntegrationPoint, illegal type2 ", int(h->typ));
      }


    for (int i = 1; i <= GetNIP(); i++)
      {
        IntegrationPointData * ipd = new IntegrationPointData;
        GetIntegrationPoint (i, ipd->p, ipd->weight);
        ipd->shape.SetSize(GetNP());
        ipd->dshape.SetSize(3, GetNP());

        GetShape (ipd->p, ipd->shape);
        GetDShape (ipd->p, ipd->dshape);

        switch (GetType())
          {
          case TET: ipdtet[i-1].reset(ipd); break;
          case TET10: ipdtet10[i-1].reset(ipd); break;
          default:
            PrintSysError ("Element::ComputeIntegrationPoint(2), illegal type ", int(h->typ));
          }
      }
  }







  const string RegionBase :: default_name = "default";

  Region<2> :: Region()
  { 
    surfnr = domin = domout  = bcprop = 0; 
    domin_singular = domout_singular = 0.;
    // Philippose - 06/07/2009
    // Initialise surface colour
    surfcolour = Vec<4>(0.0,1.0,0.0,1.0);
    tlosurf = -1; 
    firstelement = SurfaceElementIndex::INVALID;
  }

  Region<2> :: Region(const Region& other)
    : RegionBase(other),
      surfnr(other.surfnr), domin(other.domin), domout(other.domout),
      tlosurf(other.tlosurf), bcprop(other.bcprop), 
      surfcolour(other.surfcolour),
      domin_singular(other.domin_singular), domout_singular(other.domout_singular)
  { 
    firstelement = SurfaceElementIndex::INVALID;
  }

  Region<2> :: Region(int surfnri, int domini, int domouti, int tlosurfi)
  { 
    surfnr = surfnri; 
    domin = domini; 
    domout = domouti;
    // Philippose - 06/07/2009
    // Initialise surface colour
    surfcolour = Vec<4>(0.0,1.0,0.0,1.0);
    tlosurf = tlosurfi; 
    bcprop = surfnri;
    domin_singular = domout_singular = 0.;
    firstelement = SurfaceElementIndex::INVALID;
  }

  void Region<2> :: DoArchive (Archive & ar)
  {
    string bcname = GetName();
    ar & surfnr & domin & domout & tlosurf & bcprop
      & surfcolour & bcname   
      & domin_singular & domout_singular ;
    // don't need:  firstelement
    if (ar.Input()) SetName(bcname);
  }
  

  
  ostream & operator<<(ostream  & s, const FaceRegion & fd)
  {
    s << "surfnr = " << fd.SurfNr() 
      << ", domin = " << fd.DomainIn()
      << ", domout = " << fd.DomainOut()
      << ", tlosurf = " << fd.TLOSurface()
      << ", bcprop = " << fd.BCProperty()
      << ", bcname = " << fd.GetBCName()
      << ", domin_sing = " << fd.DomainInSingular()
      << ", domout_sing = " << fd.DomainOutSingular()
      << ", colour = " << fd.SurfColour();
    return s;
  }


  Identifications :: Identifications (Mesh & amesh)
    : mesh(amesh), identifiedpoints(100), identifiedpoints_nr(100)
  {
    // identifiedpoints = new INDEX_2_HASHTABLE<int>(100);
    // identifiedpoints_nr = new INDEX_3_HASHTABLE<int>(100);
    maxidentnr = 0;
  }

  Identifications :: ~Identifications ()
  {
    ;
    // delete identifiedpoints;
    // delete identifiedpoints_nr;
  }

    void Identifications :: DoArchive (Archive & ar)
    {
      ar & maxidentnr;
      ar & identifiedpoints & identifiedpoints_nr;
      // idpoints_table is archived as raw bytes: store 1-based numbers, independent of BASE
      auto to_nr1 = [&]()
      {
        for (int i = 0; i < idpoints_table.Size(); i++)
          for (auto & p : idpoints_table[i])
            for (int k = 0; k < 2; k++)
              p[k] = PointIndex::FromNr0(p[k].Nr1());
      };
      auto from_nr1 = [&]()
      {
        for (int i = 0; i < idpoints_table.Size(); i++)
          for (auto & p : idpoints_table[i])
            for (int k = 0; k < 2; k++)
              p[k] = PointIndex::FromNr1(p[k].Nr0());
      };
      if (ar.Output()) to_nr1();
      ar & idpoints_table;
      from_nr1();
      
      if (ar.Output())
        {
          size_t s = type.Size();
          ar & s;
          for (auto & t : type)
            ar & (unsigned char&)(t);
        }
      else
        {
          size_t s;
          ar & s;
          type.SetSize(s);
          for (auto & t : type)
            ar & (unsigned char&)(t);
        }
      if (ar.GetVersion("netgen") > "v6.2.2404-66")
        ar & names;
    }    

  

  void Identifications :: Add (PointIndex pi1, PointIndex pi2, int identnr)
  {
    //  (*testout) << "Identification::Add, pi1 = " << pi1 << ", pi2 = " << pi2 << ", identnr = " << identnr << endl;
    PointIndices<2> pair (pi1, pi2);
    identifiedpoints.Set (pair, identnr);

    // IVec<3> tripl (pi1, pi2, identnr);
    identifiedpoints_nr.Set ( { { pi1, pi2 }, identnr }, 1);

    if (identnr > maxidentnr) maxidentnr = identnr;
    names.SetSize(maxidentnr);

    if (identnr+1 > idpoints_table.Size())
      idpoints_table.ChangeSize (identnr+1);
    idpoints_table.Add (identnr, pair);
  
    //  timestamp = NextTimeStamp();
  }




  void Identifications :: GetMap (int identnr, idmap_type & identmap, bool symmetric) const
  {
    identmap.SetSize (mesh.GetNP());
    identmap = PointIndex(PointIndex::INVALID);

    // idpoints_table only has a row for identifications that got points added
    if (identnr && identnr < idpoints_table.Size())
      for (int i = 0; i < idpoints_table[identnr].Size(); i++)
        {
          PointIndices<2> pair = idpoints_table[identnr][i];
          identmap[pair[0]] = pair[1];
          if(symmetric)
            identmap[pair[1]] = pair[0];
        }

    else
      {
        /*
        for (int i = 1; i <= identifiedpoints_nr.GetNBags(); i++)
          for (int j = 1; j <= identifiedpoints_nr.GetBagSize(i); j++)
        */
        for (auto [hash, val] : identifiedpoints_nr)
            {
              /*
              IVec<3> i3;
              int dummy;
              identifiedpoints_nr.GetData (i, j, i3, dummy);
              */

              auto [hash_pts, hash_nr] = hash;
              
              if (hash_nr == identnr || !identnr)
                {
                  /*
                  identmap.Elem(i3[0]) = i3[1];
                  if(symmetric)
                    identmap.Elem(i3[1]) = i3[0];
                  */
                  identmap[hash_pts[0]] = hash_pts[1];
                  if(symmetric)
                    identmap[hash_pts[1]] = hash_pts[0];
                }
            }  
      }

  }


  Array<std::tuple<PointIndices<2>, int>> Identifications :: GetPairs () const
  {
    Array<std::tuple<PointIndices<2>, int>> pairs;
    for(auto [hash, dummy] : identifiedpoints_nr)
      pairs.Append (hash);
    return pairs;
  }

  void Identifications :: GetPairs (int identnr, 
                                    Array<PointIndices<2>> & identpairs) const
  {
    identpairs.SetSize(0);
  
    if (identnr == 0)
      {
        /*
      for (int i = 1; i <= identifiedpoints.GetNBags(); i++)
        for (int j = 1; j <= identifiedpoints.GetBagSize(i); j++)
          {
            IVec<2> i2;
            int nr;
            identifiedpoints.GetData (i, j, i2, nr);
            identpairs.Append (i2);
          }
        */
        for (auto [hash,val] : identifiedpoints)
          identpairs.Append (hash);
      }
    else
      {
        /*
      for (int i = 1; i <= identifiedpoints_nr.GetNBags(); i++)
        for (int j = 1; j <= identifiedpoints_nr.GetBagSize(i); j++)
          {
            IVec<3> i3;
            int dummy;
            identifiedpoints_nr.GetData (i, j, i3 , dummy);
          
            if (i3[2] == identnr)
              identpairs.Append (IVec<2>(i3[0], i3[1]));
          }
        */
        for (auto [hash,val] : identifiedpoints_nr)
          {
            auto [hash_pts, hash_nr] = hash;
            if (hash_nr == identnr)
              identpairs.Append (hash_pts);
          }
      }
  }


  ostream & operator<< (ostream & ost, const BoundaryLayerParameters & mp)
  {
    auto print_vec = [&ost](auto & v) {
        ost << "[";
        for (const auto & val : v)
          ost << val << " ";
        ost << "]";
    };
    ost << "BoundaryLayerParameters" << endl;
    ost << "  boundary: ";
    switch(mp.boundary.index())
      {
        case 0: ost << std::get<0>(mp.boundary); break;
        case 1: ost << std::get<1>(mp.boundary); break;
        case 2: print_vec(std::get<2>(mp.boundary)); break;
      }
    ost << "\n  thickness: ";
    switch(mp.thickness.index())
      {
        case 0: ost << std::get<0>(mp.thickness); break;
        case 1: print_vec(std::get<1>(mp.thickness)); break;
      }
    ost <<"\n  new_material: ";
    if(!mp.new_material) ost << "nullopt";
    else {
      switch(mp.new_material->index())
        {
          case 0: ost << std::get<0>(*mp.new_material); break;
          case 1:
          for (const auto & [key, value] : std::get<1>(*mp.new_material))
            ost << key << " -> " << value << ", ";
          break;
        }
    }
    ost << "\n  domain: ";
    switch(mp.domain.index())
      {
        case 0: ost << std::get<0>(mp.domain); break;
        case 1: ost << std::get<1>(mp.domain); break;
        case 2: print_vec(std::get<2>(mp.domain)); break;
      }
    ost << "\n  outside: " << mp.outside;
    ost << "\n  project_boundaries: ";
    if(mp.project_boundaries) {
      auto & proj_bnd = *mp.project_boundaries;
      switch(proj_bnd.index())
        {
          case 0: ost << std::get<0>(proj_bnd); break;
          case 1: print_vec(std::get<1>(proj_bnd)); break;
        }
    }
    else
      ost << "nullopt";
    ost << "\n  grow_edges: " << mp.grow_edges;
    ost << "\n  limit_growth_vectors: " << mp.limit_growth_vectors;
    ost << "\n  sides_keep_surfaceindex: " << (mp.sides_keep_surfaceindex ? ToString(*mp.sides_keep_surfaceindex) : "nullopt");
    ost << "\n  disable_curving: " << mp.disable_curving;
    ost << endl;
    return ost;
  }

  /*
  void MeshingParameters :: CopyFrom(const MeshingParameters & other)
  {
    //strcpy(optimize3d,other.optimize3d); 
    optimize3d = other.optimize3d;
    optsteps3d = other.optsteps3d;
    //strcpy(optimize2d,other.optimize2d); 
    optimize2d = other.optimize2d;
    optsteps2d = other.optsteps2d;
    opterrpow = other.opterrpow;
    blockfill = other.blockfill;
    filldist = other.filldist;
    safety = other.safety;
    relinnersafety = other.relinnersafety;
    uselocalh = other.uselocalh;
    grading = other.grading;
    delaunay = other.delaunay;
    maxh = other.maxh;
    //strcpy(const_cast<char*>(meshsizefilename), other.meshsizefilename);
    //const_cast<char*>(meshsizefilename) = other.meshsizefilename; //???
    meshsizefilename = other.meshsizefilename;
    startinsurface = other.startinsurface;
    checkoverlap = other.checkoverlap;
    checkoverlappingboundary = other.checkoverlappingboundary;
    checkchartboundary = other.checkchartboundary;
    curvaturesafety = other.curvaturesafety;
    segmentsperedge = other.segmentsperedge;
    parthread = other.parthread;
    elsizeweight = other.elsizeweight;
    giveuptol2d = other.giveuptol2d;
    giveuptol = other.giveuptol;
    maxoutersteps = other.maxoutersteps;
    starshapeclass = other.starshapeclass;
    baseelnp = other.baseelnp;       
    sloppy = other.sloppy;
    badellimit = other.badellimit;
    secondorder = other.secondorder;
    elementorder = other.elementorder;
    quad = other.quad;
    inverttets = other.inverttets;
    inverttrigs = other.inverttrigs;
  }
  */



}


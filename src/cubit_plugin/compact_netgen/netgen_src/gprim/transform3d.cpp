#include <mystdlib.h>

#include <myadt.hpp>
#include <gprim.hpp>
#include <linalg.hpp>

namespace netgen
{







ostream & operator<< (ostream & ost, Transformation3d & trans)
{
  ost << "offset = ";
  for (int i = 0; i <= 2; i++)
    ost << trans.offset[i] << " ";
  ost << endl << "linear = " << endl;
  for (int i = 0; i <= 2; i++)
    {
      for (int j = 0; j <= 2; j++)
        ost << trans.lin[i][j] << " ";
      ost << endl;
    }
  return ost;
}

  template <>
  Transformation<3> :: Transformation (const Point<3> & c, const Vec<3> & axes, double angle)
  {
    Vec<3> vc(c);
    Transformation<3> tc(vc);
    Transformation<3> tcinv(-vc);
    Transformation<3> r, ht, ht2;

    // r.SetAxisRotation (3, alpha);
    Vec<3> naxes = axes;
    naxes.Normalize();
    Vec<3> n1 = naxes.GetNormal();
    Vec<3> n2 = Cross(naxes, n1);
    r.v = Vec<3>(0,0,0);
    double co = cos(angle);
    double si = sin(angle);
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
        r.m(i,j) = naxes(i)*naxes(j) + co*(n1(i)*n1(j)+n2(i)*n2(j)) + si*( (n2(i)*n1(j)-n2(j)*n1(i)) );

    ht.Combine (tc, r);
    Combine (ht, tcinv);
  }

  
  template <int D>
  Transformation<D> :: Transformation (const Point<D> * pp)
  {
    v = Vec<D> (pp[0]);
    for (int i = 0; i < D; i++)
      for (int j = 0; j < D; j++)
        m(j,i) = pp[i+1](j)-pp[0](j);
  }
  
  template class Transformation<3>;
}

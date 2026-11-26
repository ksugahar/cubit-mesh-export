#include <algorithm>
#include <mystdlib.h>

#include <myadt.hpp>
#include <gprim.hpp>

namespace netgen
{
ostream & operator<<(ostream  & s, const Point<3> & p)
  {
  return s << "(" << p(0) << ", " << p(1) << ", " << p(2) << ")";
  }

ostream & operator<<(ostream  & s, const Vec<3> & v)
  {
  return s << "(" << v(0) << ", " << v(1) << ", " << v(2) << ")";
  }


/*
ostream & operator<<(ostream  & s, const ROTDenseMatrix3D & r)
  {
  return s << "{ (" << r.txx << ", " << r.txy << ", " << r.txz << ") , ("
                    << r.tyx << ", " << r.tyy << ", " << r.tyz << ") , ("
                    << r.tzx << ", " << r.tzy << ", " << r.tzz << ") }";
  }
*/

/*
Vec<3> operator- (const Point<3> & p1, const Point<3> & p2)
  {
  return Vec<3> (p1.X() - p2.X(), p1.Y() - p2.Y(),p1.Z() - p2.Z());
  }

Point<3> operator- (const Point<3> & p1, const Vec<3> & v)
  {
  return Point<3> (p1.X() - v.X(), p1.Y() - v.Y(),p1.Z() - v.Z());
  }

Point<3> operator+ (const Point<3> & p1, const Vec<3> & v)
  {
  return Point<3> (p1.X() + v.X(), p1.Y() + v.Y(),p1.Z() + v.Z());
  }

Vec<3> operator- (const Vec<3> & v1, const Vec<3> & v2)
  {
  return Vec<3> (v1.X() - v2.X(), v1.Y() - v2.Y(),v1.Z() - v2.Z());
  }

Vec<3> operator+ (const Vec<3> & v1, const Vec<3> & v2)
  {
  return Vec<3> (v1.X() + v2.X(), v1.Y() + v2.Y(),v1.Z() + v2.Z());
  }

Vec<3> operator* (double scal, const Vec<3> & v)
  {
  return Vec<3> (scal * v.X(), scal * v.Y(), scal * v.Z());
  }
*/
/*
double operator* (const Vec<3> & v1, const Vec<3> & v2)
  {
  return v1.X() * v2.X() + v1.Y() * v2.Y() + v1.Z() * v2.Z();
  }

double Cross (const Vec<3> & v1, const Vec<3> & v2)
  {
  return v1.X() * v2.Y() - v1.Y() * v2.X();
  }
*/

/*
void ROTDenseMatrix3D :: CalcRotMat(double ag, double bg, double lg, double size2, Vec<3> r)
  {
  size = size2;
  txx=size * ( cos(bg) * cos(lg) );
  txy=size * ( cos(bg) * sin(lg) );
  txz=size * (-sin(bg)           );

  tyx=size * ( sin(ag) * sin(bg) * cos(lg) - cos(ag) * sin(lg) );
  tyy=size * ( sin(ag) * sin(bg) * sin(lg) + cos(ag) * cos(lg) );
  tyz=size * ( sin(ag) * cos(bg)                               );

  tzx=size * ( cos(ag) * sin(bg) * cos(lg) + sin(ag) * sin(lg) );
  tzy=size * ( cos(ag) * sin(bg) * sin(lg) - sin(ag) * cos(lg) );
  tzz=size * ( cos(ag) * cos(bg)                               );

  deltaR=r;
  }
ROTDenseMatrix3D :: ROTDenseMatrix3D(double ag, double bg, double lg, double size2, Vec<3> r)
  {CalcRotMat(ag, bg, lg, size2, r); }

ROTDenseMatrix3D :: ROTDenseMatrix3D(Vec<3> rot2)
  {
  Vec<3> r2(0,0,0);
  CalcRotMat(rot2.X(), rot2.Y(), rot2.Z(), 1, r2);
  }

ROTDenseMatrix3D ROTDenseMatrix3D :: INV()
  {
  ROTDenseMatrix3D rinv(txx/sqr(size),tyx/sqr(size),tzx/sqr(size),
                   txy/sqr(size),tyy/sqr(size),tzy/sqr(size),
                   txz/sqr(size),tyz/sqr(size),tzz/sqr(size),
                   1/size,deltaR);
  return rinv;
  }

Vec<3> operator* (const ROTDenseMatrix3D & r, const Vec<3> & v)
  {
  return Vec<3> (r.XX() * v.X() + r.XY() * v.Y() + r.XZ() * v.Z(),
                r.YX() * v.X() + r.YY() * v.Y() + r.YZ() * v.Z(),
                r.ZX() * v.X() + r.ZY() * v.Y() + r.ZZ() * v.Z() );
  }

Point<3> operator* (const ROTDenseMatrix3D & r, const Point<3> & p)
  {
  return Point<3> (r.XX() * p.X() + r.XY() * p.Y() + r.XZ() * p.Z(),
                  r.YX() * p.X() + r.YY() * p.Y() + r.YZ() * p.Z(),
                  r.ZX() * p.X() + r.ZY() * p.Y() + r.ZZ() * p.Z() );
  }
*/








/*
int Box3d :: Intersect (const Box3d & box2) const
{
  int i;
  for (i = 0; i <= 2; i++)
    if (minx[i] > box2.maxx[i] || maxx[i] < box2.minx[i])
      return 0;
  return 1;
}
*/

/*
void Box3d :: SetPoint (const Point<3> & p)
{
  minx[0] = maxx[0] = p.X();
  minx[1] = maxx[1] = p.Y();
  minx[2] = maxx[2] = p.Z();
}

void Box3d :: AddPoint (const Point<3> & p)
{
  if (p.X() < minx[0]) minx[0] = p.X();
  if (p.X() > maxx[0]) maxx[0] = p.X();
  if (p.Y() < minx[1]) minx[1] = p.Y();
  if (p.Y() > maxx[1]) maxx[1] = p.Y();
  if (p.Z() < minx[2]) minx[2] = p.Z();
  if (p.Z() > maxx[2]) maxx[2] = p.Z();
}
*/



const Box3d& Box3d :: operator+=(const Box3d& b)
{
  minx[0] = min2 (minx[0], b.minx[0]);
  minx[1] = min2 (minx[1], b.minx[1]);
  minx[2] = min2 (minx[2], b.minx[2]);
  maxx[0] = max2 (maxx[0], b.maxx[0]);
  maxx[1] = max2 (maxx[1], b.maxx[1]);
  maxx[2] = max2 (maxx[2], b.maxx[2]);

  return *this;
}

/*
void Box3d :: CreateNegMinMaxBox()
{
  minx[0] = MAXDOUBLE;
  minx[1] = MAXDOUBLE;
  minx[2] = MAXDOUBLE;
  maxx[0] = MINDOUBLE;
  maxx[1] = MINDOUBLE;
  maxx[2] = MINDOUBLE;

}
*/









/*
double Determinant (const Vec<3> & col1,
                    const Vec<3> & col2,
                    const Vec<3> & col3)
{
  return
    col1(0) * ( col2(1) * col3(2) - col2(2) * col3(1)) +
    col1(1) * ( col2(2) * col3(0) - col2(0) * col3(2)) +
    col1(2) * ( col2(0) * col3(1) - col2(1) * col3(0));
}
*/


/*
  gcc4.8.3  warning: array subscript is above array bounds [-Warray-bounds]
 */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif






// QuadraticFunction3d gqf (Point<3> (0,0,0), Vec<3> (1, 0, 0));






}

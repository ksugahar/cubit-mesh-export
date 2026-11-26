#include <mystdlib.h>

#include <linalg.hpp>


namespace netgen
{
  DenseMatrix :: DenseMatrix () 
  {
    data = NULL;
    height = 0;
    width = 0;
  }

  DenseMatrix :: DenseMatrix (int h, int w)
  {
    if (!w) w = h;
    width = w; 
    height = h;
    int hw = h*w;
    if (hw)
      data = new double[hw];
    else 
      data = 0;

    for (int i = 0 ; i < (hw); i++)
      data[i] = 0;
  }

  /*
    DenseMatrix :: DenseMatrix (int h, int w, const double * d) 
    : BaseMatrix (h, w)
    {
    int size = h * w;  
    int i;
  
    if (size)
    {
    data = new double[size]; 
    for (i = 0; i < size; i++)
    data[i] = d[i];
    }
    else
    data = NULL;
    }    
  */

  DenseMatrix :: DenseMatrix (const DenseMatrix & m2)
  {
    data = NULL; height = width = 0;
    SetSize (m2.Height(), m2.Width());
    if (Height() && Width())
      memcpy (data, m2.data, sizeof(double) * (Height() * Width()));
  }

  DenseMatrix :: ~DenseMatrix ()
  {
    delete [] data;
  }
  
  
  void DenseMatrix :: SetSize (int h, int w)
  {
    if (!w) w = h;
    if (height == h && width == w)
      return;
          
    height = h;
    width = w;
    
    delete[] data;
    
    if (h && w)  
      data = new double[h*w];
    else
      data = NULL;
  }


  /*
    DenseMatrix & DenseMatrix :: operator= (const BaseMatrix & m2)
    {
    int i, j;

    SetSize (m2.Height(), m2.Width());

    if (data)
    for (i = 1; i <= Height(); i++)
    for (j = 1; j <= Width(); j++)
    Set (i, j, m2(i, j));
    else
    (*myerr) << "DenseMatrix::Operator=: Matrix not allocated" << endl;

    return *this;
    }
  */


  DenseMatrix & DenseMatrix :: operator= (const DenseMatrix & m2)
  {
    SetSize (m2.Height(), m2.Width());
    
    if (data) memcpy (data, m2.data, sizeof(double) * m2.Height() * m2.Width());
    return *this;
  }


  DenseMatrix & DenseMatrix :: operator+= (const DenseMatrix & m2)
  {
    int i;
    double * p, * q;
    
    if (Height() != m2.Height() || Width() != m2.Width())
      {
        throw Exception ("DenseMatrix::operator+=: sizes don't fit");
      }
    
    if (data)
      {
        p = data;
        q = m2.data;
        for (i = Width() * Height(); i > 0; i--)
          {
            *p += *q;
            p++;
            q++;
          }
      }
    else
      throw Exception ("DenseMatrix::operator+=: matrix not allocated");

    return *this;
  }


  DenseMatrix & DenseMatrix :: operator-= (const DenseMatrix & m2)
  {
    int i;
    double * p, * q;

    if (Height() != m2.Height() || Width() != m2.Width())
      {
        throw Exception ("DenseMatrix::operator-=: sizes don't fit");
      }

    if (data)
      {
        p = data;
        q = m2.data;
        for (i = Width() * Height(); i > 0; i--)
          {
            *p -= *q;
            p++;
            q++;
          }
      }
    else
      throw Exception ("DenseMatrix::operator-=: matrix not allocated");

    return *this;
  }

  DenseMatrix & DenseMatrix :: operator= (double v)
  {
    double * p = data;

    if (data)
      for (int i = width*height; i > 0; i--, p++)
        *p = v;

    return *this;
  }



  DenseMatrix & DenseMatrix :: operator*= (double v)
  {
    double * p = data;

    if (data)
      for (int i = width*height; i > 0; i--, p++)
        *p *= v;

    return *this;
  }


  double DenseMatrix :: Det () const
  {
    if (width != height)
      {
        throw Exception ("DenseMatrix::Det: width != height");
      }

    switch (width)
      {
      case 1: return data[0];
      case 2: return data[0] * data[3] - data[1] * data[2];
      case 3: return data[0] * data[4] * data[8]
          + data[1] * data[5] * data[6]
          + data[2] * data[3] * data[7] 
          - data[0] * data[5] * data[7]
          - data[1] * data[3] * data[8]
          - data[2] * data[4] * data[6];
      default:
        {
          throw Exception ("DenseMatrix::Det: general size not implemented (size=" + ToString(width) + ")");
        }
      }
  }


  void CalcInverse (const DenseMatrix & m1, DenseMatrix & m2)
  {
    double det;

    if (m1.Width() != m1.Height())
      {
        throw Exception ("CalcInverse: matrix not square");
      }
    if (m1.Width() != m2.Width() || m1.Height() != m2.Height())
      {
        throw Exception ("CalcInverse: dim(m2) != dim(m1)");
      }


    if (m1.Width() <= 3)
      {
        det = m1.Det();
        if (det == 0)
          {
            (*testout) << "CalcInverse: Matrix singular" << endl;
            return;
          }

        det = 1.0 / det;
        switch (m1.Width())
          {
          case 1:
            {
              m2(0,0) = det;
              return;
            }
          case 2:
            {
              m2(0,0) = det * m1(3);
              m2(1,1) = det * m1(0);  
              m2(0,1) = -det * m1(1);
              m2(1,0) = - det * m1(2);
              return;
            }
          case 3:
            {
              m2(0, 0) =  det * (m1(4) * m1(8) - m1(5) * m1(7));
              m2(1, 0) = -det * (m1(3) * m1(8) - m1(5) * m1(6));
              m2(2, 0) =  det * (m1(3) * m1(7) - m1(4) * m1(6));

              m2(0, 1) = -det * (m1(1) * m1(8) - m1(2) * m1(7));
              m2(1, 1) =  det * (m1(0) * m1(8) - m1(2) * m1(6));
              m2(2, 1) = -det * (m1(0) * m1(7) - m1(1) * m1(6));

              m2(0, 2) =  det * (m1(1) * m1(5) - m1(2) * m1(4));
              m2(1, 2) = -det * (m1(0) * m1(5) - m1(2) * m1(3));
              m2(2, 2) =  det * (m1(0) * m1(4) - m1(1) * m1(3));
              return;
            }
          }
      }
    
    else
      {
        int i, j, k, n;
        n = m1.Height();
      

#ifdef CHOL
        int dots = (n > 200);

        // Cholesky
      
        double x;
        Vector p(n);

        m2 = m1;
        /*
          m2.SetSymmetric();
          if (!m2.Symmetric())
          cerr << "m should be symmetric for Cholesky" << endl;
        */

        for (i = 1; i <= n; i++)
          for (j = 1; j < i; j++)
            m2.Elem(j, i) = m2.Get(i, j);
      
        for (i = 1; i <= n; i++)
          {
            if (dots && i % 10 == 0)
              cout << "." << flush;

            for (j = i; j <= n; j++)
              {
                x = m2.Get(i, j);

                const double * pik = &m2.Get(i, 1);
                const double * pjk = &m2.Get(j, 1);

                for (k = i-2; k >= 0; --k, ++pik, ++pjk)
                  x -= (*pik) * (*pjk);
                  
                // for (k = i-1; k >= 1; --k)
                //   x -= m2.Get(j, k) * m2.Get(i, k);

                if (i == j)
                  {
                    if (x <= 0)
                      {
                        cerr << "Matrix indefinite 1" << endl;
                        return;
                      }
                  
                    p.Elem(i) = 1 / sqrt(x);
                  }
                else
                  {
                    m2.Elem(j, i) = x * p.Get(i);
                  }
              }
          }

        for (i = 1; i <= n; i++)
          m2.Elem(i, i) = 1 / p.Get(i);

        // check: A = L L^t

        //       for (i = 1; i <= n; i++)
        //      for (j = 1; j <= n; j++)
        //        {
        //          x = 0;
        //          for (k = 1; k <= i && k <= j; k++)
        //            x += m2.Get(i, k) * m2.Get(j, k);
        //          (*testout) << "err " << i << "," << j << " = " << (m1.Get(i, j) - x) << endl;
        //        }


      
        // calc L^{-1}, store upper triangle
      
        //      DenseMatrix hm(n);
        //      hm = m2;

        for (i = 1; i <= n; i++)
          {
            if (dots && i % 10 == 0)
              cout << "+" << flush;

            for (j = i; j <= n; j++)
              {
                x = 0;
                if (j == i) x = 1;

                const double * pjk = &m2.Get(j, i);
                const double * pik = &m2.Get(i, i);
                for (k = i; k < j; k++, ++pjk, ++pik)
                  x -= *pik * *pjk;

                //  for (k = i; k < j; k++)
                //  x -= m2.Get(j, k) * m2.Get(i, k);

                m2.Elem(i, j) = x / m2.Get(j, j);
              }
          }
      
        //      (*testout) << "check L^-1" << endl;
        //      for (i = 1; i <= n; i++)
        //      for (j = 1; j <= n; j++)
        //        {
        //          x = 0;
        //          for (k = j; k <= i; k++)
        //            x += hm.Get(i, k) * m2.Get(j, k);
        //          (*testout) << "i, j = " << i << "," << j << " x = " << x << endl;
        //        }


        // calc A^-1 = L^-T * L^-1

        for (i = 1; i <= n; i++)
          {
            if (dots && i % 10 == 0)
              cout << "-" << flush;

            for (j = 1; j <= i; j++)
              {
                x = 0;
                k = i;
                if (j > i) k = j;

                const double * pik = &m2.Get(i, k);
                const double * pjk = &m2.Get(j, k);

                for ( ; k <= n; ++k, ++pik, ++pjk)
                  x += *pik * *pjk;
                // for (  ; k <= n; k++)
                //   x += m2.Get(i, k) * m2.Get(j, k);
              
                m2.Elem(i, j) = x;
              }
          }
          
        for (i = 1; i <= n; i++)
          for (j = 1; j < i; j++)
            m2.Elem(j, i) = m2.Get(i, j);
      
        if (dots) cout << endl;
#endif



        // Gauss - Jordan - algorithm
      
        int r, hi;
        double max, hr;
      

        Array<int> p(n);   // pivot-permutation
        Vector hv(n);
    
      
        m2 = m1;

        /*      
                if (m2.Symmetric())
                for (i = 1; i <= n; i++)
                for (j = 1; j < i; j++)
                m2.Elem(j, i) = m2.Get(i, j);
        */
      
        // Algorithm of Stoer, Einf. i. d. Num. Math, S 145
      
        for (j = 1; j <= n; j++)
          p[j-1] = j;
      
        for (j = 1; j <= n; j++)
          {
            // pivot search
          
            max = fabs(m2.Get(j, j));
            r = j;
          
            for (i = j+1; i <= n ;i++)
              if (fabs (m2.Get(i, j)) > max)
                {
                  r = i;
                  max = fabs (m2.Get(i, j));
                }
          
            if (max < 1e-20)
              {
                cerr << "Inverse matrix: matrix singular" << endl;
                *testout << "Inverse matrix: matrix singular" << endl;
                return;
              }
          
            r = j;
          
            // exchange rows
            if (r > j)
              {
                for (k = 1; k <= n; k++)
                  {
                    hr = m2.Get(j, k);
                    m2.Elem(j, k) = m2.Get(r, k);
                    m2.Elem(r, k) = hr;
                  }
                hi = p[j-1];
                p[j-1] = p[r-1];
                p[r-1] = hi;
              }
          
          
            // transformation
          
            hr = 1 / m2.Get(j, j);
            for (i = 1; i <= n; i++)
              m2.Elem(i, j) *= hr;
            m2.Elem(j, j) = hr;
          
            for (k = 1; k <= n; k++)
              if (k != j)
                {
                  for (i = 1; i <= n; i++)
                    if (i != j)
                      m2.Elem(i, k) -= m2.Elem(i, j) * m2.Elem(j, k);
                  m2.Elem(j, k) *= -hr;
                }
          }
      
        // col exchange
      
        for (i = 1; i <= n; i++)
          {
            for (k = 1; k <= n; k++)
              hv(p[k-1]-1) = m2.Get(i, k);
            for (k = 1; k <= n; k++)
              m2.Elem(i, k) = hv(k-1);
          }



        /*
          if (m1.Symmetric())
          for (i = 1; i <= n; i++)
          for (j = 1; j < i; j++)
          m1.Elem(j, i) = m1.Get(i, j);

          m2 = 0;
    
          for (i = 1; i <= n; i++)
          m2.Elem(i, i) = 1;
      
          for (i = 1; i <= n; i++)
          {
          //    cout << '.' << flush;
          q = m1.Get(i, i);
          for (k = 1; k <= n; k++)
          {
          m1.Elem(i, k) /= q;
          m2.Elem(i, k) /= q;
          }
        
          for (j = i+1; j <= n; j++)
          {
          q = m1.Elem(j, i);

          double * m1pi = &m1.Elem(i, i);
          double * m1pj = &m1.Elem(j, i);

          for (k = n; k >= i; --k, ++m1pi, ++m1pj)
          *m1pj -= q * (*m1pi);

          double * m2pi = &m2.Elem(i, 1);
          double * m2pj = &m2.Elem(j, 1);

          for (k = i; k > 0; --k, ++m2pi, ++m2pj)
          *m2pj -= q * (*m2pi);

          //        for (k = 1; k <= n; k++)  
          //          {
          //          m1.Elem(j, k) -= q * m1.Elem(i, k);
          //          m2.Elem(j, k) -= q * m2.Elem(i, k);
          //          }
          
          }
          }  
            
          for (i = n; i >= 1; i--)
          {
          //    cout << "+" << flush;
          for (j = 1; j < i; j++)
          {
          q = m1.Elem(j, i);

          double * m2pi = &m2.Elem(i, 1);
          double * m2pj = &m2.Elem(j, 1);

          for (k = n; k > 0; --k, ++m2pi, ++m2pj)
          *m2pj -= q * (*m2pi);     

            
          //        for (k = 1; k <= n; k++)
          //          {
          //            m1.Elem(j, k) -= q * m1.Elem(i, k);
          //            m2.Elem(j, k) -= q * m2.Elem(i, k);
          //          }    
          }         
          }

          if (m2.Symmetric())
          {
          for (i = 1; i <= n; i++)
          for (j = 1; j < i; j++)
          m2.Elem(i, j) = m2.Elem(j, i);
          }
        */
      }
  }






  void CalcABt (const DenseMatrix & a, const DenseMatrix & b, DenseMatrix & m2)
  {
    int n1 = a.Height();
    int n2 = a.Width();
    int n3 = b.Height();
    int i, j, k;
    double sum;

    if (m2.Height() != n1 || m2.Width() != n3 || b.Width() != n2)
      {
        throw Exception ("CalcABt: sizes don't fit");
      }

    double * pm2 = &m2.Elem(1, 1);
    const double * pa1 = &a.Get(1, 1);

    for (i = 1; i <= n1; i++)
      {
        const double * pb = &b.Get(1, 1);
        for (j = 1; j <= n3; j++)
          {
            sum = 0;
            const double * pa = pa1;
          
            for (k = 1; k <= n2; k++)
              {
                sum += *pa * *pb;
                pa++; pb++;
              }
          
            *pm2 = sum;
            pm2++;
          }
        pa1 += n2;
      }
  }








  DenseMatrix operator* (const DenseMatrix & m1, const DenseMatrix & m2)
  {
    DenseMatrix temp (m1.Height(), m2.Width());

    if (m1.Width() != m2.Height())
      throw Exception ("DenseMatrix::operator*: matrix sizes don't fit");
    Mult (m1, m2, temp);
    return temp;
  }


  void Mult (const DenseMatrix & m1, const DenseMatrix & m2, DenseMatrix & m3)
  {
    double sum;
    double *p1, *p1s, *p1sn, *p1snn, *p2, *p2s, *p2sn, *p3;

    if (m1.Width() != m2.Height() || m1.Height() != m3.Height() ||
        m2.Width() != m3.Width() )
      {
        throw Exception ("DenseMatrix::Mult: matrix sizes don't fit, m1: " + ToString(m1.Height()) + " x " + ToString(m1.Width())
                         + ", m2: " + ToString(m2.Height()) + " x " + ToString(m2.Width())
                         + ", m3: " + ToString(m3.Height()) + " x " + ToString(m3.Width()));
      }
    /*
      else if (m1.Symmetric() || m2.Symmetric() || m3.Symmetric())
      {
      (*myerr) << "DenseMatrix :: Mult: not implemented for symmetric matrices" << endl;
      return;
      }
    */
    else
      {
        //      int i, j, k;
        int n1 = m1.Height();
        int n2 = m2.Width();
        int n3 = m1.Width();

        /*
          for (i = n1 * n2-1; i >= 0; --i)
          m3.data[i] = 0;

          const double * pm1 = &m1.Get(1, 1);
          for (i = 1; i <= n1; i++)
          {
          const double * pm2 = &m2.Get(1, 1);
          double * pm3i = &m3.Elem(i, 1);

          for (j = 1; j <= n3; j++)
          {
          const double vm1 = *pm1;
          ++pm1;
          //          const double vm1 = m1.Get(i, j);
          double * pm3 = pm3i;
          //          const double * pm2 = &m2.Get(j, 1);

          for (k = 0; k < n2; k++)
          {
          *pm3 += vm1 * *pm2;
          ++pm2;
          ++pm3;
          }

          //        for (k = 1; k <= n2; k++)
          //          m3.Elem(i, k) += m1.Get(i, j) * m2.Get(j, k);
          }
          }
        */

        /*
          for (i = 1; i <= n1; i++)
          for (j = 1; j <= n2; j++)
          {
          sum = 0;
          for (k = 1; k <= n3; k++)
          sum += m1.Get(i, k) * m2.Get(k, j);
          m3.Set(i, j, sum);
          }
        */


        /*
          for (i = 1; i <= n1; i++)
          {
          const double pm1i = &m1.Get(i, 1);
          const double pm2j = &m2.Get(1, 1);

          for (j = 1; j <= n2; j++)
          {
          double sum = 0;
          const double * pm1 = pm1i;
          const double * pm2 = pm2j;
          pm2j++;

          for (k = 1; k <= n3; k++)
          {
          sum += *pm1 * *pm2;
          ++pm1;
          pm2 += n2;
          }
              
          m3.Set (i, j, sum);
          }
          }
        */


        p3 = m3.data;
        p1s = m1.data;
        p2sn = m2.data + n2;
        p1snn = p1s + n1 * n3;

        while (p1s != p1snn)
          {
            p1sn = p1s + n3;
            p2s = m2.data;
          
            while (p2s != p2sn)
              {
                sum = 0;
                p1 = p1s;
                p2 = p2s;
                p2s++;

                while (p1 != p1sn)
                  {
                    sum += *p1 * *p2;
                    p1++;
                    p2 += n2;
                  }
                *p3++ = sum;
              }
            p1s = p1sn;
          }
      }
  }  



  DenseMatrix operator+ (const DenseMatrix & m1, const DenseMatrix & m2)
  {
    DenseMatrix temp (m1.Height(), m1.Width());
    int i, j;

    if (m1.Width() != m2.Width() || m1.Height() != m2.Height())
      throw Exception ("DenseMatrix::operator+: matrix sizes don't fit");
    for (i = 1; i <= m1.Height(); i++)
      for (j = 1; j <= m1.Width(); j++)
        temp.Set(i, j, m1.Get(i, j) + m2.Get(i, j));
    return temp;
  }





  /*
    void DenseMatrix :: Mult (const Vector & v, Vector & prod) const
    {
    double sum, val;
    const double * mp, * sp;
    double * dp;
    // const Vector & v = bv.CastToVector();
    // Vector & prod = bprod.CastToVector();
  

    int n = Height();
    int m = Width();

    if (prod.Size() != n)
    prod.SetSize (n);

    {
    if (Symmetric())
    {
    int i, j;


    for (i = 1; i <= n; i++)
    {
    sp = &v.Get(1);
    dp = &prod.Elem(1);
    mp = &Get(i, 1);

    val = v.Get(i);
    sum = Get(i, i) * val;

    for (j = 1; j < i; ++j, ++mp, ++sp, ++dp)
    {
    sum += *mp * *sp;
    *dp += val * *mp;
    }

    prod.Elem(i) = sum;
    }
    }
    else
    {
    mp = data;
    dp = &prod.Elem(1);
    for (int i = 1; i <= n; i++)
    {
    sum = 0;
    sp = &v.Get(1);
              
    for (int j = 1; j <= m; j++)
    {
    //        sum += Get(i,j) * v.Get(j);
    sum += *mp * *sp;
    mp++;
    sp++;
    }
              
    //      prod.Set (i, sum);
    *dp = sum;
    dp++;
    }
    }
    }
    }
  */






  /*
    BaseMatrix * DenseMatrix :: Copy () const
    {
    return new DenseMatrix (*this);
    }
  */




  ostream & operator<< (ostream & ost, const DenseMatrix & m)
  {
    for (int i = 0; i < m.Height(); i++)
      {
        for (int j = 0; j < m.Width(); j++)
          ost << m.Get(i+1,j+1) << " ";
        ost << endl;
      }
    return ost;
  }

  

}

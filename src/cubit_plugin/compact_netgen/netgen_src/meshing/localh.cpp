#include <mystdlib.h>
#include "meshing.hpp"


namespace netgen
{

  BlockAllocator GradingBox :: ball(sizeof (GradingBox));

  void * GradingBox :: operator new(size_t)
  {
    return ball.Alloc();
  }

  void GradingBox :: operator delete (void * p)
  {
    ball.Free (p);
  }







































  void LocalH :: 
  FindInnerBoxesRec2 (GradingBox * box,
                      const class AdFront2 & adfront, 
                      FlatArray<Box<2>> faceboxes,
                      FlatArray<int> faceinds) // , int nfinbox)
  {
    if (!box) return;

    GradingBox * father = box -> father;    
    
    if (!father->flags.cutboundary)
      {
        box->flags.isinner = father->flags.isinner;
        box->flags.pinner = father->flags.pinner;
        box->flags.cutboundary = false;
      }
    else
      {        
        if (father->flags.isinner)
          {
            cout << "how is this possible ???" << endl;
            box->flags.pinner = 1;
          }
        else
          {
            Point<2> c(box->xmid[0], box->xmid[1]); 
            Point<2> fc(father->xmid[0], father->xmid[1]); 
            Box<2> boxcfc(c,fc);

            // reorder: put faces cutting boxcfc first:
            int iused = 0;
            int inotused = faceinds.Size()-1;
            while (iused <= inotused)
              {
                while ( (iused <= inotused) && boxcfc.Intersect (faceboxes[faceinds[iused]]))
                  iused++;
                while ( (iused <= inotused) && !boxcfc.Intersect (faceboxes[faceinds[inotused]]))
                  inotused--;
                if (iused < inotused)
                  {
                    Swap (faceinds[iused], faceinds[inotused]);
                    iused++;
                    inotused--;
                  }
              }

            // bool sameside = adfront->SameSide (c2d, cf2d, &faceused2);
            auto sub = faceinds.Range(0, iused);
            bool sameside = adfront.SameSide (c, fc, &sub);
            if (sameside)
              box->flags.pinner = father->flags.pinner;
            else
              box->flags.pinner = 1 - father->flags.pinner;
          }
      
        if (box->flags.cutboundary)
          box->flags.isinner = 0;
        else
          box->flags.isinner = box->flags.pinner;
      }


    
    int iused = 0;
    if (faceinds.Size())
      {
        Point<2> c(box->xmid[0], box->xmid[1]); // box->xmid[2]);
        Vec<2> v(box->h2, box->h2);
        Box<2> boxc(c-v, c+v);
        
        // reorder again: put faces cutting boxc first:    
        int inotused = faceinds.Size()-1;
        while (iused <= inotused)
          {
            while ( (iused <= inotused) && boxc.Intersect (faceboxes[faceinds[iused]]))
              iused++;
            while ( (iused <= inotused) && !boxc.Intersect (faceboxes[faceinds[inotused]]))
              inotused--;
            if (iused < inotused)
              {
                Swap (faceinds[iused], faceinds[inotused]);
                iused++;
                inotused--;
              }
          }
      }


    if (box->flags.isinner || box->flags.cutboundary)
      for (int i = 0; i < 8; i++)
        FindInnerBoxesRec2 (box->childs[i], adfront, faceboxes, faceinds.Range(0,iused));
  }



















  

  

}

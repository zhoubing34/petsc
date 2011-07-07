/* 
 GAMG geometric-algebric multiogrid PC - Mark Adams 2011
 */

#include "petscvec.h" 
#include <../src/mat/impls/aij/seq/aij.h>
#include <../src/mat/impls/aij/mpi/mpiaij.h>

#define REAL PetscReal
#include <triangle.h>

#include <assert.h>
#include <petscblaslapack.h>

typedef enum { DELETED, SELECTED, NOT_DONE } NState;

/* Private context for the GAMG preconditioner */
typedef struct{
  PetscInt       m_gid;      // global vertex index
  PetscInt       m_lid;      // local vertex index
  PetscInt       m_degree;   // vertex degree
  PetscInt       m_next_idx; // Next list item
  NState         m_state;    // Selected, not decided, or deleted
  PetscMPIInt    m_procID;   // The process
} GNode;

int compare (const void * a, const void * b)
{
  return (((GNode*)a)->m_degree - ((GNode*)b)->m_degree);
}

/* -------------------------------------------------------------------------- */
/*
   selectCrs - simple maximal independent set (MIS)

   Input Parameter:
   . Gmat - glabal matrix on this fine level
   . gnodes - node list
   . a_Sel - num selected
*/
#undef __FUNCT__
#define __FUNCT__ "selectCrs"
PetscErrorCode selectCrs( Mat Gmat, GNode gnodes[],PetscInt *a_Sel)
{
  PetscErrorCode ierr;
  PetscInt       kk,Istart,Iend,nloc,col;

  PetscFunctionBegin;
  ierr = MatGetOwnershipRange(Gmat,&Istart,&Iend);  CHKERRQ(ierr); /* always AIJ */
  nloc = Iend - Istart;
  {  /* need an inverse map - locals */
    PetscInt lid_graphID[nloc+1];
    PetscInt nDone = 0;
    for(kk=0;kk<nloc;kk++){
      lid_graphID[gnodes[kk].m_lid] = kk;
    }
    /* MIS */
    *a_Sel = 0; 
    while ( nDone < nloc ) {
      for(kk=0;kk<nloc;kk++){
        if( gnodes[kk].m_state == NOT_DONE ) {
          PetscInt gid = gnodes[kk].m_gid;
          const PetscInt *idx; PetscInt ncols;
          ierr = MatGetRow(Gmat,gid,&ncols,&idx,0); CHKERRQ(ierr);
          /* add parallel test with test for degree */
          if (PETSC_TRUE){ /* select */
            PetscMPIInt sendProcs[64]; PetscInt nSends = 0;
            gnodes[kk].m_state = SELECTED;
            (*a_Sel)++; nDone++;
            //PetscPrintf(PETSC_COMM_WORLD,"%s select %d, degree=%d\n",__FUNCT__,gid,gnodes[kk].m_degree);
            /* delete ndighbors and collect sends */
            for(col=0;col<ncols;col++){
              PetscInt gidj = idx[col], lidj = gidj - Istart;
              PetscInt jj; /* index in sorted space */
              if(lidj<0 || lidj >= nloc ) { /* get local index in gnodes ?*/
                SETERRQ(((PetscObject)Gmat)->comm,PETSC_ERR_LIB,"non-local node!!!");
                PetscMPIInt proc = 0; /*????*/
                PetscInt kk;
                for(kk=0;kk<nSends;kk++)if( sendProcs[kk]==proc ) break;
                if(kk==nSends)sendProcs[nSends++] = proc;
                jj = 0; // hash table lookup
              }
              else{
                jj = lid_graphID[lidj];
              }
              if( gnodes[jj].m_state == NOT_DONE ){ /* need to check procID for parallel */
                //PetscPrintf(PETSC_COMM_WORLD,"\t%s delete %d with %d\n",__FUNCT__,jj,kk);
                nDone++;
                gnodes[jj].m_state = DELETED;
                gnodes[jj].m_next_idx = gnodes[kk].m_next_idx;
                gnodes[kk].m_next_idx = jj;
              }
            }
            /* send message - should collect these */
            for(kk=0;kk<nSends;kk++){
            }
          }
          ierr = MatRestoreRow(Gmat,gid,&ncols,&idx,0); CHKERRQ(ierr);
        }
      }
      /* poll for messages */
    }
  }
  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------- */
/*
 triangulateAndFormProl
 
   Input Parameter:
   . gnodes - nodes with selected flag
   . a_coords - blocked Vec of coords
   . a_nSel - 
   . Prol - prolongation operator 
*/
#undef __FUNCT__
#define __FUNCT__ "triangulateAndFormProl"
PetscErrorCode triangulateAndFormProl( GNode gnodes[],Vec a_coords,const PetscInt a_nSel,Mat Prol)
{
  PetscErrorCode ierr;
  PetscInt       kk,nloc_wg,nn,my0,bs=1,jj,tid,tt,sid,cid,kkk,crsID,idx;
  PetscScalar    *lid_crd;
  struct triangulateio in,mid;

  PetscFunctionBegin;
  ierr = VecGetLocalSize( a_coords, &nn ); CHKERRQ(ierr);
  nloc_wg = nn/2; /* 2D */
  VecGetArray( a_coords, &lid_crd );
  /* traingle */
  /* Define input points - in*/
  in.numberofpoints = a_nSel;
  in.numberofpointattributes = 0;
  /* get a_nSel points */
  ierr = PetscMalloc( 2*(a_nSel)*sizeof(REAL), &in.pointlist ); CHKERRQ(ierr);
  for(kk=0,sid=0;kk<nloc_wg;kk++){
    if( gnodes[kk].m_state == SELECTED ) {
      PetscInt lid = gnodes[kk].m_lid;
      for(jj=0;jj<2;jj++,sid++) in.pointlist[sid] = lid_crd[2*lid+jj];
    }
  }
  in.numberofsegments = 0;
  in.numberofedges = 0;
  in.numberofholes = 0;
  in.numberofregions = 0;
  in.trianglelist = 0; 
  in.segmentmarkerlist = 0;
  in.pointattributelist = 0; 
  in.pointmarkerlist = 0;
  in.triangleattributelist = 0; 
  in.trianglearealist = 0;
  in.segmentlist = 0;
  in.holelist = 0;
  in.regionlist = 0;
  in.edgelist = 0;
  in.edgemarkerlist = 0;
  in.normlist = 0;
 
  /* triangulate */
  mid.pointlist = 0;            /* Not needed if -N switch used. */
  /* Not needed if -N switch used or number of point attributes is zero: */
  mid.pointattributelist = 0;
  mid.pointmarkerlist = 0; /* Not needed if -N or -B switch used. */
  mid.trianglelist = 0;          /* Not needed if -E switch used. */
  /* Not needed if -E switch used or number of triangle attributes is zero: */
  mid.triangleattributelist = 0;
  mid.neighborlist = 0;         /* Needed only if -n switch used. */
  /* Needed only if segments are output (-p or -c) and -P not used: */
  mid.segmentlist = 0;
  /* Needed only if segments are output (-p or -c) and -P and -B not used: */
  mid.segmentmarkerlist = 0;
  mid.edgelist = 0;             /* Needed only if -e switch used. */
  mid.edgemarkerlist = 0;   /* Needed if -e used and -B not used. */

  /* Triangulate the points.  Switches are chosen to read and write a  */
  /*   PSLG (p), preserve the convex hull (c), number everything from  */
  /*   zero (z), assign a regional attribute to each element (A), and  */
  /*   produce an edge list (e), a Voronoi diagram (v), and a triangle */
  /*   neighbor list (n).                                            */
  {
    char args[] = "pczQ"; /* c is needed ? */
    triangulate(args, &in, &mid, (struct triangulateio *) NULL );
    /* output .poly files for 'showme' */
    if(!PETSC_TRUE) {
      static int level = 0;
      FILE *file; char fname[32]; 
      sprintf(fname,"C%d.poly",level);
      file = fopen(fname, "w");
      /*First line: <# of vertices> <dimension (must be 2)> <# of attributes> <# of boundary markers (0 or 1)>*/
      fprintf(file, "%d  %d  %d  %d\n",in.numberofpoints,2,0,0);
      /*Following lines: <vertex #> <x> <y> */
      for(kk=0,sid=0;kk<in.numberofpoints;kk++){
        fprintf(file, "%d %e %e\n",kk,in.pointlist[sid],in.pointlist[sid+1]);
        sid += 2;
      }
      /*One line: <# of segments> <# of boundary markers (0 or 1)> */
      fprintf(file, "%d  %d\n",0,0);
      /*Following lines: <segment #> <endpoint> <endpoint> [boundary marker] */
      /* One line: <# of holes> */
      fprintf(file, "%d\n",0);
      /* Following lines: <hole #> <x> <y> */
      /* Optional line: <# of regional attributes and/or area constraints> */
      /* Optional following lines: <region #> <x> <y> <attribute> <maximum area> */
      fclose(file);
      /* elems */
      sprintf(fname,"C%d.ele",level); file = fopen(fname, "w");
      /*First line: <# of triangles> <nodes per triangle> <# of attributes> */
      fprintf(file, "%d %d %d\n",mid.numberoftriangles,3,0);
      /*Remaining lines: <triangle #> <node> <node> <node> ... [attributes]*/
      for(kk=0,sid=0;kk<mid.numberoftriangles;kk++){
        fprintf(file, "%d %d %d %d\n",kk,mid.trianglelist[sid],mid.trianglelist[sid+1],mid.trianglelist[sid+2]);
        sid += 3;
      }
      fclose(file);
      sprintf(fname,"C%d.node",level); file = fopen(fname, "w");
      /*First line: <# of vertices> <dimension (must be 2)> <# of attributes> <# of boundary markers (0 or 1)>*/
      fprintf(file, "%d  %d  %d  %d\n",in.numberofpoints,2,0,0);
      /*Following lines: <vertex #> <x> <y> */
      for(kk=0,sid=0;kk<in.numberofpoints;kk++){
        fprintf(file, "%d %e %e\n",kk,in.pointlist[sid],in.pointlist[sid+1]);
        sid += 2;
      }
      fclose(file);
      level++;
    }
  }

  { /* form P - setup some maps */
    PetscInt Istart,Iend,nloc;
    PetscInt clid_fidx[a_nSel]; /* so this does work ... */
    PetscInt nTri[a_nSel], node_tri[a_nSel][8];
    
    ierr = MatGetOwnershipRange(Prol,&Istart,&Iend);    CHKERRQ(ierr); /* BAIJ??? */
    my0 = Istart; /*bs?*/
    nloc = Iend - Istart; /* bs? */
    
    /* need an inverse map - coarse nodes */
    for(kk=0,cid=0;kk<nloc;kk++){
      if( gnodes[kk].m_state == SELECTED ) {
        clid_fidx[cid++] = gnodes[kk].m_lid;
      }
    }
    
    /* need list of triagles on node*/
    for(kk=0;kk<a_nSel;kk++) nTri[kk] = 0;
    for(tid=0,kk=0;tid<mid.numberoftriangles;tid++){
      for(jj=0;jj<3;jj++) {
        PetscInt cid = mid.trianglelist[kk++];
        if( nTri[cid] < 8 ) node_tri[cid][nTri[cid]++] = tid;
      }
    }
    
    /* find points and set prolongation */
    for(kkk=0,crsID=0;kkk<nloc;kkk++){
      if( gnodes[kkk].m_state == SELECTED ) {
        PetscInt id = kkk;
        do{
          PetscInt gid = gnodes[id].m_gid, lid = gid - my0;
          /* compute shape function for gid */
          const PetscReal fcoord[3] = { lid_crd[2*lid], lid_crd[2*lid+1], 1.0 };
          PetscBool haveit = PETSC_FALSE;  PetscScalar alpha[3];  PetscInt cids[3];
          for(jj=0 ; jj<nTri[crsID] && !haveit ; jj++) {
            PetscScalar AA[3][3];
            PetscInt tid = node_tri[crsID][jj];
            for(tt=0;tt<3;tt++){
              PetscInt cid2 = mid.trianglelist[3*tid + tt];
              PetscInt lid2 = clid_fidx[cid2]; /* get to coordinate through fine grid */
              AA[tt][0] = lid_crd[2*lid2]; AA[tt][1] = lid_crd[2*lid2 + 1]; AA[tt][2] = 1.0;
              cids[tt] = cid2; /* store for interp */
            }
            for(tt=0;tt<3;tt++) alpha[tt] = fcoord[tt];
            /* SUBROUTINE DGESV( N, NRHS, A, LDA, IPIV, B, LDB, INFO ) */
            PetscBLASInt N=3,NRHS=1,LDA=3,IPIV[3],LDB=3,INFO;
            dgesv_(&N, &NRHS, (PetscScalar*)AA, &LDA, IPIV, alpha, &LDB, &INFO);
            PetscBool have=PETSC_TRUE;
#define EPS 1.e-5
            for(tt=0; tt<3 && have ;tt++) if(alpha[tt] > 1.0+EPS || alpha[tt] < 0.0-EPS ) have=PETSC_FALSE;
            haveit = have;
          }
          if(!haveit) {
            /* brute force */
            PetscInt bestTID = -1; PetscScalar best_alpha = 1.e10; 
            for(tid=0 ; tid<mid.numberoftriangles && !haveit ; tid++ ){
              PetscScalar AA[3][3];
              for(tt=0;tt<3;tt++){
                PetscInt cid2 = mid.trianglelist[3*tid + tt];
                PetscInt lid2 = clid_fidx[cid2];
                AA[tt][0] = lid_crd[2*lid2]; AA[tt][1] = lid_crd[2*lid2 + 1]; AA[tt][2] = 1.0;
                cids[tt] = cid2; /* store for interp */
              }
              for(tt=0;tt<3;tt++) alpha[tt] = fcoord[tt];
              /* SUBROUTINE DGESV( N, NRHS, A, LDA, IPIV, B, LDB, INFO ) */
              PetscBLASInt N=3,NRHS=1,LDA=3,IPIV[3],LDB=3,INFO;
              dgesv_(&N, &NRHS, (PetscScalar*)AA, &LDA, IPIV, alpha, &LDB, &INFO);
              PetscBool have=PETSC_TRUE;  PetscScalar worst = 0.0,v;
              for(tt=0; tt<3 && have ;tt++) {
#define EPS2 1.e-2
                if(alpha[tt] > 1.0+EPS2 || alpha[tt] < 0.0-EPS2 ) have=PETSC_FALSE;
                if( (v=PetscAbs(alpha[tt]-0.5)) > worst ) worst = v;
              }
              if( worst < best_alpha ) {
                best_alpha = worst; bestTID = tid;
              }
              haveit = have;
            }
            if( !haveit ) {
              /* use best one */
              PetscScalar AA[3][3];
              for(tt=0;tt<3;tt++){
                PetscInt cid2 = mid.trianglelist[3*bestTID + tt];
                PetscInt lid2 = clid_fidx[cid2];
                AA[tt][0] = lid_crd[2*lid2]; AA[tt][1] = lid_crd[2*lid2 + 1]; AA[tt][2] = 1.0;
                cids[tt] = cid2; /* store for interp */
              }
              for(tt=0;tt<3;tt++) alpha[tt] = fcoord[tt];
              /* SUBROUTINE DGESV( N, NRHS, A, LDA, IPIV, B, LDB, INFO ) */
              PetscBLASInt N=3,NRHS=1,LDA=3,IPIV[3],LDB=3,INFO;
              dgesv_(&N, &NRHS, (PetscScalar*)AA, &LDA, IPIV, alpha, &LDB, &INFO);
            }
          }
          /* put in row of P */
          for(idx=0;idx<3;idx++){
            PetscReal shp = alpha[idx];
            if( PetscAbs(shp) > 1.e-6 ) {
              PetscInt cgid = cids[idx];
              PetscInt jj = cgid*bs, ii = gid*bs; /* need to gloalize */
              for(tt=0;tt<bs;tt++,ii++,jj++){
                ierr = MatSetValues(Prol,1,&ii,1,&jj,&shp,INSERT_VALUES); CHKERRQ(ierr);
              }
            }
          }
        } while( (id=gnodes[id].m_next_idx) != -1 );
        crsID++;
      }
    }
    ierr = MatAssemblyBegin(Prol,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(Prol,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  }
  VecRestoreArray(a_coords,&lid_crd);
  
  free( mid.trianglelist );
  ierr = PetscFree( in.pointlist );  CHKERRQ(ierr);

  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------- */
/*
   createProlongation

   Input Parameter:
   . Amat - matrix on this fine level
   . P_out - prolongation operator to the next level
   . coords - coordinates
   . dim - dimention
*/
#undef __FUNCT__
#define __FUNCT__ "createProlongation"
PetscErrorCode createProlongation( Mat Amat,Mat *P_out,PetscReal a_coords[],PetscReal **a_coords_out,const PetscInt a_dim)
{
  PetscErrorCode ierr;
  PetscInt       Istart,Iend,Ii,nloc,bs,my0,jj,kk,sid;
  Mat            Prol; 
  PetscMPIInt    mype;
  Mat Gmat;

  PetscFunctionBegin;
  ierr = MPI_Comm_rank(((PetscObject)Amat)->comm,&mype);CHKERRQ(ierr);
  ierr = MatGetOwnershipRange(Amat,&Istart,&Iend);    CHKERRQ(ierr); /* BAIJ */
  ierr = MatGetBlockSize(Amat, &bs);    CHKERRQ(ierr);
  nloc = (Iend - Istart)/bs;
  my0 = Istart/bs;

  /* scale Amat (this should be a scalar matrix even if Amat is blocked) */ 
  {
    Vec diag;
    ierr = MatGetVecs(Amat, &diag, 0);    CHKERRQ(ierr);
    ierr = MatGetDiagonal( Amat, diag );  CHKERRQ(ierr);
    ierr = VecReciprocal( diag );         CHKERRQ(ierr);
    ierr = VecSqrtAbs( diag );            CHKERRQ(ierr);
    ierr = MatDuplicate( Amat, MAT_COPY_VALUES, &Gmat ); CHKERRQ(ierr); /* AIJ */
    ierr = MatDiagonalScale( Gmat, diag, diag );CHKERRQ(ierr);
    ierr = VecDestroy( &diag );           CHKERRQ(ierr);
  }
  ierr = MatGetOwnershipRange(Gmat,&Istart,&Iend);CHKERRQ(ierr); /* use AIJ from here */
  /* filter Gmat */
  {
    Mat Gmat2;
    ierr = MatCreateSeqAIJ(PETSC_COMM_WORLD,nloc*bs,nloc*bs,15,PETSC_NULL,&Gmat2);CHKERRQ(ierr);
    const PetscScalar *vals;  PetscScalar v; const PetscInt *idx; PetscInt ncols;
    for (Ii=Istart; Ii<Iend; Ii++) {
      ierr = MatGetRow(Gmat,Ii,&ncols,&idx,&vals); CHKERRQ(ierr);
      for(jj=0;jj<ncols;jj++){
        if( (v=PetscAbs(vals[jj])) > 0.02 ) { // hard wired filter!!!
          ierr = MatSetValues(Gmat2,1,&Ii,1,&idx[jj],&v,INSERT_VALUES); CHKERRQ(ierr);
        }
        /*else PetscPrintf(PETSC_COMM_SELF,"\t%s filtered %d, v=%e\n",__FUNCT__,Ii,vals[jj]);*/
      }
      ierr = MatRestoreRow(Gmat,Ii,&ncols,&idx,&vals); CHKERRQ(ierr);
    }
    ierr = MatAssemblyBegin(Gmat2,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(Gmat2,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatDestroy( &Gmat );  CHKERRQ(ierr);
    Gmat = Gmat2;
  }
  /* modify matrix for fast coarsening in MIS */
  if(PETSC_FALSE){
    Mat Gmat2; /* this also symmetrizes - needed if Amat is !sym */
    ierr = MatCreateNormal( Gmat, &Gmat2 );    CHKERRQ(ierr);
    ierr = MatDestroy( &Gmat );  CHKERRQ(ierr);
    Gmat = Gmat2;
  }
  
  if(!PETSC_TRUE) {
    PetscViewer        viewer;
    ierr = PetscViewerASCIIOpen(PETSC_COMM_SELF, "Gmat.m", &viewer);  CHKERRQ(ierr);
    ierr = PetscViewerSetFormat( viewer, PETSC_VIEWER_ASCII_MATLAB);  CHKERRQ(ierr);
    ierr = MatView(Gmat,viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy( &viewer );
  }

  {
    /* select coarse grid points - need to get local Mat of Gmat and GROW for parallel (m_gid?) ... */
    
    
    PetscInt nLocalGrid = nloc;
    GNode gnodes[nLocalGrid+1];
    PetscInt ncols;
    PetscInt nSelected;
    Vec crdsVec;

    /* Mat subMat = Gmat; */
    ierr = MatGetOwnershipRange(Gmat,&Istart,&Iend);CHKERRQ(ierr);
    for (Ii=Istart; Ii<Iend; Ii++) { /* locals only? */
      ierr = MatGetRow(Gmat,Ii,&ncols,0,0); CHKERRQ(ierr); 
      PetscInt lid = Ii - Istart;
      gnodes[lid].m_gid = Ii; 
      gnodes[lid].m_lid = lid;
      gnodes[lid].m_degree = ncols;
      gnodes[lid].m_next_idx = -1;
      gnodes[lid].m_state = NOT_DONE; 
      gnodes[lid].m_procID = mype;
      ierr = MatRestoreRow(Gmat,Ii,&ncols,0,0); CHKERRQ(ierr);
    }
    qsort (gnodes, nloc, sizeof(GNode), compare ); /* only sort locals */
    /* get m_gid and m_procID in gnodes from subMat? ... */
    
    
    

    /* select coarse points */
    ierr = selectCrs( Gmat, gnodes, &nSelected ); CHKERRQ(ierr);

    /* create prolongator */
    ierr = MatCreateSeqAIJ(PETSC_COMM_WORLD,nloc*bs,nSelected*bs,15,PETSC_NULL,&Prol);CHKERRQ(ierr);
    /* need coords on GROWN local block - use block Vec */
    ierr = VecCreate(((PetscObject)Amat)->comm,&crdsVec); CHKERRQ(ierr);
    ierr = VecSetBlockSize(crdsVec,a_dim); CHKERRQ(ierr);
    ierr = VecSetSizes(crdsVec,a_dim*nloc,PETSC_DECIDE); CHKERRQ(ierr);
    ierr = VecSetFromOptions( crdsVec ); CHKERRQ(ierr);
    /* set local */
    for(kk=0; kk<nloc; kk++) {
      PetscInt lid = gnodes[kk].m_lid, gid = my0 + lid;
      ierr = VecSetValuesBlocked(crdsVec, 1, &gid, &a_coords[lid*a_dim], INSERT_VALUES ); CHKERRQ(ierr);
    }
    ierr = VecAssemblyBegin(crdsVec); CHKERRQ(ierr);
    ierr = VecAssemblyEnd(crdsVec); CHKERRQ(ierr);
    /* grow crdVec like Gmat, with data .... */

    /* triangulate */
    if( a_dim == 2 ) {
      ierr = triangulateAndFormProl( gnodes, crdsVec, nSelected, Prol ); CHKERRQ(ierr);
    } else {
      SETERRQ(((PetscObject)Amat)->comm,PETSC_ERR_LIB,"3D not implemented");
    }
    ierr = VecDestroy(&crdsVec); CHKERRQ(ierr);

    { /* create next coords - output */
      PetscReal *crs_crds;
      ierr = PetscMalloc( a_dim*(nSelected)*sizeof(PetscReal), &crs_crds ); CHKERRQ(ierr);
      for(kk=0,sid=0;kk<nloc;kk++){ /* grab local select nodes to promote - output */
        if( gnodes[kk].m_state == SELECTED ) {
          PetscInt lid = gnodes[kk].m_lid;
          for(jj=0;jj<a_dim;jj++,sid++) crs_crds[sid] = a_coords[a_dim*lid+jj];
        }
      }
      *a_coords_out = crs_crds; /* out */
    }
  }
  ierr = MatDestroy( &Gmat );  CHKERRQ(ierr);

  *P_out = Prol;  /* out */
  PetscFunctionReturn(0);
}
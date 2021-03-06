
static char help[] = "Tests DMSwarm\n\n";

#include <petscdm.h>
#include <petscdmda.h>
#include <petscdmswarm.h>

/* 
 Checks for variable blocksize
*/
#undef __FUNCT__
#define __FUNCT__ "ex2_1"
PetscErrorCode ex2_1(void)
{
  DM dms;
  PetscErrorCode ierr;
  Vec x;
  PetscMPIInt rank,commsize;
  PetscInt p,bs,nlocal;
  
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&commsize);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);

  ierr = DMCreate(PETSC_COMM_WORLD,&dms);CHKERRQ(ierr);
  ierr = DMSetType(dms,DMSWARM);CHKERRQ(ierr);

  ierr = DMSwarmInitializeFieldRegister(dms);CHKERRQ(ierr);
  
  ierr = DMSwarmRegisterPetscDatatypeField(dms,"viscosity",1,PETSC_REAL);CHKERRQ(ierr);
  ierr = DMSwarmRegisterPetscDatatypeField(dms,"strain",3,PETSC_REAL);CHKERRQ(ierr);
  
  ierr = DMSwarmFinalizeFieldRegister(dms);CHKERRQ(ierr);
  
  ierr = DMSwarmSetLocalSizes(dms,5+rank,4);CHKERRQ(ierr);
  ierr = DMView(dms,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  
  ierr = DMSwarmGetLocalSize(dms,&nlocal);CHKERRQ(ierr);

  {
    PetscReal *array;
    ierr = DMSwarmGetField(dms,"viscosity",&bs,NULL,(void**)&array);CHKERRQ(ierr);
    for (p=0; p<nlocal; p++) {
      array[p] = 11.1 + p*0.1 + rank*100.0;
    }
    ierr = DMSwarmRestoreField(dms,"viscosity",&bs,NULL,(void**)&array);CHKERRQ(ierr);
  }

  {
    PetscReal *array;
    ierr = DMSwarmGetField(dms,"strain",&bs,NULL,(void**)&array);CHKERRQ(ierr);
    for (p=0; p<nlocal; p++) {
      array[bs*p+0] = 2.0e-2 + p*0.001 + rank*1.0;
      array[bs*p+1] = 2.0e-2 + p*0.002 + rank*1.0;
      array[bs*p+2] = 2.0e-2 + p*0.003 + rank*1.0;
    }
    ierr = DMSwarmRestoreField(dms,"strain",&bs,NULL,(void**)&array);CHKERRQ(ierr);
  }

  ierr = DMSwarmCreateGlobalVectorFromField(dms,"viscosity",&x);CHKERRQ(ierr);
  ierr = VecView(x,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  ierr = DMSwarmDestroyGlobalVectorFromField(dms,"viscosity",&x);CHKERRQ(ierr);

  ierr = DMSwarmCreateGlobalVectorFromField(dms,"strain",&x);CHKERRQ(ierr);
  ierr = VecView(x,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  ierr = DMSwarmDestroyGlobalVectorFromField(dms,"strain",&x);CHKERRQ(ierr);

  ierr = DMSwarmVectorDefineField(dms,"strain");CHKERRQ(ierr);
  ierr = DMCreateGlobalVector(dms,&x);CHKERRQ(ierr);
  ierr = VecView(x,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  ierr = VecDestroy(&x);CHKERRQ(ierr);
  
  ierr = DMDestroy(&dms);CHKERRQ(ierr);
  
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc,char **argv)
{
  PetscErrorCode ierr;
  
  ierr = PetscInitialize(&argc,&argv,(char*)0,help);CHKERRQ(ierr);
  ierr = ex2_1();CHKERRQ(ierr);
  ierr = PetscFinalize();
  return 0;
}

/******************************************************************************
*            Model Order Reduction plugin for SOFA                            *
*                         version 1.0                                         *
*                       Copyright © Inria                                     *
*                       All rights reserved                                   *
*                       2018                                                  *
*                                                                             *
* This software is under the GNU General Public License v2 (GPLv2)            *
*            https://www.gnu.org/licenses/licenses.en.html                    *
*                                                                             *
*                                                                             *
*                                                                             *
* Authors: Olivier Goury, Felix Vanneste                                      *
*                                                                             *
* Contact information: https://project.inria.fr/modelorderreduction/contact   *
******************************************************************************/
#ifndef SOFA_COMPONENT_FORCEFIELD_HYPERREDUCEDTETRAHEDRONFEMFORCEFIELD_INL
#define SOFA_COMPONENT_FORCEFIELD_HYPERREDUCEDTETRAHEDRONFEMFORCEFIELD_INL

#include "HyperReducedTetrahedronFEMForceField.h"
#include <sofa/core/visual/VisualParams.h>
#include <SofaBaseTopology/GridTopology.h>
#include <sofa/simulation/Simulation.h>
#include <sofa/helper/decompose.h>
#include <sofa/helper/gl/template.h>
#include <assert.h>
#include <fstream> // for reading the file
#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>
#include <set>
#include <SofaBaseLinearSolver/CompressedRowSparseMatrix.h>
#include <sofa/simulation/AnimateBeginEvent.h>
#include <sofa/helper/AdvancedTimer.h>
#include <sofa/core/topology/BaseMeshTopology.h>
#include "../loader/MatrixLoader.h"

// verify timing
#include <sofa/helper/system/thread/CTime.h>


namespace sofa
{

namespace component
{

namespace forcefield
{

using sofa::component::loader::MatrixLoader;


//////////////////////////////////////////////////////////////////////
////////////////////  basic computation methods  /////////////////////
//////////////////////////////////////////////////////////////////////

template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::computeStrainDisplacement( StrainDisplacement &J, Coord a, Coord b, Coord c, Coord d )
{
    // shape functions matrix
    defaulttype::Mat<2, 3, Real> M;

    M[0][0] = b[1];
    M[0][1] = c[1];
    M[0][2] = d[1];
    M[1][0] = b[2];
    M[1][1] = c[2];
    M[1][2] = d[2];
    J[0][0] = J[1][3] = J[2][5]   = - peudo_determinant_for_coef( M );
    M[0][0] = b[0];
    M[0][1] = c[0];
    M[0][2] = d[0];
    J[0][3] = J[1][1] = J[2][4]   = peudo_determinant_for_coef( M );
    M[1][0] = b[1];
    M[1][1] = c[1];
    M[1][2] = d[1];
    J[0][5] = J[1][4] = J[2][2]   = - peudo_determinant_for_coef( M );

    M[0][0] = c[1];
    M[0][1] = d[1];
    M[0][2] = a[1];
    M[1][0] = c[2];
    M[1][1] = d[2];
    M[1][2] = a[2];
    J[3][0] = J[4][3] = J[5][5]   = peudo_determinant_for_coef( M );
    M[0][0] = c[0];
    M[0][1] = d[0];
    M[0][2] = a[0];
    J[3][3] = J[4][1] = J[5][4]   = - peudo_determinant_for_coef( M );
    M[1][0] = c[1];
    M[1][1] = d[1];
    M[1][2] = a[1];
    J[3][5] = J[4][4] = J[5][2]   = peudo_determinant_for_coef( M );

    M[0][0] = d[1];
    M[0][1] = a[1];
    M[0][2] = b[1];
    M[1][0] = d[2];
    M[1][1] = a[2];
    M[1][2] = b[2];
    J[6][0] = J[7][3] = J[8][5]   = - peudo_determinant_for_coef( M );
    M[0][0] = d[0];
    M[0][1] = a[0];
    M[0][2] = b[0];
    J[6][3] = J[7][1] = J[8][4]   = peudo_determinant_for_coef( M );
    M[1][0] = d[1];
    M[1][1] = a[1];
    M[1][2] = b[1];
    J[6][5] = J[7][4] = J[8][2]   = - peudo_determinant_for_coef( M );

    M[0][0] = a[1];
    M[0][1] = b[1];
    M[0][2] = c[1];
    M[1][0] = a[2];
    M[1][1] = b[2];
    M[1][2] = c[2];
    J[9][0] = J[10][3] = J[11][5]   = peudo_determinant_for_coef( M );
    M[0][0] = a[0];
    M[0][1] = b[0];
    M[0][2] = c[0];
    J[9][3] = J[10][1] = J[11][4]   = - peudo_determinant_for_coef( M );
    M[1][0] = a[1];
    M[1][1] = b[1];
    M[1][2] = c[1];
    J[9][5] = J[10][4] = J[11][2]   = peudo_determinant_for_coef( M );

}

template<class DataTypes>
typename HyperReducedTetrahedronFEMForceField<DataTypes>::Real HyperReducedTetrahedronFEMForceField<DataTypes>::peudo_determinant_for_coef ( const defaulttype::Mat<2, 3, Real>&  M )
{
    return  M[0][1]*M[1][2] - M[1][1]*M[0][2] -  M[0][0]*M[1][2] + M[1][0]*M[0][2] + M[0][0]*M[1][1] - M[1][0]*M[0][1];
}

template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::computeStiffnessMatrix( StiffnessMatrix& S,StiffnessMatrix& SR,const MaterialStiffness &K, const StrainDisplacement &J, const Transformation& Rot )
{
    defaulttype::MatNoInit<6, 12, Real> Jt;
    Jt.transpose( J );

    defaulttype::MatNoInit<12, 12, Real> JKJt;
    JKJt = J*K*Jt;

    defaulttype::MatNoInit<12, 12, Real> RR,RRt;
    RR.clear();
    RRt.clear();
    for(int i=0; i<3; ++i)
        for(int j=0; j<3; ++j)
        {
            RR[i][j]=RR[i+3][j+3]=RR[i+6][j+6]=RR[i+9][j+9]=Rot[i][j];
            RRt[i][j]=RRt[i+3][j+3]=RRt[i+6][j+6]=RRt[i+9][j+9]=Rot[j][i];
        }
    S = RR*JKJt;
    SR = S*RRt;
}

template <class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::getElementStiffnessMatrix(Real* stiffness, unsigned int elementIndex)
{
    if(needUpdateTopology)
    {
        reinit();
        needUpdateTopology = false;
    }
    Transformation Rot;
    StiffnessMatrix JKJt,tmp;
    Rot[0][0]=Rot[1][1]=Rot[2][2]=1;
    Rot[0][1]=Rot[0][2]=0;
    Rot[1][0]=Rot[1][2]=0;
    Rot[2][0]=Rot[2][1]=0;
    computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[elementIndex], strainDisplacements[elementIndex],_initialRotations[elementIndex]);
    for(int i=0; i<12; i++)
    {
        for(int j=0; j<12; j++)
            stiffness[i*12+j]=tmp(i,j);
    }
}

template <class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::getElementStiffnessMatrix(Real* stiffness, Tetrahedron& te)
{
    if (needUpdateTopology)
    {
        reinit();
        needUpdateTopology = false;
    }
    const VecCoord X0=this->mstate->read(core::ConstVecCoordId::restPosition())->getValue();
    Index a = te[0];
    Index b = te[1];
    Index c = te[2];
    Index d = te[3];

    Transformation R_0_1;
    computeRotationLarge( R_0_1, (X0), a, b, c);

    MaterialStiffness	materialMatrix;
    StrainDisplacement	strainMatrix;
    helper::fixed_array<Coord,4> rotatedInitialElements;

    rotatedInitialElements[0] = R_0_1*(X0)[a];
    rotatedInitialElements[1] = R_0_1*(X0)[b];
    rotatedInitialElements[2] = R_0_1*(X0)[c];
    rotatedInitialElements[3] = R_0_1*(X0)[d];

    rotatedInitialElements[1] -= rotatedInitialElements[0];
    rotatedInitialElements[2] -= rotatedInitialElements[0];
    rotatedInitialElements[3] -= rotatedInitialElements[0];
    rotatedInitialElements[0] = Coord(0,0,0);

    computeMaterialStiffness(materialMatrix,a,b,c,d);
    computeStrainDisplacement(strainMatrix, rotatedInitialElements[0], rotatedInitialElements[1], rotatedInitialElements[2], rotatedInitialElements[3]);

    Transformation Rot;
    StiffnessMatrix JKJt,tmp;
    Rot[0][0]=Rot[1][1]=Rot[2][2]=1;
    Rot[0][1]=Rot[0][2]=0;
    Rot[1][0]=Rot[1][2]=0;
    Rot[2][0]=Rot[2][1]=0;

    R_0_1.transpose();
    computeStiffnessMatrix(JKJt, tmp, materialMatrix, strainMatrix, R_0_1);
    for(int i=0; i<12; i++)
    {
        for(int j=0; j<12; j++)
            stiffness[i*12+j]=tmp(i,j);
    }
}


template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::computeMaterialStiffness(int i, Index&a, Index&b, Index&c, Index&d)
{
    const VecReal& localStiffnessFactor = _localStiffnessFactor.getValue();
    Real youngModulusElement;
    if (_youngModulus.getValue().size() == _indexedElements->size()) youngModulusElement = _youngModulus.getValue()[i];
    else if (_youngModulus.getValue().size() > 0) youngModulusElement = _youngModulus.getValue()[0];
    else
    {
        setYoungModulus(500.0f);
        youngModulusElement = _youngModulus.getValue()[0];
    }
    const Real youngModulus = (localStiffnessFactor.empty() ? 1.0f : localStiffnessFactor[i*localStiffnessFactor.size()/_indexedElements->size()])*youngModulusElement;
    const Real poissonRatio = _poissonRatio.getValue();

    materialsStiffnesses[i][0][0] = materialsStiffnesses[i][1][1] = materialsStiffnesses[i][2][2] = 1;
    materialsStiffnesses[i][0][1] = materialsStiffnesses[i][0][2] = materialsStiffnesses[i][1][0]
            = materialsStiffnesses[i][1][2] = materialsStiffnesses[i][2][0] =
                    materialsStiffnesses[i][2][1] = poissonRatio/(1-poissonRatio);
    materialsStiffnesses[i][0][3] = materialsStiffnesses[i][0][4] = materialsStiffnesses[i][0][5] = 0;
    materialsStiffnesses[i][1][3] = materialsStiffnesses[i][1][4] = materialsStiffnesses[i][1][5] = 0;
    materialsStiffnesses[i][2][3] = materialsStiffnesses[i][2][4] = materialsStiffnesses[i][2][5] = 0;
    materialsStiffnesses[i][3][0] = materialsStiffnesses[i][3][1] = materialsStiffnesses[i][3][2] = materialsStiffnesses[i][3][4] = materialsStiffnesses[i][3][5] = 0;
    materialsStiffnesses[i][4][0] = materialsStiffnesses[i][4][1] = materialsStiffnesses[i][4][2] = materialsStiffnesses[i][4][3] = materialsStiffnesses[i][4][5] = 0;
    materialsStiffnesses[i][5][0] = materialsStiffnesses[i][5][1] = materialsStiffnesses[i][5][2] = materialsStiffnesses[i][5][3] = materialsStiffnesses[i][5][4] = 0;
    materialsStiffnesses[i][3][3] = materialsStiffnesses[i][4][4] = materialsStiffnesses[i][5][5] = (1-2*poissonRatio)/(2*(1-poissonRatio));
    materialsStiffnesses[i] *= (youngModulus*(1-poissonRatio))/((1+poissonRatio)*(1-2*poissonRatio));


    if (_computeVonMisesStress.getValue() >0) {
        elemLambda[i] = materialsStiffnesses[i][0][1];
        elemMu[i] = materialsStiffnesses[i][3][3];
    }

    const VecCoord &initialPoints=_initialPoints.getValue();
    Coord A = initialPoints[b] - initialPoints[a];
    Coord B = initialPoints[c] - initialPoints[a];
    Coord C = initialPoints[d] - initialPoints[a];
    Coord AB = cross(A, B);
    Real volumes6 = fabs( dot( AB, C ) );

    m_restVolume += volumes6/6;
    if (volumes6<0)
    {
        msg_error(this) << "Negative volume for tetra "<<i<<" <"<<a<<','<<b<<','<<c<<','<<d<<"> = "<<volumes6/6;
    }
    materialsStiffnesses[i] /= (volumes6*6); // 36*Volume in the formula
}

template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::computeMaterialStiffness(MaterialStiffness& materialMatrix, Index&a, Index&b, Index&c, Index&d)
{
    //const VecReal& localStiffnessFactor = _localStiffnessFactor.getValue();
    const Real youngModulus = _youngModulus.getValue()[0];
    const Real poissonRatio = _poissonRatio.getValue();

    materialMatrix[0][0] = materialMatrix[1][1] = materialMatrix[2][2] = 1;
    materialMatrix[0][1] = materialMatrix[0][2] = materialMatrix[1][0] = materialMatrix[1][2] = materialMatrix[2][0] = materialMatrix[2][1] = poissonRatio/(1-poissonRatio);
    materialMatrix[0][3] = materialMatrix[0][4] = materialMatrix[0][5] = 0;
    materialMatrix[1][3] = materialMatrix[1][4] = materialMatrix[1][5] = 0;
    materialMatrix[2][3] = materialMatrix[2][4] = materialMatrix[2][5] = 0;
    materialMatrix[3][0] = materialMatrix[3][1] = materialMatrix[3][2] = materialMatrix[3][4] = materialMatrix[3][5] = 0;
    materialMatrix[4][0] = materialMatrix[4][1] = materialMatrix[4][2] = materialMatrix[4][3] = materialMatrix[4][5] = 0;
    materialMatrix[5][0] = materialMatrix[5][1] = materialMatrix[5][2] = materialMatrix[5][3] = materialMatrix[5][4] = 0;
    materialMatrix[3][3] = materialMatrix[4][4] = materialMatrix[5][5] = (1-2*poissonRatio)/(2*(1-poissonRatio));
    materialMatrix *= (youngModulus*(1-poissonRatio))/((1+poissonRatio)*(1-2*poissonRatio));

    // divide by 36 times volumes of the element
    const VecCoord X0=this->mstate->read(core::ConstVecCoordId::restPosition())->getValue();

    Coord A = (X0)[b] - (X0)[a];
    Coord B = (X0)[c] - (X0)[a];
    Coord C = (X0)[d] - (X0)[a];
    Coord AB = cross(A, B);
    Real volumes6 = fabs( dot( AB, C ) );
    m_restVolume += volumes6/6;
    if (volumes6<0)
    {
        msg_error(this) << "Negative volume for tetra"<<a<<','<<b<<','<<c<<','<<d<<"> = "<<volumes6/6<<sendl;
    }
    materialMatrix  /= (volumes6*6);
}




template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::computeForce( Displacement &F, const Displacement &Depl, VoigtTensor &plasticStrain, const MaterialStiffness &K, const StrainDisplacement &J )
{

    // Unit of K = unit of youngModulus / unit of volume = Pa / m^3 = kg m^-4 s^-2
    // Unit of J = m^2
    // Unit of JKJt =  kg s^-2
    // Unit of displacement = m
    // Unit of force = kg m s^-2

#if 0
    F = J*(K*(J.multTranspose(Depl)));
#else
    /* We have these zeros
                                  K[0][3]   K[0][4]   K[0][5]
                                  K[1][3]   K[1][4]   K[1][5]
                                  K[2][3]   K[2][4]   K[2][5]
    K[3][0]   K[3][1]   K[3][2]             K[3][4]   K[3][5]
    K[4][0]   K[4][1]   K[4][2]   K[4][3]             K[4][5]
    K[5][0]   K[5][1]   K[5][2]   K[5][3]   K[5][4]


              J[0][1]   J[0][2]             J[0][4]
    J[1][0]             J[1][2]                       J[1][5]
    J[2][0]   J[2][1]             J[2][3]
              J[3][1]   J[3][2]             J[3][4]
    J[4][0]             J[4][2]                       J[4][5]
    J[5][0]   J[5][1]             J[5][3]
              J[6][1]   J[6][2]             J[6][4]
    J[7][0]             J[7][2]                       J[7][5]
    J[8][0]   J[8][1]             J[8][3]
              J[9][1]   J[9][2]             J[9][4]
    J[10][0]            J[10][2]                      J[10][5]
    J[11][0]  J[11][1]            J[11][3]
    */


    VoigtTensor JtD;
    JtD[0] =   J[ 0][0]*Depl[ 0]+/*J[ 1][0]*Depl[ 1]+  J[ 2][0]*Depl[ 2]+*/
            J[ 3][0]*Depl[ 3]+/*J[ 4][0]*Depl[ 4]+  J[ 5][0]*Depl[ 5]+*/
            J[ 6][0]*Depl[ 6]+/*J[ 7][0]*Depl[ 7]+  J[ 8][0]*Depl[ 8]+*/
            J[ 9][0]*Depl[ 9] /*J[10][0]*Depl[10]+  J[11][0]*Depl[11]*/;
    JtD[1] = /*J[ 0][1]*Depl[ 0]+*/J[ 1][1]*Depl[ 1]+/*J[ 2][1]*Depl[ 2]+*/
            /*J[ 3][1]*Depl[ 3]+*/J[ 4][1]*Depl[ 4]+/*J[ 5][1]*Depl[ 5]+*/
            /*J[ 6][1]*Depl[ 6]+*/J[ 7][1]*Depl[ 7]+/*J[ 8][1]*Depl[ 8]+*/
            /*J[ 9][1]*Depl[ 9]+*/J[10][1]*Depl[10] /*J[11][1]*Depl[11]*/;
    JtD[2] = /*J[ 0][2]*Depl[ 0]+  J[ 1][2]*Depl[ 1]+*/J[ 2][2]*Depl[ 2]+
            /*J[ 3][2]*Depl[ 3]+  J[ 4][2]*Depl[ 4]+*/J[ 5][2]*Depl[ 5]+
            /*J[ 6][2]*Depl[ 6]+  J[ 7][2]*Depl[ 7]+*/J[ 8][2]*Depl[ 8]+
            /*J[ 9][2]*Depl[ 9]+  J[10][2]*Depl[10]+*/J[11][2]*Depl[11]  ;
    JtD[3] =   J[ 0][3]*Depl[ 0]+  J[ 1][3]*Depl[ 1]+/*J[ 2][3]*Depl[ 2]+*/
            J[ 3][3]*Depl[ 3]+  J[ 4][3]*Depl[ 4]+/*J[ 5][3]*Depl[ 5]+*/
            J[ 6][3]*Depl[ 6]+  J[ 7][3]*Depl[ 7]+/*J[ 8][3]*Depl[ 8]+*/
            J[ 9][3]*Depl[ 9]+  J[10][3]*Depl[10] /*J[11][3]*Depl[11]*/;
    JtD[4] = /*J[ 0][4]*Depl[ 0]+*/J[ 1][4]*Depl[ 1]+  J[ 2][4]*Depl[ 2]+
            /*J[ 3][4]*Depl[ 3]+*/J[ 4][4]*Depl[ 4]+  J[ 5][4]*Depl[ 5]+
            /*J[ 6][4]*Depl[ 6]+*/J[ 7][4]*Depl[ 7]+  J[ 8][4]*Depl[ 8]+
            /*J[ 9][4]*Depl[ 9]+*/J[10][4]*Depl[10]+  J[11][4]*Depl[11]  ;
    JtD[5] =   J[ 0][5]*Depl[ 0]+/*J[ 1][5]*Depl[ 1]*/ J[ 2][5]*Depl[ 2]+
            J[ 3][5]*Depl[ 3]+/*J[ 4][5]*Depl[ 4]*/ J[ 5][5]*Depl[ 5]+
            J[ 6][5]*Depl[ 6]+/*J[ 7][5]*Depl[ 7]*/ J[ 8][5]*Depl[ 8]+
            J[ 9][5]*Depl[ 9]+/*J[10][5]*Depl[10]*/ J[11][5]*Depl[11];

    // eventually remove a part of the strain to simulate plasticity
    if( _plasticMaxThreshold.getValue() > 0 )
    {
        VoigtTensor elasticStrain = JtD; // JtD is the total strain
        elasticStrain -= plasticStrain; // totalStrain = elasticStrain + plasticStrain

        // if( ||elasticStrain||  > c_yield ) plasticStrain += dt * c_creep * dt * elasticStrain
        if( elasticStrain.norm2() > _plasticYieldThreshold.getValue()*_plasticYieldThreshold.getValue() )
            plasticStrain += _plasticCreep.getValue() * elasticStrain;

        // if( ||plasticStrain|| > c_max ) plasticStrain *= c_max / ||plasticStrain||
        Real plasticStrainNorm2 = plasticStrain.norm2();
        if( plasticStrainNorm2 > _plasticMaxThreshold.getValue()*_plasticMaxThreshold.getValue() )
            plasticStrain *= _plasticMaxThreshold.getValue() / helper::rsqrt( plasticStrainNorm2 );

        // remaining elasticStrain = totatStrain - plasticStrain
        JtD -= plasticStrain;
    }



    VoigtTensor KJtD;
    KJtD[0] =   K[0][0]*JtD[0]+  K[0][1]*JtD[1]+  K[0][2]*JtD[2]
            /*K[0][3]*JtD[3]+  K[0][4]*JtD[4]+  K[0][5]*JtD[5]*/;
    KJtD[1] =   K[1][0]*JtD[0]+  K[1][1]*JtD[1]+  K[1][2]*JtD[2]
            /*K[1][3]*JtD[3]+  K[1][4]*JtD[4]+  K[1][5]*JtD[5]*/;
    KJtD[2] =   K[2][0]*JtD[0]+  K[2][1]*JtD[1]+  K[2][2]*JtD[2]
            /*K[2][3]*JtD[3]+  K[2][4]*JtD[4]+  K[2][5]*JtD[5]*/;
    KJtD[3] = /*K[3][0]*JtD[0]+  K[3][1]*JtD[1]+  K[3][2]*JtD[2]+*/
        K[3][3]*JtD[3] /*K[3][4]*JtD[4]+  K[3][5]*JtD[5]*/;
    KJtD[4] = /*K[4][0]*JtD[0]+  K[4][1]*JtD[1]+  K[4][2]*JtD[2]+*/
        /*K[4][3]*JtD[3]+*/K[4][4]*JtD[4] /*K[4][5]*JtD[5]*/;
    KJtD[5] = /*K[5][0]*JtD[0]+  K[5][1]*JtD[1]+  K[5][2]*JtD[2]+*/
        /*K[5][3]*JtD[3]+  K[5][4]*JtD[4]+*/K[5][5]*JtD[5]  ;

    F[ 0] =   J[ 0][0]*KJtD[0]+/*J[ 0][1]*KJtD[1]+  J[ 0][2]*KJtD[2]+*/
            J[ 0][3]*KJtD[3]+/*J[ 0][4]*KJtD[4]+*/J[ 0][5]*KJtD[5]  ;
    F[ 1] = /*J[ 1][0]*KJtD[0]+*/J[ 1][1]*KJtD[1]+/*J[ 1][2]*KJtD[2]+*/
            J[ 1][3]*KJtD[3]+  J[ 1][4]*KJtD[4] /*J[ 1][5]*KJtD[5]*/;
    F[ 2] = /*J[ 2][0]*KJtD[0]+  J[ 2][1]*KJtD[1]+*/J[ 2][2]*KJtD[2]+
            /*J[ 2][3]*KJtD[3]+*/J[ 2][4]*KJtD[4]+  J[ 2][5]*KJtD[5]  ;
    F[ 3] =   J[ 3][0]*KJtD[0]+/*J[ 3][1]*KJtD[1]+  J[ 3][2]*KJtD[2]+*/
            J[ 3][3]*KJtD[3]+/*J[ 3][4]*KJtD[4]+*/J[ 3][5]*KJtD[5]  ;
    F[ 4] = /*J[ 4][0]*KJtD[0]+*/J[ 4][1]*KJtD[1]+/*J[ 4][2]*KJtD[2]+*/
            J[ 4][3]*KJtD[3]+  J[ 4][4]*KJtD[4] /*J[ 4][5]*KJtD[5]*/;
    F[ 5] = /*J[ 5][0]*KJtD[0]+  J[ 5][1]*KJtD[1]+*/J[ 5][2]*KJtD[2]+
            /*J[ 5][3]*KJtD[3]+*/J[ 5][4]*KJtD[4]+  J[ 5][5]*KJtD[5]  ;
    F[ 6] =   J[ 6][0]*KJtD[0]+/*J[ 6][1]*KJtD[1]+  J[ 6][2]*KJtD[2]+*/
            J[ 6][3]*KJtD[3]+/*J[ 6][4]*KJtD[4]+*/J[ 6][5]*KJtD[5]  ;
    F[ 7] = /*J[ 7][0]*KJtD[0]+*/J[ 7][1]*KJtD[1]+/*J[ 7][2]*KJtD[2]+*/
            J[ 7][3]*KJtD[3]+  J[ 7][4]*KJtD[4] /*J[ 7][5]*KJtD[5]*/;
    F[ 8] = /*J[ 8][0]*KJtD[0]+  J[ 8][1]*KJtD[1]+*/J[ 8][2]*KJtD[2]+
            /*J[ 8][3]*KJtD[3]+*/J[ 8][4]*KJtD[4]+  J[ 8][5]*KJtD[5]  ;
    F[ 9] =   J[ 9][0]*KJtD[0]+/*J[ 9][1]*KJtD[1]+  J[ 9][2]*KJtD[2]+*/
            J[ 9][3]*KJtD[3]+/*J[ 9][4]*KJtD[4]+*/J[ 9][5]*KJtD[5]  ;
    F[10] = /*J[10][0]*KJtD[0]+*/J[10][1]*KJtD[1]+/*J[10][2]*KJtD[2]+*/
            J[10][3]*KJtD[3]+  J[10][4]*KJtD[4] /*J[10][5]*KJtD[5]*/;
    F[11] = /*J[11][0]*KJtD[0]+  J[11][1]*KJtD[1]+*/J[11][2]*KJtD[2]+
            /*J[11][3]*KJtD[3]+*/J[11][4]*KJtD[4]+  J[11][5]*KJtD[5]  ;

#endif
}

template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::computeForce( Displacement &F, const Displacement &Depl, const MaterialStiffness &K, const StrainDisplacement &J, SReal fact )
{

    // Unit of K = unit of youngModulus / unit of volume = Pa / m^3 = kg m^-4 s^-2
    // Unit of J = m^2
    // Unit of JKJt =  kg s^-2
    // Unit of displacement = m
    // Unit of force = kg m s^-2

#if 0
    F = J*(K*(J.multTranspose(Depl)));
    F *= fact;
#else
    /* We have these zeros
                                  K[0][3]   K[0][4]   K[0][5]
                                  K[1][3]   K[1][4]   K[1][5]
                                  K[2][3]   K[2][4]   K[2][5]
    K[3][0]   K[3][1]   K[3][2]             K[3][4]   K[3][5]
    K[4][0]   K[4][1]   K[4][2]   K[4][3]             K[4][5]
    K[5][0]   K[5][1]   K[5][2]   K[5][3]   K[5][4]


              J[0][1]   J[0][2]             J[0][4]
    J[1][0]             J[1][2]                       J[1][5]
    J[2][0]   J[2][1]             J[2][3]
              J[3][1]   J[3][2]             J[3][4]
    J[4][0]             J[4][2]                       J[4][5]
    J[5][0]   J[5][1]             J[5][3]
              J[6][1]   J[6][2]             J[6][4]
    J[7][0]             J[7][2]                       J[7][5]
    J[8][0]   J[8][1]             J[8][3]
              J[9][1]   J[9][2]             J[9][4]
    J[10][0]            J[10][2]                      J[10][5]
    J[11][0]  J[11][1]            J[11][3]
    */

    defaulttype::VecNoInit<6,Real> JtD;
    JtD[0] =   J[ 0][0]*Depl[ 0]+/*J[ 1][0]*Depl[ 1]+  J[ 2][0]*Depl[ 2]+*/
            J[ 3][0]*Depl[ 3]+/*J[ 4][0]*Depl[ 4]+  J[ 5][0]*Depl[ 5]+*/
            J[ 6][0]*Depl[ 6]+/*J[ 7][0]*Depl[ 7]+  J[ 8][0]*Depl[ 8]+*/
            J[ 9][0]*Depl[ 9] /*J[10][0]*Depl[10]+  J[11][0]*Depl[11]*/;
    JtD[1] = /*J[ 0][1]*Depl[ 0]+*/J[ 1][1]*Depl[ 1]+/*J[ 2][1]*Depl[ 2]+*/
            /*J[ 3][1]*Depl[ 3]+*/J[ 4][1]*Depl[ 4]+/*J[ 5][1]*Depl[ 5]+*/
            /*J[ 6][1]*Depl[ 6]+*/J[ 7][1]*Depl[ 7]+/*J[ 8][1]*Depl[ 8]+*/
            /*J[ 9][1]*Depl[ 9]+*/J[10][1]*Depl[10] /*J[11][1]*Depl[11]*/;
    JtD[2] = /*J[ 0][2]*Depl[ 0]+  J[ 1][2]*Depl[ 1]+*/J[ 2][2]*Depl[ 2]+
            /*J[ 3][2]*Depl[ 3]+  J[ 4][2]*Depl[ 4]+*/J[ 5][2]*Depl[ 5]+
            /*J[ 6][2]*Depl[ 6]+  J[ 7][2]*Depl[ 7]+*/J[ 8][2]*Depl[ 8]+
            /*J[ 9][2]*Depl[ 9]+  J[10][2]*Depl[10]+*/J[11][2]*Depl[11]  ;
    JtD[3] =   J[ 0][3]*Depl[ 0]+  J[ 1][3]*Depl[ 1]+/*J[ 2][3]*Depl[ 2]+*/
            J[ 3][3]*Depl[ 3]+  J[ 4][3]*Depl[ 4]+/*J[ 5][3]*Depl[ 5]+*/
            J[ 6][3]*Depl[ 6]+  J[ 7][3]*Depl[ 7]+/*J[ 8][3]*Depl[ 8]+*/
            J[ 9][3]*Depl[ 9]+  J[10][3]*Depl[10] /*J[11][3]*Depl[11]*/;
    JtD[4] = /*J[ 0][4]*Depl[ 0]+*/J[ 1][4]*Depl[ 1]+  J[ 2][4]*Depl[ 2]+
            /*J[ 3][4]*Depl[ 3]+*/J[ 4][4]*Depl[ 4]+  J[ 5][4]*Depl[ 5]+
            /*J[ 6][4]*Depl[ 6]+*/J[ 7][4]*Depl[ 7]+  J[ 8][4]*Depl[ 8]+
            /*J[ 9][4]*Depl[ 9]+*/J[10][4]*Depl[10]+  J[11][4]*Depl[11]  ;
    JtD[5] =   J[ 0][5]*Depl[ 0]+/*J[ 1][5]*Depl[ 1]*/ J[ 2][5]*Depl[ 2]+
            J[ 3][5]*Depl[ 3]+/*J[ 4][5]*Depl[ 4]*/ J[ 5][5]*Depl[ 5]+
            J[ 6][5]*Depl[ 6]+/*J[ 7][5]*Depl[ 7]*/ J[ 8][5]*Depl[ 8]+
            J[ 9][5]*Depl[ 9]+/*J[10][5]*Depl[10]*/ J[11][5]*Depl[11];
//         serr<<"HyperReducedTetrahedronFEMForceField<DataTypes>::computeForce, D = "<<Depl<<sendl;
//         serr<<"HyperReducedTetrahedronFEMForceField<DataTypes>::computeForce, JtD = "<<JtD<<sendl;

    defaulttype::VecNoInit<6,Real> KJtD;
    KJtD[0] =   K[0][0]*JtD[0]+  K[0][1]*JtD[1]+  K[0][2]*JtD[2]
            /*K[0][3]*JtD[3]+  K[0][4]*JtD[4]+  K[0][5]*JtD[5]*/;
    KJtD[1] =   K[1][0]*JtD[0]+  K[1][1]*JtD[1]+  K[1][2]*JtD[2]
            /*K[1][3]*JtD[3]+  K[1][4]*JtD[4]+  K[1][5]*JtD[5]*/;
    KJtD[2] =   K[2][0]*JtD[0]+  K[2][1]*JtD[1]+  K[2][2]*JtD[2]
            /*K[2][3]*JtD[3]+  K[2][4]*JtD[4]+  K[2][5]*JtD[5]*/;
    KJtD[3] = /*K[3][0]*JtD[0]+  K[3][1]*JtD[1]+  K[3][2]*JtD[2]+*/
        K[3][3]*JtD[3] /*K[3][4]*JtD[4]+  K[3][5]*JtD[5]*/;
    KJtD[4] = /*K[4][0]*JtD[0]+  K[4][1]*JtD[1]+  K[4][2]*JtD[2]+*/
        /*K[4][3]*JtD[3]+*/K[4][4]*JtD[4] /*K[4][5]*JtD[5]*/;
    KJtD[5] = /*K[5][0]*JtD[0]+  K[5][1]*JtD[1]+  K[5][2]*JtD[2]+*/
        /*K[5][3]*JtD[3]+  K[5][4]*JtD[4]+*/K[5][5]*JtD[5]  ;

    KJtD *= fact;

    F[ 0] =   J[ 0][0]*KJtD[0]+/*J[ 0][1]*KJtD[1]+  J[ 0][2]*KJtD[2]+*/
            J[ 0][3]*KJtD[3]+/*J[ 0][4]*KJtD[4]+*/J[ 0][5]*KJtD[5]  ;
    F[ 1] = /*J[ 1][0]*KJtD[0]+*/J[ 1][1]*KJtD[1]+/*J[ 1][2]*KJtD[2]+*/
            J[ 1][3]*KJtD[3]+  J[ 1][4]*KJtD[4] /*J[ 1][5]*KJtD[5]*/;
    F[ 2] = /*J[ 2][0]*KJtD[0]+  J[ 2][1]*KJtD[1]+*/J[ 2][2]*KJtD[2]+
            /*J[ 2][3]*KJtD[3]+*/J[ 2][4]*KJtD[4]+  J[ 2][5]*KJtD[5]  ;
    F[ 3] =   J[ 3][0]*KJtD[0]+/*J[ 3][1]*KJtD[1]+  J[ 3][2]*KJtD[2]+*/
            J[ 3][3]*KJtD[3]+/*J[ 3][4]*KJtD[4]+*/J[ 3][5]*KJtD[5]  ;
    F[ 4] = /*J[ 4][0]*KJtD[0]+*/J[ 4][1]*KJtD[1]+/*J[ 4][2]*KJtD[2]+*/
            J[ 4][3]*KJtD[3]+  J[ 4][4]*KJtD[4] /*J[ 4][5]*KJtD[5]*/;
    F[ 5] = /*J[ 5][0]*KJtD[0]+  J[ 5][1]*KJtD[1]+*/J[ 5][2]*KJtD[2]+
            /*J[ 5][3]*KJtD[3]+*/J[ 5][4]*KJtD[4]+  J[ 5][5]*KJtD[5]  ;
    F[ 6] =   J[ 6][0]*KJtD[0]+/*J[ 6][1]*KJtD[1]+  J[ 6][2]*KJtD[2]+*/
            J[ 6][3]*KJtD[3]+/*J[ 6][4]*KJtD[4]+*/J[ 6][5]*KJtD[5]  ;
    F[ 7] = /*J[ 7][0]*KJtD[0]+*/J[ 7][1]*KJtD[1]+/*J[ 7][2]*KJtD[2]+*/
            J[ 7][3]*KJtD[3]+  J[ 7][4]*KJtD[4] /*J[ 7][5]*KJtD[5]*/;
    F[ 8] = /*J[ 8][0]*KJtD[0]+  J[ 8][1]*KJtD[1]+*/J[ 8][2]*KJtD[2]+
            /*J[ 8][3]*KJtD[3]+*/J[ 8][4]*KJtD[4]+  J[ 8][5]*KJtD[5]  ;
    F[ 9] =   J[ 9][0]*KJtD[0]+/*J[ 9][1]*KJtD[1]+  J[ 9][2]*KJtD[2]+*/
            J[ 9][3]*KJtD[3]+/*J[ 9][4]*KJtD[4]+*/J[ 9][5]*KJtD[5]  ;
    F[10] = /*J[10][0]*KJtD[0]+*/J[10][1]*KJtD[1]+/*J[10][2]*KJtD[2]+*/
            J[10][3]*KJtD[3]+  J[10][4]*KJtD[4] /*J[10][5]*KJtD[5]*/;
    F[11] = /*J[11][0]*KJtD[0]+  J[11][1]*KJtD[1]+*/J[11][2]*KJtD[2]+
            /*J[11][3]*KJtD[3]+*/J[11][4]*KJtD[4]+  J[11][5]*KJtD[5]  ;
#endif
}

//////////////////////////////////////////////////////////////////////
////////////////////  small displacements method  ////////////////////
//////////////////////////////////////////////////////////////////////

template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::initSmall(int i, Index&a, Index&b, Index&c, Index&d)
{
    const VecCoord &initialPoints=_initialPoints.getValue();
    computeStrainDisplacement( strainDisplacements[i], initialPoints[a], initialPoints[b], initialPoints[c], initialPoints[d] );
}

template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::accumulateForceSmall( Vector& f, const Vector & p, typename VecElement::const_iterator elementIt, Index elementIndex )
{

    const VecCoord &initialPoints=_initialPoints.getValue();
    //serr<<"HyperReducedTetrahedronFEMForceField<DataTypes>::accumulateForceSmall"<<sendl;
    Element index = *elementIt;
    Index a = index[0];
    Index b = index[1];
    Index c = index[2];
    Index d = index[3];

    // displacements
    Displacement D;
    D[0] = 0;
    D[1] = 0;
    D[2] = 0;
    D[3] =  initialPoints[b][0] - initialPoints[a][0] - p[b][0]+p[a][0];
    D[4] =  initialPoints[b][1] - initialPoints[a][1] - p[b][1]+p[a][1];
    D[5] =  initialPoints[b][2] - initialPoints[a][2] - p[b][2]+p[a][2];
    D[6] =  initialPoints[c][0] - initialPoints[a][0] - p[c][0]+p[a][0];
    D[7] =  initialPoints[c][1] - initialPoints[a][1] - p[c][1]+p[a][1];
    D[8] =  initialPoints[c][2] - initialPoints[a][2] - p[c][2]+p[a][2];
    D[9] =  initialPoints[d][0] - initialPoints[a][0] - p[d][0]+p[a][0];
    D[10] = initialPoints[d][1] - initialPoints[a][1] - p[d][1]+p[a][1];
    D[11] = initialPoints[d][2] - initialPoints[a][2] - p[d][2]+p[a][2];

    // compute force on element
    Displacement F;

    if(!_assembling.getValue())
    {
        computeForce( F, D, _plasticStrains[elementIndex], materialsStiffnesses[elementIndex], strainDisplacements[elementIndex] );
    }
    else if( _plasticMaxThreshold.getValue() <= 0 )
    {
        Transformation Rot;
        Rot[0][0]=Rot[1][1]=Rot[2][2]=1;
        Rot[0][1]=Rot[0][2]=0;
        Rot[1][0]=Rot[1][2]=0;
        Rot[2][0]=Rot[2][1]=0;


        StiffnessMatrix JKJt,tmp;
        computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[elementIndex], strainDisplacements[elementIndex],Rot);

        //erase the stiffness matrix at each time step
        if(elementIndex==0)
        {
            for(unsigned int i=0; i<_stiffnesses.size(); ++i)
            {
                _stiffnesses[i].resize(0);
            }
        }

        for(int i=0; i<12; ++i)
        {
            int row = index[i/3]*3+i%3;

            for(int j=0; j<12; ++j)
            {
                if(JKJt[i][j]!=0)
                {

                    int col = index[j/3]*3+j%3;
                    // search if the vertex is already take into account by another element
                    typename CompressedValue::iterator result = _stiffnesses[row].end();
                    for(typename CompressedValue::iterator it=_stiffnesses[row].begin(); it!=_stiffnesses[row].end()&&result==_stiffnesses[row].end(); ++it)
                    {
                        if( (*it).first == col )
                            result = it;
                    }

                    if( result==_stiffnesses[row].end() )
                        _stiffnesses[row].push_back( Col_Value(col,JKJt[i][j] )  );
                    else
                        (*result).second += JKJt[i][j];
                }
            }
        }
        F = JKJt * D;
    }
    else
    {
        msg_warning(this) << "TODO(HyperReducedTetrahedronFEMForceField): support for assembling system matrix when using plasticity.";
        return;
    }


    f[a] += Deriv( F[0], F[1], F[2] );
    f[b] += Deriv( F[3], F[4], F[5] );
    f[c] += Deriv( F[6], F[7], F[8] );
    f[d] += Deriv( F[9], F[10], F[11] );

}

// getPotentialEnergy only for small method and if assembling is false
template<class DataTypes>
inline SReal HyperReducedTetrahedronFEMForceField<DataTypes>::getPotentialEnergy(const core::MechanicalParams*, const DataVecCoord&   x) const
{
    unsigned int i;
    typename VecElement::const_iterator it;
    SReal energyPotential = 0;
    const VecCoord &initialPoints=_initialPoints.getValue();
    const VecCoord &p            = x.getValue();

    switch(method)
    {
    case SMALL :
    {

        for(it=_indexedElements->begin(), i = 0 ; it!=_indexedElements->end(); ++it,++i)
        {
            Element index = *it;
            Index a = index[0];
            Index b = index[1];
            Index c = index[2];
            Index d = index[3];

            // displacements
            Displacement D;
            D[0] = 0;
            D[1] = 0;
            D[2] = 0;
            D[3] =  initialPoints[b][0] - initialPoints[a][0] - p[b][0]+p[a][0];
            D[4] =  initialPoints[b][1] - initialPoints[a][1] - p[b][1]+p[a][1];
            D[5] =  initialPoints[b][2] - initialPoints[a][2] - p[b][2]+p[a][2];
            D[6] =  initialPoints[c][0] - initialPoints[a][0] - p[c][0]+p[a][0];
            D[7] =  initialPoints[c][1] - initialPoints[a][1] - p[c][1]+p[a][1];
            D[8] =  initialPoints[c][2] - initialPoints[a][2] - p[c][2]+p[a][2];
            D[9] =  initialPoints[d][0] - initialPoints[a][0] - p[d][0]+p[a][0];
            D[10] = initialPoints[d][1] - initialPoints[a][1] - p[d][1]+p[a][1];
            D[11] = initialPoints[d][2] - initialPoints[a][2] - p[d][2]+p[a][2];

            if(!_assembling.getValue())
            {

                // compute force on element
                Displacement F;

                // ComputeForce without the case of  plasticity simulation when  _plasticMaxThreshold.getValue() > 0
                // This case actually modifies  the member plasticStrain and getPotentialEnergy is a const fonction.
                MaterialStiffness K = materialsStiffnesses[i];
                StrainDisplacement J = strainDisplacements[i];

                #if 0
                    F = J*(K*(J.multTranspose(D)));
                #else

                    VoigtTensor JtD;
                    JtD[0] = J[ 0][0]*D[ 0]+ J[ 3][0]*D[ 3]+ J[ 6][0]*D[ 6]+ J[ 9][0]*D[ 9];
                    JtD[1] = J[ 1][1]*D[ 1]+ J[ 4][1]*D[ 4]+ J[ 7][1]*D[ 7]+ J[10][1]*D[10];
                    JtD[2] = J[ 2][2]*D[ 2]+ J[ 5][2]*D[ 5]+ J[ 8][2]*D[ 8]+ J[11][2]*D[11];
                    JtD[3] = J[ 0][3]*D[ 0]+ J[ 1][3]*D[ 1]+ J[ 3][3]*D[ 3]+ J[ 4][3]*D[ 4]+
                             J[ 6][3]*D[ 6]+ J[ 7][3]*D[ 7]+ J[ 9][3]*D[ 9]+ J[10][3]*D[10];
                    JtD[4] = J[ 1][4]*D[ 1]+ J[ 2][4]*D[ 2]+ J[ 4][4]*D[ 4]+ J[ 5][4]*D[ 5]+
                             J[ 7][4]*D[ 7]+ J[ 8][4]*D[ 8]+ J[10][4]*D[10]+ J[11][4]*D[11];
                    JtD[5] = J[ 0][5]*D[ 0]+ J[ 2][5]*D[ 2]+ J[ 3][5]*D[ 3]+ J[ 5][5]*D[ 5]+
                             J[ 6][5]*D[ 6]+ J[ 8][5]*D[ 8]+ J[ 9][5]*D[ 9]+ J[11][5]*D[11];


                    VoigtTensor KJtD;
                    KJtD[0] = K[0][0]*JtD[0]+  K[0][1]*JtD[1]+  K[0][2]*JtD[2];
                    KJtD[1] = K[1][0]*JtD[0]+  K[1][1]*JtD[1]+  K[1][2]*JtD[2];
                    KJtD[2] = K[2][0]*JtD[0]+  K[2][1]*JtD[1]+  K[2][2]*JtD[2];
                    KJtD[3] = K[3][3]*JtD[3] ;
                    KJtD[4] = K[4][4]*JtD[4];
                    KJtD[5] = K[5][5]*JtD[5]  ;

                    F[ 0] = J[ 0][0]*KJtD[0]+ J[ 0][3]*KJtD[3]+ J[ 0][5]*KJtD[5];
                    F[ 1] = J[ 1][1]*KJtD[1]+ J[ 1][3]*KJtD[3]+ J[ 1][4]*KJtD[4];
                    F[ 2] = J[ 2][2]*KJtD[2]+ J[ 2][4]*KJtD[4]+ J[ 2][5]*KJtD[5];
                    F[ 3] = J[ 3][0]*KJtD[0]+ J[ 3][3]*KJtD[3]+ J[ 3][5]*KJtD[5];
                    F[ 4] = J[ 4][1]*KJtD[1]+ J[ 4][3]*KJtD[3]+ J[ 4][
                            4]*KJtD[4];
                    F[ 5] = J[ 5][2]*KJtD[2]+ J[ 5][4]*KJtD[4]+ J[ 5][5]*KJtD[5];
                    F[ 6] = J[ 6][0]*KJtD[0]+ J[ 6][3]*KJtD[3]+ J[ 6][5]*KJtD[5];
                    F[ 7] = J[ 7][1]*KJtD[1]+ J[ 7][3]*KJtD[3]+ J[ 7][4]*KJtD[4];
                    F[ 8] = J[ 8][2]*KJtD[2]+ J[ 8][4]*KJtD[4]+ J[ 8][5]*KJtD[5];
                    F[ 9] = J[ 9][0]*KJtD[0]+ J[ 9][3]*KJtD[3]+ J[ 9][5]*KJtD[5];
                    F[10] = J[10][1]*KJtD[1]+ J[10][3]*KJtD[3]+ J[10][4]*KJtD[4];
                    F[11] = J[11][2]*KJtD[2]+ J[11][4]*KJtD[4]+ J[11][5]*KJtD[5];

                #endif

                // Compute potentialEnergy
                energyPotential += dot(Deriv( F[0], F[1], F[2] ) ,-Deriv( D[0], D[1], D[2]));
                energyPotential += dot(Deriv( F[3], F[4], F[5] ) ,-Deriv( D[3], D[4], D[5] ));
                energyPotential += dot(Deriv( F[6], F[7], F[8] ) ,-Deriv( D[6], D[7], D[8] ));
                energyPotential += dot(Deriv( F[9], F[10], F[11]),-Deriv( D[9], D[10], D[11] ));

            }

        }
        energyPotential/=-2.0;

        break;
    }
    }

    return energyPotential;

}

template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::applyStiffnessSmall( Vector& f, const Vector& x, int i, Index a, Index b, Index c, Index d, SReal fact )
{
    Displacement X;

    X[0] = x[a][0];
    X[1] = x[a][1];
    X[2] = x[a][2];

    X[3] = x[b][0];
    X[4] = x[b][1];
    X[5] = x[b][2];

    X[6] = x[c][0];
    X[7] = x[c][1];
    X[8] = x[c][2];

    X[9] = x[d][0];
    X[10] = x[d][1];
    X[11] = x[d][2];

    Displacement F;
    computeForce( F, X, materialsStiffnesses[i], strainDisplacements[i], fact );

    f[a] += Deriv( -F[0], -F[1],  -F[2] );
    f[b] += Deriv( -F[3], -F[4],  -F[5] );
    f[c] += Deriv( -F[6], -F[7],  -F[8] );
    f[d] += Deriv( -F[9], -F[10], -F[11] );
}

//////////////////////////////////////////////////////////////////////
////////////////////  large displacements method  ////////////////////
//////////////////////////////////////////////////////////////////////

template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::computeRotationLarge( Transformation &r, const Vector &p, const Index &a, const Index &b, const Index &c)
{
    // first vector on first edge
    // second vector in the plane of the two first edges
    // third vector orthogonal to first and second

    Coord edgex = p[b]-p[a];
    edgex.normalize();

    Coord edgey = p[c]-p[a];
    edgey.normalize();

    Coord edgez = cross( edgex, edgey );
    edgez.normalize();

    edgey = cross( edgez, edgex );
    edgey.normalize();

    r[0][0] = edgex[0];
    r[0][1] = edgex[1];
    r[0][2] = edgex[2];
    r[1][0] = edgey[0];
    r[1][1] = edgey[1];
    r[1][2] = edgey[2];
    r[2][0] = edgez[0];
    r[2][1] = edgez[1];
    r[2][2] = edgez[2];

    // TODO handle degenerated cases like in the SVD method
}

//HACK get rotation for fast contact handling with simplified compliance
template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::getRotation(Transformation& R, unsigned int nodeIdx)
{
    if(method == SMALL)
    {
        R[0][0] = 1.0 ; R[1][1] = 1.0 ; R[2][2] = 1.0 ;
        R[0][1] = 0.0 ; R[0][2] = 0.0 ;
        R[1][0] = 0.0 ; R[1][2] = 0.0 ;
        R[2][0] = 0.0 ; R[2][1] = 0.0 ;
        msg_warning(this) << "WARNING  getRotation called but no rotation computed because case== SMALL";
        return;
    }

    core::topology::BaseMeshTopology::TetrahedraAroundVertex liste_tetra = _mesh->getTetrahedraAroundVertex(nodeIdx);

    R[0][0] = 0.0 ; R[0][1] = 0.0 ; R[0][2] = 0.0 ;
    R[1][0] = 0.0 ; R[1][1] = 0.0 ;  R[1][2] = 0.0 ;
    R[2][0] = 0.0 ; R[2][1] = 0.0 ; R[2][2] = 0.0 ;

    unsigned int numTetra=liste_tetra.size();
    if (numTetra==0)
    {
        if (!_rotationIdx.empty())
        {
            Transformation R0t;
            R0t.transpose(_initialRotations[_rotationIdx[nodeIdx]]);
            R = rotations[_rotationIdx[nodeIdx]] * R0t;
        }
        else
        {
            R[0][0] = R[1][1] = R[2][2] = 1.0 ;
            R[0][1] = R[0][2] = R[1][0] = R[1][2] = R[2][0] = R[2][1] = 0.0 ;
        }
        return;
    }

    for (unsigned int ti=0; ti<numTetra; ti++)
    {
        Transformation R0t;
        R0t.transpose(_initialRotations[liste_tetra[ti]]);
        R += rotations[liste_tetra[ti]] * R0t;
    }

// on average
    R[0][0] = R[0][0]/numTetra ; R[0][1] = R[0][1]/numTetra ; R[0][2] = R[0][2]/numTetra ;
    R[1][0] = R[1][0]/numTetra ; R[1][1] = R[1][1]/numTetra ; R[1][2] = R[1][2]/numTetra ;
    R[2][0] = R[2][0]/numTetra ; R[2][1] = R[2][1]/numTetra ; R[2][2] = R[2][2]/numTetra ;

    defaulttype::Mat<3,3,Real> Rmoy;


    helper::Decompose<Real>::polarDecomposition( R, Rmoy );

    R = Rmoy;


}

template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::initLarge(int i, Index&a, Index&b, Index&c, Index&d)
{
    // Rotation matrix (initial Tetrahedre/world)
    // first vector on first edge
    // second vector in the plane of the two first edges
    // third vector orthogonal to first and second

    const VecCoord &initialPoints=_initialPoints.getValue();
    Transformation R_0_1;
    computeRotationLarge( R_0_1, initialPoints, a, b, c);
    _initialRotations[i].transpose(R_0_1);
    rotations[i] = _initialRotations[i];

    //save the element index as the node index
    _rotationIdx[a] = i;
    _rotationIdx[b] = i;
    _rotationIdx[c] = i;
    _rotationIdx[d] = i;

    _rotatedInitialElements[i][0] = R_0_1*initialPoints[a];
    _rotatedInitialElements[i][1] = R_0_1*initialPoints[b];
    _rotatedInitialElements[i][2] = R_0_1*initialPoints[c];
    _rotatedInitialElements[i][3] = R_0_1*initialPoints[d];

    _rotatedInitialElements[i][1] -= _rotatedInitialElements[i][0];
    _rotatedInitialElements[i][2] -= _rotatedInitialElements[i][0];
    _rotatedInitialElements[i][3] -= _rotatedInitialElements[i][0];
    _rotatedInitialElements[i][0] = Coord(0,0,0);

    computeStrainDisplacement( strainDisplacements[i],_rotatedInitialElements[i][0], _rotatedInitialElements[i][1],_rotatedInitialElements[i][2],_rotatedInitialElements[i][3] );
}

template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::accumulateForceLarge( Vector& f, const Vector & p, typename VecElement::const_iterator elementIt, Index elementIndex )
{
    Element index = *elementIt;

    std::vector<double> GieUnit;
    if (d_prepareECSW.getValue())
    {
        GieUnit.resize(d_nbModes.getValue());
    }
    // Rotation matrix (deformed and displaced Tetrahedron/world)
    Transformation R_0_2;
    computeRotationLarge( R_0_2, p, index[0],index[1],index[2]);

    rotations[elementIndex].transpose(R_0_2);

    // positions of the deformed and displaced Tetrahedron in its frame
    helper::fixed_array<Coord,4> deforme;
    for(int i=0; i<4; ++i)
        deforme[i] = R_0_2*p[index[i]];

    deforme[1][0] -= deforme[0][0];
    deforme[2][0] -= deforme[0][0];
    deforme[2][1] -= deforme[0][1];
    deforme[3] -= deforme[0];

    // displacement
    Displacement D;
    D[0] = 0;
    D[1] = 0;
    D[2] = 0;
    D[3] = _rotatedInitialElements[elementIndex][1][0] - deforme[1][0];
    D[4] = 0;
    D[5] = 0;
    D[6] = _rotatedInitialElements[elementIndex][2][0] - deforme[2][0];
    D[7] = _rotatedInitialElements[elementIndex][2][1] - deforme[2][1];
    D[8] = 0;
    D[9] = _rotatedInitialElements[elementIndex][3][0] - deforme[3][0];
    D[10] = _rotatedInitialElements[elementIndex][3][1] - deforme[3][1];
    D[11] =_rotatedInitialElements[elementIndex][3][2] - deforme[3][2];

    Displacement F;
    if(_updateStiffnessMatrix.getValue())
    {
        strainDisplacements[elementIndex][0][0]   = ( - deforme[2][1]*deforme[3][2] );
        strainDisplacements[elementIndex][1][1] = ( deforme[2][0]*deforme[3][2] - deforme[1][0]*deforme[3][2] );
        strainDisplacements[elementIndex][2][2]   = ( deforme[2][1]*deforme[3][0] - deforme[2][0]*deforme[3][1] + deforme[1][0]*deforme[3][1] - deforme[1][0]*deforme[2][1] );

        strainDisplacements[elementIndex][3][0]   = ( deforme[2][1]*deforme[3][2] );
        strainDisplacements[elementIndex][4][1]  = ( - deforme[2][0]*deforme[3][2] );
        strainDisplacements[elementIndex][5][2]   = ( - deforme[2][1]*deforme[3][0] + deforme[2][0]*deforme[3][1] );

        strainDisplacements[elementIndex][7][1]  = ( deforme[1][0]*deforme[3][2] );
        strainDisplacements[elementIndex][8][2]   = ( - deforme[1][0]*deforme[3][1] );

        strainDisplacements[elementIndex][11][2] = ( deforme[1][0]*deforme[2][1] );
    }

    if(!_assembling.getValue())
    {
        // compute force on element
        computeForce( F, D, _plasticStrains[elementIndex], materialsStiffnesses[elementIndex], strainDisplacements[elementIndex] );


        if (d_performECSW.getValue()){
            for(int i=0; i<12; i+=3){
                f[index[i/3]] +=  weights(elementIndex)*rotations[elementIndex] * Deriv( F[i], F[i+1],  F[i+2] );
            }
        }
        else
        {
            for(int i=0; i<12; i+=3)
                f[index[i/3]] += rotations[elementIndex] * Deriv( F[i], F[i+1],  F[i+2] );
        }
        if (d_prepareECSW.getValue())
        {
            int numTest = this->getContext()->getTime()/this->getContext()->getDt();
            if (numTest%d_periodSaveGIE.getValue() == 0)       // Take a measure every periodSaveGIE timesteps
            {

                numTest = numTest/d_periodSaveGIE.getValue();

                for (unsigned int modNum = 0 ; modNum < d_nbModes.getValue() ; modNum++)
                {

                    GieUnit[modNum] = 0;
                    for(int i=0; i<12; i+=3){
                        GieUnit[modNum] += (rotations[elementIndex] * Deriv( F[i], F[i+1],  F[i+2] ))*Deriv(m_modes(3*index[i/3],modNum),m_modes(3*index[i/3]+1,modNum),m_modes(3*index[i/3]+2,modNum));
                    }

                }
                for (unsigned int i = 0 ; i < d_nbModes.getValue() ; i++)
                {
                    if ( d_nbModes.getValue()*numTest < d_nbModes.getValue()*d_nbTrainingSet.getValue() )
                    {
                        Gie[d_nbModes.getValue()*numTest+i][elementIndex] = GieUnit[i];
                    }
                }
            }
        }

    }
    else if( _plasticMaxThreshold.getValue() <= 0 )
    {
        strainDisplacements[elementIndex][6][0] = 0;
        strainDisplacements[elementIndex][9][0] = 0;
        strainDisplacements[elementIndex][10][1] = 0;

        StiffnessMatrix RJKJt, RJKJtRt;
        computeStiffnessMatrix(RJKJt,RJKJtRt,materialsStiffnesses[elementIndex], strainDisplacements[elementIndex],rotations[elementIndex]);


        //erase the stiffness matrix at each time step
        if(elementIndex==0)
        {
            for(unsigned int i=0; i<_stiffnesses.size(); ++i)
            {
                _stiffnesses[i].resize(0);
            }
        }

        for(int i=0; i<12; ++i)
        {
            int row = index[i/3]*3+i%3;

            for(int j=0; j<12; ++j)
            {
                int col = index[j/3]*3+j%3;

                // search if the vertex is already take into account by another element
                typename CompressedValue::iterator result = _stiffnesses[row].end();
                for(typename CompressedValue::iterator it=_stiffnesses[row].begin(); it!=_stiffnesses[row].end()&&result==_stiffnesses[row].end(); ++it)
                {
                    if( (*it).first == col )
                    {
                        result = it;
                    }
                }

                if( result==_stiffnesses[row].end() )
                {
                    _stiffnesses[row].push_back( Col_Value(col,RJKJtRt[i][j] )  );
                }
                else
                {
                    (*result).second += RJKJtRt[i][j];
                }
            }
        }

        F = RJKJt*D;

        for(int i=0; i<12; i+=3)
            f[index[i/3]] += Deriv( F[i], F[i+1],  F[i+2] );
    }
    else
    {
        msg_warning(this) << "TODO(HyperReducedTetrahedronFEMForceField): support for assembling system matrix when using plasticity.";
    }
}

//////////////////////////////////////////////////////////////////////
////////////////////  polar decomposition method  ////////////////////
//////////////////////////////////////////////////////////////////////


template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::initPolar(int i, Index& a, Index&b, Index&c, Index&d)
{
    const VecCoord &initialPoints=_initialPoints.getValue();
    Transformation A;
    A[0] = initialPoints[b]-initialPoints[a];
    A[1] = initialPoints[c]-initialPoints[a];
    A[2] = initialPoints[d]-initialPoints[a];

    Transformation R_0_1;
    helper::Decompose<Real>::polarDecomposition( A, R_0_1 );

    _initialRotations[i].transpose( R_0_1 );
    rotations[i] = _initialRotations[i];



    //save the element index as the node index
    _rotationIdx[a] = i;
    _rotationIdx[b] = i;
    _rotationIdx[c] = i;
    _rotationIdx[d] = i;

    _rotatedInitialElements[i][0] = R_0_1*initialPoints[a];
    _rotatedInitialElements[i][1] = R_0_1*initialPoints[b];
    _rotatedInitialElements[i][2] = R_0_1*initialPoints[c];
    _rotatedInitialElements[i][3] = R_0_1*initialPoints[d];

    computeStrainDisplacement( strainDisplacements[i],_rotatedInitialElements[i][0], _rotatedInitialElements[i][1],_rotatedInitialElements[i][2],_rotatedInitialElements[i][3] );

}

template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::accumulateForcePolar( Vector& f, const Vector & p, typename VecElement::const_iterator elementIt, Index elementIndex )
{
    Element index = *elementIt;

    Transformation A;
    A[0] = p[index[1]]-p[index[0]];
    A[1] = p[index[2]]-p[index[0]];
    A[2] = p[index[3]]-p[index[0]];

    Transformation R_0_2;
    helper::Decompose<Real>::polarDecomposition( A, R_0_2 );

    rotations[elementIndex].transpose( R_0_2 );

    // positions of the deformed and displaced Tetrahedre in its frame
    helper::fixed_array<Coord, 4>  deforme;
    for(int i=0; i<4; ++i)
        deforme[i] = R_0_2 * p[index[i]];

    // displacement
    Displacement D;
    D[0] = _rotatedInitialElements[elementIndex][0][0] - deforme[0][0];
    D[1] = _rotatedInitialElements[elementIndex][0][1] - deforme[0][1];
    D[2] = _rotatedInitialElements[elementIndex][0][2] - deforme[0][2];
    D[3] = _rotatedInitialElements[elementIndex][1][0] - deforme[1][0];
    D[4] = _rotatedInitialElements[elementIndex][1][1] - deforme[1][1];
    D[5] = _rotatedInitialElements[elementIndex][1][2] - deforme[1][2];
    D[6] = _rotatedInitialElements[elementIndex][2][0] - deforme[2][0];
    D[7] = _rotatedInitialElements[elementIndex][2][1] - deforme[2][1];
    D[8] = _rotatedInitialElements[elementIndex][2][2] - deforme[2][2];
    D[9] = _rotatedInitialElements[elementIndex][3][0] - deforme[3][0];
    D[10] = _rotatedInitialElements[elementIndex][3][1] - deforme[3][1];
    D[11] = _rotatedInitialElements[elementIndex][3][2] - deforme[3][2];



    Displacement F;
    if(_updateStiffnessMatrix.getValue())
    {
        // shape functions matrix
        computeStrainDisplacement( strainDisplacements[elementIndex], deforme[0],deforme[1],deforme[2],deforme[3] );
    }

    if(!_assembling.getValue())
    {
        computeForce( F, D, _plasticStrains[elementIndex], materialsStiffnesses[elementIndex], strainDisplacements[elementIndex] );
        for(int i=0; i<12; i+=3)
            f[index[i/3]] += rotations[elementIndex] * Deriv( F[i], F[i+1],  F[i+2] );
    }
    else
    {
        msg_warning(this) << "TODO(HyperReducedTetrahedronFEMForceField): support for assembling system matrix when using polar method.";
    }
}



//////////////////////////////////////////////////////////////////////
////////////////////  svd decomposition method  ////////////////////
//////////////////////////////////////////////////////////////////////

template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::initSVD( int i, Index& a, Index&b, Index&c, Index&d )
{
    const VecCoord &initialPoints=_initialPoints.getValue();
    Transformation A;
    A[0] = initialPoints[b]-initialPoints[a];
    A[1] = initialPoints[c]-initialPoints[a];
    A[2] = initialPoints[d]-initialPoints[a];
    _initialTransformation[i].invert( A );

    Transformation R_0_1;
    helper::Decompose<Real>::polarDecomposition( A, R_0_1 );

    _initialRotations[i].transpose( R_0_1 );
    rotations[i] = _initialRotations[i];

    //save the element index as the node index
    _rotationIdx[a] = i;
    _rotationIdx[b] = i;
    _rotationIdx[c] = i;
    _rotationIdx[d] = i;

    _rotatedInitialElements[i][0] = R_0_1*initialPoints[a];
    _rotatedInitialElements[i][1] = R_0_1*initialPoints[b];
    _rotatedInitialElements[i][2] = R_0_1*initialPoints[c];
    _rotatedInitialElements[i][3] = R_0_1*initialPoints[d];

    computeStrainDisplacement( strainDisplacements[i],_rotatedInitialElements[i][0], _rotatedInitialElements[i][1],_rotatedInitialElements[i][2],_rotatedInitialElements[i][3] );
}




template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::accumulateForceSVD( Vector& f, const Vector & p, typename VecElement::const_iterator elementIt, Index elementIndex )
{
    if( _assembling.getValue() )
    {
        msg_warning(this) << "TODO(HyperReducedTetrahedronFEMForceField): support for assembling system matrix when using SVD method.";
        return;
    }

    Element index = *elementIt;

    Transformation A;
    A[0] = p[index[1]]-p[index[0]];
    A[1] = p[index[2]]-p[index[0]];
    A[2] = p[index[3]]-p[index[0]];

    defaulttype::Mat<3,3,Real> R_0_2;

    defaulttype::Mat<3,3,Real> F = A * _initialTransformation[elementIndex];

    if( determinant(F) < 1e-6 ) // inverted or too flat element -> SVD decomposition + handle degenerated cases
    {
        helper::Decompose<Real>::polarDecomposition_stable( F, R_0_2 );
        R_0_2 = R_0_2.multTransposed( _initialRotations[elementIndex] );
    }
    else // not inverted & not degenerated -> classical polar
    {
        helper::Decompose<Real>::polarDecomposition( A, R_0_2 );
    }



    rotations[elementIndex].transpose( R_0_2 );


    // positions of the deformed and displaced tetrahedron in its frame
    helper::fixed_array<Coord, 4>  deforme;
    for(int i=0; i<4; ++i)
        deforme[i] = R_0_2 * p[index[i]];

    // displacement
    Displacement D;
    D[0]  = _rotatedInitialElements[elementIndex][0][0] - deforme[0][0];
    D[1]  = _rotatedInitialElements[elementIndex][0][1] - deforme[0][1];
    D[2]  = _rotatedInitialElements[elementIndex][0][2] - deforme[0][2];
    D[3]  = _rotatedInitialElements[elementIndex][1][0] - deforme[1][0];
    D[4]  = _rotatedInitialElements[elementIndex][1][1] - deforme[1][1];
    D[5]  = _rotatedInitialElements[elementIndex][1][2] - deforme[1][2];
    D[6]  = _rotatedInitialElements[elementIndex][2][0] - deforme[2][0];
    D[7]  = _rotatedInitialElements[elementIndex][2][1] - deforme[2][1];
    D[8]  = _rotatedInitialElements[elementIndex][2][2] - deforme[2][2];
    D[9]  = _rotatedInitialElements[elementIndex][3][0] - deforme[3][0];
    D[10] = _rotatedInitialElements[elementIndex][3][1] - deforme[3][1];
    D[11] = _rotatedInitialElements[elementIndex][3][2] - deforme[3][2];

    if( _updateStiffnessMatrix.getValue() )
    {
        computeStrainDisplacement( strainDisplacements[elementIndex], deforme[0], deforme[1], deforme[2], deforme[3] );
    }

    Displacement Forces;
    computeForce( Forces, D, _plasticStrains[elementIndex], materialsStiffnesses[elementIndex], strainDisplacements[elementIndex] );
    for( int i=0 ; i<12 ; i+=3 )
    {
        f[index[i/3]] += rotations[elementIndex] * Deriv( Forces[i], Forces[i+1],  Forces[i+2] );
    }
}



///////////////////////////////////////////////////////////////////////////////////////
////////////////  specific methods for corotational large, polar, svd  ////////////////
///////////////////////////////////////////////////////////////////////////////////////

template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::applyStiffnessCorotational( Vector& f, const Vector& x, int i, Index a, Index b, Index c, Index d, SReal fact )
{
    Displacement X;

    // rotate by rotations[i] transposed
    X[0]  = rotations[i][0][0] * x[a][0] + rotations[i][1][0] * x[a][1] + rotations[i][2][0] * x[a][2];
    X[1]  = rotations[i][0][1] * x[a][0] + rotations[i][1][1] * x[a][1] + rotations[i][2][1] * x[a][2];
    X[2]  = rotations[i][0][2] * x[a][0] + rotations[i][1][2] * x[a][1] + rotations[i][2][2] * x[a][2];

    X[3]  = rotations[i][0][0] * x[b][0] + rotations[i][1][0] * x[b][1] + rotations[i][2][0] * x[b][2];
    X[4]  = rotations[i][0][1] * x[b][0] + rotations[i][1][1] * x[b][1] + rotations[i][2][1] * x[b][2];
    X[5]  = rotations[i][0][2] * x[b][0] + rotations[i][1][2] * x[b][1] + rotations[i][2][2] * x[b][2];

    X[6]  = rotations[i][0][0] * x[c][0] + rotations[i][1][0] * x[c][1] + rotations[i][2][0] * x[c][2];
    X[7]  = rotations[i][0][1] * x[c][0] + rotations[i][1][1] * x[c][1] + rotations[i][2][1] * x[c][2];
    X[8]  = rotations[i][0][2] * x[c][0] + rotations[i][1][2] * x[c][1] + rotations[i][2][2] * x[c][2];

    X[9]  = rotations[i][0][0] * x[d][0] + rotations[i][1][0] * x[d][1] + rotations[i][2][0] * x[d][2];
    X[10] = rotations[i][0][1] * x[d][0] + rotations[i][1][1] * x[d][1] + rotations[i][2][1] * x[d][2];
    X[11] = rotations[i][0][2] * x[d][0] + rotations[i][1][2] * x[d][1] + rotations[i][2][2] * x[d][2];

    Displacement F;

    computeForce( F, X, materialsStiffnesses[i], strainDisplacements[i], fact );

    if (d_performECSW.getValue()){
        // rotate by rotations[i]
        f[a][0] -=  weights(i)*(rotations[i][0][0] *  F[0] +  rotations[i][0][1] * F[1]  + rotations[i][0][2] * F[2]);
        f[a][1] -=  weights(i)*(rotations[i][1][0] *  F[0] +  rotations[i][1][1] * F[1]  + rotations[i][1][2] * F[2]);
        f[a][2] -=  weights(i)*(rotations[i][2][0] *  F[0] +  rotations[i][2][1] * F[1]  + rotations[i][2][2] * F[2]);

        f[b][0] -=  weights(i)*(rotations[i][0][0] *  F[3] +  rotations[i][0][1] * F[4]  + rotations[i][0][2] * F[5]);
        f[b][1] -=  weights(i)*(rotations[i][1][0] *  F[3] +  rotations[i][1][1] * F[4]  + rotations[i][1][2] * F[5]);
        f[b][2] -=  weights(i)*(rotations[i][2][0] *  F[3] +  rotations[i][2][1] * F[4]  + rotations[i][2][2] * F[5]);

        f[c][0] -=  weights(i)*(rotations[i][0][0] *  F[6] +  rotations[i][0][1] * F[7]  + rotations[i][0][2] * F[8]);
        f[c][1] -=  weights(i)*(rotations[i][1][0] *  F[6] +  rotations[i][1][1] * F[7]  + rotations[i][1][2] * F[8]);
        f[c][2] -=  weights(i)*(rotations[i][2][0] *  F[6] +  rotations[i][2][1] * F[7]  + rotations[i][2][2] * F[8]);

        f[d][0] -=  weights(i)*(rotations[i][0][0] *  F[9] +  rotations[i][0][1] * F[10] + rotations[i][0][2] * F[11]);
        f[d][1]	-=  weights(i)*(rotations[i][1][0] *  F[9] +  rotations[i][1][1] * F[10] + rotations[i][1][2] * F[11]);
        f[d][2]	-=  weights(i)*(rotations[i][2][0] *  F[9] +  rotations[i][2][1] * F[10] + rotations[i][2][2] * F[11]);
    }
    else
    {
        // rotate by rotations[i]
        f[a][0] -= rotations[i][0][0] *  F[0] +  rotations[i][0][1] * F[1]  + rotations[i][0][2] * F[2];
        f[a][1] -= rotations[i][1][0] *  F[0] +  rotations[i][1][1] * F[1]  + rotations[i][1][2] * F[2];
        f[a][2] -= rotations[i][2][0] *  F[0] +  rotations[i][2][1] * F[1]  + rotations[i][2][2] * F[2];

        f[b][0] -= rotations[i][0][0] *  F[3] +  rotations[i][0][1] * F[4]  + rotations[i][0][2] * F[5];
        f[b][1] -= rotations[i][1][0] *  F[3] +  rotations[i][1][1] * F[4]  + rotations[i][1][2] * F[5];
        f[b][2] -= rotations[i][2][0] *  F[3] +  rotations[i][2][1] * F[4]  + rotations[i][2][2] * F[5];

        f[c][0] -= rotations[i][0][0] *  F[6] +  rotations[i][0][1] * F[7]  + rotations[i][0][2] * F[8];
        f[c][1] -= rotations[i][1][0] *  F[6] +  rotations[i][1][1] * F[7]  + rotations[i][1][2] * F[8];
        f[c][2] -= rotations[i][2][0] *  F[6] +  rotations[i][2][1] * F[7]  + rotations[i][2][2] * F[8];

        f[d][0] -= rotations[i][0][0] *  F[9] +  rotations[i][0][1] * F[10] + rotations[i][0][2] * F[11];
        f[d][1]	-= rotations[i][1][0] *  F[9] +  rotations[i][1][1] * F[10] + rotations[i][1][2] * F[11];
        f[d][2]	-= rotations[i][2][0] *  F[9] +  rotations[i][2][1] * F[10] + rotations[i][2][2] * F[11];
    }

}


//////////////////////////////////////////////////////////////////////
////////////////  generic main computations methods  /////////////////
//////////////////////////////////////////////////////////////////////

template <class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::init()
{
    const VecReal& youngModulus = _youngModulus.getValue();
    minYoung=youngModulus[0];
    maxYoung=youngModulus[0];
    for (unsigned i=0; i<youngModulus.size(); i++)
    {
        if (youngModulus[i]<minYoung) minYoung=youngModulus[i];
        if (youngModulus[i]>maxYoung) maxYoung=youngModulus[i];
    }

    if (_updateStiffness.getValue())
        this->f_listening.setValue(true);

    // ParallelDataThrd is used to build the matrix asynchronusly (when listening = true)
    // This feature is activated when callin handleEvent with ParallelizeBuildEvent
    // At init parallelDataSimu == parallelDataThrd (and it's the case since handleEvent is called)

    this->core::behavior::ForceField<DataTypes>::init();
    _mesh = this->getContext()->getMeshTopology();
    if (_mesh==NULL)
    {
        msg_error(this) << "ERROR(HyperReducedTetrahedronFEMForceField): object must have a BaseMeshTopology.";
        return;
    }

    if (_mesh==NULL || (_mesh->getNbTetrahedra()<=0 && _mesh->getNbHexahedra()<=0))
    {
        msg_error(this) << "ERROR(HyperReducedTetrahedronFEMForceField): object must have a tetrahedric BaseMeshTopology.";
        return;
    }
    if (!_mesh->getTetrahedra().empty())
    {
        _indexedElements = & (_mesh->getTetrahedra());
    }
    else
    {
        core::topology::BaseMeshTopology::SeqTetrahedra* tetrahedra = new core::topology::BaseMeshTopology::SeqTetrahedra;
        int nbcubes = _mesh->getNbHexahedra();
        // These values are only correct if the mesh is a grid topology
        int nx = 2;
        int ny = 1;
        {
            topology::GridTopology* grid = dynamic_cast<topology::GridTopology*>(_mesh);
            if (grid != NULL)
            {
                nx = grid->getNx()-1;
                ny = grid->getNy()-1;
            }
        }

        // Tesselation of each cube into 6 tetrahedra
        tetrahedra->reserve(nbcubes*6);
        for (int i=0; i<nbcubes; i++)
        {
            core::topology::BaseMeshTopology::Hexa c = _mesh->getHexahedron(i);
#define swap(a,b) { int t = a; a = b; b = t; }
            if (!((i%nx)&1))
            {
                // swap all points on the X edges
                swap(c[0],c[1]);
                swap(c[3],c[2]);
                swap(c[4],c[5]);
                swap(c[7],c[6]);
            }
            if (((i/nx)%ny)&1)
            {
                // swap all points on the Y edges
                swap(c[0],c[3]);
                swap(c[1],c[2]);
                swap(c[4],c[7]);
                swap(c[5],c[6]);
            }
            if ((i/(nx*ny))&1)
            {
                // swap all points on the Z edges
                swap(c[0],c[4]);
                swap(c[1],c[5]);
                swap(c[2],c[6]);
                swap(c[3],c[7]);
            }
#undef swap
            typedef core::topology::BaseMeshTopology::Tetra Tetra;
            tetrahedra->push_back(Tetra(c[0],c[5],c[1],c[6]));
            tetrahedra->push_back(Tetra(c[0],c[1],c[3],c[6]));
            tetrahedra->push_back(Tetra(c[1],c[3],c[6],c[2]));
            tetrahedra->push_back(Tetra(c[6],c[3],c[0],c[7]));
            tetrahedra->push_back(Tetra(c[6],c[7],c[0],c[5]));
            tetrahedra->push_back(Tetra(c[7],c[5],c[4],c[0]));
        }

        _indexedElements = tetrahedra;
    }
    if (d_prepareECSW.getValue()){
        MatrixLoader<Eigen::MatrixXd>* matLoader = new MatrixLoader<Eigen::MatrixXd>();
        matLoader->setFileName(d_modesPath.getValue());
        matLoader->load();
        matLoader->getMatrix(m_modes);
        delete matLoader;
        m_modes.conservativeResize(Eigen::NoChange,d_nbModes.getValue());

        Gie.resize(d_nbTrainingSet.getValue()*d_nbModes.getValue());
        for (unsigned int i = 0; i < d_nbTrainingSet.getValue()*d_nbModes.getValue(); i++)
        {
            Gie[i].resize(_indexedElements->size());
            for (unsigned int j = 0; j < _indexedElements->size(); j++)
            {
                Gie[i][j] = 0;
            }
        }
    }

    if (d_performECSW.getValue())
    {

        MatrixLoader<Eigen::VectorXd>* weightsMatLoader = new MatrixLoader<Eigen::VectorXd>();
        weightsMatLoader->setFileName(d_weightsPath.getValue());
        weightsMatLoader->load();
        weightsMatLoader->getMatrix(weights);
        delete weightsMatLoader;

        MatrixLoader<Eigen::VectorXi>* RIDMatLoader = new MatrixLoader<Eigen::VectorXi>();
        RIDMatLoader->setFileName(d_RIDPath.getValue());
        RIDMatLoader->load();
        RIDMatLoader->getMatrix(reducedIntegrationDomain);
        delete RIDMatLoader;

        m_RIDsize = reducedIntegrationDomain.rows();

    }
    else
    {
        m_RIDsize = _indexedElements->size();  // the reduced integration contains all the elements in this case.
        reducedIntegrationDomain.resize(m_RIDsize);
        for (unsigned int i = 0; i<m_RIDsize; i++)
            reducedIntegrationDomain(i) = i;
    }

    reinit(); // compute per-element stiffness matrices and other precomputed values
}


template <class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::reset()
{
    for( unsigned i=0 ; i < _plasticStrains.size() ; ++i )
    {
        _plasticStrains[i].clear();
    }
}


template <class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::reinit()
{

    if (!this->mstate) return;
    if (!_mesh->getTetrahedra().empty())
    {
        _indexedElements = & (_mesh->getTetrahedra());
    }

    setMethod(f_method.getValue() );
    const VecCoord& p = this->mstate->read(core::ConstVecCoordId::restPosition())->getValue();
    _initialPoints.setValue(p);
    strainDisplacements.resize( _indexedElements->size() );
    materialsStiffnesses.resize(_indexedElements->size() );
    _plasticStrains.resize(     _indexedElements->size() );
    if(_assembling.getValue())
    {
        _stiffnesses.resize( _initialPoints.getValue().size()*3 );
    }

    /// initialization of structures for vonMises stress computations
    if (_computeVonMisesStress.getValue() > 0) {
        elemLambda.resize( _indexedElements->size() );
        elemMu.resize( _indexedElements->size() );

        helper::WriteAccessor<Data<helper::vector<Real> > > vME =  _vonMisesPerElement;
        vME.resize(_indexedElements->size());

        helper::WriteAccessor<Data<helper::vector<Real> > > vMN =  _vonMisesPerNode;
        vMN.resize(this->mstate->getSize());

        prevMaxStress = -1.0;
        updateVonMisesStress = true;
    }

    m_restVolume = 0;

    unsigned int i;
    typename VecElement::const_iterator it;
    switch(method)
    {
    case SMALL :
    {
        for(it = _indexedElements->begin(), i = 0 ; it != _indexedElements->end() ; ++it, ++i)
        {
            Index a = (*it)[0];
            Index b = (*it)[1];
            Index c = (*it)[2];
            Index d = (*it)[3];
            this->computeMaterialStiffness(i,a,b,c,d);
            this->initSmall(i,a,b,c,d);
        }
        break;
    }
    case LARGE :
    {
        rotations.resize( _indexedElements->size() );
        _initialRotations.resize( _indexedElements->size() );
        _rotationIdx.resize(_indexedElements->size() *4);
        _rotatedInitialElements.resize(_indexedElements->size());
        for(it = _indexedElements->begin(), i = 0 ; it != _indexedElements->end() ; ++it, ++i)
        {
            Index a = (*it)[0];
            Index b = (*it)[1];
            Index c = (*it)[2];
            Index d = (*it)[3];
            computeMaterialStiffness(i,a,b,c,d);
            initLarge(i,a,b,c,d);
        }
        break;
    }
    case POLAR :
    {
        rotations.resize( _indexedElements->size() );
        _initialRotations.resize( _indexedElements->size() );
        _rotationIdx.resize(_indexedElements->size() *4);
        _rotatedInitialElements.resize(_indexedElements->size());
        unsigned int i=0;
        typename VecElement::const_iterator it;
        for(it = _indexedElements->begin(), i = 0 ; it != _indexedElements->end() ; ++it, ++i)
        {
            Index a = (*it)[0];
            Index b = (*it)[1];
            Index c = (*it)[2];
            Index d = (*it)[3];
            computeMaterialStiffness(i,a,b,c,d);
            initPolar(i,a,b,c,d);
        }
        break;
    }
    case SVD :
    {
        rotations.resize( _indexedElements->size() );
        _initialRotations.resize( _indexedElements->size() );
        _rotationIdx.resize(_indexedElements->size() *4);
        _rotatedInitialElements.resize(_indexedElements->size());
        _initialTransformation.resize(_indexedElements->size());
        unsigned int i=0;
        typename VecElement::const_iterator it;
        for(it = _indexedElements->begin(), i = 0 ; it != _indexedElements->end() ; ++it, ++i)
        {
            Index a = (*it)[0];
            Index b = (*it)[1];
            Index c = (*it)[2];
            Index d = (*it)[3];
            computeMaterialStiffness(i,a,b,c,d);
            initSVD(i,a,b,c,d);
        }
        break;
    }
    }

    if (_computeVonMisesStress.getValue() > 0) {
        elemDisplacements.resize(  _indexedElements->size() );

        helper::ReadAccessor<Data<VecCoord> > X0 =  _initialPoints;

        elemShapeFun.resize(_indexedElements->size());
        for(it = _indexedElements->begin(), i = 0 ; it != _indexedElements->end() ; ++it, ++i)
        {
            Mat44 matVert;

            for (size_t k = 0; k < 4; k++) {
                Index ix = (*it)[k];
                matVert[k][0] = 1.0;
                for (size_t l = 1; l < 4; l++)
                    matVert[k][l] = X0[ix][l-1];
            }

            invertMatrix(elemShapeFun[i], matVert);
        }
         computeVonMisesStress();
    }

}


template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::addForce (const core::MechanicalParams* /*mparams*/, DataVecDeriv& d_f, const DataVecCoord& d_x, const DataVecDeriv& /* d_v */)
{
    VecDeriv& f = *d_f.beginEdit();
    const VecCoord& p = d_x.getValue();

    sofa::helper::AdvancedTimer::stepBegin("time in AddForce");
    f.resize(p.size());

    if (needUpdateTopology)
    {
        reinit();
        needUpdateTopology = false;
    }

    unsigned int i;
    typename VecElement::const_iterator it, it0;
    switch(method)
    {
    case SMALL :
    {
        for(it=_indexedElements->begin(), i = 0 ; it!=_indexedElements->end(); ++it,++i)
        {
            accumulateForceSmall( f, p, it, i );
        }
        break;
    }
    case LARGE :
    {
        if (d_performECSW.getValue()){
                it0=_indexedElements->begin();
                for( i = 0 ; i<m_RIDsize ;++i)
                {
                    it = it0 + reducedIntegrationDomain(i);
                    accumulateForceLarge( f, p, it, reducedIntegrationDomain(i) );
                }
        }
        else
        {
            for(it=_indexedElements->begin(), i = 0 ; it!=_indexedElements->end(); ++it,++i)
            {
                accumulateForceLarge( f, p, it, i );
            }
        }
        if (d_prepareECSW.getValue())
        {
            int numTest = this->getContext()->getTime()/this->getContext()->getDt();
            if (numTest%d_periodSaveGIE.getValue() == 0)       // A new value was taken
            {
                numTest = numTest/d_periodSaveGIE.getValue();
                if (numTest < d_nbTrainingSet.getValue()){
                    std::stringstream gieFileNameSS;
                    gieFileNameSS << this->name << "_Gie.txt";
                    std::string gieFileName = gieFileNameSS.str();
                    std::ofstream myfileGie (gieFileName, std::fstream::app);
                    msg_info(this) << "Storing case number " << numTest+1 << " in " << gieFileName << " ...";
                    for (unsigned int k=numTest*d_nbModes.getValue(); k<(numTest+1)*d_nbModes.getValue();k++){
                        for (unsigned int l=0;l<_indexedElements->size();l++){
                            myfileGie << Gie[k][l] << " ";
                        }
                        myfileGie << std::endl;
                    }
                    myfileGie.close();
                    msg_info(this) << "Storing Done";
                }
                else
                {
                    msg_info(this) << d_nbTrainingSet.getValue() << "were already stored. Learning phase completed.";
                }
            }
        }



        break;
    }
    case POLAR :
    {
        for(it=_indexedElements->begin(), i = 0 ; it!=_indexedElements->end(); ++it,++i)
        {
            accumulateForcePolar( f, p, it, i );
        }
        break;
    }
    case SVD :
    {
        for(it=_indexedElements->begin(), i = 0 ; it!=_indexedElements->end(); ++it,++i)
        {
            accumulateForceSVD( f, p, it, i );
        }
        break;
    }
    }
    d_f.endEdit();

    updateVonMisesStress = true;
    sofa::helper::AdvancedTimer::stepEnd("time in AddForce");
}

template<class DataTypes>
inline void HyperReducedTetrahedronFEMForceField<DataTypes>::addDForce(const core::MechanicalParams* mparams, DataVecDeriv& d_df, const DataVecDeriv& d_dx)
{
    VecDeriv& df = *d_df.beginEdit();
    const VecDeriv& dx = d_dx.getValue();
    Real kFactor = (Real)mparams->kFactorIncludingRayleighDamping(this->rayleighStiffness.getValue());

    sofa::helper::AdvancedTimer::stepBegin("time in AddDDDForce");

    df.resize(dx.size());
    unsigned int i;
    typename VecElement::const_iterator it,it0;
    if( method == SMALL )
    {
        for(it = _indexedElements->begin(), i = 0 ; it != _indexedElements->end() ; ++it, ++i)
        {
            Index a = (*it)[0];
            Index b = (*it)[1];
            Index c = (*it)[2];
            Index d = (*it)[3];

            applyStiffnessSmall( df,dx, i, a,b,c,d, kFactor );
        }
    }
    else
    {
        if (d_performECSW.getValue())
        {
            it0=_indexedElements->begin();
            for( i = 0 ; i<m_RIDsize ;++i)
            {
                it = it0 + reducedIntegrationDomain(i);
                Index a = (*it)[0];
                Index b = (*it)[1];
                Index c = (*it)[2];
                Index d = (*it)[3];
                applyStiffnessCorotational( df,dx, reducedIntegrationDomain(i), a,b,c,d, kFactor );
            }
        }
        else
        {
            for(it = _indexedElements->begin(), i = 0 ; it != _indexedElements->end() ; ++it, ++i)
            {
                Index a = (*it)[0];
                Index b = (*it)[1];
                Index c = (*it)[2];
                Index d = (*it)[3];

                applyStiffnessCorotational( df,dx, i, a,b,c,d, kFactor );
            }
        }
    }

    d_df.endEdit();
    sofa::helper::AdvancedTimer::stepEnd("time in AddDDDForce");
}

//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::draw(const core::visual::VisualParams* vparams)
{

    if (_computeVonMisesStress.getValue() > 0) {
        if (updateVonMisesStress)
            computeVonMisesStress();
    }

    if (!vparams->displayFlags().getShowForceFields()) return;
    if (!this->mstate) return;

    if(needUpdateTopology)
    {
        reinit();
        needUpdateTopology = false;
    }

    const VecCoord& x = this->mstate->read(core::ConstVecCoordId::position())->getValue();

    const bool edges = (drawAsEdges.getValue() || vparams->displayFlags().getShowWireFrame());
    const bool heterogeneous = (drawHeterogeneousTetra.getValue() && minYoung!=maxYoung);

    const VecReal & youngModulus = _youngModulus.getValue();

    /// vonMises stress
    Real minVM = (Real)1e20, maxVM = (Real)-1e20;
    Real minVMN = (Real)1e20, maxVMN = (Real)-1e20;
    helper::ReadAccessor<Data<helper::vector<Real> > > vM =  _vonMisesPerElement;
    helper::ReadAccessor<Data<helper::vector<Real> > > vMN =  _vonMisesPerNode;
    if (_computeVonMisesStress.getValue() > 0) {
        if (updateVonMisesStress)
            computeVonMisesStress();

        for (size_t i = 0; i < vM.size(); i++) {
            minVM = (vM[i] < minVM) ? vM[i] : minVM;
            maxVM = (vM[i] > maxVM) ? vM[i] : maxVM;
        }

        if (maxVM < prevMaxStress)
            maxVM = prevMaxStress;

        for (size_t i = 0; i < vMN.size(); i++) {
            minVMN = (vMN[i] < minVMN) ? vMN[i] : minVMN;
            maxVMN = (vMN[i] > maxVMN) ? vMN[i] : maxVMN;
        }

        maxVM*=_showStressAlpha.getValue();
        maxVMN*=_showStressAlpha.getValue();

    }

    vparams->drawTool()->setLightingEnabled(false);

#ifdef SIMPLEFEM_COLORMAP
    if (_showVonMisesStressPerNode.getValue()) {
        std::vector<defaulttype::Vec4f> nodeColors(x.size());
        std::vector<defaulttype::Vector3> pts(x.size());
        helper::ColorMap::evaluator<Real> evalColor = m_VonMisesColorMap.getEvaluator(minVMN, maxVMN);
        for (size_t nd = 0; nd < x.size(); nd++) {
            pts[nd] = x[nd];
            nodeColors[nd] = evalColor(vMN[nd]);
        }
        vparams->drawTool()->drawPoints(pts, 10, nodeColors);
    }
#endif

    if (edges)
    {
        std::vector< defaulttype::Vector3 > points[3];
        typename VecElement::const_iterator it;
        int i;
        for(it = _indexedElements->begin(), i = 0 ; it != _indexedElements->end() ; ++it, ++i)
        {
            Index a = (*it)[0];
            Index b = (*it)[1];
            Index c = (*it)[2];
            Index d = (*it)[3];
            Coord pa = x[a];
            Coord pb = x[b];
            Coord pc = x[c];
            Coord pd = x[d];

            points[0].push_back(pa);
            points[0].push_back(pb);
            points[0].push_back(pc);
            points[0].push_back(pd);

            points[1].push_back(pa);
            points[1].push_back(pc);
            points[1].push_back(pb);
            points[1].push_back(pd);

            points[2].push_back(pa);
            points[2].push_back(pd);
            points[2].push_back(pb);
            points[2].push_back(pc);

            if(heterogeneous)
            {
                float col = (float)((youngModulus[i]-minYoung) / (maxYoung-minYoung));
                float fac = col * 0.5f;
                defaulttype::Vec<4,float> color2 = defaulttype::Vec<4,float>(col      , 0.5f - fac , 1.0f-col,1.0f);
                defaulttype::Vec<4,float> color3 = defaulttype::Vec<4,float>(col      , 1.0f - fac , 1.0f-col,1.0f);
                defaulttype::Vec<4,float> color4 = defaulttype::Vec<4,float>(col+0.5f , 1.0f - fac , 1.0f-col,1.0f);

                vparams->drawTool()->drawLines(points[0],1,color2 );
                vparams->drawTool()->drawLines(points[1],1,color3 );
                vparams->drawTool()->drawLines(points[2],1,color4 );

                for(unsigned int i=0 ; i<3 ; i++) points[i].clear();
            } else {
#ifdef SIMPLEFEM_COLORMAP
                if (_computeVonMisesStress.getValue() > 0) {
                    for(unsigned int i=0 ; i<3 ; i++) points[i].clear();
                }
#endif
            }
        }

        if(!heterogeneous
#ifdef SIMPLEFEM_COLORMAP
           && _computeVonMisesStress.getValue() == 0
#endif
        )
        {
            vparams->drawTool()->drawLines(points[0], 1, defaulttype::Vec<4,float>(0.0,0.5,1.0,1.0));
            vparams->drawTool()->drawLines(points[1], 1, defaulttype::Vec<4,float>(0.0,1.0,1.0,1.0));
            vparams->drawTool()->drawLines(points[2], 1, defaulttype::Vec<4,float>(0.5,1.0,1.0,1.0));
        }
    }
    else
    {

        std::vector< defaulttype::Vector3 > points[4];
        typename VecElement::const_iterator it, it0;
        unsigned int i;

        it0 = _indexedElements->begin();
        for( i = 0 ; i<m_RIDsize ;++i)
        {
            it = it0 + reducedIntegrationDomain(i);
            Index a = (*it)[0];
            Index b = (*it)[1];
            Index c = (*it)[2];
            Index d = (*it)[3];
            Coord center = (x[a]+x[b]+x[c]+x[d])*0.125;
            Coord pa = (x[a]+center)*(Real)0.666667;
            Coord pb = (x[b]+center)*(Real)0.666667;
            Coord pc = (x[c]+center)*(Real)0.666667;
            Coord pd = (x[d]+center)*(Real)0.666667;

            points[0].push_back(pa);
            points[0].push_back(pb);
            points[0].push_back(pc);

            points[1].push_back(pb);
            points[1].push_back(pc);
            points[1].push_back(pd);

            points[2].push_back(pc);
            points[2].push_back(pd);
            points[2].push_back(pa);

            points[3].push_back(pd);
            points[3].push_back(pa);
            points[3].push_back(pb);

            if(heterogeneous)
            {
                float col = (float)((youngModulus[i]-minYoung) / (maxYoung-minYoung));
                float fac = col * 0.5f;
                defaulttype::Vec<4,float> color1 = defaulttype::Vec<4,float>(col      , 0.0f - fac , 1.0f-col,1.0f);
                defaulttype::Vec<4,float> color2 = defaulttype::Vec<4,float>(col      , 0.5f - fac , 1.0f-col,1.0f);
                defaulttype::Vec<4,float> color3 = defaulttype::Vec<4,float>(col      , 1.0f - fac , 1.0f-col,1.0f);
                defaulttype::Vec<4,float> color4 = defaulttype::Vec<4,float>(col+0.5f , 1.0f - fac , 1.0f-col,1.0f);

                vparams->drawTool()->drawTriangles(points[0],color1 );
                vparams->drawTool()->drawTriangles(points[1],color2 );
                vparams->drawTool()->drawTriangles(points[2],color3 );
                vparams->drawTool()->drawTriangles(points[3],color4 );

                for(unsigned int i=0 ; i<4 ; i++) points[i].clear();
            } else {
#ifdef SIMPLEFEM_COLORMAP
                if (_computeVonMisesStress.getValue() > 0) {
                    helper::ColorMap::evaluator<Real> evalColor = m_VonMisesColorMap.getEvaluator(minVM, maxVM);
                    defaulttype::Vec4f col = evalColor(vM[i]); //*vM[i]);

                    col[3] = 1.0f;
                    vparams->drawTool()->drawTriangles(points[0],col);
                    vparams->drawTool()->drawTriangles(points[1],col);
                    vparams->drawTool()->drawTriangles(points[2],col);
                    vparams->drawTool()->drawTriangles(points[3],col);

                    for(unsigned int i=0 ; i<4 ; i++) points[i].clear();
                }
#endif
            }

        }

        if(!heterogeneous
#ifdef SIMPLEFEM_COLORMAP
           && _computeVonMisesStress.getValue() == 0
#endif
        )
        {
            vparams->drawTool()->drawTriangles(points[0], defaulttype::Vec<4,float>(0.0,0.0,1.0,1.0));
            vparams->drawTool()->drawTriangles(points[1], defaulttype::Vec<4,float>(0.0,0.5,1.0,1.0));
            vparams->drawTool()->drawTriangles(points[2], defaulttype::Vec<4,float>(0.0,1.0,1.0,1.0));
            vparams->drawTool()->drawTriangles(points[3], defaulttype::Vec<4,float>(0.5,1.0,1.0,1.0));
        }

    }

    ////////////// AFFICHAGE DES ROTATIONS ////////////////////////
    if (vparams->displayFlags().getShowNormals())
    {

        std::vector< defaulttype::Vector3 > points[3];

        for(unsigned ii = 0; ii<  x.size() ; ii++)
        {
            Coord a = x[ii];
            Transformation R;
            getRotation(R, ii);
            Deriv v;
            // x
            v.x() =1.0; v.y()=0.0; v.z()=0.0;
            Coord b = a + R*v;
            points[0].push_back(a);
            points[0].push_back(b);
            // y
            v.x() =0.0; v.y()=1.0; v.z()=0.0;
            b = a + R*v;
            points[1].push_back(a);
            points[1].push_back(b);
            // z
            v.x() =0.0; v.y()=0.0; v.z()=1.0;
            b = a + R*v;
            points[2].push_back(a);
            points[2].push_back(b);
        }

        vparams->drawTool()->drawLines(points[0], 5, defaulttype::Vec<4,float>(1,0,0,1));
        vparams->drawTool()->drawLines(points[1], 5, defaulttype::Vec<4,float>(0,1,0,1));
        vparams->drawTool()->drawLines(points[2], 5, defaulttype::Vec<4,float>(0,0,1,1));

    }
}

template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::addKToMatrix(const core::MechanicalParams* mparams, const sofa::core::behavior::MultiMatrixAccessor* matrix )
{
    sofa::core::behavior::MultiMatrixAccessor::MatrixRef r = matrix->getMatrix(this->mstate);
    if (r)
        addKToMatrix(r.matrix, mparams->kFactorIncludingRayleighDamping(this->rayleighStiffness.getValue()), r.offset);
    else msg_error(this) << "addKToMatrix found no valid matrix accessor." ;
}

template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::addKToMatrix(sofa::defaulttype::BaseMatrix *mat, SReal k, unsigned int &offset)
{
    double timeScale, time ;
    sofa::helper::system::thread::CTime *timer;
    timeScale = 1000.0 / (double)sofa::helper::system::thread::CTime::getTicksPerSec();
    time = (double)timer->getTime();


    // Build Matrix Block for this ForceField
    unsigned int i,j,n1, n2, row, column, ROW, COLUMN , IT;

    Transformation Rot;
    StiffnessMatrix JKJt,tmp;

    typename VecElement::const_iterator it, it0;

    Index noeud1, noeud2;
    int offd3 = offset/3;

    Rot[0][0]=Rot[1][1]=Rot[2][2]=1;
    Rot[0][1]=Rot[0][2]=0;
    Rot[1][0]=Rot[1][2]=0;
    Rot[2][0]=Rot[2][1]=0;
    //msg_info(this) << "in ADDKtoMatrix" ;
    if (sofa::component::linearsolver::CompressedRowSparseMatrix<defaulttype::Mat<3,3,double> > * crsmat = dynamic_cast<sofa::component::linearsolver::CompressedRowSparseMatrix<defaulttype::Mat<3,3,double> > * >(mat))
    {
        if (d_performECSW.getValue())
        {
            it0=_indexedElements->begin();
            for(int iECSW = 0 ; iECSW<m_RIDsize ;++iECSW)
            {
                it = it0 + reducedIntegrationDomain(iECSW);
                IT = reducedIntegrationDomain(iECSW);
                if (method == SMALL) computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],Rot);
                else computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],rotations[IT]);

                defaulttype::Mat<3,3,double> tmpBlock[4][4];
                // find index of node 1
                for (n1=0; n1<4; n1++)
                {
                    for(i=0; i<3; i++)
                    {
                        for (n2=0; n2<4; n2++)
                        {
                            for (j=0; j<3; j++)
                            {
                                tmpBlock[n1][n2][i][j] = - tmp[n1*3+i][n2*3+j]*k;
                            }
                        }
                    }
                }
                *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[0],true) +=  weights(IT)*tmpBlock[0][0];
                *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[1],true) +=  weights(IT)*tmpBlock[0][1];
                *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[2],true) +=  weights(IT)*tmpBlock[0][2];
                *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[3],true) +=  weights(IT)*tmpBlock[0][3];

                *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[0],true) +=  weights(IT)*tmpBlock[1][0];
                *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[1],true) +=  weights(IT)*tmpBlock[1][1];
                *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[2],true) +=  weights(IT)*tmpBlock[1][2];
                *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[3],true) +=  weights(IT)*tmpBlock[1][3];

                *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[0],true) +=  weights(IT)*tmpBlock[2][0];
                *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[1],true) +=  weights(IT)*tmpBlock[2][1];
                *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[2],true) +=  weights(IT)*tmpBlock[2][2];
                *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[3],true) +=  weights(IT)*tmpBlock[2][3];

                *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[0],true) +=  weights(IT)*tmpBlock[3][0];
                *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[1],true) +=  weights(IT)*tmpBlock[3][1];
                *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[2],true) +=  weights(IT)*tmpBlock[3][2];
                *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[3],true) +=  weights(IT)*tmpBlock[3][3];
            }
        }
        else
        {
            for(it = _indexedElements->begin(), IT=0 ; it != _indexedElements->end() ; ++it,++IT)
            {
                if (method == SMALL) computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],Rot);
                else computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],rotations[IT]);

                defaulttype::Mat<3,3,double> tmpBlock[4][4];
                // find index of node 1
                for (n1=0; n1<4; n1++)
                {
                    for(i=0; i<3; i++)
                    {
                        for (n2=0; n2<4; n2++)
                        {
                            for (j=0; j<3; j++)
                            {
                                tmpBlock[n1][n2][i][j] = - tmp[n1*3+i][n2*3+j]*k;
                            }
                        }
                    }
                }
                *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[0],true) += tmpBlock[0][0];
                *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[1],true) += tmpBlock[0][1];
                *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[2],true) += tmpBlock[0][2];
                *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[3],true) += tmpBlock[0][3];

                *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[0],true) += tmpBlock[1][0];
                *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[1],true) += tmpBlock[1][1];
                *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[2],true) += tmpBlock[1][2];
                *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[3],true) += tmpBlock[1][3];

                *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[0],true) += tmpBlock[2][0];
                *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[1],true) += tmpBlock[2][1];
                *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[2],true) += tmpBlock[2][2];
                *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[3],true) += tmpBlock[2][3];

                *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[0],true) += tmpBlock[3][0];
                *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[1],true) += tmpBlock[3][1];
                *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[2],true) += tmpBlock[3][2];
                *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[3],true) += tmpBlock[3][3];
            }
        }
    }
    else if (sofa::component::linearsolver::CompressedRowSparseMatrix<defaulttype::Mat<3,3,float> > * crsmat = dynamic_cast<sofa::component::linearsolver::CompressedRowSparseMatrix<defaulttype::Mat<3,3,float> > * >(mat))
    {
        for(it = _indexedElements->begin(), IT=0 ; it != _indexedElements->end() ; ++it,++IT)
        {

            if (method == SMALL) computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],Rot);
            else computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],rotations[IT]);

            defaulttype::Mat<3,3,double> tmpBlock[4][4];
            // find index of node 1
            for (n1=0; n1<4; n1++)
            {
                for(i=0; i<3; i++)
                {
                    for (n2=0; n2<4; n2++)
                    {
                        for (j=0; j<3; j++)
                        {
                            tmpBlock[n1][n2][i][j] = - tmp[n1*3+i][n2*3+j]*k;
                        }
                    }
                }
            }

            *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[0],true) += tmpBlock[0][0];
            *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[1],true) += tmpBlock[0][1];
            *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[2],true) += tmpBlock[0][2];
            *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[3],true) += tmpBlock[0][3];

            *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[0],true) += tmpBlock[1][0];
            *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[1],true) += tmpBlock[1][1];
            *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[2],true) += tmpBlock[1][2];
            *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[3],true) += tmpBlock[1][3];

            *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[0],true) += tmpBlock[2][0];
            *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[1],true) += tmpBlock[2][1];
            *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[2],true) += tmpBlock[2][2];
            *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[3],true) += tmpBlock[2][3];

            *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[0],true) += tmpBlock[3][0];
            *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[1],true) += tmpBlock[3][1];
            *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[2],true) += tmpBlock[3][2];
            *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[3],true) += tmpBlock[3][3];
        }
    }
    else
    {
        if (d_performECSW.getValue())
        {
            unsigned int ReducedElementIndex;

            it0=_indexedElements->begin();
            for( ReducedElementIndex = 0 ; ReducedElementIndex<m_RIDsize ;++ReducedElementIndex)
            {
                IT = reducedIntegrationDomain(ReducedElementIndex);
                sofa::core::topology::Topology::Tetrahedron t = (*_indexedElements)[IT];
                it = it0 + reducedIntegrationDomain(ReducedElementIndex);

                if (method == SMALL)
                    computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],Rot);
                else
                    computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],rotations[IT]);
                // find index of node 1
                for (n1=0; n1<4; n1++)
                {
                    noeud1 = t[n1];

                    for(i=0; i<3; i++)
                    {
                        ROW = offset+3*noeud1+i;
                        row = 3*n1+i;
                        // find index of node 2
                        for (n2=0; n2<4; n2++)
                        {
                            noeud2 = t[n2];


                            for (j=0; j<3; j++)
                            {
                                COLUMN = offset+3*noeud2+j;
                                column = 3*n2+j;
                                mat->add(ROW, COLUMN, -  weights(IT)*tmp[row][column]*k);
                            }
                        }
                    }
                }

            }
        }
        else
        {
            for(it = _indexedElements->begin(), IT=0 ; it != _indexedElements->end() ; ++it,++IT)
            {

                if (method == SMALL)
                    computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],Rot);
                else
                    computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],rotations[IT]);
                // find index of node 1
                for (n1=0; n1<4; n1++)
                {
                    noeud1 = (*it)[n1];

                    for(i=0; i<3; i++)
                    {
                        ROW = offset+3*noeud1+i;
                        row = 3*n1+i;
                        // find index of node 2
                        for (n2=0; n2<4; n2++)
                        {
                            noeud2 = (*it)[n2];

                            for (j=0; j<3; j++)
                            {
                                COLUMN = offset+3*noeud2+j;
                                column = 3*n2+j;
                                mat->add(ROW, COLUMN, - tmp[row][column]*k);
                            }
                        }
                    }
                }
            }
        }

    }
    //msg_info(this) << " HyperReducedTetrahedronFEMForceField<DataTypes>::addKToMatrix : "<<( (double)timer->getTime() - time)*timeScale<<" ms";
}

template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::addSubKToMatrix(sofa::defaulttype::BaseMatrix *mat, const helper::vector<unsigned> & subMatrixIndex, SReal k, unsigned int &offset) {
    // Build Matrix Block for this ForceField
    int i,j,n1, n2, row, column, ROW, COLUMN , IT;

    Transformation Rot;
    StiffnessMatrix JKJt,tmp;

    typename VecElement::const_iterator it;

    Index noeud1, noeud2;
    int offd3 = offset/3;

    Rot[0][0]=Rot[1][1]=Rot[2][2]=1;
    Rot[0][1]=Rot[0][2]=0;
    Rot[1][0]=Rot[1][2]=0;
    Rot[2][0]=Rot[2][1]=0;

    helper::vector<int> itTetraBuild;
    for(unsigned e = 0;e< subMatrixIndex.size();e++) {
        // search all the tetra connected to the point in subMatrixIndex
        for(it = _indexedElements->begin(), IT=0 ; it != _indexedElements->end() ; ++it,++IT) {
            if ((*it)[0] == subMatrixIndex[e] || (*it)[1] == subMatrixIndex[e] || (*it)[2] == subMatrixIndex[e] || (*it)[3] == subMatrixIndex[e]) {

                /// try to add the tetra in the set of point subMatrixIndex (add it only once)
                unsigned i=0;
                for (;i<itTetraBuild.size();i++) {
                    if (itTetraBuild[i] == IT) break;
                }
                if (i == itTetraBuild.size()) itTetraBuild.push_back(IT);
            }
        }
    }

    if (sofa::component::linearsolver::CompressedRowSparseMatrix<defaulttype::Mat<3,3,double> > * crsmat = dynamic_cast<sofa::component::linearsolver::CompressedRowSparseMatrix<defaulttype::Mat<3,3,double> > * >(mat)) {
        for(unsigned e = 0;e< itTetraBuild.size();e++) {
            IT = itTetraBuild[e];
            it = _indexedElements->begin() + IT;

            if (method == SMALL) computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],Rot);
            else computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],rotations[IT]);

            defaulttype::Mat<3,3,double> tmpBlock[4][4];
            // find index of node 1
            for (n1=0; n1<4; n1++) {
                for(i=0; i<3; i++) {
                    for (n2=0; n2<4; n2++) {
                        for (j=0; j<3; j++) {
                            tmpBlock[n1][n2][i][j] = - tmp[n1*3+i][n2*3+j]*k;
                        }
                    }
                }
            }
            *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[0],true) += tmpBlock[0][0];
            *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[1],true) += tmpBlock[0][1];
            *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[2],true) += tmpBlock[0][2];
            *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[3],true) += tmpBlock[0][3];

            *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[0],true) += tmpBlock[1][0];
            *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[1],true) += tmpBlock[1][1];
            *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[2],true) += tmpBlock[1][2];
            *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[3],true) += tmpBlock[1][3];

            *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[0],true) += tmpBlock[2][0];
            *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[1],true) += tmpBlock[2][1];
            *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[2],true) += tmpBlock[2][2];
            *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[3],true) += tmpBlock[2][3];

            *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[0],true) += tmpBlock[3][0];
            *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[1],true) += tmpBlock[3][1];
            *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[2],true) += tmpBlock[3][2];
            *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[3],true) += tmpBlock[3][3];
        }
    } else if (sofa::component::linearsolver::CompressedRowSparseMatrix<defaulttype::Mat<3,3,float> > * crsmat = dynamic_cast<sofa::component::linearsolver::CompressedRowSparseMatrix<defaulttype::Mat<3,3,float> > * >(mat)) {
        for(unsigned e = 0;e< itTetraBuild.size();e++) {
            IT = itTetraBuild[e];
            it = _indexedElements->begin() + IT;

            if (method == SMALL) computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],Rot);
            else computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],rotations[IT]);

            defaulttype::Mat<3,3,double> tmpBlock[4][4];
            // find index of node 1
            for (n1=0; n1<4; n1++) {
                for(i=0; i<3; i++) {
                    for (n2=0; n2<4; n2++) {
                        for (j=0; j<3; j++) {
                            tmpBlock[n1][n2][i][j] = - tmp[n1*3+i][n2*3+j]*k;
                        }
                    }
                }
            }
            *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[0],true) += tmpBlock[0][0];
            *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[1],true) += tmpBlock[0][1];
            *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[2],true) += tmpBlock[0][2];
            *crsmat->wbloc(offd3 + (*it)[0], offd3 + (*it)[3],true) += tmpBlock[0][3];

            *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[0],true) += tmpBlock[1][0];
            *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[1],true) += tmpBlock[1][1];
            *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[2],true) += tmpBlock[1][2];
            *crsmat->wbloc(offd3 + (*it)[1], offd3 + (*it)[3],true) += tmpBlock[1][3];

            *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[0],true) += tmpBlock[2][0];
            *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[1],true) += tmpBlock[2][1];
            *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[2],true) += tmpBlock[2][2];
            *crsmat->wbloc(offd3 + (*it)[2], offd3 + (*it)[3],true) += tmpBlock[2][3];

            *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[0],true) += tmpBlock[3][0];
            *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[1],true) += tmpBlock[3][1];
            *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[2],true) += tmpBlock[3][2];
            *crsmat->wbloc(offd3 + (*it)[3], offd3 + (*it)[3],true) += tmpBlock[3][3];
        }
    } else {
        for(unsigned e = 0;e< itTetraBuild.size();e++) {
            IT = itTetraBuild[e];
            it = _indexedElements->begin() + IT;

            if (method == SMALL) computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],Rot);
            else computeStiffnessMatrix(JKJt,tmp,materialsStiffnesses[IT], strainDisplacements[IT],rotations[IT]);

            // find index of node 1
            for (n1=0; n1<4; n1++) {
                noeud1 = (*it)[n1];

                for(i=0; i<3; i++) {
                    ROW = offset+3*noeud1+i;
                    row = 3*n1+i;
                    // find index of node 2
                    for (n2=0; n2<4; n2++) {
                        noeud2 = (*it)[n2];

                        for (j=0; j<3; j++) {
                            COLUMN = offset+3*noeud2+j;
                            column = 3*n2+j;
                            mat->add(ROW, COLUMN, - tmp[row][column]*k);
                        }
                    }
                }
            }
        }

    }
}

template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::handleEvent(core::objectmodel::Event *event)
{
    if (sofa::simulation::AnimateBeginEvent::checkEventType(event)) {
        if (_updateStiffness.getValue()) {
            const VecReal& youngModulus = _youngModulus.getValue();
            minYoung=youngModulus[0];
            maxYoung=youngModulus[0];
            for (unsigned i=0; i<youngModulus.size(); i++)
            {
                if (youngModulus[i]<minYoung) minYoung=youngModulus[i];
                if (youngModulus[i]>maxYoung) maxYoung=youngModulus[i];
            }

            unsigned int i;
            typename VecElement::const_iterator it;
            for(it = _indexedElements->begin(), i = 0 ; it != _indexedElements->end() ; ++it, ++i)
            {
                Index a = (*it)[0];
                Index b = (*it)[1];
                Index c = (*it)[2];
                Index d = (*it)[3];
                this->computeMaterialStiffness(i,a,b,c,d);
            }
        }
    }

}

template<class DataTypes>
void HyperReducedTetrahedronFEMForceField<DataTypes>::computeVonMisesStress()
{

    typename core::behavior::MechanicalState<DataTypes>* mechanicalObject;
    this->getContext()->get(mechanicalObject);
    const VecCoord& X = mechanicalObject->read(core::ConstVecCoordId::position())->getValue();

    helper::ReadAccessor<Data<VecCoord> > X0 =  _initialPoints;

#ifdef SOFAHYPERREDUCEDTETRAHEDRONFEMFORCEFIELD_COLORMAP
#ifndef SOFA_NO_OPENGL
#endif
#endif

    VecCoord U;
    U.resize(X.size());
    for (size_t i = 0; i < X0.size(); i++)
        U[i] = X[i] - X0[i];

    typename VecElement::const_iterator it, it0;
    size_t el;
    helper::WriteAccessor<Data<helper::vector<Real> > > vME =  _vonMisesPerElement;

    it0 = _indexedElements->begin();
    for(unsigned int i = 0 ; i<m_RIDsize ;++i)
    {
        it = it0 + reducedIntegrationDomain(i);
        el = reducedIntegrationDomain(i);
        defaulttype::Vec<6,Real> vStrain;
        Mat33 gradU;

        if (_computeVonMisesStress.getValue() == 2) {
            Mat44& shf = elemShapeFun[el];

            /// compute gradU
            for (size_t k = 0; k < 3; k++) {
                for (size_t l = 0; l < 3; l++)  {
                    gradU[k][l] = 0.0;
                    for (size_t m = 0; m < 4; m++)
                        gradU[k][l] += shf[l+1][m] * U[(*it)[m]][k];
                }
            }

            Mat33 strain = ((Real)0.5)*(gradU + gradU.transposed() + gradU.transposed()*gradU);

            for (size_t i = 0; i < 3; i++)
                vStrain[i] = strain[i][i];
            vStrain[3] = strain[1][2];
            vStrain[4] = strain[0][2];
            vStrain[5] = strain[0][1];
        }

        if (_computeVonMisesStress.getValue() == 1) {
            Element index = *it;
            size_t elementIndex = el;

            // Rotation matrix (deformed and displaced Tetrahedron/world)
            Transformation R_0_2;
            Displacement D;
            if (method == LARGE) {
                computeRotationLarge( R_0_2, X, index[0],index[1],index[2]);

                rotations[elementIndex].transpose(R_0_2);
                // positions of the deformed and displaced Tetrahedron in its frame
                helper::fixed_array<Coord,4> deforme;
                for(int i=0; i<4; ++i)
                    deforme[i] = R_0_2*X[index[i]];

                deforme[1][0] -= deforme[0][0];
                deforme[2][0] -= deforme[0][0];
                deforme[2][1] -= deforme[0][1];
                deforme[3] -= deforme[0];

                // displacement
                D[0] = 0;
                D[1] = 0;
                D[2] = 0;
                D[3] = _rotatedInitialElements[elementIndex][1][0] - deforme[1][0];
                D[4] = 0;
                D[5] = 0;
                D[6] = _rotatedInitialElements[elementIndex][2][0] - deforme[2][0];
                D[7] = _rotatedInitialElements[elementIndex][2][1] - deforme[2][1];
                D[8] = 0;
                D[9] = _rotatedInitialElements[elementIndex][3][0] - deforme[3][0];
                D[10] = _rotatedInitialElements[elementIndex][3][1] - deforme[3][1];
                D[11] =_rotatedInitialElements[elementIndex][3][2] - deforme[3][2];
            }
            else // POLAR / SVD
            {
                Transformation A;
                A[0] = X[index[1]]-X[index[0]];
                A[1] = X[index[2]]-X[index[0]];
                A[2] = X[index[3]]-X[index[0]];

                helper::Decompose<Real>::polarDecomposition( A, R_0_2 );


                rotations[elementIndex].transpose(R_0_2);
                // positions of the deformed and displaced Tetrahedron in its frame
                helper::fixed_array<Coord,4> deforme;
                for(int i=0; i<4; ++i)
                    deforme[i] = R_0_2*X[index[i]];

                D[0] = _rotatedInitialElements[elementIndex][0][0] - deforme[0][0];
                D[1] = _rotatedInitialElements[elementIndex][0][1] - deforme[0][1];
                D[2] = _rotatedInitialElements[elementIndex][0][2] - deforme[0][2];
                D[3] = _rotatedInitialElements[elementIndex][1][0] - deforme[1][0];
                D[4] = _rotatedInitialElements[elementIndex][1][1] - deforme[1][1];
                D[5] = _rotatedInitialElements[elementIndex][1][2] - deforme[1][2];
                D[6] = _rotatedInitialElements[elementIndex][2][0] - deforme[2][0];
                D[7] = _rotatedInitialElements[elementIndex][2][1] - deforme[2][1];
                D[8] = _rotatedInitialElements[elementIndex][2][2] - deforme[2][2];
                D[9] = _rotatedInitialElements[elementIndex][3][0] - deforme[3][0];
                D[10] = _rotatedInitialElements[elementIndex][3][1] - deforme[3][1];
                D[11] = _rotatedInitialElements[elementIndex][3][2] - deforme[3][2];
            }

            Mat44& shf = elemShapeFun[el];

            /// compute gradU
            for (size_t k = 0; k < 3; k++) {
                for (size_t l = 0; l < 3; l++)  {
                    gradU[k][l] = 0.0;
                    for (size_t m = 0; m < 4; m++)
                        gradU[k][l] += shf[l+1][m] * D[3*m+k];
                }
            }

            Mat33 strain = Real(0.5)*(gradU + gradU.transposed());

            for (size_t i = 0; i < 3; i++)
                vStrain[i] = strain[i][i];
            vStrain[3] = strain[1][2];
            vStrain[4] = strain[0][2];
            vStrain[5] = strain[0][1];
        }

        Real lambda=elemLambda[el];
        Real mu = elemMu[el];

        /// stress
        VoigtTensor s;

        Real traceStrain = 0.0;
        for (size_t k = 0; k < 3; k++) {
            traceStrain += vStrain[k];
            s[k] = vStrain[k]*2*mu;
        }

        for (size_t k = 3; k < 6; k++)
            s[k] = vStrain[k]*2*mu;

        for (size_t k = 0; k < 3; k++)
            s[k] += lambda*traceStrain;


        vME[el] = helper::rsqrt(s[0]*s[0] + s[1]*s[1] + s[2]*s[2] - s[0]*s[1] - s[1]*s[2] - s[2]*s[0] + 3*s[3]*s[3] + 3*s[4]*s[4] + 3*s[5]*s[5]);
        if (vME[el] < 1e-10)
            vME[el] = 0.0;
    }

    const VecCoord& dofs = this->mstate->read(core::ConstVecCoordId::position())->getValue();
    helper::WriteAccessor<Data<helper::vector<Real> > > vMN =  _vonMisesPerNode;

    /// compute the values of vonMises stress in nodes
    for(size_t dof = 0; dof < dofs.size(); dof++) {
        core::topology::BaseMeshTopology::TetrahedraAroundVertex tetrasAroundDOF = _mesh->getTetrahedraAroundVertex(dof);

        vMN[dof] = 0.0;
        for (size_t at = 0; at < tetrasAroundDOF.size(); at++)
            vMN[dof] += vME[tetrasAroundDOF[at]];
        if (!tetrasAroundDOF.empty())
            vMN[dof] /= Real(tetrasAroundDOF.size());
    }

    updateVonMisesStress=false;

    helper::WriteAccessor<Data<helper::vector<defaulttype::Vec4f> > > vonMisesStressColors(_vonMisesStressColors);
    vonMisesStressColors.clear();
    helper::vector<unsigned int> vonMisesStressColorsCoeff;

    Real minVM = (Real)1e20, maxVM = (Real)-1e20;

    for (size_t i = 0; i < vME.size(); i++) {
        minVM = (vME[i] < minVM) ? vME[i] : minVM;
        maxVM = (vME[i] > maxVM) ? vME[i] : maxVM;
    }

    if (maxVM < prevMaxStress)
        maxVM = prevMaxStress;

#ifdef SIMPLEFEM_COLORMAP
    maxVM*=_showStressAlpha.getValue();
    vonMisesStressColors.resize(_mesh->getNbPoints());
    vonMisesStressColorsCoeff.resize(_mesh->getNbPoints());
    std::fill(vonMisesStressColorsCoeff.begin(), vonMisesStressColorsCoeff.end(), 0);
#ifndef SOFA_NO_OPENGL
    unsigned int i = 0;
    for(it = _indexedElements->begin() ; it != _indexedElements->end() ; ++it, ++i)
    {
        helper::ColorMap::evaluator<Real> evalColor = m_VonMisesColorMap.getEvaluator(minVM, maxVM);
        defaulttype::Vec4f col = evalColor(vME[i]);
        Tetrahedron tetra = (*_indexedElements)[i];

        for(unsigned int j=0 ; j<4 ; j++)
        {
            vonMisesStressColors[tetra[j]] += (col);
            vonMisesStressColorsCoeff[tetra[j]] ++;
        }
    }

    for(unsigned int i=0 ; i<vonMisesStressColors.size() ; i++)
    {
        if(vonMisesStressColorsCoeff[i] != 0)
        {
            vonMisesStressColors[i] /= vonMisesStressColorsCoeff[i];
        }
    }
#endif
#endif
}


} // namespace forcefield

} // namespace component

} // namespace sofa

#endif // SOFA_COMPONENT_FORCEFIELD_HYPERREDUCEDTETRAHEDRONFEMFORCEFIELD_INL


//
//  Quaternion.h
//  libRealSpace
//
//  Created by Fabien Sanglard on 1/14/2014.
//  Copyright (c) 2014 Fabien Sanglard. All rights reserved.
//

#pragma once

#include "Matrix.h"

class Quaternion{
    
    public:
    
    Quaternion();
    ~Quaternion();
    
    void Multiply(Quaternion* other);
    
    Matrix ToMatrix(void);
    
    void FromMatrix(Matrix* matrix);
    
    void Conjugate(void);
    
    float DotProduct(Quaternion* other);
    
    Quaternion Slerp(Quaternion* other, float alpha);
    
    void Normalize(void);
    void GetAngles(float& pitch, float& yaw, float& roll);
    void fromEulerAngles(float pitch, float roll);
    // Builds a quaternion representing a rotation of angleRad (radians)
    // around the given axis (expected normalized, e.g. (0,1,0) for yaw).
    // Used to integrate a per-frame incremental rotation into a persistent
    // orientation quaternion via Multiply(), instead of composing/
    // re-extracting Euler angles every frame — the latter is what causes
    // gimbal lock as pitch approaches +-90 (see SCJdynPlane::updatePosition).
    void FromAxisAngle(float axisX, float axisY, float axisZ, float angleRad);
            
    private:
    
        float w;
        float x;
        float y;
        float z;
    
};

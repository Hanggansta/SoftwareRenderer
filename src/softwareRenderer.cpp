#include "softwareRenderer.h"
#include "shader.h"
#include "mesh.h"
#include "omp.h"

SoftwareRenderer::SoftwareRenderer(){}
SoftwareRenderer::~SoftwareRenderer(){}

bool SoftwareRenderer::startUp(int w, int h){
    if( !createBuffers(w, h) ){
        return false;
    }
    startUpComplete = true;
    return true;
}

void SoftwareRenderer::shutDown(){
    mCamera = nullptr;
    if (startUpComplete){
        delete zBuffer;
        delete pixelBuffer;
    }
}

void SoftwareRenderer::drawTriangularMesh(Model * currentModel){
    //Getting the vertices, faces, texture data 

    Mesh *triMesh = currentModel->getMesh();
    std::vector<Vector3i> * vIndices = &triMesh->vertexIndices;
    std::vector<Vector3i> * tIndices = &triMesh->textureIndices;
    std::vector<Vector3i> * nIndices = &triMesh->normalsIndices;
    std::vector<Vector3f> * fNormals = &triMesh->fNormals;

    std::vector<Vector3f> * vertices   = &triMesh->vertices;
    std::vector<Vector3f> * texels     = &triMesh->texels;
    std::vector<Vector3f> * normals    = &triMesh->normals;
    std::vector<Vector3f> * tangents   = &triMesh->tangents;
    std::vector<Vector3f> * biTangents = &triMesh->biTangents;
    int numFaces = triMesh->numFaces;

    //Array grouping vertices together into triangle
    Vector3f trianglePrimitive[3], normalPrim[3], uvPrim[3],
             tangentPrim[3], biTangentPrim[3];

    //Initializing shader 
    NormalMapShader shader;
    shader.albedoT = currentModel->getAlbedo();
    shader.normalT = currentModel->getNormal();
    shader.MV  = (mCamera->viewMatrix)*(*(currentModel->getModelMatrix()));
    shader.MVP = (mCamera->projectionMatrix)*shader.MV;
    shader.V   = (mCamera->viewMatrix);
    shader.M   = *(currentModel->getModelMatrix());
    shader.N   = (shader.M.inverse()).transpose(); 
    shader.cameraPos = mCamera->position;

    //Basic light direction
    float t = static_cast<float>(SDL_GetTicks());
    float radius = 1;
    float lX   = std::sin(t/4000) * radius;
    float lY   = std::cos(t/4000) * radius;
    Vector3f lightDir{1, 0, 0};
    lightDir = lightDir.normalized();

    //Building worldToObject matrix
    Matrix4 worldToObject = (*(currentModel->getModelMatrix())).inverse();

    // Iterate through every triangle
    int count = 0;

    #pragma omp parallel for private(trianglePrimitive, normalPrim, uvPrim, tangentPrim, biTangentPrim) firstprivate(shader)
    for (int j= 0; j < numFaces; ++j){
        //Current vertex and normal indices
        Vector3i f = (*vIndices)[j];
        Vector3i n = (*nIndices)[j];
        Vector3i u = (*tIndices)[j];

        //Pack vertex, normal and UV data into arrays
        buildTri(f, trianglePrimitive, *vertices);
        buildTri(n, normalPrim, *normals);
        buildTri(u, uvPrim, *texels);
        buildTri(f, tangentPrim, *tangents);
        buildTri(f, biTangentPrim, *biTangents);

        //Early quit if 
        if (backFaceCulling((*fNormals)[j], trianglePrimitive[0], worldToObject)) continue;
        ++count;
        //Apply vertex shader
        for(int i = 0; i < 3; ++i){
            trianglePrimitive[i] = shader.vertex(trianglePrimitive[i], normalPrim[i],
                                                uvPrim[i], tangentPrim[i], 
                                                biTangentPrim[i], lightDir, i);
        }

        //Skip triangles that are outside viewing frustrum
        //Does not rebuild triangles that are partially out TO DO
        if (clipTriangles(trianglePrimitive)) continue;

        perspectiveDivide(trianglePrimitive);

        //Send to rasterizer which will also call the fragment shader and write to the 
        //zbuffer and pixel buffer.
        Rasterizer::drawTriangles(trianglePrimitive, shader, pixelBuffer, zBuffer);
        
    }
    printf("%d faces drawn.\n", count);
}

void SoftwareRenderer::clearBuffers(){
    zBuffer->clear();
    pixelBuffer->clear();
}

Buffer<Uint32>* SoftwareRenderer::getRenderTarget(){
    return pixelBuffer;
}

void SoftwareRenderer::setCameraToRenderFrom(Camera * camera){
    mCamera = camera;
}

bool SoftwareRenderer::createBuffers(int w, int h){
    int pixelCount = w*h;
    bool success = true;

    zBuffer = new Buffer<float>(w, h, new float[pixelCount]);
    if( zBuffer == nullptr){
        printf("Could not build z-Buffer.\n");
        success = false;
    }
    else{
        pixelBuffer = new Buffer<Uint32>(w, h, new Uint32[pixelCount]);
        if( pixelBuffer == nullptr){
            printf("Could not build pixel Buffer.\n");
            success = false;
        }
    }
    return success;
}

void SoftwareRenderer::buildTri(Vector3i &index, Vector3f *primitive, std::vector<Vector3f> &vals){
    for(int i = 0; i < 3; ++i){
        primitive[i] = vals[index.data[i]];
    }
}

bool SoftwareRenderer::backFaceCulling(Vector3f &facetNormal, Vector3f &vert,  Matrix4 &worldToObject){
        Vector3f viewDir =  worldToObject.matMultVec(mCamera->position) -  vert;
        viewDir.normalized();

        //Returns false if the triangle cannot see the camera
        float intensity =  facetNormal.dotProduct(viewDir);
        return intensity <= 0.0;
}

bool SoftwareRenderer::clipTriangles(Vector3f *clipSpaceVertices){
    int count = 0;
    for(int i = 0; i < 3; ++i){
        Vector3f vertex = clipSpaceVertices[i];
        bool inside = (-vertex.w <= vertex.x <= vertex.w ) 
                    && (-vertex.w <= vertex.y <= vertex.w)
                    && (0 <= vertex.z <= vertex.w);
        if (!inside) ++count;
    }
    //If count equals three it means every vertex was out so we skip it
    return count == 3 ;
}

void SoftwareRenderer::perspectiveDivide(Vector3f *clippedVertices){
    for(int i = 0; i < 3; ++i){
        clippedVertices[i].perspectiveDivide();
    }
}
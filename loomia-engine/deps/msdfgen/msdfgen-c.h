#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct msEdge;
struct msContour;
struct msShape;
typedef struct msEdge msEdge;
typedef struct msContour msContour;
typedef struct msShape msShape;

MSDFGEN_PUBLIC msShape* msShapeCreate(void);
MSDFGEN_PUBLIC void msShapeDestroy(msShape* shape);
MSDFGEN_PUBLIC msContour* msShapeAddContour(msShape* shape);
MSDFGEN_PUBLIC void msShapeNormalize(msShape* cShape);
MSDFGEN_PUBLIC void msShapeOrientContours(msShape* cShape);
MSDFGEN_PUBLIC void msEdgeColoringSimple(msShape* cShape, double angleThreshold, unsigned long long seed);
MSDFGEN_PUBLIC void msContourAddLinearEdge(msContour* cContour, float x1, float y1, float x2, float y2);
MSDFGEN_PUBLIC void msContourAddQuadraticEdge(msContour* cContour, float x1, float y1, float x2, float y2, float x3, float y3);
MSDFGEN_PUBLIC void msContourAddCubicEdge(msContour* cContour, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4);
MSDFGEN_PUBLIC void msGenerateSDF(float* data, int w, int h, msShape* shape, double range, float sx, float sy, float dx, float dy);
MSDFGEN_PUBLIC void msGeneratePseudoSDF(float* data, int w, int h, msShape* shape, double range, float sx, float sy, float dx, float dy);
MSDFGEN_PUBLIC void msGenerateMSDF(float* data, int w, int h, msShape* shape, double range, float sx, float sy, float dx, float dy);
MSDFGEN_PUBLIC void msGenerateMTSDF(float* data, int w, int h, msShape* shape, double range, float sx, float sy, float dx, float dy);

#ifdef __cplusplus
}
#endif

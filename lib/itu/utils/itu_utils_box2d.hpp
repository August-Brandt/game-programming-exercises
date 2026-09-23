// utility functions for box2d shared between early exercises and later systems

#ifndef ITU_UTILS_BOX2D_HPP
#define ITU_UTILS_BOX2D_HPP

#ifndef ITU_UNITY_BUILD
#include <box2d/box2d.h>
#endif // ITU_UNITY_BUILD

void fn_box2d_wrapper_draw_polygon(b2Transform transform, const b2Vec2* vertices, int vertexCount, float radius, b2HexColor color, void* context);
void fn_box2d_wrapper_draw_circle(b2Transform transform, float radius, b2HexColor b2_color, void* context);
void fn_box2d_wrapper_draw_capsule(b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor b2_color, void* context);

#endif // ITU_UTILS_BOX2D_HPP

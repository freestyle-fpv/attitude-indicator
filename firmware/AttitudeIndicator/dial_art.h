#pragma once
#include <cstdint>
namespace instrument { namespace dialart {
struct Point { float x,y; };
bool inside(float x,float y,const Point* points,int count);
void line(uint16_t* face,Point a,Point b,float width,uint16_t color);
void lettering(uint16_t* face,const char* text,float x,float y,float size,uint16_t color);
} }

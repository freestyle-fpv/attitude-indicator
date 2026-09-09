#pragma once
namespace instrument {
// Only for finite, screen-space coordinates representable as int. Unlike the
// general libm functions these need no NaN, infinity or large-exponent paths.
inline int floorBound(float value){const int whole=static_cast<int>(value);return whole-(whole>value);}
inline int ceilBound(float value){const int whole=static_cast<int>(value);return whole+(whole<value);}
// Round halves away from zero, including already-integral large Q18 values.
// Adding 0.5 directly would incorrectly round odd integers above 2^23.
inline int roundBound(float value){
  const int whole=static_cast<int>(value);const float fraction=value-whole;
  return whole+(fraction>=.5f)-(fraction<=-.5f);
}
inline float minBound(float a,float b){return a<b?a:b;}
inline float maxBound(float a,float b){return a>b?a:b;}
}

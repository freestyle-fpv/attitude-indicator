#pragma once
namespace instrument { namespace altitude_scale {
// Shared by simulation/input and rendering so the displayed scale cannot drift
// from the accepted range. All quantities are meters.
constexpr float MaxM=3000;
constexpr float DefaultM=275;
constexpr float DemoAmplitudeM=150;
constexpr float LongRevolutionM=100;
constexpr float ShortRevolutionM=1000;
constexpr float TriangleRevolutionM=10000;
} }

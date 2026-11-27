// NeoBeat — 50 new jack swing patterns, all original, 112-126 BPM; and 16 house and techno
// patterns, 120-132 BPM (clubDefs, below).
//
// Written as step strings, one character per 16th ('|' marks the bar line):
//   .  rest      x  hit (100)     X  accent (124)     o  ghost (64)    g  soft ghost (42)
//   2 3 4  ratchet: that many hits inside the step
//   t  triplet roll over this step and the next (the next must be '.')   T  accented
// A 16-step string repeats to fill a 32-step pattern.
// Per-track nudge (percent of a step) gives the pocket: snares a hair late,
// hats a hair early.
//
// Pads: K kick, K2 kick 2, S snare, S2 snare 2, CH closed hat, OH open hat,
//       H3 hat 3, RM rim/bonk, TH thud, CL clank, JK junk, DR drop,
//       HN horn, ST stab, BL bell, FX
#pragma once

#include "Sequencer.h"
#include <string>
#include <vector>

namespace nb
{
struct Preset
{
    std::string name, family;
    float tempo = 120.0f, swing = 0.62f;
    Pattern pattern;
};

namespace presetdata
{
enum T { K, K2, S, S2, CH, OH, H3, RM, TH, CL, JK, DR, HN, ST, BL, FX };
struct Line { int track; const char* steps; int nudge; };
struct Def { const char* name; const char* family; float tempo, swing; std::vector<Line> lines; };

// clang-format off
inline const std::vector<Def>& defs()
{
    static const std::vector<Def> d = {
    // ------------------------------------------------------------ Swingbeat
    { "Swingbeat Classic", "Swingbeat", 120, 0.64f, {
        { K,  "x.....x...x..x..|x.....x.x.....x.", 0 },
        { S,  "....X.......X...|....X.......X..o", 6 },
        { CH, "xoxoxoxoxoxoxoxo|xoxoxoxoxoxot.xo", -3 },
        { OH, "..............x.|......x.........", 0 },
        { RM, "...........o....|...o.......o....", 0 } } },
    { "Pocket Talk", "Swingbeat", 118, 0.62f, {
        { K,  "x..x..x...x.....|x..x..x...x..x..", 0 },
        { S,  "....X..o....X...|....X..o....X.oo", 8 },
        { CH, "x.xox.xox.xox.xo|x.xox.xox.xot.x.", -2 },
        { OH, "......x.........|......x.......x.", 0 },
        { TH, "................|.............x.x", 0 } } },
    { "Velvet Rope", "Swingbeat", 121, 0.65f, {
        { K,  "x.x...x..x.x....|x.x...x..x...x..", 0 },
        { S,  "....X.......X...|....X.......X...", 5 },
        { S2, "............o...|............o..x", 5 },
        { CH, "xxoxxxoxxxoxxxox|xxoxxxoxxxoxt.ox", -3 },
        { H3, "....g.......g...|....g.......g...", 0 } } },
    { "Block Party Step", "Swingbeat", 122, 0.63f, {
        { K,  "x.....xx..x..x..|x.....xx..x.x...", 0 },
        { S,  "....X.......X...|....X.......X.X.", 6 },
        { CH, "xoxoxoxoxoxoxoxo", -2 },
        { OH, "..x...x...x...x.|..x...x...x.....", 0 },
        { RM, "...x.....x......|...x.....x...x..", 0 },
        { HN, "................|..............x.", 0 } } },
    { "Satin Sixteens", "Swingbeat", 119, 0.66f, {
        { K,  "x..x...xx..x....|x..x...xx..x..x.", 0 },
        { S,  "....X.......X...|....X..o....X...", 7 },
        { CH, "xgxgxgxgxgxgxgxg|xgxgxgxgxgxgt.t.", -3 },
        { OH, "..............x.", 0 },
        { DR, "................|........o.......", 0 } } },
    { "Corner Store", "Swingbeat", 120, 0.61f, {
        { K,  "x.....x..x....x.|x.....x..x..x.x.", 0 },
        { S,  "....X.......X...|....X.......X..o", 5 },
        { CH, "x.x.x.x.x.x.x.x.|x.x.x.x.x.x.t.x.", -2 },
        { H3, "gogogogogogogogo", 0 },
        { CL, "................|...........o....", 0 } } },
    { "Fade Line", "Swingbeat", 123, 0.64f, {
        { K,  "x.x..x....x..x..|x.x..x....xx..x.", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { S2, "....o.......o...|....o.......o.oo", 6 },
        { CH, "xoxoxoxoxoxot.xo|xoxoxoxot.xoxoxo", -3 },
        { OH, "......x.........|..............x.", 0 } } },
    { "Mirror Ball Strut", "Swingbeat", 124, 0.62f, {
        { K,  "x..x.x....x..x..|x..x.x....x.x..x", 0 },
        { S,  "....X.......X...|....X.......X...", 5 },
        { CH, "xoxoxoxoxoxoxoxo|xoxoxoxoxoxo4.xo", -2 },
        { OH, "..x.......x.....|..x.......x...x.", 0 },
        { RM, ".......o.......o|.......o...o...o", 0 },
        { BL, "x...............|................", 0 } } },
    { "Two Way Pager", "Swingbeat", 118, 0.65f, {
        { K,  "xx....x...x..x..|x.....x..xx.x...", 0 },
        { S,  "....X.......X...|....X.......X..o", 7 },
        { CH, "x.xox.xox.xox.xo", -3 },
        { OH, "..............x.|......x.....x...", 0 },
        { RM, "..o.....o.......|..o.....o.....o.", 0 },
        { ST, "................|............x...", 0 } } },
    { "Last Bus Home", "Swingbeat", 121, 0.63f, {
        { K,  "x.....x...x..x..|x.....x...x.x.x.", 0 },
        { K2, "................|..............x.", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { CH, "xoxoxoxoxoxoxoxo|xoxoxoxot.xoxoxo", -2 },
        { HN, "x...............|................", 0 },
        { FX, "................|........o.......", 0 } } },

    // ------------------------------------------------------------ Triplet Roll
    { "Roll Call", "Triplet Roll", 120, 0.64f, {
        { K,  "x.....x...x..x..|x.....x.x.....x.", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { CH, "x.x.x.x.t.x.x.x.|x.x.t.x.x.x.t.t.", -3 },
        { OH, "..............x.", 0 } } },
    { "Hat Waterfall", "Triplet Roll", 122, 0.62f, {
        { K,  "x..x..x...x.....|x..x..x...x..x..", 0 },
        { S,  "....X.......X...|....X.......X..o", 6 },
        { CH, "t.x.t.x.t.x.t.x.|t.x.t.x.t.x.T.t.", -2 },
        { RM, "...o.......o....", 0 } } },
    { "Snare Stutter", "Triplet Roll", 119, 0.65f, {
        { K,  "x.x...x..x.x....|x.x...x..x...x..", 0 },
        { S,  "....X.......X...|....X.......X.t.", 6 },
        { CH, "xoxoxoxoxoxoxoxo", -3 },
        { OH, "......x.........|......x.........", 0 },
        { S2, "................|..............o.", 6 } } },
    { "Thirty Second Rain", "Triplet Roll", 121, 0.63f, {
        { K,  "x.....xx..x..x..|x.....xx..x.x...", 0 },
        { S,  "....X.......X...|....X.......X...", 5 },
        { CH, "x.x3x.x.x.x3x.x.|x.x3x.x.x.x3t.x.", -2 },
        { H3, "....g.......g...", 0 } } },
    { "Rolling Chrome", "Triplet Roll", 123, 0.64f, {
        { K,  "x..x...xx..x....|x..x...xx..x..x.", 0 },
        { S,  "....X..o....X...|....X..o....X...", 7 },
        { CH, "xoxot.xoxoxot.xo|xoxot.xoxoxoT.T.", -3 },
        { OH, "..............x.|................", 0 },
        { CL, "................|..............x.", 0 } } },
    { "Kick Triplets", "Triplet Roll", 118, 0.62f, {
        { K,  "x.....x...t.x...|x.....x...t.x.t.", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { CH, "x.xox.xox.xox.xo", -2 },
        { OH, "..x...........x.", 0 } } },
    { "Rim Shot Roll", "Triplet Roll", 120, 0.65f, {
        { K,  "x.....x..x....x.|x.....x..x..x.x.", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { RM, "...o..t.....o...|...o..t.....t.t.", 0 },
        { CH, "xoxoxoxoxoxoxoxo", -3 } } },
    { "Ratchet Shine", "Triplet Roll", 124, 0.61f, {
        { K,  "x.x..x....x..x..|x.x..x....xx..x.", 0 },
        { S,  "....X.......X...|....X.......X..r", 5 },
        { CH, "x.x.x.x2x.x.x.x2|x.x.x.x2x.x.x.4.", -2 },
        { OH, "......x.........", 0 },
        { DR, "................|.......o........", 0 } } },
    { "Tom Tumble", "Triplet Roll", 119, 0.64f, {
        { K,  "x..x.x....x..x..|x..x.x....x.....", 0 },
        { S,  "....X.......X...|....X...........", 6 },
        { TH, "................|........t.t.T.t.", 0 },
        { CH, "xoxoxoxoxoxoxoxo|xoxoxoxo........", -3 } } },
    { "Double Time Rolls", "Triplet Roll", 126, 0.6f, {
        { K,  "x.....x...x..x..|x.....x...x..xx.", 0 },
        { S,  "....X.......X...|....X.......X...", 5 },
        { CH, "t.t.t.t.t.t.t.t.|t.t.t.t.t.t.T.T.", -2 },
        { OH, "..............x.", 0 },
        { HN, "................|x...............", 0 } } },

    // ------------------------------------------------------------ Go-Go Bounce
    { "Go-Go Pocket", "Go-Go Bounce", 118, 0.63f, {
        { K,  "x.....x...x.....|x.....x...x..x..", 0 },
        { S,  "....X.......X...|....X.......X...", 7 },
        { RM, "..x.x..x..x.x..x|..x.x..x..x.x.xx", 0 },
        { TH, "x..x......x..x..|x..x......x...x.", 0 },
        { CH, "x.x.x.x.x.x.x.x.", -2 } } },
    { "Cowbell Strut", "Go-Go Bounce", 120, 0.64f, {
        { K,  "x..x..x...x.....|x..x..x...x..x..", 0 },
        { S,  "....X.......X...|....X.......X..o", 6 },
        { CL, "x...x..x..x.x...|x...x..x..x.x..x", 0 },
        { RM, "...o.....o.....o|...o.....o...o.o", 0 },
        { CH, "xoxoxoxoxoxoxoxo", -3 } } },
    { "Conga Line Drift", "Go-Go Bounce", 119, 0.65f, {
        { K,  "x.....xx..x.....|x.....xx..x..x..", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { RM, "x..o..x.o.x..o..|x..o..x.o.x..ooo", 0 },
        { TH, "..x.....x...x...|..x.....x...x.x.", 0 },
        { OH, "..............x.", 0 } } },
    { "Chuck Bounce", "Go-Go Bounce", 121, 0.62f, {
        { K,  "x.x...x..x.x....|x.x...x..x...x..", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { H3, "xoxoxoxoxoxoxoxo", 0 },
        { RM, "..x...x...x...x.|..x...x...x..x.x", 0 },
        { JK, "................|...............x", 0 } } },
    { "Tambourine Sway", "Go-Go Bounce", 118, 0.66f, {
        { K,  "x..x...xx..x....|x..x...xx..x..x.", 0 },
        { S,  "....X.......X...|....X.......X...", 8 },
        { H3, "..x...x...x...x.|..x...x...x.t.x.", 0 },
        { CH, "x.x.x.x.x.x.x.x.", -2 },
        { TH, "................|..........x..x..", 0 } } },
    { "Roto Toms", "Go-Go Bounce", 122, 0.63f, {
        { K,  "x.....x..x....x.|x.....x..x..x.x.", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { TH, "...x..x....x..x.|...x..x....x.t..", 0 },
        { RM, "x.......x.......|x.......x...x...", 0 },
        { CH, "xoxoxoxoxoxoxoxo", -3 } } },
    { "Junkyard Percussion", "Go-Go Bounce", 120, 0.64f, {
        { K,  "x.....x...x..x..|x.....x.x.....x.", 0 },
        { S,  "....X.......X...|....X.......X..o", 6 },
        { JK, "...x.....x......|...x.....x...x..", 0 },
        { CL, "......x.......x.|......x.....x.x.", 0 },
        { DR, "................|........x.......", 0 },
        { CH, "x.xox.xox.xox.xo", -2 } } },
    { "Bell Pattern Bounce", "Go-Go Bounce", 119, 0.63f, {
        { K,  "x..x..x...x.....|x..x..x...x..x..", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { BL, "x.......x.......|x.......x.....x.", 0 },
        { CL, "..x..x....x..x..|..x..x....x..x.x", 0 },
        { CH, "xoxoxoxoxoxoxoxo", -3 } } },
    { "Street Drumline", "Go-Go Bounce", 123, 0.6f, {
        { K,  "x.x...x...x.x...|x.x...x...x.x.x.", 0 },
        { S,  "....X..o.o..X..o|....X..o.o..X.t.", 5 },
        { S2, "..o.......o.....|..o.......o.....", 5 },
        { TH, "................|............xxxx", 0 },
        { CH, "x.x.x.x.x.x.x.x.", -2 } } },
    { "Hand Drum Swing", "Go-Go Bounce", 118, 0.65f, {
        { K,  "x.....x...x.....|x.....x...x..x..", 0 },
        { S,  "....X.......X...|....X.......X...", 7 },
        { RM, "o.o..o.oo.o..o.o|o.o..o.oo.o..ooo", 0 },
        { TH, "x.....x.....x...|x.....x.....x.x.", 0 },
        { H3, "....g.......g...", 0 } } },

    // ------------------------------------------------------------ Hard Swing
    { "Steel Toe", "Hard Swing", 124, 0.62f, {
        { K,  "x.x..x....x..x..|x.x..x....xx..x.", 0 },
        { K2, "x...............|x.........x.....", 0 },
        { S,  "....X.......X...|....X.......X...", 5 },
        { S2, "....X.......X...|....X.......X.X.", 5 },
        { CH, "xoxoxoxoxoxoxoxo", -2 },
        { JK, "................|............x...", 0 } } },
    { "Crane Swing", "Hard Swing", 122, 0.64f, {
        { K,  "x..x..x...x..x..|x..x..x...x.x...", 0 },
        { S,  "....X.......X...|....X.......X..o", 6 },
        { S2, "....x.......x...", 6 },
        { CL, "..x.......x.....|..x.......x...x.", 0 },
        { CH, "x.xox.xox.xox.xo|x.xox.xox.xot.x.", -3 },
        { OH, "..............x.", 0 } } },
    { "Horn Section", "Hard Swing", 120, 0.63f, {
        { K,  "x.....xx..x..x..|x.....xx..x.x...", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { HN, "x.....x.........|x.....x.....x...", 0 },
        { CH, "xoxoxoxoxoxoxoxo", -2 },
        { OH, "..x...........x.", 0 } } },
    { "Brake Lights", "Hard Swing", 123, 0.61f, {
        { K,  "x..x...xx..x....|x..x...xx..x..x.", 0 },
        { S,  "....X.......X...|....X.......X...", 5 },
        { S2, "................|............X...", 5 },
        { ST, "......x.........|......x.....x.x.", 0 },
        { CH, "x.x.x.x.x.x.x.x.|x.x.x.x.x.x.t.x.", -2 },
        { K2, "................|..............x.", 0 } } },
    { "Rivet Gun", "Hard Swing", 125, 0.6f, {
        { K,  "x.....x...x..x..|x.....x...x..xx.", 0 },
        { S,  "....X.......X...|....X.......X...", 5 },
        { H3, "xxxxxxxxxxxxxxxx|xxxxxxxxxxxx4.xx", 0 },
        { CL, "...x.......x....", 0 },
        { OH, "......x.........", 0 } } },
    { "Bark And Bite", "Hard Swing", 121, 0.64f, {
        { K2, "x.....x...x..x..|x.....x.x.....x.", 0 },
        { S,  "....X.......X...|....X.......X..o", 6 },
        { S2, "....x.......x...", 6 },
        { CH, "xoxoxoxoxoxoxoxo|xoxoxoxoxoxoT.xo", -3 },
        { JK, "........x.......|........x.....x.", 0 } } },
    { "Iron Lung", "Hard Swing", 124, 0.63f, {
        { K,  "xx....x...x..x..|x.....x..xx.x...", 0 },
        { K2, "................|.........x......", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { CL, "x.......x.......|x.......x...x...", 0 },
        { CH, "x.xox.xox.xox.xo", -2 },
        { FX, "................|..............x.", 0 } } },
    { "Overpass", "Hard Swing", 122, 0.62f, {
        { K,  "x.x...x..x.x....|x.x...x..x...x..", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { S2, "........o.......|........o.....oo", 6 },
        { HN, "................|............x...", 0 },
        { BL, "x...............|........x.......", 0 },
        { CH, "xoxoxoxoxoxoxoxo", -3 } } },
    { "Pneumatic", "Hard Swing", 125, 0.61f, {
        { K,  "x..x.x....x..x..|x..x.x....x.x..x", 0 },
        { S,  "....X.......X...|....X.......X...", 5 },
        { FX, "...x............|...x.......x....", 0 },
        { H3, "x.x.x.x.x.x.x.x.|x.x.x.x.x.x.t.t.", 0 },
        { JK, "......x.......x.", 0 } } },
    { "Full Weight", "Hard Swing", 120, 0.65f, {
        { K,  "x.....x..x....x.|x.....x..x..x.x.", 0 },
        { K2, "x.....x.........|x.....x.........", 0 },
        { S,  "....X.......X...|....X.......X...", 7 },
        { S2, "....X.......X...|....X.......X...", 7 },
        { TH, "................|..........x.x.xx", 0 },
        { CH, "xoxoxoxoxoxoxoxo|xoxoxoxoxo......", -2 } } },

    // ------------------------------------------------------------ Metal Silk
    { "Chrome Slow Jam", "Metal Silk", 118, 0.66f, {
        { K,  "x.......x.x.....|x.......x.x..x..", 0 },
        { S,  "........X.......|........X.....o.", 8 },
        { CH, "xgxgxgxgxgxgxgxg|xgxgxgxgxgxgt.xg", -3 },
        { BL, "x...............|................", 0 },
        { DR, "......o.........|..............o.", 0 } } },
    { "Liquid Pager", "Metal Silk", 121, 0.63f, {
        { K,  "x.....x...x..x..|x.....x.x.....x.", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { DR, "..o.......o.....|..o.......o...o.", 0 },
        { CH, "x.x.x.x.x.x.x.x.", -2 },
        { ST, "................|..............x.", 0 } } },
    { "Bell Tower Swing", "Metal Silk", 119, 0.64f, {
        { K,  "x..x..x...x.....|x..x..x...x..x..", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { BL, "x.......x.......|x...........x...", 0 },
        { CH, "xoxoxoxoxoxoxoxo", -3 },
        { OH, "......x.......x.", 0 } } },
    { "Drill Bit Shuffle", "Metal Silk", 122, 0.62f, {
        { K,  "x.x...x..x.x....|x.x...x..x...x..", 0 },
        { S,  "....X.......X...|....X.......X..o", 6 },
        { H3, "x.x.x.x.x.x.x.x.|x.x.x.x.x.x.t.x.", 0 },
        { CL, "...........o....", 0 },
        { DR, "................|...o............", 0 } } },
    { "Train Doors Closing", "Metal Silk", 120, 0.64f, {
        { K,  "x.....xx..x..x..|x.....xx..x.x...", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { FX, "x...............|................", 0 },
        { CH, "x.xox.xox.xox.xo|x.xox.xox.xot.x.", -2 },
        { OH, "..............x.", 0 } } },
    { "Mercury Drops", "Metal Silk", 123, 0.63f, {
        { K,  "x..x...xx..x....|x..x...xx..x..x.", 0 },
        { S,  "....X.......X...|....X.......X...", 5 },
        { DR, "x.....o...x.....|x.....o...x...oo", 0 },
        { CH, "xoxoxoxoxoxoxoxo", -3 },
        { ST, "................|........x.......", 0 } } },
    { "Neon Scrap", "Metal Silk", 121, 0.62f, {
        { K,  "x.....x..x....x.|x.....x..x..x.x.", 0 },
        { S,  "....X.......X...|....X.......X..o", 6 },
        { JK, "..o.....o.....o.|..o.....o...o.o.", 0 },
        { CL, "......x.........|......x.....x...", 0 },
        { CH, "x.x.x.x.x.x.x.x.", -2 } } },
    { "Glass Elevator", "Metal Silk", 124, 0.61f, {
        { K,  "x.x..x....x..x..|x.x..x....xx..x.", 0 },
        { S,  "....X.......X...|....X.......X...", 5 },
        { BL, "................|x...............", 0 },
        { ST, "x.......x.......|x.......x.......", 0 },
        { CH, "xoxoxoxoxoxot.xo|xoxoxoxoxoxoxoxo", -2 },
        { OH, "..x.......x.....", 0 } } },
    { "Rubber Silk", "Metal Silk", 118, 0.65f, {
        { K,  "x..x.x....x..x..|x..x.x....x.x..x", 0 },
        { S,  "....X.......X...|....X.......X...", 7 },
        { RM, "...o.......o....|...o.......o..o.", 0 },
        { TH, "................|.............o.o", 0 },
        { CH, "xgxoxgxoxgxoxgxo", -3 },
        { DR, "................|......o.........", 0 } } },
    { "City Lights Out", "Metal Silk", 120, 0.64f, {
        { K,  "x.....x...x..x..|x.....x...x.....", 0 },
        { S,  "....X.......X...|....X.......X...", 6 },
        { HN, "................|............x...", 0 },
        { FX, "................|..............x.", 0 },
        { BL, "x...............|................", 0 },
        { CH, "xoxoxoxoxoxoxoxo|xoxoxoxoxoxo....", -3 } } },
    };
    return d;
}
// ------------------------------------------------------------------ House and Techno (27 Nov 2026)
// Asked for by the user to make the family more comprehensive while staying grounded in new jack
// swing. Written from docs/research/New jack swing parts and grooves.md (MidiJaQ tree), "House and
// techno invert which lane carries the motion" - craft-convention defaults, not measurements, to be
// trusted less than ears. Swing is NeoBeat's own scale, the MPC one: 0.50 straight, 0.667 triplet.
// What makes them read as their genre, whatever the sounds:
//   - the kick on every beat; the open hat on every offbeat eighth (steps 3 7 11 15), full velocity
//   - the skeleton sits on even 16ths, which swing never moves: only the 16th hats swing
//   - velocity, not timing, carries the groove in the straight styles (alternating hats)
//   - garage is the one shuffled style (0.72-0.74), its clap pushed a few ms early, beat 4 of
//     bar 2 pushed further (the "jack")
//   - Detroit: two hat layers and cross-rhythm percussion layers; the rim never on a kick
//   - no fills: a fill every 8 bars is already too many in these idioms
// The crossovers keep a techno or house skeleton and add new jack swing's own rules: the snare on
// 2 late and on 4 straight, a swung pickup kick at the tail of the bar, a triplet hat roll at the
// end of the phrase, and new jack swing's depth of swing.
inline const std::vector<Def>& clubDefs()
{
    static const std::vector<Def> d = {
    // ------------------------------------------------------------ House
    { "Chicago Jack", "House", 122, 0.52f, {
        { K,  "x...x...x...x..o|x...x...x.x.x...", 0 },
        { S,  "....x.......x...", 0 },
        { S2, "....X.......X..x", 0 },
        { CH, "xoxoxoxoxoxoxoxo", 0 },
        { OH, "..X...X...X...X.", 0 },
        { RM, ".o.....o...o.o..", 0 } } },
    { "Piston Floor", "House", 124, 0.54f, {
        { K,  "x...x...x...x...|x...x...x...x.x.", 0 },
        { S2, "....X.......X...", 0 },
        { CH, "xoxoxoxoxoxoxoxo", 0 },
        { OH, "..X...X...X...X.", 0 },
        { JK, "...o.......o....|...o.......o..o.", 0 } } },
    { "Warehouse Stab", "House", 123, 0.53f, {
        { K,  "x...x...x...x...", 0 },
        { S2, "....X.......X...", 0 },
        { CH, "x.x.x.x.x.x.x.x.", 0 },
        { H3, "gogogogogogogogo", 0 },
        { OH, "..X...X...X...X.", 0 },
        { ST, "......x.......x.|..x.......x.....", 0 } } },
    { "Garage Shuffle", "House", 122, 0.74f, {
        { K,  "x...x...x...x...", 0 },
        { K2, "................|..........x.....", 0 },
        { S2, "....X.......X...|....X...........", -4 },
        { S,  "................|............X...", -12 },
        { CH, ".g.o...g.o...o..|.o.g...o.g.o...g", 0 },
        { OH, "..x...x...x...x.", 0 },
        { FX, ".o..............|................", 0 } } },
    { "Jersey Sunday", "House", 121, 0.72f, {
        { K,  "x...x...x...x...", 0 },
        { K2, "................|..........x.....", 0 },
        { S2, "....X.......X...|....X...........", -4 },
        { S,  "................|............X...", -12 },
        { CH, ".o.g.o...o.g.o..", 0 },
        { OH, "..x...x...x...x.", 0 },
        { ST, "...x.....x......|...x.....x...x..", 0 } } },
    { "Night Bus Garage", "House", 123, 0.70f, {
        { K,  "x...x...x...x...", 0 },
        { K2, "................|..........x...x.", 0 },
        { S2, "....X.......X...|....X...........", -4 },
        { S,  "................|............X...", -12 },
        { CH, "gogo.ogogo.ogogo", 0 },
        { OH, "..x...x...x...x.", 0 },
        { DR, "................|.......o........", 0 } } },
    { "Pearl Basement", "House", 120, 0.56f, {
        { K,  "x...x...x...x...", 0 },
        { S2, "....x.......x...", 0 },
        { CH, "g.g.g.g.g.g.g.g.|g.g.g.g.g.g.g.go", 0 },
        { OH, "..X...X...X...X.", 0 },
        { RM, ".o.....o..o..o..|...o..o..o.....o", 0 },
        { BL, "................|......o.........", 0 } } },
    { "Swing House", "House", 120, 0.66f, {
        { K,  "x...x...x...x...|x...x...x...x..x", 0 },
        { S,  "....X...........", 6 },
        { S2, "............X...", 0 },
        { CH, "xoxoxoxoxoxoxoxo|xoxoxoxoxoxot.xo", -2 },
        { OH, "..X...X...X...X.", 0 },
        { RM, "...........o....|...o...........o", 0 } } },

    // ------------------------------------------------------------ Techno
    { "Motor Grid", "Techno", 124, 0.57f, {
        { K,  "x...x...x...x...", 0 },
        { S2, "....x.......x...", 0 },
        { CH, "xoxoxoxoxoxoxoxo", 0 },
        { H3, "gggggggggggggggg", 0 },
        { OH, "..x...x...x...x.", 0 },
        { CL, "x..", 0 } } },
    { "Belleville Steel", "Techno", 122, 0.58f, {
        { K,  "x...x...x...x...", 0 },
        { S,  "....x.......x...", 0 },
        { CH, "x.x.x.x.x.x.x.x.", 0 },
        { H3, "gogogogogogogogo", 0 },
        { OH, "..x...x...x...x.", 0 },
        { RM, ".o.....o..o..o..|...o..o..o.....o", 0 },
        { ST, "x.....x.....x...|..x.....x.....x.", 0 } } },
    { "Assembly Line", "Techno", 125, 0.56f, {
        { K,  "x...x...x...x...", 0 },
        { S2, "....x.......x...", 0 },
        { CH, "xoxoxoxoxoxoxoxo", 0 },
        { H3, "g.g.g.g.g.g.g.g.", 0 },
        { OH, "..x...x...x...x.", 0 },
        { BL, "..o..", 0 },
        { JK, "...o......o.....|.o......o......o", 0 } } },
    { "Clank Polymeter", "Techno", 126, 0.58f, {
        { K,  "x...x...x...x...", 0 },
        { S2, "....x.......x...", 0 },
        { CH, "xoxoxoxoxoxoxoxo", 0 },
        { OH, "..x...x...x...x.", 0 },
        { CL, "x..", 0 },
        { BL, "..o..", 0 },
        { JK, "...o...", 0 } } },
    { "Minimal Rubber", "Techno", 128, 0.55f, {
        { K,  "x...x...x...x...", 0 },
        { CH, "x.x.x.x.x.x.x.x.", 0 },
        { H3, "..g...g...g...g.", 0 },
        { OH, "..x...x...x...x.", 0 },
        { RM, "...........o....|.......o........", 0 } } },
    { "Click Floor", "Techno", 130, 0.56f, {
        { K,  "x...x...x...x...", 0 },
        { S2, "................|............x...", 0 },
        { CH, "gogogogogogogogo", 0 },
        { OH, "..x...x...x...x.", 0 },
        { FX, "................|..............o.", 0 } } },
    { "Tunnel Pressure", "Techno", 132, 0.57f, {
        { K,  "x...x...x...x...", 0 },
        { CH, "xoxoxoxoxoxoxoxo", 0 },
        { H3, "gggggggggggggggg", 0 },
        { OH, "..x...x...x...x.", 0 },
        { TH, ".o....o...o..oo.", 0 } } },
    { "Swing Techno", "Techno", 124, 0.62f, {
        { K,  "x...x...x...x...|x...x...x...x..x", 0 },
        { S,  "....X...........", 6 },
        { S2, "............X...", 0 },
        { CH, "xoxoxoxoxoxoxoxo|xoxoxoxoxoxot.xo", -2 },
        { OH, "..x...x...x...x.", 0 },
        { CL, "x..", 0 } } },
    };
    return d;
}
// clang-format on

inline void parseLine (Pattern& p, int track, const std::string& raw, int nudge)
{
    std::string s;
    for (char c : raw) if (c != '|' && c != ' ') s += c;
    if (s.empty()) return;
    for (int i = 0; i < p.length; ++i)
    {
        const char c = s[(size_t) i % s.size()];
        Step st;
        switch (c)
        {
            case 'x': st.vel = 100; break;
            case 'X': st.vel = 124; break;
            case 'o': st.vel = 64; break;
            case 'g': st.vel = 42; break;
            case '2': st.vel = 96; st.roll = 2; break;
            case '3': st.vel = 96; st.roll = 3; break;
            case '4': st.vel = 96; st.roll = 4; break;
            case 'r': st.vel = 80; st.roll = 2; break;
            case 't': st.vel = 100; st.roll = 5; break;
            case 'T': st.vel = 122; st.roll = 5; break;
            default: break;
        }
        if (st.vel > 0) st.nudge = (int8_t) nudge;
        p.steps[(size_t) track][(size_t) i] = st;
    }
}
} // namespace presetdata

namespace presetdata
{
inline std::vector<Preset> build (const std::vector<Def>& defs)
{
    std::vector<Preset> out;
    for (const auto& d : defs)
    {
        Preset p;
        p.name = d.name; p.family = d.family; p.tempo = d.tempo; p.swing = d.swing;
        p.pattern.length = 32;
        for (const auto& l : d.lines) presetdata::parseLine (p.pattern, l.track, l.steps, l.nudge);
        out.push_back (p);
    }
    return out;
}
} // namespace presetdata

// The 50 new jack swing patterns. The 78 variations in PatternBank.h are made from these alone.
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = presetdata::build (presetdata::defs());
    return list;
}

// The house and techno patterns, after the 128: numbered from 129, so no existing number moves.
inline const std::vector<Preset>& clubPresets()
{
    static const std::vector<Preset> list = presetdata::build (presetdata::clubDefs());
    return list;
}

constexpr int kNumPresets = 50;
} // namespace nb

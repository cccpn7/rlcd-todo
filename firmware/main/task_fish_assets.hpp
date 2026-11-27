#pragma once
// Original task fish shared with the approved web prototype.
namespace tank {
inline constexpr const char *TaskFish0[]={
"..........##.........",
".........#ww#........",
"#.......#wwww#.......",
".##....#wwwwww###....",
".#w##.#wwwwwwwwww#...",
"..#ww##ww#ww#wwww##..",
"..#ww#wwwwwwwwwwww#..",
".#w##.##wwwwwwwww#...",
".##.....#########....",
"#....................",
"....................."
};
inline constexpr const char *TaskFish1[]={
"........#####.....",
".......#www#w#....",
".......#ww#www#...",
"......#ww#wwww#...",
"#....#ww#www#ww#..",
"#####ww#www#www##.",
"#wwwww#www#wwwwww#",
"#wwww#www#wwwwwww#",
"#w###www#www#www#.",
"##...#w#www#www#..",
"#.....#www#####...",
".......####.......",
".................."
};
inline constexpr const char *TaskFish2[]={
".................",
"........#........",
".......#w#.......",
".......####......",
".......###w#.....",
"......#w##ww#....",
"#....#ww##www#...",
"#####www##wwww#..",
"#wwwwwww##wwww##.",
"#www#www##wwww##.",
"####.#ww##www#...",
"#.....#w##ww#....",
".......###w#.....",
".........#w#.....",
".........##......",
".........##......",
"................."
};
inline constexpr const char *TaskFish3[]={
"........#..........",
".......#w#.........",
"#.....#www####.....",
"##...##www##ww#....",
"#w#####www##www##..",
"#wwww##www##wwww##.",
"#wwww##www##wwwwww#",
"#www###www##wwwww#.",
"#w##.##www##wwww#..",
"##....######w###...",
"#.........#w#......",
"...........#......."
};
inline constexpr const char *TaskFish4[]={
".......................",
".......................",
"##.........####........",
".#####..###w#ww####....",
"..#www############w###.",
"..#www##wwww#wwwwwwww#.",
".#####..###w#wwww####..",
"##.........##ww##......",
".............##........",
".............#........."
};
inline constexpr const char *TaskFish5[]={
"..........#........",
"#........#w#.......",
"##......#www#......",
"#w##...##www###....",
"#www#.#wwwwwwww#...",
"#wwww#wwwwwwwwww##.",
"#wwwwwww#www#wwww#.",
"#wwww#wwwwwwwwww#..",
"#www#.##wwwwww##...",
"#w##....###ww#.....",
"##.........##......",
"#..................",
"..................."
};
struct Sprite { const char *const *rows; int w,h; };
inline constexpr Sprite TaskFishArt[]={
{TaskFish0,21,11},
{TaskFish1,18,13},
{TaskFish2,17,17},
{TaskFish3,19,12},
{TaskFish4,23,10},
{TaskFish5,19,13}
};
}

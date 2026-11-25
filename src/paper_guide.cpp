#include "paper_guide.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace mdsp {
namespace {

constexpr double pi = std::numbers::pi_v<double>;
constexpr double take_end = 1.35;
constexpr double turn_end = 3.15;
constexpr double output_x = 65;
constexpr double center_x = 175;
constexpr double input_x = 285;
constexpr double tray_y = 54;

struct Point {
    double x;
    double y;
};

struct Drawing {
    HDC dc;
    double scale;
    double left;
    double top;

    POINT point(Point value) const
    {
        return {std::lround(left + value.x * scale), std::lround(top + value.y * scale)};
    }

    template <std::size_t count>
    void shape(const std::array<Point, count>& vertices, COLORREF fill, COLORREF outline) const
    {
        std::array<POINT, count> pixels{};
        std::ranges::transform(vertices, pixels.begin(), [this](Point p) { return point(p); });
        SetDCBrushColor(dc, fill);
        SetDCPenColor(dc, outline);
        Polygon(dc, pixels.data(), static_cast<int>(count));
    }

    void label(double x, const wchar_t* text) const
    {
        const auto start = point({x - 64, 96});
        const auto end = point({x + 64, 121});
        RECT area{start.x, start.y, end.x, end.y};
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        DrawTextW(dc, text, -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
};

double ease(double value)
{
    const double t = std::clamp(value, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

void draw_tray(const Drawing& draw, double x)
{
    const auto fill = GetSysColor(COLOR_WINDOW);
    const auto edge = GetSysColor(COLOR_3DSHADOW);
    draw.shape(std::array{Point{x - 43, 24}, Point{x + 43, 24}, Point{x + 43, 83},
                          Point{x - 43, 83}}, fill, edge);
    draw.shape(std::array{Point{x - 43, 77}, Point{x - 17, 77}, Point{x - 11, 72}, Point{x + 11, 72},
                          Point{x + 17, 77}, Point{x + 43, 77}, Point{x + 43, 85}, Point{x - 43, 85}},
               GetSysColor(COLOR_3DFACE), edge);
}

void draw_stack(const Drawing& draw, Point center, double angle)
{
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const auto transform = [&](double x, double y) {
        return Point{center.x + x * cosine - y * sine, center.y + x * sine + y * cosine};
    };
    const auto rectangle = [&](double left, double top, double right, double bottom,
                               COLORREF fill, COLORREF outline) {
        draw.shape(std::array{transform(left, top), transform(right, top),
                              transform(right, bottom), transform(left, bottom)}, fill, outline);
    };
    const auto edge = GetSysColor(COLOR_3DSHADOW);
    const auto paper = GetSysColor(COLOR_WINDOW);
    const auto ink = GetSysColor(COLOR_WINDOWTEXT);
    const auto mark = GetSysColor(COLOR_HIGHLIGHT);
    rectangle(-16, -22, 22, 27, paper, edge);
    rectangle(-18, -24, 20, 25, paper, edge);
    rectangle(-20, -26, 18, 23, paper, edge);
    rectangle(-15, -20, 13, -15, mark, mark);
    rectangle(-15, -8, 8, -7, ink, ink);
    rectangle(-15, -2, 12, -1, edge, edge);
    rectangle(-15, 4, 5, 5, edge, edge);
}

void draw_turn_arrow(const Drawing& draw)
{
    constexpr double radius = 34;
    constexpr Point center{center_x, 52};
    const auto position = [&](double angle) {
        return Point{center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
    };
    constexpr double start = -0.95 * pi;
    constexpr double end = 0.54 * pi;
    std::array<POINT, 25> arc{};
    for (std::size_t index = 0; index < arc.size(); ++index) {
        const double angle = std::lerp(start, end, static_cast<double>(index) / static_cast<double>(arc.size() - 1));
        arc[index] = draw.point(position(angle));
    }
    const auto mark = GetSysColor(COLOR_HIGHLIGHT);
    SetDCPenColor(draw.dc, mark);
    Polyline(draw.dc, arc.data(), static_cast<int>(arc.size()));
    const Point tip = position(end);
    const Point base{tip.x + std::sin(end) * 9, tip.y - std::cos(end) * 9};
    draw.shape(std::array{tip, Point{base.x + std::cos(end) * 5, base.y + std::sin(end) * 5},
                          Point{base.x - std::cos(end) * 5, base.y - std::sin(end) * 5}}, mark, mark);
}

} // namespace

const wchar_t* paper_guide_caption(std::optional<double> seconds)
{
    if (!seconds) {
        return L"取出纸叠 → 顺时针旋转 180° → 放回";
    }
    if (*seconds < take_end) {
        return L"1. 从出纸托盘取出整叠纸";
    }
    if (*seconds < turn_end) {
        return L"2. 保持印刷面朝向，顺时针旋转 180°";
    }
    return L"3. 将纸叠放回进纸盘";
}

void draw_paper_guide(HDC dc, const RECT& bounds, std::optional<double> seconds)
{
    FillRect(dc, &bounds, GetSysColorBrush(COLOR_3DFACE));
    const double scale = std::min((bounds.right - bounds.left) / 350.0, (bounds.bottom - bounds.top) / 128.0);
    if (scale <= 0) {
        return;
    }
    const Drawing draw{dc, scale, bounds.left + (bounds.right - bounds.left - 350 * scale) / 2,
                      bounds.top + (bounds.bottom - bounds.top - 128 * scale) / 2};
    const int state = SaveDC(dc);
    if (state == 0) {
        return;
    }
    SelectObject(dc, GetStockObject(DC_PEN));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetBkMode(dc, TRANSPARENT);

    draw_tray(draw, output_x);
    draw_tray(draw, input_x);
    draw.label(output_x, L"出纸托盘");
    draw.label(input_x, L"进纸盘");
    draw_turn_arrow(draw);
    if (!seconds) {
        draw_stack(draw, {output_x, tray_y}, 0);
        draw_stack(draw, {input_x, tray_y}, pi);
    } else {
        const double lift = ease((*seconds - 0.42) / (take_end - 0.42));
        const double turn = ease((*seconds - take_end) / (turn_end - take_end));
        const double place = ease((*seconds - turn_end) / (4.15 - turn_end));
        const double x = std::lerp(output_x, center_x, lift) + (input_x - center_x) * place;
        draw_stack(draw, {x, tray_y - 7 * (lift - place)}, pi * turn);
    }
    RestoreDC(dc, state);
}

} // namespace mdsp

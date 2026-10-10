#include "PedalAnimator.h"

#include <cstdio>

namespace
{
    // 8x9 增广矩阵求解（部分主元高斯消元）
    bool solveLinear8 (double m[8][9], double out[8])
    {
        for (int col = 0; col < 8; ++col)
        {
            int piv = col;
            for (int r = col + 1; r < 8; ++r)
                if (std::abs (m[r][col]) > std::abs (m[piv][col]))
                    piv = r;

            if (std::abs (m[piv][col]) < 1e-12)
                return false;

            if (piv != col)
                for (int c = col; c < 9; ++c)
                    std::swap (m[col][c], m[piv][c]);

            const double d = m[col][col];
            for (int c = col; c < 9; ++c)
                m[col][c] /= d;

            for (int r = 0; r < 8; ++r)
            {
                if (r == col) continue;
                const double f = m[r][col];
                if (f == 0.0) continue;
                for (int c = col; c < 9; ++c)
                    m[r][c] -= f * m[col][c];
            }
        }

        for (int i = 0; i < 8; ++i)
            out[i] = m[i][8];
        return true;
    }

    // 4 点单应矩阵：src -> dst
    bool homography (const double src[4][2], const double dst[4][2], double H[3][3])
    {
        double m[8][9] {};
        for (int i = 0; i < 4; ++i)
        {
            const double x = src[i][0], y = src[i][1];
            const double u = dst[i][0], v = dst[i][1];
            double* r0 = m[2 * i];
            double* r1 = m[2 * i + 1];
            r0[0] = x;    r0[1] = y;    r0[2] = 1.0;
            r0[6] = -u * x; r0[7] = -u * y; r0[8] = u;
            r1[3] = x;    r1[4] = y;    r1[5] = 1.0;
            r1[6] = -v * x; r1[7] = -v * y; r1[8] = v;
        }

        double h[8];
        if (! solveLinear8 (m, h))
            return false;

        H[0][0] = h[0]; H[0][1] = h[1]; H[0][2] = h[2];
        H[1][0] = h[3]; H[1][1] = h[4]; H[1][2] = h[5];
        H[2][0] = h[6]; H[2][1] = h[7]; H[2][2] = 1.0;
        return true;
    }
}

void PedalAnimator::currentQuad (float force01, juce::Point<float> outQuad[4]) const
{
    const float f = juce::jlimit (0.0f, 1.0f, force01);
    for (int i = 0; i < 4; ++i)
        outQuad[i] = { juce::jmap (f, cal.restQuad[i][0], cal.fullQuad[i][0]),
                       juce::jmap (f, cal.restQuad[i][1], cal.fullQuad[i][1]) };
}

void PedalAnimator::render (float force01, juce::Image& outOverlay, juce::Point<int>& outPos)
{
    outOverlay = {};
    if (sprite.isNull())
        return;

    const float f = juce::jlimit (0.0f, 1.0f, force01);

    // 当前姿态四角 = REST 与 FULL 四角线性插值
    juce::Point<float> qf[4];
    currentQuad (f, qf);

    double cur[4][2];
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (int i = 0; i < 4; ++i)
    {
        cur[i][0] = (double) qf[i].x;
        cur[i][1] = (double) qf[i].y;
        minX = juce::jmin (minX, cur[i][0]); maxX = juce::jmax (maxX, cur[i][0]);
        minY = juce::jmin (minY, cur[i][1]); maxY = juce::jmax (maxY, cur[i][1]);
    }

    const int bx0 = (int) std::floor (minX) - 2;
    const int by0 = (int) std::floor (minY) - 2;
    const int bw  = (int) std::ceil (maxX) + 3 - bx0;
    const int bh  = (int) std::ceil (maxY) + 3 - by0;
    if (bw <= 0 || bh <= 0)
        return;

    // H：精灵局部坐标（rest 四角） -> overlay 局部坐标（cur 四角）
    double srcLocal[4][2], dstLocal[4][2];
    for (int i = 0; i < 4; ++i)
    {
        srcLocal[i][0] = (double) cal.restQuad[i][0] - cal.spriteX;
        srcLocal[i][1] = (double) cal.restQuad[i][1] - cal.spriteY;
        dstLocal[i][0] = cur[i][0] - bx0;
        dstLocal[i][1] = cur[i][1] - by0;
    }

    double H[3][3];
    if (! homography (srcLocal, dstLocal, H))
        return;

#ifdef BREAK_DEBUG_RENDER
    std::printf ("H = [%.4f %.4f %.4f; %.4f %.4f %.4f; %.4f %.4f %.4f]\n",
                 H[0][0], H[0][1], H[0][2], H[1][0], H[1][1], H[1][2],
                 H[2][0], H[2][1], H[2][2]);
#endif

    // Hi = H^-1：overlay 像素 -> 精灵像素（逆映射采样）
    const double det = H[0][0] * (H[1][1] * H[2][2] - H[1][2] * H[2][1])
                     - H[0][1] * (H[1][0] * H[2][2] - H[1][2] * H[2][0])
                     + H[0][2] * (H[1][0] * H[2][1] - H[1][1] * H[2][0]);
    if (std::abs (det) < 1e-12)
        return;

    double Hi[3][3];
    Hi[0][0] =  (H[1][1] * H[2][2] - H[1][2] * H[2][1]) / det;
    Hi[0][1] = -(H[0][1] * H[2][2] - H[0][2] * H[2][1]) / det;
    Hi[0][2] =  (H[0][1] * H[1][2] - H[0][2] * H[1][1]) / det;
    Hi[1][0] = -(H[1][0] * H[2][2] - H[1][2] * H[2][0]) / det;
    Hi[1][1] =  (H[0][0] * H[2][2] - H[0][2] * H[2][0]) / det;
    Hi[1][2] = -(H[0][0] * H[1][2] - H[0][2] * H[1][0]) / det;
    Hi[2][0] =  (H[1][0] * H[2][1] - H[1][1] * H[2][0]) / det;
    Hi[2][1] = -(H[0][0] * H[2][1] - H[0][1] * H[2][0]) / det;
    Hi[2][2] =  (H[0][0] * H[1][1] - H[0][1] * H[1][0]) / det;

    juce::Image out (juce::Image::ARGB, bw, bh, true);
    {
        // 踩下暗化：预乘 ARGB 只缩 RGB 即等比变暗（alpha 不动，预乘语义保持）
        const float shadeMul = 1.0f - darkening * f;

        juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);
        juce::Image::BitmapData srcDat (sprite, juce::Image::BitmapData::readOnly);
        const int sw = sprite.getWidth();
        const int sh = sprite.getHeight();

        for (int py = 0; py < bh; ++py)
        {
            auto* dstRow = reinterpret_cast<juce::PixelARGB*> (dst.data + (size_t) py * dst.lineStride);

            for (int px = 0; px < bw; ++px)
            {
                const double x = (double) px, y = (double) py;
                const double den = Hi[2][0] * x + Hi[2][1] * y + Hi[2][2];
                if (std::abs (den) < 1e-9)
                    continue;

                const double sx = (Hi[0][0] * x + Hi[0][1] * y + Hi[0][2]) / den;
                const double sy = (Hi[1][0] * x + Hi[1][1] * y + Hi[1][2]) / den;

                if (sx < -0.5 || sy < -0.5 || sx > sw - 0.5 || sy > sh - 0.5)
                    continue;   // 落在精灵外，保持透明

#ifdef BREAK_DEBUG_RENDER
                static std::atomic<int> n { 0 };
                if (n.fetch_add (1) < 3)
                    std::printf ("sample px=(%d,%d) -> sprite=(%.2f,%.2f)\n", px, py, sx, sy);
#endif

                const int x0 = juce::jlimit (0, sw - 1, (int) std::floor (sx));
                const int y0 = juce::jlimit (0, sh - 1, (int) std::floor (sy));
                const int x1 = juce::jmin (x0 + 1, sw - 1);
                const int y1 = juce::jmin (y0 + 1, sh - 1);
                const float fx = (float) (sx - std::floor (sx));
                const float fy = (float) (sy - std::floor (sy));

                // 预乘 ARGB 直接双线性插值（插值结果仍是合法预乘值）
                const auto* p00 = reinterpret_cast<const juce::PixelARGB*> (srcDat.data + (size_t) y0 * srcDat.lineStride) + x0;
                const auto* p01 = reinterpret_cast<const juce::PixelARGB*> (srcDat.data + (size_t) y0 * srcDat.lineStride) + x1;
                const auto* p10 = reinterpret_cast<const juce::PixelARGB*> (srcDat.data + (size_t) y1 * srcDat.lineStride) + x0;
                const auto* p11 = reinterpret_cast<const juce::PixelARGB*> (srcDat.data + (size_t) y1 * srcDat.lineStride) + x1;

                auto chan = [fx, fy] (float a, float b, float c, float d)
                {
                    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
                };

                const float a = chan (p00->getAlpha(), p01->getAlpha(), p10->getAlpha(), p11->getAlpha());
                if (a <= 0.5f)
                    continue;   // 近似透明，跳过写入

                const float r  = chan (p00->getRed(),   p01->getRed(),   p10->getRed(),   p11->getRed()) * shadeMul;
                const float gg = chan (p00->getGreen(), p01->getGreen(), p10->getGreen(), p11->getGreen()) * shadeMul;
                const float b  = chan (p00->getBlue(),  p01->getBlue(),  p10->getBlue(),  p11->getBlue()) * shadeMul;

                // 双线性插值预乘 ARGB（结果仍是合法预乘值，无需再 premultiply）
                juce::PixelARGB outPx ((juce::uint8) juce::jlimit (0.0f, 255.0f, a),
                                       (juce::uint8) juce::jlimit (0.0f, 255.0f, r),
                                       (juce::uint8) juce::jlimit (0.0f, 255.0f, gg),
                                       (juce::uint8) juce::jlimit (0.0f, 255.0f, b));
                dstRow[px] = outPx;
            }
        }
    }

    outPos = { bx0, by0 };
    outOverlay = std::move (out);
}

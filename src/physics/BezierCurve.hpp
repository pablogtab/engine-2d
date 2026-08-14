#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

namespace Engine {

enum class EasingPreset {
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutCubic,
    EaseOutElastic,
    EaseOutBounce,
    CustomBezier
};

class BezierCurve {
public:
    EasingPreset preset{EasingPreset::Linear};
    std::vector<glm::vec2> control_points; // N Control Points: P0, P1, P2, ..., Pn

    BezierCurve() {
        // Default 2-point linear: (0,0) -> (1,1)
        control_points = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    }

    explicit BezierCurve(EasingPreset p) : preset(p) {
        switch (p) {
            case EasingPreset::Linear:
                control_points = {{0.0f, 0.0f}, {1.0f, 1.0f}};
                break;
            case EasingPreset::EaseInQuad:
                // Cubic approximation: P1=(0.11, 0.0), P2=(0.5, 0.0)
                control_points = {{0.0f, 0.0f}, {0.11f, 0.0f}, {0.5f, 0.0f}, {1.0f, 1.0f}};
                break;
            case EasingPreset::EaseOutQuad:
                // Cubic approximation: P1=(0.25, 1.0), P2=(0.5, 1.0)
                control_points = {{0.0f, 0.0f}, {0.25f, 1.0f}, {0.5f, 1.0f}, {1.0f, 1.0f}};
                break;
            case EasingPreset::EaseInOutCubic:
                // Cubic Adobe Premiere curve: P1=(0.65, 0.0), P2=(0.35, 1.0)
                control_points = {{0.0f, 0.0f}, {0.65f, 0.0f}, {0.35f, 1.0f}, {1.0f, 1.0f}};
                break;
            case EasingPreset::EaseOutElastic:
                // 5-Point Elastic overshoot spline: P0(0,0), P1(0.2, 1.4), P2(0.4, 0.8), P3(0.7, 1.1), P4(1,1)
                control_points = {{0.0f, 0.0f}, {0.2f, 1.4f}, {0.4f, 0.8f}, {0.7f, 1.1f}, {1.0f, 1.0f}};
                break;
            case EasingPreset::EaseOutBounce:
                // 5-Point Bounce spline: P0(0,0), P1(0.3, 1.3), P2(0.6, 0.7), P3(0.85, 1.05), P4(1,1)
                control_points = {{0.0f, 0.0f}, {0.3f, 1.3f}, {0.6f, 0.7f}, {0.85f, 1.05f}, {1.0f, 1.0f}};
                break;
            default:
                control_points = {{0.0f, 0.0f}, {1.0f, 1.0f}};
                break;
        }
    }

    // Static Builder for Standard Adobe Premiere 4-Control-Point Cubic Bezier Curve (CSS cubic-bezier)
    static BezierCurve Cubic(const glm::vec2& p1, const glm::vec2& p2) {
        BezierCurve curve;
        curve.preset = EasingPreset::CustomBezier;
        curve.control_points = {{0.0f, 0.0f}, p1, p2, {1.0f, 1.0f}};
        return curve;
    }

    // Static Builder for General N-Control-Point Bezier Splines (P0, P1, P2, ..., Pn)
    static BezierCurve MultiPoint(const std::vector<glm::vec2>& points) {
        BezierCurve curve;
        curve.preset = EasingPreset::CustomBezier;
        curve.control_points = points;
        if (curve.control_points.empty()) {
            curve.control_points = {{0.0f, 0.0f}, {1.0f, 1.0f}};
        }
        return curve;
    }

    // De Casteljau's Algorithm for arbitrary N-Point Bezier Curve Evaluation at parameter u in [0, 1]
    glm::vec2 evaluate_point(float u) const {
        if (control_points.empty()) return {u, u};
        if (control_points.size() == 1) return control_points[0];

        u = std::clamp(u, 0.0f, 1.0f);
        std::vector<glm::vec2> temp = control_points;
        size_t n = temp.size();

        for (size_t k = 1; k < n; ++k) {
            for (size_t i = 0; i < n - k; ++i) {
                temp[i] = (1.0f - u) * temp[i] + u * temp[i + 1];
            }
        }
        return temp[0];
    }

    // Evaluates curve value Y for a given normalized time t in [0, 1]
    float evaluate(float t) const {
        t = std::clamp(t, 0.0f, 1.0f);

        if (preset == EasingPreset::Linear) {
            return t;
        }

        // For Bezier curves, evaluateDeCasteljau computes parameter u -> (X(u), Y(u))
        // Binary search to find parameter u such that X(u) == t, returning Y(u)
        float low = 0.0f;
        float high = 1.0f;
        float u = t;

        for (int i = 0; i < 10; ++i) {
            glm::vec2 pt = evaluate_point(u);
            float err = pt.x - t;
            if (std::abs(err) < 0.001f) break;
            if (err > 0.0f) {
                high = u;
            } else {
                low = u;
            }
            u = (low + high) * 0.5f;
        }

        return evaluate_point(u).y;
    }
};

} // namespace Engine

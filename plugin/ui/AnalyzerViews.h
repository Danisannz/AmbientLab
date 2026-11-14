#pragma once
#include "AnalyzerView.h"

namespace ambient::ui
{
std::unique_ptr<AnalyzerView> createScopeView(viz::VisualizerHub&, UiSettings&);
std::unique_ptr<AnalyzerView> createStereoView(viz::VisualizerHub&, UiSettings&);
std::unique_ptr<AnalyzerView> createSpectrumView(viz::VisualizerHub&, UiSettings&);
} // namespace ambient::ui

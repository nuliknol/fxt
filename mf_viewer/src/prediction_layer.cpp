/*
 * prediction_layer.cpp - HMM Prediction Visualization Layer Implementation
 */

#include "prediction_layer.h"

PredictionLayer::PredictionLayer()
    : _startIndex(-1),
      _visible(true)
{
}

void PredictionLayer::setPredictions(int startIndex,
                                     const std::vector<PredictedCandle> &predictions)
{
    _startIndex = startIndex;
    _predictions = predictions;
}

void PredictionLayer::clear()
{
    _startIndex = -1;
    _predictions.clear();
}

bool PredictionLayer::hasPredictions() const
{
    return !_predictions.empty() && _startIndex >= 0;
}

bool PredictionLayer::hasPredictionAt(int index) const
{
    if (_startIndex < 0 || _predictions.empty()) {
        return false;
    }
    int localIndex = index - _startIndex;
    return localIndex >= 0 && localIndex < static_cast<int>(_predictions.size());
}

PredictedCandle PredictionLayer::getPredictionAt(int index) const
{
    if (!hasPredictionAt(index)) {
        return PredictedCandle{0, 0, 0, 0, 0.0};
    }
    return _predictions[index - _startIndex];
}

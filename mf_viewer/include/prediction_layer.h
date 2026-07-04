/*
 * prediction_layer.h - HMM Prediction Visualization Layer
 *
 * Stores and provides access to HMM-predicted candlestick data
 * for overlay rendering on the chart.
 */

#ifndef PREDICTION_LAYER_H
#define PREDICTION_LAYER_H

#include <vector>
#include <cstdint>

/* Predicted candlestick difference values */
struct PredictedCandle {
    int32_t high_open_diff;     /* Predicted high - open */
    int32_t close_open_diff;    /* Predicted close - open */
    int32_t low_close_diff;     /* Predicted low - close */
    int32_t high_low_diff;      /* Predicted high - low */
    double  confidence;         /* Prediction confidence (0.0 - 1.0) */
};

class PredictionLayer {
public:
    PredictionLayer();

    /* Set predictions starting at a given candle index */
    void setPredictions(int startIndex, const std::vector<PredictedCandle> &predictions);

    /* Clear all predictions */
    void clear();

    /* Check if there are any predictions */
    bool hasPredictions() const;

    /* Check if there's a prediction at a specific index */
    bool hasPredictionAt(int index) const;

    /* Get prediction at a specific index (returns default if not found) */
    PredictedCandle getPredictionAt(int index) const;

    /* Get the range of predictions */
    int getStartIndex() const { return _startIndex; }
    int getEndIndex() const { return _startIndex + static_cast<int>(_predictions.size()); }
    int getCount() const { return static_cast<int>(_predictions.size()); }

    /* Visibility control */
    void setVisible(bool visible) { _visible = visible; }
    bool isVisible() const { return _visible; }

private:
    int _startIndex;
    std::vector<PredictedCandle> _predictions;
    bool _visible;
};

#endif /* PREDICTION_LAYER_H */

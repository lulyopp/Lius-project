#ifndef TUNINGENGINE_H
#define TUNINGENGINE_H

#include "MassSpecData.h"

class TuningEngine {
public:
    // ----------仪器参数调整
    // 数据预处理
    QVector<double> smoothData(const QVector<double>& yData);

    // 寻峰与计算
    double calculatePeakWidth(const QVector<double>& xData, const QVector<double>& yData, int peakIndex, double thresholdRatio);
    PeakResult analyzeSinglePeak(const QVector<double>& xData, const QVector<double>& yData, double targetMz,
                                 double window, double thresholdRatio, double minHeight,
                                 double stretchLower, double stretchUpper, double distancePenaltyWeight);
    QVector<PeakResult> analyzeMultiplePeaks(const QVector<double>& xData, const QVector<double>& yData,
                                             const QList<double>& targetMasses,
                                             double maxWindow, double thresholdRatio, double minHeight,
                                             double stretchLower, double stretchUpper, double distancePenaltyWeight);
    // 控制反馈公式 (返回调整后的新值)
    double calculateRSD(const QVector<double>& values);
    double adjustGain(const QVector<PeakResult>& results, double currentGain, double& gainStep,
                      int& currentDir, double rsdTarget, bool& isDone);
    double adjustOffset(const QVector<PeakResult>& results, double currentOffset,
                        double& offsetStep, int& currentDir,
                        double lowerBound, double upperBound, bool& isDone);
    double adjustPosition(double currentPosition, double actualMz, double targetMz);
    // 计算趋势线
    TrendlineResult calculateTrendline(const QVector<double>& targetMzList, const QVector<double>& actualPositions);

    // ----------电参数调整
    double calculateSingleIntensityR2(double current, double dest); //计算非整体偏置R2
    double calculateGlobalIntensityR2(const QVector<double>& currents, const QVector<double>& dests);   //整体偏置R2
    int checkGlobalBiasDirection(const QVector<double>& currents, const QVector<double>& dests);        //判断整体 or 非整体偏置

private:
    //DFS搜索方法
    void dfsMatch(int tIdx, int lastPIdx, QVector<int> &currentPath, double currentScore,
                  int &bestInliers, double &bestScore, QVector<int> &bestPath,
                  const QVector<PeakCandidate> &actualPeaks, const QList<double> &targets,
                  double maxWindow, double stretchLower, double stretchUpper, double distancePenaltyWeight);
    //动态基线计算与扣除
    QVector<double> calculateDynamicBaseline(const QVector<double>& yData, int windowSize = 30);
    QVector<double> removeBaseline(const QVector<double>& yData, const QVector<double>& baseline);
    //噪声估计
    double estimateNoise(const QVector<double>& data);
};
#endif // TUNINGENGINE_H

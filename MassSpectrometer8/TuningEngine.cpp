#include "TuningEngine.h"
#include <cmath>
#include <QList>
#include <QDebug>

//数据平滑
QVector<double> TuningEngine::smoothData(const QVector<double>& yData) {
    int n = yData.size();
    QVector<double> smoothed(n, 0.0);
    if (n < 5) return yData;
    for (int i = 0; i < 2; ++i) smoothed[i] = yData[i];
    for (int i = n - 2; i < n; ++i) smoothed[i] = yData[i];
    for (int i = 2; i < n - 2; ++i) {
        smoothed[i] = (yData[i-2] + 2*yData[i-1] + 4*yData[i] + 2*yData[i+1] + yData[i+2]) / 10.0;
    }
    return smoothed;
}
// 根据目标出峰位置寻峰
// 当未找齐峰时，判断是否前后端峰位拉伸距离过大，stretch = (actualMz - prevActualMz) / (targetMz - prevTargetMz)是否在形变率范围内，可适当调大形变率上下限
// 当峰位被错误配准时，可尝试调整距离惩罚权重系数
// 单峰分析
PeakResult TuningEngine::analyzeSinglePeak(const QVector<double>& xData, const QVector<double>& yData, double targetMz,
                                           double window, double thresholdRatio, double minHeight,
                                           double stretchLower, double stretchUpper, double distancePenaltyWeight){
    PeakResult res = {false, targetMz, 0.0, 0.0, 0.0};
    if (xData.isEmpty()) return res;
    // 直接调用最新的动态基线 DFS 算法，传入单一目标 targetMz
    QVector<PeakResult> results = analyzeMultiplePeaks(xData, yData, QList<double>{targetMz},
                                                       window, thresholdRatio, minHeight,
                                                       stretchLower, stretchUpper, distancePenaltyWeight);

    // 返回 DFS 算法得出的统一结果
    if (!results.isEmpty()) {
        return results.first();
    }

    // 异常保底返回
    return {false, targetMz, 0.0, 0.0, 0.0};
}
//多峰分析
QVector<PeakResult> TuningEngine::analyzeMultiplePeaks(const QVector<double> &xData, const QVector<double> &yData,
                                                       const QList<double> &targetMasses,
                                                       double maxWindow, double thresholdRatio, double minHeight,
                                                       double stretchLower, double stretchUpper, double distancePenaltyWeight)
{
    int tCount = targetMasses.size();
    QVector<PeakResult> finalResults(tCount);
    for(int i=0; i<tCount; ++i) finalResults[i] = {false, targetMasses[i], 0.0, 0.0, 0.0};

    if (xData.size() < 3 || tCount == 0) return finalResults;
    // =======================================================
    // 生成并扣除动态基线
    // 根据当前 xData 的物理范围和点数，动态计算窗口大小
    // 强制要求基线滑动窗口在任何分辨率下，都要跨越至少 3.0 AMU！
    // =======================================================
    int windowPoints = 30; // 默认保底值
    if (xData.size() > 2) {
        double span = xData.last() - xData.first();
        if (span > 0) {
            // 算出 3.0 AMU 物理宽度对应多少个数组索引点
            windowPoints = static_cast<int>((3.0 / span) * xData.size());
        }
    }
    // 防止计算出极端值：最低不少于 10 个点，最高不超过数组长度的一半
    windowPoints = std::max(10, std::min(windowPoints, xData.size() / 2));
    QVector<double> baseline = calculateDynamicBaseline(yData, windowPoints);
    QVector<double> yCorrected = removeBaseline(yData, baseline);
    //自适应计算当前谱图的底噪
    double sigmaNoise = estimateNoise(yCorrected);
    //净峰高必须大于 3 倍噪声标准差
    double autoMinHeight = sigmaNoise * 3.0;
    qDebug()  << autoMinHeight;
    //保底机制
    autoMinHeight = std::max(autoMinHeight, minHeight);
    // 1. 提取所有真实波峰
    QVector<PeakCandidate> actualPeaks;
    for (int i = 1; i < xData.size() - 1; ++i) {
        if (yCorrected[i] > autoMinHeight && yCorrected[i] > yCorrected[i-1] && yCorrected[i] > yCorrected[i+1]) {
            actualPeaks.append({i, xData[i], yCorrected[i]});
        }
    }

    if (actualPeaks.isEmpty()) return finalResults;

    // 2. 初始化 DFS 记录器
    int bestInliers = -1;
    double bestScore = -1.0;
    QVector<int> bestPath(tCount, -1);
    QVector<int> currentPath;

    // 3. 启动 DFS 全局搜索 (树的根节点)
    dfsMatch(0, -1, currentPath, 0.0, bestInliers, bestScore, bestPath, actualPeaks, targetMasses,
             maxWindow, stretchLower, stretchUpper, distancePenaltyWeight);

    // 4. 将 DFS 找出的最完美路径解析为最终结果
    for (int i = 0; i < tCount; ++i) {
        int peakIdx = bestPath[i];
        if (peakIdx != -1) {
            int dataIdx = actualPeaks[peakIdx].index;
            finalResults[i].found = true;
            finalResults[i].centerMz = actualPeaks[peakIdx].mz;
            finalResults[i].height = actualPeaks[peakIdx].height;
            finalResults[i].width = calculatePeakWidth(xData, yCorrected, dataIdx, thresholdRatio);
        }
    }

    return finalResults;
}
// DFS 深度优先回溯匹配算法 (全局最优特征匹配)
void TuningEngine::dfsMatch(int tIdx, int lastPIdx, QVector<int> &currentPath, double currentScore,
                            int &bestInliers, double &bestScore, QVector<int> &bestPath,
                            const QVector<PeakCandidate> &actualPeaks, const QList<double> &targets,
                            double maxWindow, double stretchLower, double stretchUpper, double distancePenaltyWeight)
{
    // 【终止条件】：所有目标峰都已经做出了选择（选中或跳过）
    if (tIdx == targets.size()) {
        int inliers = 0;
        for (int p : currentPath) {
            if (p != -1) inliers++;
        }

        // 命中数量多的优先；数量相同时，总峰高（强度）大的优先
        if (inliers > bestInliers || (inliers == bestInliers && currentScore > bestScore)) {
            bestInliers = inliers;
            bestScore = currentScore;
            bestPath = currentPath; // 记录当前全局最优路径
        }
        return;
    }

    // --------------------------------------------------
    // 分支 1：放弃当前目标峰 (允许找不到该峰，增强容错率)
    // --------------------------------------------------
    currentPath.append(-1);
    dfsMatch(tIdx + 1, lastPIdx, currentPath, currentScore, bestInliers, bestScore, bestPath, actualPeaks, targets,
             maxWindow,stretchLower, stretchUpper, distancePenaltyWeight);
    currentPath.removeLast(); // 回溯

    // --------------------------------------------------
    // 分支 2：尝试在候选池中为当前目标峰分配一个真实的波峰
    // --------------------------------------------------
    // 【剪枝 1：顺序约束】实际峰的索引必须严格大于上一个被分配的实际峰索引 (lastPIdx)
    for (int pIdx = lastPIdx + 1; pIdx < actualPeaks.size(); ++pIdx) {
        double targetMz = targets[tIdx];
        double actualMz = actualPeaks[pIdx].mz;

        // 【剪枝 2：绝对距离约束】不能超过最大允许的全局偏移窗口
        if (std::abs(actualMz - targetMz) > maxWindow) continue;

        // 【剪枝 3：物理拉伸约束】(极其重要，防止把相邻峰错认)
        bool validStretch = true;
        // 往回找当前路径中上一个成功分配的峰
        for (int prevT = tIdx - 1; prevT >= 0; --prevT) {
            int prevP = currentPath[prevT];
            if (prevP != -1) {
                double prevTargetMz = targets[prevT];
                double prevActualMz = actualPeaks[prevP].mz;

                // 计算质量轴的局部拉伸系数
                double stretch = (actualMz - prevActualMz) / (targetMz - prevTargetMz);
                // --------------------------------------------------!!!!!
                // 质谱仪不能产生超过这个形变率的谱图，如果超差，剪断这根树枝！
                if (stretch < stretchLower || stretch > stretchUpper) { //未找到/找错峰调这里（1）
                    validStretch = false;
                    break;
                }
            }
        }
        if (!validStretch) continue; // 拉伸系数不合理，剪枝！放弃这个实际峰

        // 走到这里说明该实际峰完美符合所有物理约束，加入路径，向下深搜
        currentPath.append(pIdx);
        // 引入“柔性距离惩罚权重（Distance Penalty）”
        // 距离目标越远，该峰的得分权重越低。
        // 算法将优先选择“位置正确的小峰”，而不是“较远的大峰”
        // =======================================================！！！！！
        double dist = std::abs(actualMz - targetMz);
        double weightedHeight = actualPeaks[pIdx].height / (1.0 + (dist * dist) / distancePenaltyWeight);   //未找到/找错峰调这里（2）

        dfsMatch(tIdx + 1, pIdx, currentPath, currentScore + weightedHeight,
                 bestInliers, bestScore, bestPath, actualPeaks, targets,
                 maxWindow, stretchLower, stretchUpper, distancePenaltyWeight);
        currentPath.removeLast(); // 回溯恢复现场，准备尝试下一个兄弟节点
    }
}
//计算峰宽
double TuningEngine::calculatePeakWidth(const QVector<double>& xData, const QVector<double>& yData, int peakIndex, double thresholdRatio) {
    int n = xData.size();
    double threshold = yData[peakIndex] * thresholdRatio;
    double leftBoundX = xData[peakIndex], rightBoundX = xData[peakIndex];
    // ---------------- 向左寻找 thresholdRatio 交叉点 ----------------
    for (int i = peakIndex; i > 0; --i) {
        if (yData[i] <= threshold) {
            // 两点线性插值求精确 X 坐标
            double x1 = xData[i],   y1 = yData[i];     // 低于10%的点
            double x2 = xData[i+1], y2 = yData[i+1];   // 高于10%的点

            // 防止除零异常
            if (y2 != y1) {
                leftBoundX = x1 + (threshold - y1) * (x2 - x1) / (y2 - y1);
            } else {
                leftBoundX = x1;
            }
            break;
        }
        // 如果一直走到数组尽头都没降到 thresholdRatio，强行把最边缘作为边界
        if (i == 1 && yData[i] > threshold) {
            leftBoundX = xData[0];
        }
    }

    // ---------------- 向右寻找 thresholdRatio 交叉点 ----------------
    for (int i = peakIndex; i < n - 1; ++i) {
        if (yData[i] <= threshold) {
            // 两点线性插值求精确 X 坐标
            double x1 = xData[i-1], y1 = yData[i-1];   // 高于10%的点
            double x2 = xData[i],   y2 = yData[i];     // 低于10%的点

            // 防止除零异常
            if (y1 != y2) {
                rightBoundX = x1 + (threshold - y1) * (x2 - x1) / (y2 - y1);
            } else {
                rightBoundX = x2;
            }
            break;
        }
        // 如果一直走到数组尽头都没降到 thresholdRatio，强行把最边缘作为边界
        if (i == n - 2 && yData[i] > threshold) {
            rightBoundX = xData[n-1];
        }
    }

    // 返回计算出的宽度
    return rightBoundX - leftBoundX;
}
// 计算动态基线 (滑动最小值 + 迭代平滑逼近)
// windowSize 取决于数据密度，通常取能覆盖最宽单峰的宽度 (如 30~50 个数据点)
QVector<double> TuningEngine::calculateDynamicBaseline(const QVector<double>& yData, int windowSize) {
    int n = yData.size();
    QVector<double> baseline(n, 0.0);
    if (n < 3) return yData;
    if (windowSize >= n) windowSize = n / 2;
    if (windowSize < 3) windowSize = 3;

    int half = windowSize / 2;

    // 1. 滑动最小值窗口：提取图谱的“底部包络线”
    // 这一步能把所有的“波峰”削平，只留下谷底的连线
    for (int i = 0; i < n; ++i) {
        double minVal = yData[i];
        int start = std::max(0, i - half);
        int end = std::min(n - 1, i + half);
        for (int j = start; j <= end; ++j) {
            if (yData[j] < minVal) {
                minVal = yData[j];
            }
        }
        baseline[i] = minVal;
    }

    // 2. 迭代平滑：让底部包络线变得平滑、自然，贴合真实化学背景
    QVector<double> smoothed = baseline;
    QVector<double> temp(n, 0.0);
    int smoothHalf = half; // 使用同样的窗口进行平滑

    // 迭代 3 次低通滤波，足以消除极值窗口带来的阶梯感
    for (int iter = 0; iter < 3; ++iter) {
        for (int i = 0; i < n; ++i) {
            double sum = 0.0;
            int count = 0;
            int start = std::max(0, i - smoothHalf);
            int end = std::min(n - 1, i + smoothHalf);
            for (int j = start; j <= end; ++j) {
                sum += smoothed[j];
                count++;
            }
            temp[i] = sum / count;
        }
        smoothed = temp;
    }
    return smoothed;
}
//扣除基线
QVector<double> TuningEngine::removeBaseline(const QVector<double>& yData, const QVector<double>& baseline) {
    int n = yData.size();
    QVector<double> corrected(n, 0.0);
    for (int i = 0; i < n; ++i) {
        corrected[i] = yData[i] - baseline[i];
        // 物理保护：扣除基线后，微小的负数噪声强制归零，防止后续计算异常
        if (corrected[i] < 0.0) corrected[i] = 0.0;
    }
    return corrected;
}
//噪声估计
double TuningEngine::estimateNoise(const QVector<double> &data)
{
    int n = data.size();
    if (n < 2) return 0.0;

    QVector<double> diffs;
    for (int i = 1; i < n; ++i) {
        // 计算相邻两点之间的差异（一阶导数的绝对值）
        diffs.append(std::abs(data[i] - data[i-1]));
    }

    // 找出这些差异的中位数
    std::sort(diffs.begin(), diffs.end());
    double mad = diffs[diffs.size() / 2];

    // 转换为标准差估计 (0.6745 是正态分布下的常数)
    return mad / 0.6745;
}
//计算RSD相对标准偏差
double TuningEngine::calculateRSD(const QVector<double>& values) {
    if (values.isEmpty()) return 0.0;
    double mean = 0.0, varSum = 0.0;
    for (double v : values) mean += v;
    mean /= values.size();
    if (mean == 0.0) return 0.0;
    for (double v : values) varSum += std::pow(v - mean, 2);
    return (std::sqrt(varSum / values.size()) / mean) * 100.0;
}

//调试Gain值,Gain值大后端峰窄（自适应二分趋近）
double TuningEngine::adjustGain(const QVector<PeakResult>& results, double currentGain, double& gainStep,
                                int& currentDir, double rsdTarget, bool& isDone) {
    isDone = false;

    QVector<double> widths;
    for (const auto& r : results) widths.append(r.width);
    double rsd = calculateRSD(widths);

    bool trendOk = (widths.size() >= 2) ? (widths.last() > widths.first()) : true;

    // ==========================================
    if (rsd < rsdTarget && trendOk) //满足RSD且后大于前端峰宽
    {
        isDone = true;
        return currentGain;
    }
    // 物理判定：当前需要的理论调节方向
    // 如果后峰 <= 前峰 ，说明 Gain 太大，必须向下减小 (-1)
    // 如果后峰 > 前峰 (但RSD还没达标)，说明 Gain 太小，必须向上增加 (1)
    int requiredDir = (widths.size() >= 2 && widths.last() <= widths.first()) ? -1 : 1;

    // 初始第一步盲探：直接按照需要的方向走，步长绝不减半！
    if (currentDir == 0) {
        currentDir = requiredDir;
        return currentGain + (currentDir * gainStep);
    }

    // 方向反转检测
    // 只有当“本次需要的方向”和“上次走的方向”相反时，才说明跨过了最佳点(Overshoot)
    if (currentDir != requiredDir) {
        gainStep = gainStep / 2.0;    // 跨过头了，步长减半
        currentDir = requiredDir;     // 彻底掉头
    }

    // 按照当前确定的方向和步长，步进
    return currentGain + (currentDir * gainStep);
}

//调试偏移值,偏移大峰窄（自适应二分趋近）
double TuningEngine::adjustOffset(const QVector<PeakResult>& results, double currentOffset,
                                  double& offsetStep, int& currentDir,
                                  double lowerBound, double upperBound, bool& isDone)
{
    isDone = false;
    double sumW = 0.0;
    bool inRange = true;
    for (const auto& r : results)
    {
        sumW += r.width;
        if (r.width < lowerBound || r.width > upperBound) inRange = false;
    }
    if (inRange)
    {
        isDone = true;
        return currentOffset;
    }
    double avgW = sumW / results.size();
    // 物理判定：当前需要的理论调节方向
    int requiredDir = 1;
    if (avgW > upperBound) {
        requiredDir = 1;  // 太宽了，需要增加偏移让峰变窄
    } else if (avgW < lowerBound) {
        requiredDir = -1; // 太窄了，需要减小偏移让峰变宽
    } else {
        // 平均值达标，但方差大导致个别出界，此时向中心目标靠拢
        double targetW = (lowerBound + upperBound) / 2.0;
        requiredDir = (avgW > targetW) ? 1 : -1;
    }

    // 1.初始第一步盲探：直接走，不减半
    if (currentDir == 0) {
        currentDir = requiredDir;
        return currentOffset + (currentDir * offsetStep);
    }

    // 2.方向反转检测
    if (currentDir != requiredDir) {
        offsetStep = offsetStep / 2.0; // 走过头了，步长永久减半
        currentDir = requiredDir;      // 彻底掉头
    }

    // 3.执行本轮调节
    return currentOffset + (currentDir * offsetStep);
}

//调试position值
double TuningEngine::adjustPosition(double currentPosition, double actualMz, double targetMz) {
    // 【P1修复】增强防御性校验，防止除零和异常值
    if (targetMz <= 0 || std::isnan(targetMz) || std::isnan(actualMz))
    {
        return currentPosition;
    }
    return currentPosition * (actualMz / targetMz);
}

//计算相关判定系数
TrendlineResult TuningEngine::calculateTrendline(const QVector<double>& xVals, const QVector<double>& yVals) {
    TrendlineResult res = {0,0,0};
    int n = xVals.size();
    if (n < 2) return res;
    double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
    for (int i=0; i<n; i++)
    {
        sumX+=xVals[i]; sumY+=yVals[i]; sumXY+=xVals[i]*yVals[i]; sumX2+=xVals[i]*xVals[i]; sumY2+=yVals[i]*yVals[i];
    }
    double den = (n * sumX2 - sumX * sumX);
    if (den == 0) return res;
    res.k = (n * sumXY - sumX * sumY) / den;
    res.b = (sumY * sumX2 - sumX * sumXY) / den;
    double num = (n * sumXY - sumX * sumY);
    double r = num / std::sqrt(den * (n * sumY2 - sumY * sumY));
    res.rSquared = r * r;
    return res;
}

//----------------电参调整
// 1. 计算单峰强度R²（非整体偏置）
double TuningEngine::calculateSingleIntensityR2(double current, double dest) {
    if (dest <= 1e-20) return 0.0; // 防御除以0

    // R2_error = |理论mzY - 实际mzY| / 理论mzY
    double error = std::abs(dest - current) / dest;

    // 转换为UI设置的相似度得分 (目标是 0.9999)
    double r2 = 1.0 - error;
    return (r2 < 0.0) ? 0.0 : r2;
}

// 2. 计算整体偏置R²（整体偏置）
double TuningEngine::calculateGlobalIntensityR2(const QVector<double>& currents, const QVector<double>& dests) {
    //---------------方法1：计算R2
    // 用之前的 calculateTrendline 计算
    // xVals为目标强度(dests)，yVals为实际强度(currents)，其线性相关性即为 R²
    // TrendlineResult trend = calculateTrendline(dests, currents);
    // return trend.rSquared;

    //---------------方法2：计算 1-平均绝对相对误差
    int n = currents.size();
    if (n == 0 || n != dests.size()) return -1.0;

    double sum = 0.0;
    int validCount = 0;
    for (int i = 0; i < n; ++i) {
        if (dests[i] == 0) continue; // 避免除零
        sum += std::abs(currents[i] - dests[i]) / std::abs(dests[i]);
        ++validCount;
    }
    return  1 - sum / validCount;
}

// 3. 辅助判断函数：是否为“整体偏置”
// 返回值: 1 (整体偏高), -1 (整体偏低), 0 (非整体偏置，即有高有低)
int TuningEngine::checkGlobalBiasDirection(const QVector<double>& currents, const QVector<double>& dests) {
    bool allHigher = true;
    bool allLower = true;

    for (int i = 0; i < currents.size(); ++i) {
        if (currents[i] <= dests[i]) allHigher = false;
        if (currents[i] >= dests[i]) allLower = false;
    }

    if (allHigher) return 1;
    if (allLower) return -1;
    return 0; // 参差不齐
}
